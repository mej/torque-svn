/* this file contains functions for manipulating job arrays

  included functions:

  is_array() determine if jobnum is actually an array identifyer
  get_array() return array struct for given "parent id"
  array_save() save array struct to disk
  array_get_parent_id() return id of parent job if job belongs to a job array
  array_recov() recover the array struct for a job array at restart
  array_delete() free memory used by struct and delete sved struct on disk
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h> /* INT_MAX */

/* this macro is for systems like BSD4 that do not have O_SYNC in fcntl.h,
 * but do have O_FSYNC! */

#ifndef O_SYNC
#define O_SYNC O_FSYNC
#endif /* !O_SYNC */

#include <unistd.h>
#include <pthread.h>

#include "pbs_ifl.h"
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "server.h"
#include "pbs_job.h"
#include "queue.h"
#include "pbs_error.h"
#include "svrfunc.h"
#include "work_task.h"

#include "array.h"


extern void  job_clone_wt(struct work_task *);
extern int array_upgrade(job_array *, int, int, int *);
extern char *get_correct_jobname(const char *jobid);
extern int count_user_queued_jobs(pbs_queue *,char *);
extern int copy_batchrequest(struct batch_request **newreq, struct batch_request *preq, int type, int jobid);
extern void post_modify_arrayreq(struct work_task *pwt);

/* global data items used */

/* list of job arrays */
extern struct server   server;

struct all_jobs          array_summary;
static struct all_arrays allarrays;

extern char *path_arrays;
extern char *path_jobs;
extern int    LOGLEVEL;
extern char *pbs_o_host;


static int is_num(char *);
static int array_request_token_count(char *);
static int array_request_parse_token(char *, int *, int *);
static int parse_array_request(char *request, tlist_head *tl);



/* search job array list to determine if id is a job array */

int is_array(
    
  char *id)

  {
  job_array      *pa;

  int             iter = -1;

  char      *bracket_ptr;
  char       jobid[PBS_MAXSVRJOBID];

  /* remove the extra [] if present */
  if ((bracket_ptr = strchr(id,'[')) != NULL)
    {
    if ((bracket_ptr = strchr(bracket_ptr+1,'[')) != NULL)
      {
      *bracket_ptr = '\0';
      strcpy(jobid,id);
      *bracket_ptr = '[';
      bracket_ptr = strchr(bracket_ptr+1,']');

      if (bracket_ptr != NULL)
        {
        strcat(jobid,bracket_ptr+1);
        }
      }
    else
      {
      strcpy(jobid,id);
      }
    }
  else
    {
    strcpy(jobid,id);
    }

  while ((pa = next_array(&iter)) != NULL)
    {
    if (strcmp(pa->ai_qs.parent_id, jobid) == 0)
      {
      pthread_mutex_unlock(pa->ai_mutex);

      return(TRUE);
      }

    pthread_mutex_unlock(pa->ai_mutex);
    }

  return(FALSE);
  } /* END is_array() */





/* return a server's array info struct corresponding to an array id */
job_array *get_array(
    
  char *id)

  {
  job_array      *pa;
 
  int             iter = -1;

  while ((pa = next_array(&iter)) != NULL)
    {
    if (strcmp(pa->ai_qs.parent_id, id) == 0)
      {
      return(pa);
      }

    pthread_mutex_unlock(pa->ai_mutex);
    }

  return(NULL);
  } /* END get_array() */





/* save a job array struct to disk returns zero if no errors*/
int array_save(
    
  job_array *pa)

  {
  int fds;
  char namebuf[MAXPATHLEN];
  array_request_node *rn;
  int num_tokens = 0;

  strcpy(namebuf, path_arrays);
  strcat(namebuf, pa->ai_qs.fileprefix);
  strcat(namebuf, ARRAY_FILE_SUFFIX);

  fds = open(namebuf, O_Sync | O_TRUNC | O_WRONLY | O_CREAT, 0600);

  if (fds < 0)
    {
    return -1;
    }

  if (write(fds,  &(pa->ai_qs), sizeof(struct array_info)) == -1)
    {
    unlink(namebuf);
    close(fds);
    return -1;
    }

  /* count number of request tokens left */
  for (rn = (array_request_node*)GET_NEXT(pa->request_tokens), num_tokens = 0;
       rn != NULL;
       rn = (array_request_node*)GET_NEXT(rn->request_tokens_link), num_tokens++);


  if (write(fds, &num_tokens, sizeof(num_tokens)) == -1)
    {
    unlink(namebuf);
    close(fds);
    return -1;
    }

  if (num_tokens > 0)
    {

    for (rn = (array_request_node*)GET_NEXT(pa->request_tokens); rn != NULL;
         rn = (array_request_node*)GET_NEXT(rn->request_tokens_link))
      {
      if (write(fds, rn, sizeof(array_request_node)) == -1)
        {
        unlink(namebuf);
        close(fds);
        return -1;
        }
      }
    }

  close(fds);

  return 0;
  }


