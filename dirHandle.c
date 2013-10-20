/*
 * dirHandle.c
 * Yanhua Liu
 * This file defines directory processing functions
 * For CS820 Assignment 2
 * Created on: Oct 17, 2013
 * Author: Yanhua Liu ytz2
 */


#include "dirHandle.h"

/*
 * debuging purpose
 * usage: dbg(__func__,__LINE__,msg)
 *        or dbg(__func__,node->counter,node->path)
 * leave here for assignment 3, no use in current project
 * and leave it here.
 */
void dbg(const char* func, const char* path, int count)
{
	fprintf(stderr,"%s: %s is %d \n",func,path,count);
	fflush(stderr);
}
/*
 * make a history stack node
 */
Node* make_node(char *dname)
{
	Node* temp;
	DIR *dir;
	char* rptr,*memptr;

	errno=0;

	/* use full path name to get real path */
	if ((rptr = get_realpath(dname,&memptr))==NULL)
	{
		/* error report has been done within get_realpath */
		if (memptr)
			free(memptr);
		return NULL;
	}

	/* open the direcotry with full path */
	if((dir=opendir(rptr))==NULL)
	{
		if (!no_err_msg)
			perror(rptr);
		return NULL;
	}


	/* give space to a node */
	if ((temp=(Node*)malloc(sizeof(Node)))==NULL)
	{
		if (!no_err_msg)
			perror("malloc()");
		return NULL;
	}

	/* initialize the node */
	temp->counter=1; // set to 1 at the first time to be made
	temp->dir=dir; // the dir object about the path
	temp->path=memptr; // realpath of the dir
	temp->prev=NULL; // will be pointed to its parent dir
	return temp;
}

/*
 * do some housekeeping about a node
 */
void clear_node(Node* node)
{
	if (node->dir)
		closedir(node->dir);
	if (node->path)
		free(node->path);
	free(node);
}
/*/*
 * Initialize a stack
 */
int stack_init(stack *st)
{
	int err;
	st->head=NULL;
	// initialize the lock
	err=pthread_rwlock_init(&st->s_lock,NULL);
	if (err!=0)
		return(err);
	// initialize the thread attribute
	err=pthread_attr_init(&st->attr);
	if (err!=0)
		return(err);
	// set the attribute to be detached
	err=pthread_attr_setdetachstate(&st->attr,PTHREAD_CREATE_DETACHED);
	if (err!=0)
		return(err);
	return 0;
}
/*
 * destroy the stack
 */

int stack_destroy(stack *st)
{
	int err;
	if((err=pthread_attr_destroy(&st->attr))!=0)
		return err;
	return 0;
}

/*
 * push a node on the "top" of the tree-like stack
 */
