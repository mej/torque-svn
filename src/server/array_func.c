/* this file contains functions for manipulating job arrays

  included functions:

  is_array() determine if jobnum is actually an array identifyer
  get_array() return array struct for given "parent id"
  array_save() save array struct to disk
  get_parent_id() return id of parent job if job belongs to a job array
  recover_array_struct() recover the array struct for a job array at restart
  delete_array_struct() free memory used by struct and delete sved struct on disk
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


#include "pbs_ifl.h"
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "job.h"
#include "log.h"
#include "pbs_error.h"

#include "work_task.h"

/* #include "array.h" */

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

extern void  job_clone_wt A_((struct work_task *));

/* global data items used */

/* list of job arrays */
extern tlist_head svr_jobarrays;
extern char *path_arrays;
extern char *path_jobs;
extern time_t time_now;
extern int    LOGLEVEL;

/* search job array list to determine if id is a job array */
int is_array(char *id)
  {

  array_job_list *pajl;
  
  
    
  pajl = (array_job_list*)GET_NEXT(svr_jobarrays);  
  while (pajl != NULL)
    {
    if (strcmp(pajl->ai_qs.parent_id, id) == 0)
      {
      return TRUE;
      }
    pajl = (array_job_list*)GET_NEXT(pajl->all_arrays);
    }
  
  return FALSE;
  }
  
/* return a server's array info struct corresponding to an array id */
array_job_list *get_array(char *id)
  {
  array_job_list *pajl;
   
    
  pajl = (array_job_list*)GET_NEXT(svr_jobarrays);  
  while (pajl != NULL)
    {
    if (strcmp(pajl->ai_qs.parent_id, id) == 0)
      {
      return pajl;
      }
    pajl = (array_job_list*)GET_NEXT(pajl->all_arrays);
    }
    
  return (array_job_list*)NULL;
  }
  
  
/* save a job array struct to disk returns zero if no errors*/  
int array_save(array_job_list *pajl)

  { 
  int fds;
  int openflags;
  char namebuf[MAXPATHLEN];
  
  strcpy(namebuf, path_arrays);
  strcat(namebuf, pajl->ai_qs.fileprefix);
  strcat(namebuf, ARRAY_FILE_SUFFIX);
  
  
  openflags =  O_WRONLY | O_SYNC | O_CREAT;
  fds = open(namebuf, openflags, 0600);
  
  if (fds < 0)
    {
    return -1;
    }
    
  write(fds,  &(pajl->ai_qs), sizeof(pajl->ai_qs));  
  close(fds);
  
  return 0;
  }
  
  
/* if a job belongs to an array, this will return the id of the parent job
 * this assumes that the caller allocates the storage for the parent id 
 * and that this will only be called on jobs that actually have or had
 *  a parent job 
 */  
void get_parent_id(char *job_id, char *parent_id)
  {
  char *c;
  char *pid;
  
  c = job_id;
  *parent_id = '\0';
  pid = parent_id;
  
  /* copy until the '-' */
  while (*c != '-' && *c != '\0')
    {
    *pid = *c;
    c++;
    pid++;
    } 
    
  /* skip the until the first '.' */
  while (*c != '.' && *c != '\0')
    {
    c++;
    }
    
  /* copy the rest of the id */
  *pid = '\0';
  strcat(pid, c);

  }  



/* recover_array_struct reads in  an array struct saved to disk and inserts it into 
   the servers list of arrays */