/* if a job belongs to an array, this will return the id of the parent job
 * returns job id if not array parent id
 */
void array_get_parent_id(
    
  char *job_id,
  char *parent_id)

  {
  char *c;
  char *pid;
  int bracket = 0;

  c = job_id;
  *parent_id = '\0';
  pid = parent_id;

  /* copy up to the '[' */

  while (!bracket && *c != '\0')
    {
    if (*c == '[')
      {
      bracket = 1;
      }
    *pid = *c;
    c++;
    pid++;
    }

  /* skip the until the closing bracket */
  while (*c != ']' && *c != '\0')
    {
    c++;
    }

  /* copy the rest of the id */
  *pid = '\0';
  strcat(pid, c);

  }


/*
 * find_array_template() - find an array template job by jobid
 *
 * Return NULL if not found or pointer to job struct if found
 */

job *find_array_template(
    
  char *arrayid)

  {
  char *at;
  char *comp;
  int   different = FALSE;
  int   iter = -1;

  job  *pj;

  if ((at = strchr(arrayid, (int)'@')) != NULL)
    * at = '\0'; /* strip off @server_name */

  if ((server.sv_attr[SRV_ATR_display_job_server_suffix].at_flags & ATR_VFLAG_SET) ||
      (server.sv_attr[SRV_ATR_job_suffix_alias].at_flags & ATR_VFLAG_SET))
    {
    comp = get_correct_jobname(arrayid);
    different = TRUE;

    if (comp == NULL)
      return NULL;
    }
  else
    {
    comp = arrayid;
    }

  while ((pj = next_job(&array_summary,&iter)) != NULL)
    {
    if (!strcmp(comp, pj->ji_qs.ji_jobid))
      break;

    pthread_mutex_unlock(pj->ji_mutex);
    }

  if (at)
    *at = '@'; /* restore @server_name */

  if (different)
    free(comp);

  return(pj);  /* may be NULL */
  }   /* END find_array_template() */



/* array_recov reads in  an array struct saved to disk and inserts it into
   the servers list of arrays */
job_array *array_recov(
    
  char *path)

  {
  job_array          *pa;
  array_request_node *rn;
  int                 fd;
  int                 old_version;
  int                 num_tokens;
  int                 i;

  char                log_buf[LOCAL_LOG_BUF_SIZE];

  old_version = ARRAY_QS_STRUCT_VERSION;

  /* allocate the storage for the struct */
  pa = (job_array*)calloc(1,sizeof(job_array));

  if (pa == NULL)
    {
    return NULL;
    }

  /* initialize the linked list nodes */
  CLEAR_LINK(pa->all_arrays);

  CLEAR_HEAD(pa->request_tokens);

  fd = open(path, O_RDONLY, 0);

  /* read the file into the struct previously allocated.
   */

  if ((read(fd, &(pa->ai_qs), sizeof(pa->ai_qs)) != sizeof(pa->ai_qs) &&
       pa->ai_qs.struct_version == ARRAY_QS_STRUCT_VERSION) ||
      (pa->ai_qs.struct_version != ARRAY_QS_STRUCT_VERSION &&
       array_upgrade(pa, fd, pa->ai_qs.struct_version, &old_version) != 0))
    {
    sprintf(log_buf, "unable to read %s", path);

    log_err(errno, "pbsd_init", log_buf);

    free(pa);
    close(fd);
    return NULL;
    }

  pa->jobs = malloc(sizeof(job *) * pa->ai_qs.array_size);
  memset(pa->jobs,0,sizeof(job *) * pa->ai_qs.array_size);

  /* check to see if there is any additional info saved in the array file */
  /* check if there are any array request tokens that haven't been fully
  processed */

  if (old_version > 1)
    {
    if (read(fd, &num_tokens, sizeof(int)) != sizeof(int))
      {
      sprintf(log_buf, "error reading token count from %s", path);
      log_err(errno, "pbsd_init", log_buf);

      free(pa);
      close(fd);
      return NULL;
      }

    for (i = 0; i < num_tokens; i++)
      {
      rn = (array_request_node*)malloc(sizeof(array_request_node));

      if (read(fd, rn, sizeof(array_request_node)) != sizeof(array_request_node))
        {

        sprintf(log_buf, "error reading array_request_node from %s", path);
        log_err(errno, "pbsd_init", log_buf);

        free(rn);

        for (rn = (array_request_node*)GET_NEXT(pa->request_tokens);
             rn != NULL;
             rn = (array_request_node*)GET_NEXT(pa->request_tokens))
          {
          delete_link(&rn->request_tokens_link);
          free(rn);
          }

        free(pa);

        close(fd);
        return NULL;
        }

      CLEAR_LINK(rn->request_tokens_link);

      append_link(&pa->request_tokens, &rn->request_tokens_link, (void*)rn);

      }

    }

  close(fd);

  CLEAR_HEAD(pa->ai_qs.deps);

  if (old_version != ARRAY_QS_STRUCT_VERSION)
    {
    /* resave the array struct if the version on disk is older than the current */
    array_save(pa);
    }

  pa->ai_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(pa->ai_mutex,NULL);

  pthread_mutex_lock(pa->ai_mutex);

  /* link the struct into the servers list of job arrays */
  insert_array(pa);

  return(pa);
  } /* END array_recov() */


