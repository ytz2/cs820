/*
 * rplcs.c
 *Yanhua Liu (ytz2) CS820
 *  Created by Yanhua Liu for CS820 assignment 3
 */
#include "ospenv.h" /* this header defines POSIX, ISOC, XOPEN and EXTENSIONS */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "command_util.h"
#include "plcsIO.h"
#include "search_given.h"
#include "global.h"
#include "print_time.h"

pthread_key_t line_buffer_key;
pthread_key_t out_buffer_key;
pthread_key_t err_buffer_key;

#ifdef __sun__
pthread_once_t init_done= {PTHREAD_ONCE_INIT}; // once key
#else
pthread_key_t strerr_key; // key to bind a line buffer
pthread_once_t init_done = PTHREAD_ONCE_INIT; // once key
#endif

/*
 * The following parameters are made global
 * they are set via switches and interfaces
 * with plcsIO and is externed by global.h
 * any usage of these global prameters
 * must include "global.h"
 */

/*main function*/
int main(int argc, char *argv[]) {

	search *mysearch; /*ptr to a structure holding the search request */
	remote *rmt;
	Client_para *para;
	pthread_t id;
	int err, rmt_flag;
	char *temp;
	time_type t_start, t_end;
	double tdiff;
	/*initialization*/
	err = 0;
	rmt_flag = 0;
	mysearch = NULL;
	init_search(&mysearch);

	/* process all the command line switches */
	opterr = 0; /* prevent getopt() from printing error messages */
	scan_opt_search(argc, argv, mysearch); /*move the getopt to command_util.h to shorten the main */
	/*build the shift table*/
	build_shifttable(mysearch);
	/*
	 * if there is no argument in list of files
	 * directly go to stdin
	 */
	if (optind >= argc) {
		search_stream(stdin, NULL, mysearch, NULL);
		return 0;
	}
	get_time(&t_start);
	/* process the list of files*/
	for (; optind < argc; optind++) {
		/* if it a remote search */
		temp = argv[optind];
		/* if it is remote search and has been successfully parsed*/
		if ((rmt = scan_remote_search(temp, &rmt_flag)) != NULL) {
			if ((para = (Client_para*) malloc(sizeof(Client_para))) == NULL) {
				perror("malloc");
				continue;
			}
			para->mysearch = mysearch;
			para->rmt = rmt;
			/* spawn a thread the perform remote search*/
			// increment the stack count
			pthread_mutex_lock(&(mysearch->lock));
			err = pthread_create(&id, NULL, client_agent, (void*) para);
			mysearch->stk_count++;
			if (err != 0) {
				fprintf(stderr, "Pthread_create of client\n");
				free(para);
				mysearch->stk_count--;
				pthread_mutex_unlock(&(mysearch->lock));
				continue;
			}
			pthread_mutex_unlock(&(mysearch->lock));
			continue;
		} else if (rmt_flag == 1) {
			/* if a : used to appear there but has not been parsed, neglect it */
			continue;
		}
		if (strcmp(temp, STREAM_REDIRECT) == 0)
			/* "-" redirect the io to stdin*/
		{
			search_stream(stdin, NULL, mysearch, NULL);
			continue;
		}
		search_given(temp, mysearch);
	}
	/* wait all the thread to be done*/
	while (mysearch->stk_count != 0) {
		pthread_cond_wait(&(mysearch->ready), &(mysearch->lock));
	}
	get_time(&t_end);
	tdiff = time_diff(&t_start, &t_end);
	/* print the statistical result*/
	print_stat(stdout, &(mysearch->statistics), tdiff);
	destroy_search(mysearch);

	return 0;
} /* main */

