#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>
#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "queue.h"
#include "pbs_error.h"
#include "acct.h"
#include "log.h"
#include "../lib/Liblog/log_event.h"
#include "svrfunc.h"

#include "array.h"

extern int LOGLEVEL;
extern int  svr_authorize_req(struct batch_request *preq, char *owner, char *submit_host);
extern void job_purge(job *pjob);

extern struct work_task *apply_job_delete_nanny(struct job *, int);
extern int has_job_delete_nanny(struct job *);
extern void remove_stagein(job *pjob);
extern void change_restart_comment_if_needed(struct job *);

extern char *msg_unkarrayid;
extern char *msg_permlog;

static void post_delete(struct work_task *pwt);

void array_delete_wt(struct work_task *ptask);


/**
 * attempt_delete()
 * deletes a job differently depending on the job's state
 *
 * @return TRUE if the job was deleted, FALSE if skipped
 * @param pjob - a pointer to the job being handled
 */
int attempt_delete(

  void *j) /* I */

  {
  int        skipped = FALSE;
  int        iter = -1;
  int        release_mutex = TRUE;

  char      *jobid_copy;
  work_task *pwtold;
  job       *pjob;
  time_t     time_now = time(NULL);

  /* job considered deleted if null */
  if (j == NULL)
    return(TRUE);

  pjob = (job *)j;

  if (pjob->ji_qs.ji_state == JOB_STATE_TRANSIT)
    {
    /*
     * Find pid of router from existing work task entry,
     * then establish another work task on same child.
     * Next, signal the router and wait for its completion;
     */
    
    while ((pwtold = next_task(pjob->ji_svrtask,&iter)) != NULL)
      {
      if ((pwtold->wt_type == WORK_Deferred_Child) ||
          (pwtold->wt_type == WORK_Deferred_Cmp))
        {
        kill((pid_t)pwtold->wt_event, SIGTERM);
        
        pjob->ji_qs.ji_substate = JOB_SUBSTATE_ABORT;
        }
      
      pthread_mutex_unlock(pwtold->wt_mutex);
      }

    skipped = TRUE;
    
    return(!skipped);
    }  /* END if (pjob->ji_qs.ji_state == JOB_SUBSTATE_TRANSIT) */

  else if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN)
    {
    /* we'll wait for the mom to get this job, then delete it */
    skipped = TRUE;
    }  /* END if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN) */

  else if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /* set up nanny */
    
    if (!has_job_delete_nanny(pjob))
      {
      apply_job_delete_nanny(pjob, time_now + 60);
      
      /* need to issue a signal to the mom, but we don't want to sent an ack to the
       * client when the mom replies */
      issue_signal(pjob, "SIGTERM", post_delete, NULL);
      }

    if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0)
      {
      /* job has restart file at mom, change restart comment if failed */
      change_restart_comment_if_needed(pjob);
      }
    
    return(!skipped);
    }  /* END if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING) */

  if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0)
    {
    /* job has restart file at mom, change restart comment if failed */    
    change_restart_comment_if_needed(pjob);
    
    /* job has restart file at mom, do end job processing */
    svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_EXITING);

    pjob->ji_momhandle = -1;
    
    /* force new connection */
    jobid_copy = strdup(pjob->ji_qs.ji_jobid);
    set_task(WORK_Immed, 0, on_job_exit, jobid_copy, FALSE);
    }
  else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_StagedIn) != 0)
    {
    /* job has staged-in file, should remove them */
    
    remove_stagein(pjob);
    
    job_abt(&pjob, NULL);

    release_mutex = FALSE;
    }
  else
    {
    /*
     * the job is not transitting (though it may have been) and
     * is not running, so put in into a complete state.
     */

    struct work_task *ptask;
    struct pbs_queue *pque;
    int  KeepSeconds = 0;

    svr_setjobstate(pjob, JOB_STATE_COMPLETE, JOB_SUBSTATE_COMPLETE);
    
    if ((pque = get_jobs_queue(pjob)) != NULL)
      {
      pque->qu_numcompleted++;

      unlock_queue(pque, "attempt_delete", NULL, LOGLEVEL);
      }
    
    KeepSeconds = attr_ifelse_long(
        &pque->qu_attr[QE_ATR_KeepCompleted],
        &server.sv_attr[SRV_ATR_KeepCompleted],
        0);

    jobid_copy = strdup(pjob->ji_qs.ji_jobid);

    ptask = set_task(WORK_Timed, time_now + KeepSeconds, on_job_exit, jobid_copy, TRUE);
    
    if (ptask != NULL)
      {
      insert_task(pjob->ji_svrtask, ptask, TRUE);

      pthread_mutex_unlock(ptask->wt_mutex);
      }
    }

  if (release_mutex == TRUE)
    pthread_mutex_unlock(pjob->ji_mutex);

  return(!skipped);
  } /* END attempt_delete() */