/* delete a job array struct from memory and disk. This is used when the number
 *  of jobs that belong to the array becomes zero.
 *  returns zero if there are no errors, non-zero otherwise
 */
int array_delete(
    
  job_array *pa)

  {
  char                path[MAXPATHLEN + 1];
  char                log_buf[LOCAL_LOG_BUF_SIZE];
  array_request_node *rn;

  /* first thing to do is take this out of the servers list of all arrays */
  remove_array(pa);

  /* unlock the mutex and free it */
  pthread_mutex_unlock(pa->ai_mutex);
  free(pa->ai_mutex);

  /* delete the on disk copy of the struct */

  strcpy(path, path_arrays);
  strcat(path, pa->ai_qs.fileprefix);
  strcat(path, ARRAY_FILE_SUFFIX);

  if (unlink(path))
    {
    sprintf(log_buf, "unable to delete %s", path);
    log_err(errno, "array_delete", log_buf);
    }

  /* clear array request linked list */

  for (rn = (array_request_node*)GET_NEXT(pa->request_tokens);
       rn != NULL;
       rn = (array_request_node*)GET_NEXT(pa->request_tokens))
    {
    delete_link(&rn->request_tokens_link);
    free(rn);
    }

  /* free the memory for the job pointers */
  free(pa->jobs);

  /* purge the "template" job, 
     this also deletes the shared script file for the array*/
  if (pa->template_job)
    {
    job_purge(pa->template_job);
    }

  /* free the memory allocated for the struct */
  free(pa);

  return 0;
  } /* END array_delete() */





/* 
 * set_slot_limit()
 * sets how many jobs can be run from this array at once
 *
 * @param request - the string array request
 * @param pa - the array to receive a slot limit
 *
 * @return 0 on SUCCESS
 */

int set_slot_limit(

  char      *request, /* I */
  job_array *pa)      /* O */

  {
  char *pcnt;
  int   max_limit = NO_SLOT_LIMIT;

  /* check for a max slot limit */
  if (server.sv_attr[SRV_ATR_MaxSlotLimit].at_flags & ATR_VFLAG_SET)
    {
    max_limit = server.sv_attr[SRV_ATR_MaxSlotLimit].at_val.at_long;
    }

  if ((pcnt = strchr(request,'%')) != NULL)
    {
    /* remove '%' from the request, or else it can't be parsed */
    while (*pcnt == '%')
      {
      *pcnt = '\0';
      pcnt++;
      }

    /* read the number if one is given */
    if (strlen(pcnt) > 0)
      {
      pa->ai_qs.slot_limit = atoi(pcnt);
      if ((max_limit != NO_SLOT_LIMIT) &&
          (max_limit < pa->ai_qs.slot_limit))
        {
        return(INVALID_SLOT_LIMIT);
        }
      }
    else
      {
      pa->ai_qs.slot_limit = max_limit;
      }
    }
  else
    {
    pa->ai_qs.slot_limit = max_limit;
    }

  return(0);
  } /* END set_slot_limit() */