int stack_push(stack *st,Node *current, Node *next)
{
	/* do some safe check */
	if (current==NULL)
		return -1;

	int err;
	if ((err=pthread_rwlock_wrlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_wrlock: %s", strerror(err));
		return err;
	}

	/* when the head is null set the current as head
	 * this handles the call stack_push(st,current,NULL)
	 * */
	if (st->head==NULL)
		st->head=current;
	else
	/* if one subdirectory added, the current counter increase*/
	/* link the next to the current node*/
	{
		current->counter++;
		next->prev=current;
	}


	if ((err=pthread_rwlock_unlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_unlock: %s", strerror(err));
		return err;
	}

	return 0;
}

/*
 * pop a node on the "top" of the tree-like stack
 */
int stack_job_done(stack *st,Node *current)
{
	int err;
	Node *temp,*tofree;
	temp=current;
	if ((err=pthread_rwlock_wrlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_wrlock: %s", strerror(err));
		return err;
	}
	// decrement the leaf counter

	/*
	 * from the current node to loop, if the counter of
	 * the current node is 0, clean the node and continue
	 * to check, else just stop
	 */
	while (temp!=NULL)
	{
		/* decrement the counter of the current ptr
		 * this happens firstly for the leaf dir
		 */
		temp->counter--;

		/* if the counter is 0, trigger the avalanche */
		if (temp->counter==0)
		{
			tofree=temp;
			temp=temp->prev;
			clear_node(tofree); // delete the node
		}
		else /* if counter is not 0, it means its job has not been finished*/
			break;
	}


	if ((err=pthread_rwlock_unlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_unlock: %s", strerror(err));
		return err;
	}
	return 0;
}

/*
 *  find the realpath which used to appear in the tree
 */
Node* stack_find_history(stack *st,Node* current,char *path)
{
	int err;
	Node *temp;
	if ((err=pthread_rwlock_rdlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_rdlock: %s", strerror(err));
		return NULL;
	}

	for (temp=current;temp!=NULL;temp=temp->prev)
	{
		if(strcmp(temp->path,path)==0)
			break;
	}

	if ((err=pthread_rwlock_unlock(&st->s_lock))!=0)
	{
		if (!no_err_msg)
			fprintf(stderr, "rwlock_unlock: %s", strerror(err));
		return NULL;
	}
	return temp;
}

/*
 * get the full path from the realpath and d_name
 */
char* get_fullpath(char* rpath,char *fname)
{
	/*
	 * basic string operations
	 * give a buffer, copy the realpath
	 * realpath+'/'+filename=full_path
	 */
	char *fullpath;
	int len;
	fullpath=path_alloc(NULL);
	len=strlen(rpath);
	strcpy(fullpath,rpath);
	fullpath[len++]='/';
	fullpath[len]='\0';
	strcat(fullpath,fname);
	return fullpath;
}

/*
 * test if the symlink pointing to a dir
 * if it is, return bool 1
 * else false 0
 */
int is_sym_dir(char* full_name)
{
	struct stat st;
	int val;
	errno=0;
/*
 * use the stat to follow the link
 */
	if (stat(full_name,&st)==-1)
	{
		/* if the link does not exist, issue error*/
		if (!no_err_msg)
			perror(full_name);
		val=-1;
	}
	else if (S_ISDIR(st.st_mode))
		val=1;
	else
		val=0;

	return val;
}


/*
 * recursively walk the directory
 */

int walk_recur(int depth,stack *stk,Node* current)
{

	DIR *dir;
	char *current_path,*full_path;
	struct dirent entry;
	struct dirent *result;
	struct stat st;
	Para *para;
	int err;
	Node *next;
	Node *prev;
	pthread_t id;
	int sym_link_flag;

	printf("%*s: thread id: %lu\n",3*depth+(int)strlen(current->path),current->path,pthread_self());

	/* get the current directory info*/
	dir=current->dir;
	current_path=current->path;

	/* now loop directory */
	for (err = readdir_r(dir, &entry, &result);
	         result != NULL && err == 0;
	         err = readdir_r(dir, &entry, &result))
	{
		// initialize a flag to indicate whether it is a sym_link
		sym_link_flag=0;
		/* neglect the . and .. */
		if (!strcmp(result->d_name, ".") || !strcmp(result->d_name, ".."))
			continue;

		/*	handle the files or directories start with .	 */
		if (dot_no_access && result->d_name[0]=='.')
			continue;

		/* generate the full path of subdirectory or files */
		full_path=get_fullpath(current_path,result->d_name);

		/* get the stat info of subdir/file*/
		if (lstat(full_path,&st)==-1)
		{
			if (!no_err_msg)
				perror(full_path);
			free(full_path);
			continue;
		}

		/* deal with symlink -f*/
		if (S_ISLNK(st.st_mode))
		{
			/* if -f is set, do not follow link */
			if ( not_follow_link)
			{
				if (!no_err_msg)
					fprintf(stderr,"Symlink: %s\n",full_path);
				free(full_path);
				continue;
			}
			/* use stat to follow the symlink to test whether the symlink to
			 * dir or file exist
			 */
			sym_link_flag=is_sym_dir(full_path);
			/* if it does not exist, just go to process next one */
			if (sym_link_flag==-1)
			{
				free(full_path);
				continue;
			}
		}

		/* if it is the directory or symlink to a directory */
		if (depth<MAX_DEPTH && (S_ISDIR(st.st_mode)||sym_link_flag==1))
		{
			/* make a node contains info */
			if ((next=make_node(full_path))==NULL)
			{
				free(full_path);
				continue;
			}
			/* check if it used to appear in the history stack*/
			else if ((prev=stack_find_history(stk,current,next->path))!=NULL)
			{
				if (!no_err_msg)
					fprintf(stderr,"%s is detected to cause a loop\n",full_path);
				clear_node(next);
				free(full_path);
				continue;
			}
			/* push the next node to the stack on the top of current node*/
			stack_push(stk,current, next);
			/* recursively search */
			para=(Para*)malloc(sizeof(Para));
			para->current=next;
			para->depth=depth+1;
			para->stk=stk;

			int err;
			if ((err=pthread_create(&id,&stk->attr,search_dir,(void*)para))!=0)
				walk_recur(depth+1, stk, next);
			if (err)
				free(para);
			//walk_recur(depth+1, stk, next);
		}
		free(full_path);
	}
	stack_job_done(stk,current);
	/* now list the dir */
	return 0;
}

/*
 * wrapper function for directory search
 */
void* search_dir(void *para)
{
	long err=0;
	Para *temp=(Para*)para;
	int depth=temp->depth;
	Node *current=temp->current;
	stack *stk=temp->stk;
	err=walk_recur(depth,stk,current);
	return (void*)err;
}


int main(int argc, char *argv[])
{
	pthread_t id;
	stack st;
	struct stat info;
	Para para;
	printf("within thread %lu\n",pthread_self());
	stack_init(&st);
	Node *current;
	lstat(argv[1],&info);
	if (S_ISLNK(info.st_mode))
		printf("This is symlink\n");
	current=make_node(argv[1]);
	stack_push(&st,current, NULL);
	para.current=current;
	para.depth=1;
	para.stk=&st;
	pthread_create(&id,&st.attr,search_dir,(void*)&para);
	//walk_recur(1,&st,current);
	stack_destroy(&st);

	pthread_exit(0);
}