void req_deletearray(
    
  struct batch_request *preq)

  {
  job_array        *pa;

  char             *range;

  struct work_task *ptask;
  char              log_buf[LOCAL_LOG_BUF_SIZE];

  int               num_skipped;
  char              owner[PBS_MAXUSER + 1];
  time_t            time_now = time(NULL);

  pa = get_array(preq->rq_ind.rq_delete.rq_objname);

  if (pa == NULL)
    {
    reply_ack(preq);
    return;
    }

  /* check authorization */
  get_jobowner(pa->ai_qs.owner, owner);

  if (svr_authorize_req(preq, owner, pa->ai_qs.submit_host) == -1)
    {
    sprintf(log_buf, msg_permlog,
      preq->rq_type,
      "Array",
      preq->rq_ind.rq_delete.rq_objname,
      preq->rq_user,
      preq->rq_host);

    log_event(PBSEVENT_SECURITY,PBS_EVENTCLASS_JOB,preq->rq_ind.rq_delete.rq_objname,log_buf);

    pthread_mutex_unlock(pa->ai_mutex);

    req_reject(PBSE_PERM, 0, preq, NULL, "operation not permitted");
    return;
    }

  /* get the range of jobs to iterate over */
  range = preq->rq_extend;
  if ((range != NULL) &&
      (strstr(range,ARRAY_RANGE) != NULL))
    {
    /* parse the array range */
    num_skipped = delete_array_range(pa,range);

    if (num_skipped < 0)
      {
      /* ERROR */
      pthread_mutex_unlock(pa->ai_mutex);

      req_reject(PBSE_IVALREQ,0,preq,NULL,"Error in specified array range");
      return;
      }
    }
  else
    {
    num_skipped = delete_whole_array(pa);
    }

  pthread_mutex_unlock(pa->ai_mutex);

  /* check if the array is gone */
  if ((pa = get_array(preq->rq_ind.rq_delete.rq_objname)) != NULL)
    {
    pthread_mutex_unlock(pa->ai_mutex);

    /* some jobs were not deleted.  They must have been running or had
       JOB_SUBSTATE_TRANSIT */
    if (num_skipped != 0)
      {
      ptask = set_task(WORK_Timed, time_now + 2, array_delete_wt, preq, FALSE);

      if (ptask)
        {
        return;
        }
      }
    }

  /* now that the whole array is deleted, we should mail the user if necessary */

  reply_ack(preq);

  return;
  }



static void post_delete(
    
  struct work_task *pwt)

  {
  /* no op - do not reply to client */

  if (pwt)
    free(pwt);
  }


/* if jobs were in the prerun state , this attempts to keep track
   of if it was called continuously on the same array for over 10 seconds.
   If that is the case then it deletes prerun jobs no matter what.
   if it has been less than 10 seconds or if there are jobs in
   other statest then req_deletearray is called again */

void array_delete_wt(
    
  struct work_task *ptask)

  {
  struct batch_request *preq;
  job_array            *pa;

  int                   i;

  static int            last_check = 0;
  static char          *last_id = NULL;
  char                 *jobid_copy;
  time_t                time_now = time(NULL);

  preq = ptask->wt_parm1;

  pa = get_array(preq->rq_ind.rq_delete.rq_objname);

  if (pa == NULL)
    {
    /* jobs must have exited already */
    reply_ack(preq);
    last_check = 0;
    free(last_id);
    last_id = NULL;

    free(ptask);
    return;
    }

  if (last_id == NULL)
    {
    last_id = strdup(preq->rq_ind.rq_delete.rq_objname);
    last_check = time_now;
    }
  else if (strcmp(last_id, preq->rq_ind.rq_delete.rq_objname) != 0)
    {
    last_check = time_now;
    free(last_id);
    last_id = strdup(preq->rq_ind.rq_delete.rq_objname);
    }
  else if (time_now - last_check > 10)
    {
    int num_jobs;
    int num_prerun;
    job *pjob;

    num_jobs = 0;
    num_prerun = 0;

    for (i = 0; i < pa->ai_qs.array_size; i++)
      {
      if (pa->jobs[i] == NULL)
        continue;

      pjob = (job *)pa->jobs[i];

      pthread_mutex_lock(pjob->ji_mutex);

      if (pjob->ji_being_recycled == TRUE)
        {
        pa->jobs[i] = NULL;
        pthread_mutex_unlock(pjob->ji_mutex);
        continue;
        }

      num_jobs++;

      if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN)
        {
        num_prerun++;
        /* mom still hasn't gotten job?? delete anyway */

        if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0)
          {
          /* job has restart file at mom, do end job processing */

          change_restart_comment_if_needed(pjob);

          svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_EXITING);

          pjob->ji_momhandle = -1;

          /* force new connection */
          jobid_copy = strdup(pjob->ji_qs.ji_jobid);

          set_task(WORK_Immed, 0, on_job_exit, jobid_copy, FALSE);

          pthread_mutex_unlock(pjob->ji_mutex);
          }
        else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_StagedIn) != 0)
          {
          /* job has staged-in file, should remove them */

          remove_stagein(pjob);

          job_abt(&pjob, NULL);
          }
        else
          {
          job_abt(&pjob, NULL);
          }
        } /* END if (ji_substate == JOB_SUBSTATE_PRERUN) */
      } /* END for each job in array */

    if (num_jobs == num_prerun)
      {
      reply_ack(preq);
      free(last_id);
      last_id = NULL;

      pthread_mutex_unlock(pa->ai_mutex);

      free(ptask);

      return;
      }
    }

  pthread_mutex_unlock(pa->ai_mutex);

  req_deletearray(preq);

  free(ptask);
  } /* END array_delete_wt() */