int setup_array_struct(
    
  job *pjob)

  {
  job_array          *pa;
  array_request_node *rn;

  int                 bad_token_count;
  int                 array_size;
  int                 rc;
  char                log_buf[LOCAL_LOG_BUF_SIZE];

  /* setup a link to this job array in the servers all_arrays list */
  pa = (job_array *)calloc(1,sizeof(job_array));

  pa->ai_qs.struct_version = ARRAY_QS_STRUCT_VERSION;
  
  pa->template_job = pjob;

  strcpy(pa->ai_qs.parent_id, pjob->ji_qs.ji_jobid);
  strcpy(pa->ai_qs.fileprefix, pjob->ji_qs.ji_fileprefix);
  strncpy(pa->ai_qs.owner, pjob->ji_wattr[JOB_ATR_job_owner].at_val.at_str, PBS_MAXUSER + PBS_MAXSERVERNAME + 2);
  strncpy(pa->ai_qs.submit_host, get_variable(pjob, pbs_o_host), PBS_MAXSERVERNAME);

  pa->ai_qs.num_cloned = 0;
  CLEAR_LINK(pa->all_arrays);
  CLEAR_HEAD(pa->request_tokens);

  pa->ai_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(pa->ai_mutex,NULL);
  pthread_mutex_lock(pa->ai_mutex);

  insert_array(pa);

  if (job_save(pjob, SAVEJOB_FULL, 0) != 0)
    {
    /* the array is deleted in job_purge */
    job_purge(pjob);

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

  if ((rc = set_slot_limit(pjob->ji_wattr[JOB_ATR_job_array_request].at_val.at_str, pa)))
    {
    array_delete(pa);

    snprintf(log_buf,sizeof(log_buf),
      "Array %s requested a slot limit above the max limit %ld, rejecting\n",
      pa->ai_qs.parent_id,
      server.sv_attr[SRV_ATR_MaxSlotLimit].at_val.at_long);

    log_event(PBSEVENT_SYSTEM,PBS_EVENTCLASS_JOB,pa->ai_qs.parent_id,log_buf);

    return(INVALID_SLOT_LIMIT);
    }

  pa->ai_qs.jobs_running = 0;
  pa->ai_qs.num_started = 0;
  pa->ai_qs.num_failed = 0;
  pa->ai_qs.num_successful = 0;
  
  bad_token_count = parse_array_request(
                      pjob->ji_wattr[JOB_ATR_job_array_request].at_val.at_str,
                      &(pa->request_tokens));

  /* get the number of elements that should be allocated in the array */
  rn = (array_request_node *)GET_NEXT(pa->request_tokens);
  array_size = 0;
  pa->ai_qs.num_jobs = 0;
  while (rn != NULL) 
    {
    if (rn->end > array_size)
      array_size = rn->end;
    /* calculate the actual number of jobs (different from array size) */
    pa->ai_qs.num_jobs += rn->end - rn->start + 1;

    rn = (array_request_node *)GET_NEXT(rn->request_tokens_link);
    }

  /* size of array is the biggest index + 1 */
  array_size++; 

  if (server.sv_attr[SRV_ATR_MaxArraySize].at_flags & ATR_VFLAG_SET)
    {
    int max_array_size = server.sv_attr[SRV_ATR_MaxArraySize].at_val.at_long;
    if (max_array_size < pa->ai_qs.num_jobs)
      {
      array_delete(pa);

      return(ARRAY_TOO_LARGE);
      }
    }

  /* initialize the array */
  pa->jobs = malloc(array_size * sizeof(job *));
  memset(pa->jobs,0,array_size * sizeof(job *));

  /* remember array_size */
  pa->ai_qs.array_size = array_size;

  CLEAR_HEAD(pa->ai_qs.deps);

  array_save(pa);

  if (bad_token_count > 0)
    {
    array_delete(pa);
    return 2;
    }

  pthread_mutex_unlock(pa->ai_mutex);

  return(PBSE_NONE);
  } /* END setup_array_struct() */





static int is_num(
    
  char *str)

  {
  int i;
  int len;

  len = strlen(str);

  if (len == 0)
    {
    return 0;
    }

  for (i = 0; i < len; i++)
    {
    if (str[i] < '0' || str[i] > '9')
      {
      return 0;
      }
    }

  return 1;
  } /* END is_num() */




int array_request_token_count(
    
  char *str)

  {
  int token_count;
  int len;
  int i;


  len = strlen(str);

  token_count = 1;

  for (i = 0; i < len; i++)
    {
    if (str[i] == ',')
      {
      token_count++;
      }
    }

  return token_count;

  } /* END array_request_token_count() */




static int array_request_parse_token(
    
  char *str, 
  int *start, 
  int *end)

  {
  int num_ids;
  long start_l;
  long end_l;
  char *idx;
  char *ridx;


  idx = index(str, '-');
  ridx = rindex(str, '-');

  /* token is not a range, parse it as a single task id */
  if (idx == NULL)
    {
    /* make sure it is a number...*/
    if (!is_num(str))
      {
      start_l = -1;
      end_l = -1;
      }
    else
      {
      /* token is a number, set start_l and end_l to the value */ 
      start_l = strtol(str, NULL, 10);
      end_l = start_l;
      }
    }
  /* index and rindex found the same '-' character, this is a range */
  else if (idx == ridx)
    {
    *idx = '\0';
    idx++;

    /* check for an invalid range */
    if (!is_num(str) || !is_num(idx))
      {
      start_l = -1;
      end_l = -1;
      }
    else
      {
      /* both sides of the range were numbers so we set start_l and end_l
         we will check later to make sure that the range is "good" */
      start_l = strtol(str, NULL, 10);
      end_l = strtol(idx, NULL, 10);
      }
    }
  /* index/rindex found different '-' characters, this can't be a good range */
  else
    {
    start_l = -1;
    end_l = -1;
    }

  /* restore the string so this function is non-destructive to the token */
  if (idx != NULL && idx == ridx)
    {
    idx--;
    *idx = '-';
    }
    

  /* make sure the start or end of the range is not out of the range for 
     job array task IDs, and make sure that end_l is not less than start_l 
     (it is OK for end_l to == start_l)*/
  if (start_l < 0 || start_l >= INT_MAX || end_l < 0 || end_l >= INT_MAX
      || start_l > PBS_MAXJOBARRAY || end_l > PBS_MAXJOBARRAY || end_l < start_l)
    {
    *start = -1;
    *end = -1;
    num_ids = 0;
    }
  else
    {
    /* calculate the number of task IDs in the range, and cast the start_l and
       end_l to ints when setting start and end (we already confirmed that 
       start_l and end_l are > 0 and <= INT_MAX, so we will not under/overflow)
      */
    num_ids = end_l - start_l + 1;
    *start = (int)start_l;
    *end   = (int)end_l;
    }

  return num_ids;
  } /* END array_request_parse_token() */


static int parse_array_request(
    
  char *request, 
  tlist_head *tl)

  {
  char *temp_str;
  int num_tokens;
  char **tokens;
  int i;
  int j;
  int num_elements;
  int start;
  int end;
  int num_bad_tokens;
  int searching;
  array_request_node *rn;
  array_request_node *rn2;

  temp_str = strdup(request);
  num_tokens = array_request_token_count(request);
  num_bad_tokens = 0;

  tokens = (char**)malloc(sizeof(char*) * num_tokens);
  j = num_tokens - 1;
  /* start from back and scan backwards setting pointers to tokens and changing ',' to '\0' */

  for (i = strlen(temp_str) - 1; i >= 0; i--)
    {

    if (temp_str[i] == ',')
      {
      tokens[j--] = &temp_str[i+1];
      temp_str[i] = '\0';
      }
    else if (i == 0)
      {
      tokens[0] = temp_str;
      }
    }


  for (i = 0; i < num_tokens; i++)
    {
    num_elements = array_request_parse_token(tokens[i], &start, &end);

    if (num_elements == 0)
      {
      num_bad_tokens++;
      }
    else
      {
      rn = (array_request_node*)malloc(sizeof(array_request_node));
      rn->start = start;
      rn->end = end;
      CLEAR_LINK(rn->request_tokens_link);

      rn2 = GET_NEXT(*tl);
      searching = TRUE;

      while (searching)
        {

        if (rn2 == NULL)
          {
          append_link(tl, &rn->request_tokens_link, (void*)rn);
          searching = FALSE;
          }
        else if (rn->start < rn2->start)
          {
          insert_link(&rn2->request_tokens_link, &rn->request_tokens_link, (void*)rn,
                      LINK_INSET_BEFORE);
          searching = FALSE;
          }
        else
          {
          rn2 = GET_NEXT(rn2->request_tokens_link);
          }

        }

      rn2 = GET_PRIOR(rn->request_tokens_link);

      if (rn2 != NULL && rn2->end >= rn->start)
        {
        num_bad_tokens++;
        }

      rn2 = GET_NEXT(rn->request_tokens_link);

      if (rn2 != NULL && rn2->start <= rn->end)
        {
        num_bad_tokens++;
        }

      }
    }

  free(tokens);

  free(temp_str);

  return num_bad_tokens;
  } /* END parse_array_request() */






/*
 * delete_array_range()
 *
 * deletes a range from a specific array
 *
 * @param pa - the array whose jobs are deleted
 * @param range_str - the user-given range to delete 
 * @return - the number of jobs skipped, -1 if range error 
 */
int delete_array_range(

  job_array *pa,
  char      *range_str)

  {
  tlist_head tl;
  array_request_node *rn;
  array_request_node *to_free;
  job *pjob;
  char *range;

  int i;
  int num_skipped = 0;

  /* get just the numeric range specified, '=' should
   * always be there since we put it there in qdel */
  range = strchr(range_str,'=');
  range++; /* move past the '=' */

  CLEAR_HEAD(tl);
  if (parse_array_request(range,&tl) > 0)
    {
    /* don't delete jobs if range error */

    return(-1);
    }

  rn = (array_request_node*)GET_NEXT(tl);

  while (rn != NULL)
    {
    for (i = rn->start; i <= rn->end; i++)
      {
      if (pa->jobs[i] == NULL)
        continue;

      /* don't stomp on other memory */
      if (i >= pa->ai_qs.array_size)
        continue;

      pjob = pa->jobs[i];

      pthread_mutex_lock(pjob->ji_mutex);

      if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
        {
        /* invalid state for request,  skip */
        pthread_mutex_unlock(pjob->ji_mutex);

        continue;
        }

      if (attempt_delete((void *)pjob) == FALSE)
        {
        /* if the job was deleted, this mutex would be taked care of elsewhere. When it fails,
         * release it here */
        pthread_mutex_unlock(pjob->ji_mutex);

        num_skipped++;
        }
      }

    to_free = rn;
    rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);

    /* release mem */
    free(to_free);
    }

  return(num_skipped);
  }



