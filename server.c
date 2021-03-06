/*
 * Yanhua Liu (ytz2) CS820
 * server.c - a server program that uses the socket interface to tcp
 *		  This server serves multiple clients at a time.
 *		  For each client it creates an agent thread that
 *		  repeatedly reads the name of a file
 *		  then the data content of a file and simply prints it.
 *		must be linked with -lnsl and -lsocket on solaris
 */

#include "server.h"

#define BUFSIZE		1024			/* size of data buffers */
#define TEXT_SIZE	 256			/* size of text buffer */

struct thread_params {
	int client_fd;
	struct sockaddr client;
};

/*use the set flat to build the search structure */
search* build_search(msg_one *flags) {
	search *mysearch; /*ptr to a structure holding the search request */
	int buffer_size;
	mysearch = NULL;
	init_search(&mysearch);
	if (mysearch == NULL)
		pthread_exit(NULL);
	/*set search for parameters for message 1*/
	mysearch->options_flags = ntohl(flags->options_flags);
	mysearch->max_dir_depth = ntohl(flags->max_dir_depth);
	buffer_size = ntohl(flags->line_buffer_size);
	if (buffer_size == -1)
		mysearch->line_buffer_size = DEFAULT_LINE_BUFFER;
	else
		mysearch->line_buffer_size = buffer_size;
	mysearch->max_line_number = ntohl(flags->max_line_number);
	mysearch->column_number = ntohl(flags->column_number);
	mysearch->thread_limits = ntohl(flags->thread_limits);
	return mysearch;
}

/*once receive the search parameters from message 1
 * we should print out the agent fd, threadid and options
 */
void print_search_para(int fd, search *mysearch) {
	fprintf(stderr, "Client's Options, thread fd:%d thread id: %lu\n", fd,
			(unsigned long) pthread_self());
	print_flag(mysearch->options_flags, DOT_ACCESS, "-a");
	print_flag(mysearch->options_flags, AT_BEGIN, "-b");
	print_flag(mysearch->options_flags, AT_END, "-e");
	print_flag(mysearch->options_flags, NOT_FOLLOW_LINK, "-f");
	print_flag(mysearch->options_flags, CASE_INSENSITIVE, "-i");
	print_flag(mysearch->options_flags, SHOW_PATH, "-p");
	print_flag(mysearch->options_flags, NO_ERR_MSG, "-q");
	print_flag(mysearch->options_flags, INVERSE_PRINT, "-i");
	fprintf(stderr, "%s\t%d\n", "-d", mysearch->max_dir_depth);
	fprintf(stderr, "%s\t%d\n", "-l",
			mysearch->line_buffer_size == DEFAULT_LINE_BUFFER ?
					-1 : mysearch->line_buffer_size);
	fprintf(stderr, "%s\t%d\n", "-m", mysearch->max_line_number);
	fprintf(stderr, "%s\t%d\n", "-n", mysearch->column_number);
	fprintf(stderr, "%s\t%d\n", "-t", mysearch->thread_limits);
}

