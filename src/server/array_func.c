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

/* this macro is for systems like BSD4 that do not have O_SYNC in fcntl.h,
 * but do have O_FSYNC! */

#ifndef O_SYNC
#define O_SYNC O_FSYNC
#endif /* !O_SYNC */

#include <unistd.h>


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

/* global data items used */

/* list of job arrays */
extern struct server   server;
extern tlist_head svr_jobarrays;
extern tlist_head svr_jobs_array_sum;
extern char *path_arrays;
extern char *path_jobs;
extern time_t time_now;
extern int    LOGLEVEL;
extern char *pbs_o_host;


static int is_num(char *);
static int array_request_token_count(char *);
static int array_request_parse_token(char *, int *, int *);
static int parse_array_request(char *request, tlist_head *tl);



/* search job array list to determine if id is a job array */
int is_array(char *id)
  {
  char  goodId[PBS_MAXSVRJOBID];
  char *bracket_open;
  char *bracket_close;

  job_array *pa;

  if ((bracket_open = strchr(id,'[')) != NULL)
    {
    *bracket_open = '\0';
    strcpy(goodId,id);

    if ((bracket_close = strchr(bracket_open+1,']')) != NULL)
      {
      strcat(goodId,bracket_close+1);
      id = goodId;
      }
    *bracket_open = '[';
    }

  pa = (job_array*)GET_NEXT(svr_jobarrays);

  while (pa != NULL)
    {
    if (strcmp(pa->ai_qs.parent_id, id) == 0)
      {
      return TRUE;
      }

    pa = (job_array*)GET_NEXT(pa->all_arrays);
    }

  return FALSE;
  }

/* return a server's array info struct corresponding to an array id */
job_array *get_array(char *id)
  {
  job_array *pa;


  pa = (job_array*)GET_NEXT(svr_jobarrays);

  while (pa != NULL)
    {
    if (strcmp(pa->ai_qs.parent_id, id) == 0)
      {
      return pa;
      }

    if (pa == GET_NEXT(pa->all_arrays))
      {
      pa = NULL;
      }
    else
      {
      pa = (job_array*)GET_NEXT(pa->all_arrays);
      }
    }

  return (job_array*)NULL;
  }


/* save a job array struct to disk returns zero if no errors*/
int array_save(job_array *pa)

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
void array_get_parent_id(char *job_id, char *parent_id)
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


/*
 * find_array_template() - find an array template job by jobid
 *
 * Return NULL if not found or pointer to job struct if found
 */

job *find_array_template(char *arrayid)
  {
  char *at;
  char *comp;
  int   different = FALSE;

  job  *pj;

  if ((at = strchr(arrayid, (int)'@')) != NULL)
    * at = '\0'; /* strip off @server_name */

  pj = (job *)GET_NEXT(svr_jobs_array_sum);

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

  while (pj != NULL)
    {
    if (!strcmp(comp, pj->ji_qs.ji_jobid))
      break;

    pj = (job *)GET_NEXT(pj->ji_jobs_array_sum);
    }

  if (at)
    *at = '@'; /* restore @server_name */

  if (different)
    free(comp);

  return(pj);  /* may be NULL */
  }   /* END find_job() */



/* array_recov reads in  an array struct saved to disk and inserts it into
   the servers list of arrays */