/* 
 * first_job_index()
 *
 * @param pa - the array
 * @return the index of the first job in the array
 */
int first_job_index(

  job_array *pa)

  {
  int i;

  for (i = 0; i < pa->ai_qs.array_size; i++)
    {
    if (pa->jobs[i] != NULL)
      return i;
    }

  return -1;
  } /* END first_job_index() */



/* 
 * delete_whole_array()
 *
 * iterates over the array and deletes the whole thing
 * @param pa - the array to be deleted
 * @return - the number of jobs skipped
 */
int delete_whole_array(

  job_array *pa) /* I */

  {
  int i;
  int num_skipped = 0;

  job *pjob;

  for (i = 0; i < pa->ai_qs.array_size; i++)
    {
    if (pa->jobs[i] == NULL)
      continue;

    pjob = (job *)pa->jobs[i];

    pthread_mutex_lock(pjob->ji_mutex);

    if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
      {
      /* invalid state for request,  skip */
      pthread_mutex_unlock(pjob->ji_mutex);
      
      continue;
      }

    if (attempt_delete((void *)pjob) == FALSE)
      {
      /* if the job was deleted, this mutex would be taked care of elsewhere. When it fails,
       * release it here */
      pthread_mutex_unlock(pjob->ji_mutex);

      num_skipped++;
      }
    }

  return(num_skipped);
  }