/* the agent is now a separate thread */
void *
server_agent(void *params) {
	int client_fd, n, errcode;
	enum header_types type;
	unsigned int len;
	char to_search[MAX_SEARCH_STR];
	char remote_obj[REMOTE_NAME_MAX];
	time_type t_start, t_end;
	double tdiff;
	msg_one msg1;
	search *mysearch;
	Statistics statistic;

	/* we are now successfully connected to a remote client */
	client_fd = ((struct thread_params *) params)->client_fd;
	fprintf(stderr, "Starting Agent fd: %d, thread id: %lu \n", client_fd,
			(unsigned long) pthread_self());

	/* do some initialization*/
	memset(&statistic, 0, sizeof(Statistics));
	errcode = pthread_detach(pthread_self());
	if (errcode != 0) {
		fprintf(stderr, "pthread_detach server agent: %s\n", strerror(errcode));
	}
	pthread_once(&init_done, thread_init);

	get_time(&t_start);
	len = sizeof(msg_one);
	/* first message should be the file name */
	if ((n = our_recv_message(client_fd, &type, &len, &msg1)) < 0)
		return NULL;
	if (type != OPTION_PARAMETER)
		return NULL;

	/*firstly build the search object */
	mysearch = build_search(&msg1);
	mysearch->client_fd = client_fd;
	/* then print the search options */
	print_search_para(client_fd, mysearch);

	/* receive the second message*/
	len = MAX_SEARCH_STR;
	if ((n = our_recv_message(client_fd, &type, &len, to_search)) < 0)
		return NULL;
	if (type != TO_SEARCH)
		return NULL;
	len = strlen(to_search);
	if ((mysearch->search_pattern = (char*) malloc(len + 1)) == NULL) {
		perror("malloc");
		free(params);
		return NULL;
	}
	strncpy(mysearch->search_pattern, to_search, len+1);

	/*build its own shift table if needed */
	build_shifttable(mysearch);

	/* receive the second message*/
	len = REMOTE_NAME_MAX;
	if ((n = our_recv_message(client_fd, &type, &len, remote_obj)) < 0)
		return NULL;
	if (type != REMOTE_NAME)
		return NULL;
	/* do the search job */
	search_given(remote_obj, mysearch);

	/* send the statistics message 6*/
	/*wait for directory search to be finished if it is on the server side*/
	while (mysearch->stk_count != 0) {
		pthread_cond_wait(&(mysearch->ready), &(mysearch->lock));
	}

	len = sizeof(Statistics);
	/* make a copy of little/big endian adapted statistical structure*/
	update_statistics_sock(&statistic, &mysearch->statistics);
	trans_stat2send(&statistic);
	if (our_send_message(mysearch->client_fd, STATISTICS_MSG, len, &statistic)
			!= 0) {
		fprintf(stderr, "Fail to send statistics\n");
		return NULL;
	}
	get_time(&t_end);
	tdiff = time_diff(&t_start, &t_end);
	fprintf(stderr, "Search Statistics: client fd %u, thread id %lu\n",
			mysearch->client_fd, (unsigned long) pthread_self());
	print_stat(stderr, &mysearch->statistics, tdiff);
	destroy_search(mysearch);
	fprintf(stderr, "Terminating Agent fd: %d, thread id: %lu \n", client_fd,
			(unsigned long) pthread_self());
	free(params);
	return NULL;
} /* server_agent */

void listener(char *server_port, char *interface_name) {
	socklen_t len;
	int listening_fd, errcode;
	struct sockaddr server;
	struct sockaddr_in *iptr;
	char text_buf[TEXT_SIZE];
	char local_node[HOSTMAX];
	char *host_name;
	struct thread_params *params;
	pthread_t agent_id;

	no_sigpipe();
	/* establish a server "listening post" */
	listening_fd = openlistener(server_port, interface_name, &server);

	/* get the hostname of server*/
	if (interface_name == NULL) {
		if (gethostname(local_node, HOSTMAX) < 0) {
			perror("Server gethostname");
			exit(1);
		}
		host_name = local_node;
	} else {
		host_name = interface_name;
	}

	if (listening_fd < 0)
		exit(EXIT_FAILURE);

	/* we are now successfully established as a server */
	/* infinite loop -- server serves forever, must be killed by ^C */
	for (;;) {
		iptr = (struct sockaddr_in *) &server;
		if (inet_ntop(iptr->sin_family, &iptr->sin_addr, text_buf,
				TEXT_SIZE) == NULL) {
			perror("inet_ntop server");
			break;
		}
		fprintf(stderr, "%s listening at IP address %s port %d\n", host_name,
				text_buf, ntohs(iptr->sin_port));
		/* accept a client connection (block until one arrives) */
		params = malloc(sizeof(struct thread_params));
		if (params == NULL) {
			perror("listener malloc");
			break;
		}
		len = sizeof(params->client);
		if ((params->client_fd = accept(listening_fd, &params->client, &len))
				< 0) {
			perror("rplcsd accept");
			break;
		}

		iptr = (struct sockaddr_in *) (&params->client);
		if (inet_ntop(iptr->sin_family, &iptr->sin_addr, text_buf,
				TEXT_SIZE) == NULL) {
			perror("inet_ntop client");
			free(params);
			continue;
		}
		fprintf(stderr, "connected to client at IP address %s "
				"port %d\n", text_buf, ntohs(iptr->sin_port));

		/* we are now successfully connected to a remote client */
		errcode = pthread_create(&agent_id, NULL, server_agent, params);
		if (errcode != 0) {
			fprintf(stderr, "pthread_create server agent: %s\n",
					strerror(errcode));
			break;
		}
	} /* infinite loop */

	if (close(listening_fd) < 0) {
		perror("rplcsd server close");
	}
	pthread_exit(NULL);
} /* listener */
/* vi: set autoindent tabstop=8 shiftwidth=8 : */