job_array *array_recov(char *path)
  {
  extern tlist_head svr_jobarrays;
  job_array *pa;
  array_request_node *rn;
  int fd;
  int old_version;
  int num_tokens;
  int i;

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
    sprintf(log_buffer, "unable to read %s", path);

    log_err(errno, "pbsd_init", log_buffer);

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
      sprintf(log_buffer, "error reading token count from %s", path);
      log_err(errno, "pbsd_init", log_buffer);

      free(pa);
      close(fd);
      return NULL;
      }

    for (i = 0; i < num_tokens; i++)
      {
      rn = (array_request_node*)malloc(sizeof(array_request_node));

      if (read(fd, rn, sizeof(array_request_node)) != sizeof(array_request_node))
        {

        sprintf(log_buffer, "error reading array_request_node from %s", path);
        log_err(errno, "pbsd_init", log_buffer);

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

  /* link the struct into the servers list of job arrays */
  append_link(&svr_jobarrays, &pa->all_arrays, (void*)pa);

  return pa;

  }


/* delete a job array struct from memory and disk. This is used when the number
 *  of jobs that belong to the array becomes zero.
 *  returns zero if there are no errors, non-zero otherwise
 */
int array_delete(job_array *pa)
  {

  char path[MAXPATHLEN + 1];
  array_request_node *rn;


  /* first thing to do is take this out of the servers list of all arrays */
  delete_link(&pa->all_arrays);

  strcpy(path, path_arrays);
  strcat(path, pa->ai_qs.fileprefix);
  strcat(path, ARRAY_FILE_SUFFIX);



  /* delete the on disk copy of the struct */

  if (unlink(path))
    {
    sprintf(log_buffer, "unable to delete %s", path);
    log_err(errno, "array_delete", log_buffer);
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
  }


/* 
 * set_slot_limit()
 * sets how many jobs can be run from this array at once
 */
void set_slot_limit(

  char      *request, /* I */
  job_array *pa)      /* O */

  {
  char *pcnt;

  if ((pcnt = strchr(request,'%')) != NULL)
    {
    /* remove '%' from the request, or else it can't be parsed */
    *pcnt = '\0';

    /* read the number if one is given */
    pcnt++;
    if (strlen(pcnt) > 0)
      {
      pa->ai_qs.slot_limit = atoi(pcnt);
      }
    else
      {
      pa->ai_qs.slot_limit = NO_SLOT_LIMIT;
      }
    }
  else
    {
    pa->ai_qs.slot_limit = NO_SLOT_LIMIT;
    }

  } /* END set_slot_limit() */


int setup_array_struct(job *pjob)
  {
  job_array *pa;

  /* struct work_task *wt; */
  array_request_node *rn;
  int bad_token_count;
  int array_size;

  /* setup a link to this job array in the servers all_arrays list */
  pa = (job_array *)calloc(1,sizeof(job_array));

  pa->ai_qs.struct_version = ARRAY_QS_STRUCT_VERSION;
  
  pa->template_job = pjob;

  /*pa->ai_qs.array_size  = pjob->ji_wattr[(int)JOB_ATR_job_array_size].at_val.at_long;*/

  strcpy(pa->ai_qs.parent_id, pjob->ji_qs.ji_jobid);
  strcpy(pa->ai_qs.fileprefix, pjob->ji_qs.ji_fileprefix);
  strncpy(pa->ai_qs.owner, pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str, PBS_MAXUSER + PBS_MAXSERVERNAME + 2);
  strncpy(pa->ai_qs.submit_host, get_variable(pjob, pbs_o_host), PBS_MAXSERVERNAME);

  pa->ai_qs.num_cloned = 0;
  CLEAR_LINK(pa->all_arrays);
  CLEAR_HEAD(pa->request_tokens);
  append_link(&svr_jobarrays, &pa->all_arrays, (void*)pa);

  if (job_save(pjob, SAVEJOB_FULL) != 0)
    {
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

  set_slot_limit(pjob->ji_wattr[JOB_ATR_job_array_request].at_val.at_str, pa);

  pa->ai_qs.jobs_running = 0;
  pa->ai_qs.num_started = 0;
  pa->ai_qs.num_failed = 0;
  pa->ai_qs.num_successful = 0;
  
  bad_token_count =

    parse_array_request(pjob->ji_wattr[(int)JOB_ATR_job_array_request].at_val.at_str,
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
      job_purge(pjob);
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
    job_purge(pjob);
    array_delete(pa);
    return 2;
    }

  return 0;

  }


static int is_num(char *str)
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
  }

int array_request_token_count(char *str)
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

  }

static int array_request_parse_token(char *str, int *start, int *end)
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
  }


static int parse_array_request(char *request, tlist_head *tl)
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
  }






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

      if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
        {
        /* invalid state for request,  skip */
        continue;
        }

      if (attempt_delete((void *)pjob) == FALSE)
        num_skipped++;
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

    if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
      {
      /* invalid state for request,  skip */
      continue;
      }

    if (attempt_delete((void *)pjob) == FALSE)
      num_skipped++;
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
        
        hold_job(temphold,pa->jobs[i]);
        }
      
      /* release mem */
      to_free = rn;
      rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
      free(to_free);
      }
    }

  return(0);
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
      
      if ((rc = release_job(preq,pa->jobs[i])))
        return(rc);
      }
    
    /* release mem */
    to_free = rn;
    rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
    free(to_free);
    }

  return(0);

  } /* END release_array_range() */




int modify_array_range(

  job_array *pa,              /* I/O */
  char      *range,           /* I */
  svrattrl  *plist,           /* I */
  struct batch_request *preq, /* I */
  int        checkpoint_req)  /* I */

  {
  tlist_head tl;
  int i;

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
        
        modify_job(pa->jobs[i],plist,preq,checkpoint_req);
        }
      
      /* release mem */
      to_free = rn;
      rn = (array_request_node*)GET_NEXT(rn->request_tokens_link);
      free(to_free);
      }
    }

  return(SUCCESS);
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

      break;

    default:

      /* log error? */

      break;
    }

  set_array_depend_holds(pa);
  array_save(pa);

  } /* END update_array_values() */


void update_array_statuses()
  {
  job_array *pa;
  job *pj;
  int i;
  unsigned int running;
  unsigned int queued;
  unsigned int held;
  unsigned int complete;
  
  pa = (job_array*)GET_NEXT(svr_jobarrays);

  while (pa != NULL)
    {
    running = 0;
    queued = 0;
    held = 0;
    complete = 0;
    
    for (i = 0; i < pa->ai_qs.array_size; i++)
      {
      pj = pa->jobs[i];
      
      if (pj != NULL)
        {
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
      
    pa = (job_array*)GET_NEXT(pa->all_arrays);
    }
  }




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

  if (req_str == NULL)
    return(-1);

  ptr = strtok(req_str,delim);

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





/* END array_func.c */