/*
 * hold_array_range()
 * 
 * holds just a specified range from an array
 * @param pa - the array to be acted on
 * @param range_str - string specifying the range 
 */
int hold_array_range(

  job_array *pa,         /* O */
  char      *range_str,  /* I */
  attribute *temphold)   /* I */

  {
  tlist_head tl;
  int i;

  array_request_node *rn;
  array_request_node *to_free;
  
  char *range = strchr(range_str,'=');
  if (range == NULL)
    return(PBSE_IVALREQ);

  range++; /* move past the '=' */
  
  CLEAR_HEAD(tl);
  
  if (parse_array_request(range,&tl) > 0)
    {
    /* don't hold the jobs if range error */
    
    return(PBSE_IVALREQ);
    }
  else 
    {
    /* hold just that range from the array */
    rn = (array_request_node*)GET_NEXT(tl);
    
    while (rn != NULL)
      {
      for (i = rn->start; i <= rn->end; i++)
        {
        if (pa->jobs[i] == NULL)
          continue;
        
        /* don't stomp on other memory */
        if (i >= pa->ai_qs.array_size)
          continue;

        pthread_mutex_lock(pa->jobs[i]->ji_mutex);
        
        hold_job(temphold,pa->jobs[i]);

        pthread_mutex_unlock(pa->jobs[i]->ji_mutex);
        }
      
      /* release mem */
      to_free = rn;
      rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
      free(to_free);
      }
    }

  return(PBSE_NONE);
  } /* END hold_array_range() */




int release_array_range(

  job_array            *pa,
  struct batch_request *preq,
  char                 *range_str)

  {
  tlist_head tl;
  int i;
  int rc;

  array_request_node *rn;
  array_request_node *to_free;
  
  char *range = strchr(range_str,'=');
  if (range == NULL)
    return(PBSE_IVALREQ);

  range++; /* move past the '=' */
  
  CLEAR_HEAD(tl);
  
  if (parse_array_request(range,&tl) > 0)
    {
    /* don't hold the jobs if range error */
    
    return(PBSE_IVALREQ);
    }
  
  /* hold just that range from the array */
  rn = (array_request_node*)GET_NEXT(tl);
  
  while (rn != NULL)
    {
    for (i = rn->start; i <= rn->end; i++)
      {
      if (pa->jobs[i] == NULL)
        continue;
      
      /* don't stomp on other memory */
      if (i >= pa->ai_qs.array_size)
        continue;

      pthread_mutex_lock(pa->jobs[i]->ji_mutex);
      
      if ((rc = release_job(preq,pa->jobs[i])))
        {
        pthread_mutex_unlock(pa->jobs[i]->ji_mutex);
        
        return(rc);
        }

      pthread_mutex_unlock(pa->jobs[i]->ji_mutex);
      }
    
    /* release mem */
    to_free = rn;
    rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
    free(to_free);
    }

  return(PBSE_NONE);
  } /* END release_array_range() */