array_job_list *recover_array_struct(char *path)
{
   extern tlist_head svr_jobarrays;
   array_job_list *pajl;
   int fd;
   
   /* allocate the storage for the struct */
   pajl = (array_job_list*)malloc(sizeof(array_job_list));
   
   if (pajl == NULL)
     {
     return NULL;
     }
    
   /* initialize the linked list nodes */  
   CLEAR_LINK(pajl->all_arrays);
   CLEAR_HEAD(pajl->array_alljobs);
	 
   fd = open(path, O_RDONLY,0);
   
   /* read the file into the struct previously allocated. 
    * TODO - check version of struct before trying to read the whole thing
    */
   if (read(fd, &(pajl->ai_qs), sizeof(pajl->ai_qs)) != sizeof(pajl->ai_qs))
     {
     sprintf(log_buffer,"unable to read %s", path);

     log_err(errno,"pbsd_init",log_buffer);

     free(pajl);
     return(NULL);
     }
     
   close(fd);
	 
   /* link the struct into the servers list of job arrays */ 
   append_link(&svr_jobarrays, &pajl->all_arrays, (void*)pajl);
   
   return pajl;

}


/* delete a job array struct from memory and disk. This is used when the number
 *  of jobs that belong to the array becomes zero.
 *  returns zero if there are no errors, non-zero otherwise
 */
int delete_array_struct(array_job_list *pajl)
  {
 
  char path[MAXPATHLEN + 1];
  
  /* first thing to do is take this out of the servers list of all arrays */
  delete_link(&pajl->all_arrays);
  
  strcpy(path, path_arrays);
  strcat(path, pajl->ai_qs.fileprefix);
  strcat(path, ARRAY_FILE_SUFFIX);
  
 
  
  /* delete the on disk copy of the struct */
  if (unlink(path))
    {
    sprintf(log_buffer, "unable to delete %s", path);
    log_err(errno, "delete_array_struct", log_buffer);
    }
    
  strcpy(path,path_jobs);	/* delete script file */
  strcat(path,pajl->ai_qs.fileprefix);
  strcat(path,JOB_SCRIPT_SUFFIX);

  if (unlink(path) < 0)
    {
    sprintf(log_buffer, "unable to delete %s", path);
    log_err(errno, "delete_array_struct", log_buffer);
    }
    
  else if (LOGLEVEL >= 6)
    {
    sprintf(log_buffer,"removed job script");

    log_record(PBSEVENT_DEBUG,
      PBS_EVENTCLASS_JOB,
      pajl->ai_qs.parent_id,
      log_buffer);
    }

  /* free the memory allocated for the struct */
  free(pajl);  
  return 0;
  }
  
  
int setup_array_struct(job *pjob)
  {
  array_job_list *pajl;
  struct work_task *wt;
  
  /* setup a link to this job array in the servers all_arrays list */
  pajl = (array_job_list*)malloc(sizeof(array_job_list));
    
  pajl->ai_qs.struct_version = ARRAY_STRUCT_VERSION;
  pajl->ai_qs.array_size = pjob->ji_wattr[(int)JOB_ATR_job_array_size].at_val.at_long;
  strcpy(pajl->ai_qs.parent_id, pjob->ji_qs.ji_jobid);
  strcpy(pajl->ai_qs.fileprefix, pjob->ji_qs.ji_fileprefix);
  pajl->ai_qs.num_cloned = 0;
  CLEAR_LINK(pajl->all_arrays);
  CLEAR_HEAD(pajl->array_alljobs);
  append_link(&svr_jobarrays, &pajl->all_arrays, (void*)pajl);
    
  if (job_save(pjob,SAVEJOB_FULL) != 0) 
    {
    job_purge(pjob);

    /* req_reject(PBSE_SYSTEM,0,preq,NULL,NULL); */

    if (LOGLEVEL >= 6)
      {
      log_record(
          PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          (pjob != NULL) ? pjob->ji_qs.ji_jobid : "NULL",
          "cannot save job");
      }

    return 1;
    }
    
  wt = set_task(WORK_Timed,time_now+1,job_clone_wt,(void*)pjob);
  /* svr_setjobstate(pj,JOB_STATE_HELD,JOB_SUBSTATE_HELD);*/
    
  /* reply_jobid(preq,pj->ji_qs.ji_jobid,BATCH_REPLY_CHOICE_Commit);*/
  return 0;
  	
  }