int modify_array_range(

  job_array *pa,              /* I/O */
  char      *range,           /* I */
  svrattrl  *plist,           /* I */
  struct batch_request *preq, /* I */
  int        checkpoint_req)  /* I */

  {
  char                id[] = "modify_array_range";
  char                log_buf[LOCAL_LOG_BUF_SIZE];
  tlist_head          tl;
  int                 i;
  int                 rc;
  int                 mom_relay = 0;

  array_request_node *rn;
  array_request_node *to_free;
  
  CLEAR_HEAD(tl);
  
  if (parse_array_request(range,&tl) > 0)
    {
    /* don't hold the jobs if range error */
    
    return(FAILURE);
    }
  else 
    {
    /* hold just that range from the array */
    rn = (array_request_node*)GET_NEXT(tl);
    
    while (rn != NULL)
      {
      for (i = rn->start; i <= rn->end; i++)
        {
        if ((i >= pa->ai_qs.array_size) ||
            (pa->jobs[i] == NULL))
          continue;
  
        pthread_mutex_lock(pa->jobs[i]->ji_mutex);

        rc = modify_job(pa->jobs[i],plist,preq,checkpoint_req, NO_MOM_RELAY);

        if (rc == PBSE_RELAYED_TO_MOM)
          {
          struct batch_request *array_req = NULL;
          
          /* We told modify_job not to call relay_to_mom so we need to contact the mom */
          rc = copy_batchrequest(&array_req, preq, 0, i);
          if (rc != 0)
            {
            return(rc);
            }
          
          preq->rq_refcount++;
          if (mom_relay == 0)
            {
            preq->rq_refcount++;
            }
          mom_relay++;
          if ((rc = relay_to_mom(
                      pa->jobs[i],
                      array_req,
                      post_modify_arrayreq)))
            {  
            snprintf(log_buf,sizeof(log_buf),
              "Unable to relay information to mom for job '%s'\n",
              pa->jobs[i]->ji_qs.ji_jobid);
            log_err(rc,id,log_buf);
          
            pthread_mutex_unlock(pa->jobs[i]->ji_mutex);

            return(rc); /* unable to get to MOM */
            }
        
          }

        pthread_mutex_unlock(pa->jobs[i]->ji_mutex);
        }
      
      /* release mem */
      to_free = rn;
      rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
      free(to_free);
      }
    }

  if (mom_relay)
    {
    preq->rq_refcount--;
    if (preq->rq_refcount == 0)
      {
      free_br(preq);
      }
    return(PBSE_RELAYED_TO_MOM);
    }

  return(PBSE_NONE);
  } /* END modify_array_range() */



/**
 * update_array_values()
 *
 * updates internal bookeeping values for job arrays
 * @param pa - array to update
 * @param pjob - the pjob that an event happened on
 * @param event - code for what event just happened
 */
void update_array_values(

  job_array            *pa,        /* I */
  void                 *j,         /* I */
  int                   old_state, /* I */
  enum ArrayEventsEnum  event)     /* I */

  {
  job *pjob = (job *)j;
  int exit_status;

  switch (event)
    {
    case aeQueue:

      /* NYI, nothing needs to be done for this yet */

      break;

    case aeRun:

      if (old_state != JOB_STATE_RUNNING)
        {
        pa->ai_qs.jobs_running++;
        pa->ai_qs.num_started++;
        }

      break;

    case aeTerminate:

      exit_status = pjob->ji_qs.ji_un.ji_exect.ji_exitstat;
      if (old_state == JOB_STATE_RUNNING)
        {
        if (pa->ai_qs.jobs_running > 0)
          pa->ai_qs.jobs_running--;
        }

      if (exit_status == 0)
        {
        pa->ai_qs.num_successful++;
        }
      else
        {
        pa->ai_qs.num_failed++;
        }

      /* update slot limit hold if necessary */
      if (server.sv_attr[SRV_ATR_MoabArrayCompatible].at_val.at_long != FALSE)
        {
        /* only need to update if the job wasn't previously held */
        if ((pjob->ji_wattr[JOB_ATR_hold].at_val.at_long & HOLD_l) == FALSE)
          {
          int  i;
          int  newstate;
          int  newsub;
          job *pj;

          /* find the first held job and release its hold */
          for (i = 0; i < pa->ai_qs.array_size; i++)
            {
            if (pa->jobs[i] == NULL)
              continue;

            pj = (job *)pa->jobs[i];

            if (pj->ji_wattr[JOB_ATR_hold].at_val.at_long & HOLD_l)
              {
              pj->ji_wattr[JOB_ATR_hold].at_val.at_long &= ~HOLD_l;

              if (pj->ji_wattr[JOB_ATR_hold].at_val.at_long == 0)
                {
                pj->ji_wattr[JOB_ATR_hold].at_flags &= ~ATR_VFLAG_SET;
                }
             
              svr_evaljobstate(pj, &newstate, &newsub, 1);
              svr_setjobstate(pj, newstate, newsub);
              job_save(pj, SAVEJOB_FULL, 0);

              break;
              }
            }
          }
        }

      break;

    default:

      /* log error? */

      break;
    }

  set_array_depend_holds(pa);
  array_save(pa);

  } /* END update_array_values() */


void update_array_statuses(job_array *owned)

  {
  job_array      *pa;
  job            *pj;
  int             i;
  int             j;
  unsigned int    running;
  unsigned int    queued;
  unsigned int    held;
  unsigned int    complete;

  for (j = 0; j < allarrays.ra->max; j++)
    {
    pa = (job_array *)allarrays.ra->slots[j].item;

    if (pa == NULL)
      continue;

    if (pa != owned)
      pthread_mutex_lock(pa->ai_mutex);

    running = 0;
    queued = 0;
    held = 0;
    complete = 0;
    
    for (i = 0; i < pa->ai_qs.array_size; i++)
      {
      pj = pa->jobs[i];
      
      if (pj != NULL)
        {
        pthread_mutex_lock(pj->ji_mutex);

        if (pj->ji_qs.ji_state == JOB_STATE_RUNNING)
          {
          running++;
          }
        else if (pj->ji_qs.ji_state == JOB_STATE_QUEUED)
          {
          queued++;
          }
        else if (pj->ji_qs.ji_state == JOB_STATE_HELD)
          {
          held++;
          }
        else if (pj->ji_qs.ji_state == JOB_STATE_COMPLETE)
          {
          complete++;
          }
        
        pthread_mutex_unlock(pj->ji_mutex);
        }
      }
    
    if (running > 0)
      {
      svr_setjobstate(pa->template_job, JOB_STATE_RUNNING, pa->template_job->ji_qs.ji_substate);
      }
    else if (held > 0 && queued == 0 && complete == 0)
      {
      svr_setjobstate(pa->template_job, JOB_STATE_HELD, pa->template_job->ji_qs.ji_substate);
      }
    else if (complete > 0 && queued == 0 && held == 0)
      {
      svr_setjobstate(pa->template_job, JOB_STATE_COMPLETE, pa->template_job->ji_qs.ji_substate);
      }
    else 
      {
      /* default to just calling the array queued */
      svr_setjobstate(pa->template_job, JOB_STATE_QUEUED, pa->template_job->ji_qs.ji_substate);
      }
      
    if (pa != owned)
      pthread_mutex_unlock(pa->ai_mutex);
    }
  } /* END update_array_statuses() */




/* num_array_jobs()
 *
 * determine the number of jobs in the array from the array request 
 *
 * @param req_str - the string of the array request
 * @return - the number of jobs in the array, -1 on error 
 */

int num_array_jobs(

  char *req_str) /* I */

  {
  int   num_jobs = 0;
  int   start;
  int   end;

  char *delim = ",";
  char *ptr;
  char *dash;

  char  tmp_str[MAXPATHLEN];

  if (req_str == NULL)
    return(-1);

  strcpy(tmp_str,req_str);
  ptr = strtok(tmp_str,delim);

  while (ptr != NULL)
    {
    if ((dash = strchr(ptr,'-')) != NULL)
      {
      /* this is a range */
      start = atoi(ptr);
      end   = atoi(dash+1);

      /* check for invalid range */
      if (end < start)
        return(-1);

      num_jobs += end - start + 1;
      }
    else
      {
      /* just one job */
      num_jobs++;
      }

    ptr = strtok(NULL,delim);
    }

  return(num_jobs);
  } /* END num_array_jobs */




/*
 * initializes the array to store all job_array pointers 
 */
void initialize_all_arrays_array()

  {
  allarrays.ra = initialize_resizable_array(INITIAL_NUM_ARRAYS);

  allarrays.allarrays_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(allarrays.allarrays_mutex,NULL);
  } /* END initialize_all_arrays_array() */





/* 
 * insert pa into the global array 
 */
int insert_array(

  job_array *pa)

  {
  static char  *id = "insert_array";
  int           rc;

  pthread_mutex_lock(allarrays.allarrays_mutex);

  if ((rc = insert_thing(allarrays.ra,pa)) == ENOMEM)
    {
    log_err(rc,id,"No memory to resize the array...SYSTEM FAILURE\n");
    }

  pthread_mutex_unlock(allarrays.allarrays_mutex);

  return(rc);
  } /* END insert_array() */





int remove_array(

  job_array *pa)

  {
  int rc;

  if (pthread_mutex_trylock(allarrays.allarrays_mutex))
    {
    pthread_mutex_unlock(pa->ai_mutex);
    pthread_mutex_lock(allarrays.allarrays_mutex);
    pthread_mutex_lock(pa->ai_mutex);
    }

  rc = remove_thing(allarrays.ra,pa);

  pthread_mutex_unlock(allarrays.allarrays_mutex);

  return(rc);
  } /* END remove_array() */





job_array *next_array(

  int *iter)

  {
  job_array *pa = NULL;

  pthread_mutex_lock(allarrays.allarrays_mutex);

  pa = (job_array *)next_thing(allarrays.ra,iter);

  pthread_mutex_unlock(allarrays.allarrays_mutex);

  if (pa != NULL)
    pthread_mutex_lock(pa->ai_mutex);

  return(pa);
  } /* END next_array() */


/* END array_func.c */

