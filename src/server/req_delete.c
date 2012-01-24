/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * req_delete.c
 *
 * Functions relating to the Delete Job Batch Requests.
 *
 * Included funtions are:
 *
 *
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <pthread.h>

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <errno.h>
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
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "svrfunc.h"
#include "array.h"
#include "utils.h"
#include "svr_func.h" /* get_svr_attr_* */


#define PURGE_SUCCESS 1
#define MOM_DELETE    2
#define ROUTE_DELETE  3

/* Global Data Items: */

extern char *msg_deletejob;
extern char *msg_delrunjobsig;
extern char *msg_manager;
extern char *msg_permlog;
extern char *msg_badstate;

extern struct server server;
extern int   LOGLEVEL;
extern struct all_jobs alljobs;

/* Private Functions in this file */

static void post_delete_route(struct work_task *);
static void post_delete_mom1(struct work_task *);
static void post_delete_mom2(struct work_task *);
static int forced_jobpurge(job *,struct batch_request *);
static void job_delete_nanny(struct work_task *);
static void post_job_delete_nanny(struct work_task *);
static void purge_completed_jobs(struct batch_request *);

/* Public Functions in this file */

int  apply_job_delete_nanny(struct job *, int);
void change_restart_comment_if_needed(struct job *);

/* Private Data Items */

static char *deldelaystr = DELDELAY;
static char *delpurgestr = DELPURGE;
static char *delasyncstr = DELASYNC;

/* Extern Functions */

extern void set_resc_assigned(job *, enum batch_op);
extern job  *chk_job_request(char *, struct batch_request *);
extern struct batch_request *cpy_stage(struct batch_request *, job *, enum job_atr, int);
extern int   svr_chk_owner(struct batch_request *, job *);
void chk_job_req_permissions(job **,struct batch_request *);
void          on_job_exit(struct work_task *);

/*
 * remove_stagein() - request that mom delete staged-in files for a job
 * used when the job is to be purged after files have been staged in
 */

void remove_stagein(

  job **pjob_ptr)  /* I */

  {

  struct batch_request *preq = 0;
  job                  *pjob = *pjob_ptr;
  u_long                addr;

  preq = cpy_stage(preq, pjob, JOB_ATR_stagein, 0);

  if (preq != NULL)
    {
    /* have files to delete  */

    /* change the request type from copy to delete  */

    preq->rq_type = PBS_BATCH_DelFiles;

    preq->rq_extra = NULL;

    addr = pjob->ji_qs.ji_un.ji_exect.ji_momaddr;
    addr += pjob->ji_qs.ji_un.ji_exect.ji_mom_rmport;
    addr += pjob->ji_qs.ji_un.ji_exect.ji_momport;

    /* The preq is freed in relay_to_mom (failure)
     * or in issue_Drequest (success) */
    if (relay_to_mom(&pjob, preq, release_req) == 0)
      {
      if (pjob != NULL)
        pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_StagedIn;
      }
    else
      {
      /* log that we were unable to remove the files */

      log_event(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_FILE,
        pjob->ji_qs.ji_jobid,
        "unable to remove staged in files for job");
      }
    }

  return;
  }  /* END remove_stagein() */




void force_purge_work(

  job *pjob)

  {
  char       log_buf[LOCAL_LOG_BUF_SIZE];
  pbs_queue *pque;

  snprintf(log_buf, sizeof(log_buf), "purging job %s without checking MOM", pjob->ji_qs.ji_jobid);
  log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);
  
  free_nodes(pjob);

  if ((pque = get_jobs_queue(pjob)) != NULL)
    {
    if (pjob->ji_qhdr->qu_qs.qu_type == QTYPE_Execution)
      {
      unlock_queue(pque, __func__, NULL, LOGLEVEL);
      set_resc_assigned(pjob, DECR);
      }
    else
      unlock_queue(pque, __func__, NULL, LOGLEVEL);
    }
  
  job_purge(pjob);
  } /* END force_purge_work() */





void ensure_deleted(

  struct work_task *ptask)  /* I */

  {
  job                  *pjob;
  char                 *jobid;

  jobid = ptask->wt_parm1;

  if (jobid != NULL)
    {
    if ((pjob = find_job(jobid)) != NULL)
      {
      force_purge_work(pjob);
      }
    }

  free(jobid);
  free(ptask->wt_mutex);
  free(ptask);
  } /* END ensure_deleted() */





int execute_job_delete(

  job                  *pjob,            /* M */
  char                 *Msg,             /* I */
  struct batch_request *preq)            /* I */

  {
  static char      *id = "execute_job_delete";
  struct work_task *pwtnew;

  int               rc;
  char             *sigt = "SIGTERM";
  char             *jobid_copy;

  int               has_mutex = TRUE;
  char              log_buf[LOCAL_LOG_BUF_SIZE];
  time_t            time_now = time(NULL);
  long              force_cancel = FALSE;
  long              array_compatible = FALSE;

  chk_job_req_permissions(&pjob,preq);

  if (pjob == NULL)
    {
    /* preq is rejected in chk_job_req_permissions here */
    return(-1);
    }

  if (pjob->ji_qs.ji_state == JOB_STATE_TRANSIT)
    {
    /* see note in req_delete - not sure this is possible still,
     * but the deleted code is irrelevant now. I will leave this
     * part --dbeer */
    pthread_mutex_unlock(pjob->ji_mutex);

    return(-1);
    }

  if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN ||
      pjob->ji_qs.ji_substate == JOB_SUBSTATE_RERUN ||
      pjob->ji_qs.ji_substate == JOB_SUBSTATE_RERUN1 ||
      pjob->ji_qs.ji_substate == JOB_SUBSTATE_RERUN2 ||
      pjob->ji_qs.ji_substate == JOB_SUBSTATE_RERUN3 )
    {
    /* If JOB_SUBSTATE_PRERUN being sent to MOM, wait till she gets it going */
    /* retry in one second                            */
    /* If JOB_SUBSTATE_RERUN, RERUN1, RERUN2 or RERUN3 the
       job is being requeued. Wait until finished */

    static time_t  cycle_check_when = 0;
    static char    cycle_check_jid[PBS_MAXSVRJOBID + 1];

    if (cycle_check_when != 0)
      {
      if (!strcmp(pjob->ji_qs.ji_jobid, cycle_check_jid) &&
          (time_now - cycle_check_when > 10))
        {
        /* state not updated after 10 seconds */

        /* did the mom ever get it? delete it anyways... */

        cycle_check_jid[0] = '\0';
        cycle_check_when  = 0;

        goto jump;
        }

      if (time_now - cycle_check_when > 20)
        {
        /* give up after 20 seconds */

        cycle_check_jid[0] = '\0';
        cycle_check_when  = 0;
        }
      }    /* END if (cycle_check_when != 0) */

    if (cycle_check_when == 0)
      {
      /* new PRERUN job located */

      cycle_check_when = time_now;
      strcpy(cycle_check_jid, pjob->ji_qs.ji_jobid);
      }

    sprintf(log_buf, "job cannot be deleted, state=PRERUN, requeuing delete request");

    log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

    pwtnew = set_task(WORK_Timed,time_now + 1,post_delete_route,preq,FALSE);
    
    pthread_mutex_unlock(pjob->ji_mutex);

    if (pwtnew == NULL)
      {
      req_reject(PBSE_SYSTEM, 0, preq, NULL, NULL);

      return(-1);
      }
    else
      {
      return(ROUTE_DELETE);
      }
    }  /* END if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN) */

jump:

  /*
   * Log delete and if requesting client is not job owner, send mail.
   */

  sprintf(log_buf, "requestor=%s@%s", preq->rq_user, preq->rq_host);


  /* NOTE:  should annotate accounting record with extend message (NYI) */
  account_record(PBS_ACCT_DEL, pjob, log_buf);

  sprintf(log_buf, msg_manager, msg_deletejob, preq->rq_user, preq->rq_host);

  log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

  /* NOTE:  should incorporate job delete message */

  if (Msg != NULL)
    {
    /* have text message in request extension, add it */
    strcat(log_buf, "\n");
    strcat(log_buf, Msg);
    }

  if ((svr_chk_owner(preq, pjob) != 0) &&
      (pjob->ji_has_delete_nanny == FALSE))
    {
    /* only send email if owner did not delete job and job deleted
       has not been previously attempted */

    svr_mailowner(pjob, MAIL_DEL, MAIL_FORCE, log_buf);
    /*
     * If we sent mail and already sent the extra message
     * then reset message so we don't trigger a redundant email
     * in job_abt()
    */

    if (Msg != NULL)
      {
      Msg = NULL;
      }
    }

  if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0)
    {
    /* job has restart file at mom, change restart comment if failed */

    change_restart_comment_if_needed(pjob);
    }

  if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /*
     * setup a nanny task to make sure the job is actually deleted (see the
     * comments at job_delete_nanny()).
     */

    if (pjob->ji_has_delete_nanny == TRUE)
      {
      pthread_mutex_unlock(pjob->ji_mutex);

      req_reject(PBSE_IVALREQ, 0, preq, NULL, "job cancel in progress");

      return(-1);
      }

    apply_job_delete_nanny(pjob, time_now + 60);

    /*
     * Send signal request to MOM.  The server will automagically
     * pick up and "finish" off the client request when MOM replies.
     */

    if ((rc = issue_signal(&pjob, sigt, post_delete_mom1, preq)))
      {
      /* cant send to MOM */

      req_reject(rc, 0, preq, NULL, NULL);
      }

    /* normally will ack reply when mom responds */
    if (pjob != NULL)
      {
      sprintf(log_buf, msg_delrunjobsig, sigt);
      log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);
  
      pthread_mutex_unlock(pjob->ji_mutex);
      }

    return(-1);
    }  /* END if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING) */

  /* make a cleanup task if set */
  get_svr_attr_l(SRV_ATR_JobForceCancelTime, &force_cancel);
  if (force_cancel > 0)
    {
    char *dup_jobid = strdup(pjob->ji_qs.ji_jobid);
 
    set_task(WORK_Timed, time_now + force_cancel, ensure_deleted, dup_jobid, FALSE);    
    }

  /* if configured, and this job didn't have a slot limit hold, free a job
   * held with the slot limit hold */
  get_svr_attr_l(SRV_ATR_MoabArrayCompatible, &array_compatible);
  if ((array_compatible != FALSE) &&
      ((pjob->ji_wattr[JOB_ATR_hold].at_val.at_long & HOLD_l) == FALSE))
    {
    if ((pjob->ji_arraystruct != NULL) &&
        (pjob->ji_is_array_template == FALSE))
      {
      int        i;
      int        newstate;
      int        newsub;
      job       *tmp;
      job_array *pa = get_jobs_array(pjob);

      for (i = 0; i < pa->ai_qs.array_size; i++)
        {
        if (pa->job_ids[i] == NULL)
          continue;

        if (!strcmp(pa->job_ids[i], pjob->ji_qs.ji_jobid))
          continue;

        if ((tmp = find_job(pa->job_ids[i])) == NULL)
          {
          free(pa->job_ids[i]);
          pa->job_ids[i] = NULL;
          }
        else
          {
          if (tmp->ji_wattr[JOB_ATR_hold].at_val.at_long & HOLD_l)
            {
            tmp->ji_wattr[JOB_ATR_hold].at_val.at_long &= ~HOLD_l;
            
            if (tmp->ji_wattr[JOB_ATR_hold].at_val.at_long == 0)
              {
              tmp->ji_wattr[JOB_ATR_hold].at_flags &= ~ATR_VFLAG_SET;
              }
            
            svr_evaljobstate(tmp, &newstate, &newsub, 1);
            svr_setjobstate(tmp, newstate, newsub, FALSE);
            job_save(tmp, SAVEJOB_FULL, 0);

            pthread_mutex_unlock(tmp->ji_mutex);
            
            break;
            }

          pthread_mutex_unlock(tmp->ji_mutex);
          }
        }

      if (LOGLEVEL >= 7)
        {
        sprintf(log_buf, "%s: unlocking ai_mutex", id);
        log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);
        }
      pthread_mutex_unlock(pa->ai_mutex);
      }
    } /* END MoabArrayCompatible check */

  if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0)
    {
    /* job has restart file at mom, do end job processing */
    
    svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_EXITING, FALSE);

    pjob->ji_momhandle = -1;

    /* force new connection */
    jobid_copy = strdup(pjob->ji_qs.ji_jobid);

    if(LOGLEVEL >= 7)
      {
      sprintf(log_buf, "calling on_job_exit from %s", id);
      log_event(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        log_buf);
      }
    set_task(WORK_Immed, 0, on_job_exit, jobid_copy, FALSE);
    }
  else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_StagedIn) != 0)
    {
    /* job has staged-in file, should remove them */

    remove_stagein(&pjob);

    if (pjob != NULL)
      job_abt(&pjob, Msg);

    has_mutex = FALSE;
    }
  else
    {
    /*
     * the job is not transitting (though it may have been) and
     * is not running, so put in into a complete state.
     */
    struct pbs_queue *pque;
    int  KeepSeconds = 0;

    svr_setjobstate(pjob, JOB_STATE_COMPLETE, JOB_SUBSTATE_COMPLETE, FALSE);

    if ((pque = get_jobs_queue(pjob)) != NULL)
      {
      pque->qu_numcompleted++;

      unlock_queue(pque, id, NULL, LOGLEVEL);
      
      if (LOGLEVEL >= 7)
        {
        sprintf(log_buf, "calling on_job_exit from %s", id);
        log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid, log_buf);
        }
    
      pthread_mutex_lock(server.sv_attr_mutex);
      KeepSeconds = attr_ifelse_long(
                    &pque->qu_attr[QE_ATR_KeepCompleted],
                    &server.sv_attr[SRV_ATR_KeepCompleted],
                    0);
      pthread_mutex_unlock(server.sv_attr_mutex);
      }
    else
      KeepSeconds = 0;

    jobid_copy = strdup(pjob->ji_qs.ji_jobid);

    set_task(WORK_Timed, time_now + KeepSeconds, on_job_exit, jobid_copy, FALSE);
    }  /* END else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHECKPOINT_FILE) != 0) */

  if (has_mutex == TRUE)
    pthread_mutex_unlock(pjob->ji_mutex);

  return(PBSE_NONE);
  } /* END execute_job_delete() */




struct batch_request *duplicate_request(

  struct batch_request *preq)

  {
  struct batch_request *preq_tmp = alloc_br(PBS_BATCH_DeleteJob);
  preq_tmp->rq_perm = preq->rq_perm;
  preq_tmp->rq_ind.rq_manager.rq_cmd = preq->rq_ind.rq_manager.rq_cmd;
  preq_tmp->rq_ind.rq_manager.rq_objtype = preq->rq_ind.rq_manager.rq_objtype;
  preq_tmp->rq_fromsvr = preq->rq_fromsvr;
  preq_tmp->rq_extsz = preq->rq_extsz;
  preq_tmp->rq_conn = preq->rq_conn;
  memcpy(preq_tmp->rq_ind.rq_manager.rq_objname,
    preq->rq_ind.rq_manager.rq_objname, PBS_MAXSVRJOBID + 1);
  memcpy(preq_tmp->rq_user, preq->rq_user, PBS_MAXUSER + 1);
  memcpy(preq_tmp->rq_host, preq->rq_host, PBS_MAXHOSTNAME + 1);

  return(preq_tmp);
  } /* END duplicate_request() */




int handle_delete_all(

  struct batch_request *preq,
  struct batch_request *preq_tmp,
  char                 *Msg)

  {
  /* don't use the actual request so we can reply about all of the jobs */
  struct batch_request *preq_dup = duplicate_request(preq);
  job                  *pjob;
  int                   iter = -1;
  int                   failed_deletes = 0;
  int                   total_jobs = 0;
  int                   rc;
  char                  tmpLine[MAXLINE];

  preq_dup->rq_noreply = TRUE;
  
  if (preq_tmp != NULL)
    {
    reply_ack(preq_tmp);
    preq->rq_noreply = TRUE; /* set for no more replies */
    }
  
  while ((pjob = next_job(&alljobs,&iter)) != NULL)
    {
    if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
      {
      pthread_mutex_unlock(pjob->ji_mutex);
      
      continue;
      }
    
    total_jobs++;
    
    /* mutex is freed below */
    if ((rc = forced_jobpurge(pjob,preq_dup)) == PBSE_NONE)
      rc = execute_job_delete(pjob,Msg,preq_dup);
    
    if (rc != PURGE_SUCCESS)
      {
      /* duplicate the preq so we don't have a problem with double frees */
      preq_dup = duplicate_request(preq);
      preq_dup->rq_noreply = TRUE;
      
      if ((rc == MOM_DELETE) ||
          (rc == ROUTE_DELETE))
        failed_deletes++;
      }
    }
  
  if (failed_deletes == 0)
    reply_ack(preq);
  else
    {
    snprintf(tmpLine,sizeof(tmpLine),"Deletes failed for %d of %d jobs",
      failed_deletes,
      total_jobs);
    
    req_reject(PBSE_SYSTEM, 0, preq, NULL, tmpLine);
    }

  return(PBSE_NONE);
  } /* END handle_delete_all() */




int handle_single_delete(

  struct batch_request *preq,
  struct batch_request *preq_tmp,
  char                 *Msg)

  {
  int   rc= -1;
  char *jobid = preq->rq_ind.rq_delete.rq_objname;
  job  *pjob = find_job(jobid);

  if (pjob == NULL)
    {
    log_event(PBSEVENT_DEBUG,PBS_EVENTCLASS_JOB,jobid,pbse_to_txt(PBSE_UNKJOBID));
    
    req_reject(PBSE_UNKJOBID, 0, preq, NULL, "cannot locate job");
    }
  else
    {
    if (preq_tmp != NULL)
      {
      reply_ack(preq_tmp);
      preq->rq_noreply = TRUE; /* set for no more replies */
      }
    
    /* mutex is freed below */
    if ((rc = forced_jobpurge(pjob,preq)) == PBSE_NONE)
      rc = execute_job_delete(pjob,Msg,preq);
    }
  
  if ((rc == PBSE_NONE) ||
      (rc == PURGE_SUCCESS))
    reply_ack(preq);

  return(PBSE_NONE);
  } /* END handle_single_delete() */




/*
 * req_deletejob - service the Delete Job Request
 *
 * This request deletes a job. The request is
 * initiated from an external program, most commonly
 * qdel.  Shown below is the normal messaging.
 * There are many exceptions to the normal case
 * such as missing job descriptions and failure
 * of messages to propagate.  There are also
 * exceptions related to the state of the job.
 *
 * The code at this point does not seem particularly
 * robust.  For example, some stages of the processing
 * check for existense of the job structure while
 * others do not.
 *
 * The fragileness of the code seems to be reflected
 * in practice as there are many reports in the
 * user's groups of trouble in deleting jobs.
 * There also seems to have been several attempts
 * to patch over the problems.  The purge option
 * seems to have been an afterthought as does the
 * job deletion nanny code.
 *
 * The problems in this code stem from a lack of a
 * state processing model for job deletion.
 *
 *    qdel-command     pbs_server       pbs_mom
 *    -------------    -------------    -------------
 *          |                |                |
 *          +-- DeleteJob -->|                |
 *          |                |                |
 *          |                +-- DeleteJob -->|
 *          |                |                |
 *          |                |<-- Ack --------+
 *          |                |                |
 *          |<-- Ack --------+                |
 *          |                |                |
 *          |                |                |
 */

void req_deletejob(

  struct batch_request *preq)  /* I */

  {
  char                 *Msg = NULL;
  struct batch_request *preq_tmp = NULL;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];

  /* check if we are getting a purgecomplete from scheduler */
  if (preq->rq_extend != NULL)  
    {
    if (!strncmp(preq->rq_extend,PURGECOMP,strlen(PURGECOMP)))
      {
      /* purge_completed_jobs will respond with either an ack or reject */
      purge_completed_jobs(preq);
      
      return;
      }
    else if (strncmp(preq->rq_extend, deldelaystr, strlen(deldelaystr)) &&
        strncmp(preq->rq_extend, delasyncstr, strlen(delasyncstr)) &&
        strncmp(preq->rq_extend, delpurgestr, strlen(delpurgestr)))
      {
      /* have text message in request extension, add it */
      Msg = preq->rq_extend;

      /* Message capability is only for operators and managers.
       * Check if request is authorized */
      if ((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
                            ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) == 0)
        {
        req_reject(PBSE_PERM, 0, preq, NULL,
          "must have operator or manager privilege to use -m parameter");

        return;
        }
      }
    /* check if we are getting a asynchronous delete */
    else if (!strncmp(preq->rq_extend,delasyncstr,strlen(delasyncstr)))
      {
      /*
       * Respond with an ack now instead of after MOM processing
       * Create a new batch request and fill it in. It will be freed by reply_ack
       */
      snprintf(log_buf,sizeof(log_buf), "Deleting job asynchronously");
      log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,preq->rq_ind.rq_delete.rq_objname,log_buf);

      preq_tmp = duplicate_request(preq);
      }
    }

  if (strcasecmp(preq->rq_ind.rq_delete.rq_objname,"all") == 0)
    {
    handle_delete_all(preq, preq_tmp, Msg);
    }
  else
    {
    handle_single_delete(preq, preq_tmp, Msg);
    }

  return;
  }  /* END req_deletejob() */



/*
 * change_restart_comment_if_needed - If job has restarted then the checkpoint
 * restart status attribute is used in on_job_exit() to reque/hold job on failure.
 * If we are deleting then we change the first charcter to lower case so
 * it does normal processing in on_job_exit().
 */

void change_restart_comment_if_needed(

  struct job *pjob)

  {

  if ((pjob->ji_wattr[JOB_ATR_start_count].at_val.at_long > 1) &&
      (pjob->ji_wattr[JOB_ATR_checkpoint_restart_status].at_flags & ATR_VFLAG_SET))
    {
    char *token1 = NULL;
    char *token2 = NULL;
    char *comment_ptr;
    char  commentMsg[25];
    char *ptr;

    snprintf(commentMsg, sizeof(commentMsg), "%s", pjob->ji_wattr[JOB_ATR_checkpoint_restart_status].at_val.at_str);
   
    comment_ptr = commentMsg;
    token1 = threadsafe_tokenizer(&comment_ptr, " ");
    if (token1 != NULL)
      token2 = threadsafe_tokenizer(&comment_ptr, " ");
    
    if ((token2 != NULL) && 
        ((memcmp(token2,"failure",7) == 0) || 
         (memcmp(token2,"restarted",9) == 0)))
      {
      ptr = pjob->ji_wattr[JOB_ATR_checkpoint_restart_status].at_val.at_str;
      if (isupper(*ptr))
        {
        *ptr = tolower(*ptr);
        pjob->ji_wattr[JOB_ATR_checkpoint_restart_status].at_flags |= ATR_VFLAG_SET;
        pjob->ji_modified = 1;
        }
      }
    }
  
  return;
  } /* change_restart_comment_if_needed() */


 


/*
 * post_delete_route - complete the task of deleting a job which was
 * being routed at the time the delete request was received.
 *
 * Just recycle the delete request, the job will either be here or not.
 */

static void post_delete_route(

  struct work_task *pwt)

  {
  req_deletejob((struct batch_request *)pwt->wt_parm1);

  free(pwt->wt_mutex);
  free(pwt);
  return;
  }





/*
 * post_delete_mom1 - first of 2 work task trigger functions to finish the
 * deleting of a running job.  This first part is invoked when MOM
 * responds to the SIGTERM signal request.
 */

static void post_delete_mom1(

  struct work_task *pwt)

  {
  int                   delay = 0;
  int                   dellen = strlen(deldelaystr);
  job                  *pjob;

  pbs_queue            *pque;

  struct batch_request *preq_sig;  /* signal request to MOM */

  struct batch_request *preq_clt;  /* original client request */
  int                   rc;
  time_t                time_now = time(NULL);

  preq_sig = pwt->wt_parm1;
  rc       = preq_sig->rq_reply.brp_code;
  preq_clt = preq_sig->rq_extra;

  release_req(pwt);

  pjob = find_job(preq_clt->rq_ind.rq_delete.rq_objname);

  if (pjob == NULL)
    {
    /* job has gone away */
    req_reject(PBSE_UNKJOBID, 0, preq_clt, NULL, NULL);

    return;
    }

  if (rc)
    {
    /* mom rejected request */

    if (rc == PBSE_UNKJOBID)
      {
      /* MOM claims no knowledge, so just purge it */
      log_event(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        "MOM rejected signal during delete");

      /* removed the resources assigned to job */

      free_nodes(pjob);

      set_resc_assigned(pjob, DECR);

      job_purge(pjob);

      reply_ack(preq_clt);
      }
    else
      {
      req_reject(rc, 0, preq_clt, NULL, NULL);

      pthread_mutex_unlock(pjob->ji_mutex);
      }

    return;
    }

  if (preq_clt->rq_extend)
    {
    if (strncmp(preq_clt->rq_extend, deldelaystr, dellen) == 0)
      {
      delay = atoi(preq_clt->rq_extend + dellen);
      }
    }

  reply_ack(preq_clt);  /* dont need it, reply now */

  /*
   * if no delay specified in original request, see if kill_delay
   * queue attribute is set.
   */
  if (delay == 0)
    {
    if ((pque = get_jobs_queue(pjob)) != NULL)
      {
      pthread_mutex_lock(server.sv_attr_mutex);
      delay = attr_ifelse_long(&pque->qu_attr[QE_ATR_KillDelay],
                             &server.sv_attr[SRV_ATR_KillDelay],
                             2);
      pthread_mutex_unlock(server.sv_attr_mutex);
      unlock_queue(pque, "post_delete_mom1", NULL, LOGLEVEL);
      }
    }

  set_task(WORK_Timed, delay + time_now, post_delete_mom2, strdup(pjob->ji_qs.ji_jobid), FALSE);

  /*
   * Since the first signal has succeeded, let's reschedule the
   * nanny to be 1 minute after the second phase.
   */
  apply_job_delete_nanny(pjob, time_now + delay + 60);

  pthread_mutex_unlock(pjob->ji_mutex);
  }  /* END post_delete_mom1() */





static void post_delete_mom2(

  struct work_task *pwt)

  {
  static char *id = "post_delete_mom2";
  char        *jobid;
  char        *sigk = "SIGKILL";
  char         log_buf[LOCAL_LOG_BUF_SIZE];
  job         *pjob;

  jobid = (char *)pwt->wt_parm1;
  free(pwt->wt_mutex);
  free(pwt);
  
  if (jobid == NULL)
    {
    log_err(ENOMEM,id,"Cannot allocate memory");
    return;
    }

  pjob = find_job(jobid);
  free(jobid);

  if (pjob != NULL)
    {
    if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
      {
      issue_signal(&pjob, sigk, release_req, 0);
      
      if (pjob != NULL)
        {
        sprintf(log_buf, msg_delrunjobsig, sigk);
        log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);
        }
      }
    
    if (pjob != NULL)
      pthread_mutex_unlock(pjob->ji_mutex);
    }
  }  /* END post_delete_mom2() */





/*
 * forced_jobpurge - possibly forcibly purge a job
 *
 * @return PBSE_NONE if the job hasn't been deleted but possibly can be
 * return 1 if the job was deleted, and -1 if the job hasn't been deleted and can't be
 */

static int forced_jobpurge(

  job                  *pjob,
  struct batch_request *preq)

  {
  long owner_purge = FALSE;
  /* check about possibly purging the job */
  if (preq->rq_extend != NULL)
    {
    if (!strncmp(preq->rq_extend, delpurgestr, strlen(delpurgestr)))
      {
      get_svr_attr_l(SRV_ATR_OwnerPurge, &owner_purge);

      if (((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR | ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) != 0) ||
          ((svr_chk_owner(preq, pjob) == 0) && (owner_purge)))
        {
        force_purge_work(pjob);

        return(PURGE_SUCCESS);
        }
      else
        {
        /* FAILURE */
        req_reject(PBSE_PERM, 0, preq, NULL, NULL);

        pthread_mutex_unlock(pjob->ji_mutex);

        return(-1);
        }
      }
    }

  return(PBSE_NONE);
  }  /* END forced_jobpurge() */





/* apply_job_delete_nanny - setup the job delete nanny on a job
 *
 * Only 1 nanny will be allowed at a time. Don't add a new nanny
 * if one already exists
 */

int apply_job_delete_nanny(

  struct job *pjob,
  int         delay)  /* I */

  {
  static char      *id = "apply_job_delete_nanny";
  enum work_type    tasktype;
  long              nanny = FALSE;

  /* short-circuit if nanny isn't enabled or we have a delete nanny */
  get_svr_attr_l(SRV_ATR_JobNanny, &nanny);
  if ((nanny == FALSE) ||
      (pjob->ji_has_delete_nanny == TRUE))
    {
    return(PBSE_NONE);
    }

  if (delay == 0)
    {
    tasktype = WORK_Immed;
    }
  else if (delay > 0)
    {
    tasktype = WORK_Timed;
    }
  else
    {
    log_err(-1, id, "negative delay requested for nanny");

    return(-1);
    }

  /* add a nanny task at the requested time */
  set_task(tasktype, delay, job_delete_nanny, strdup(pjob->ji_qs.ji_jobid), FALSE);

  return(PBSE_NONE);
  } /* END apply_job_delete_nanny() */





/*
 * job_delete_nanny - make sure jobs are actually deleted after a delete
 * request.  Like any good nanny, we'll be persistent with killing the job.
 *
 * jobdelete requests will set a task in the future to call job_delete_nanny().
 * Under normal conditions, we never actually get called and job deletes act
 * the same as before.  If we do get called, it means MS is having problems.
 * Our purpose is to continually send KILL signals to MS.  This is made
 * persisent by always setting ourselves as a future task.
 *
 * req_jobdelete sets us as a task 1 minute in the future and sends a SIGTERM
 * to MS.  If that succeeds, post_delete_mom1 reschedules the task to be 1
 * minute after the KILL delay.  Either way, if the job doesn't exit we'll
 * start sending our own KILLs, forever, until MS wakes up.  The purpose of
 * the rescheduling is to stay out of the way of the KILL delay and not
 * interfere with normal job deletes.
 *
 * We are also called from pbsd_init_job() after recovering EXITING jobs.
 */

static void job_delete_nanny(

  struct work_task *pwt)

  {
  static char          *id = "job_delete_nanny";
  job                  *pjob;
  char                 *sigk = "SIGKILL";
  char                 *jobid;

  struct batch_request *newreq;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];
  time_t                time_now = time(NULL);
  long                  nanny = FALSE;

  /* short-circuit if nanny isn't enabled */
  get_svr_attr_l(SRV_ATR_JobNanny, &nanny);
  if (!nanny)
    {
    jobid = (char *)pwt->wt_parm1;
    
    if (jobid != NULL)
      {
      pjob = find_job(jobid);
      
      if (pjob != NULL)
        {
        sprintf(log_buf, "exiting job '%s' still exists, sending a SIGKILL", pjob->ji_qs.ji_jobid);
        log_err(-1, "job nanny", log_buf);
        
        /* build up a Signal Job batch request */
        if ((newreq = alloc_br(PBS_BATCH_SignalJob)) != NULL)
          {
          strcpy(newreq->rq_ind.rq_signal.rq_jid, pjob->ji_qs.ji_jobid);
          snprintf(newreq->rq_ind.rq_signal.rq_signame, sizeof(newreq->rq_ind.rq_signal.rq_signame), "%s", sigk);
          }
        
        issue_signal(&pjob, sigk, post_job_delete_nanny, newreq);
        
        if (pjob != NULL)
          {
          apply_job_delete_nanny(pjob, time_now + 60);
  
          pthread_mutex_unlock(pjob->ji_mutex);
          }
        }
      }
    else
      {
      log_err(ENOMEM,id,"Cannot allocate memory");
      }
    }
  
  if (pwt->wt_parm1 != NULL)
    free(pwt->wt_parm1);

  free(pwt->wt_mutex);
  free(pwt);
  } /* END job_delete_nanny() */




/*
 * post_job_delete_nanny - second part of async job deletes.
 *
 * This is only called if one of job_delete_nanny()'s KILLs actually
 * succeeds.  The sole purpose is to purge jobs that are unknown
 * to MS (and to release the req.)
 */

static void post_job_delete_nanny(

  struct work_task *pwt)

  {
  struct batch_request *preq_sig;  /* signal request to MOM */
  int                   rc;
  job                  *pjob;
  char                  log_buf[LOCAL_LOG_BUF_SIZE];
  long                  nanny = 0;

  preq_sig = pwt->wt_parm1;
  rc       = preq_sig->rq_reply.brp_code;

  get_svr_attr_l(SRV_ATR_JobNanny, &nanny);
  if (!nanny)
    {
    /* the admin disabled nanny within the last minute or so */
    release_req(pwt);

    return;
    }

  /* extract job id from task */
  pjob = find_job(preq_sig->rq_ind.rq_signal.rq_jid);

  if (pjob == NULL)
    {
    sprintf(log_buf, "job delete nanny: the job disappeared (this is a BUG!)");

    log_event(PBSEVENT_ERROR,PBS_EVENTCLASS_JOB,preq_sig->rq_ind.rq_signal.rq_jid,log_buf);
    }
  else if (rc == PBSE_UNKJOBID)
    {
    sprintf(log_buf, "job delete nanny returned, but does not exist on mom");

    log_event(PBSEVENT_ERROR,PBS_EVENTCLASS_JOB,preq_sig->rq_ind.rq_signal.rq_jid,log_buf);

    free_nodes(pjob);

    set_resc_assigned(pjob, DECR);
  
    release_req(pwt);

    job_purge(pjob);

    return;
    }
  
  pthread_mutex_unlock(pjob->ji_mutex);

  /* free task */
  release_req(pwt);

  return;
  } /* END post_job_delete_nanny() */



/*
 * purge_completed_jobs - service the Delete Job Request
 *
 *	This request deletes a job.
 */

void purge_completed_jobs(

  struct batch_request *preq)  /* I */

  {
  char         *id = "purge_completed_jobs";
  job          *pjob;
  char         *time_str;
  time_t        purge_time = 0;
  int           iter;
  char          log_buf[LOCAL_LOG_BUF_SIZE];

  /* get the time to purge the jobs that completed before */
  time_str = preq->rq_extend;
  time_str += strlen(PURGECOMP);
  purge_time = strtol(time_str,NULL,10);
  
  /*
    * Clean unreported capability is only for operators and managers.
    * Check if request is authorized
  */

  if ((preq->rq_perm & (ATR_DFLAG_OPRD|ATR_DFLAG_OPWR|
                    ATR_DFLAG_MGRD|ATR_DFLAG_MGWR)) == 0)
    {
    req_reject(PBSE_PERM,0,preq,NULL,
      "must have operator or manager privilege to use -c parameter");
    return;
    }
    
  reply_ack(preq);

  if (LOGLEVEL >= 4)
    {
    sprintf(log_buf,"Received purge completed jobs command, purge time is %ld (%s)",
      (long)purge_time, preq->rq_extend);

    log_event(PBSEVENT_SYSTEM,PBS_EVENTCLASS_REQUEST,id,log_buf);
    }

  iter = -1;

  while ((pjob = next_job(&alljobs,&iter)) != NULL) 
    {
    if ((pjob->ji_qs.ji_substate == JOB_SUBSTATE_COMPLETE) &&
        (pjob->ji_wattr[JOB_ATR_comp_time].at_val.at_long <= purge_time) &&
        ((pjob->ji_wattr[JOB_ATR_reported].at_flags & ATR_VFLAG_SET) != 0) &&
        (pjob->ji_wattr[JOB_ATR_reported].at_val.at_long == 0))
      {
      if (LOGLEVEL >= 4)
        {
        sprintf(log_buf,"Reported job is COMPLETED (%ld), setting reported to TRUE",
          pjob->ji_wattr[JOB_ATR_comp_time].at_val.at_long);
        
        log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);
        }
      
      pjob->ji_wattr[JOB_ATR_reported].at_val.at_long = 1;
      pjob->ji_wattr[JOB_ATR_reported].at_flags = ATR_VFLAG_SET | ATR_VFLAG_MODIFY;
          
      job_save(pjob, SAVEJOB_FULL, 0); 
      }

    pthread_mutex_unlock(pjob->ji_mutex);
    }


  return;
  } /* END purge_completed_jobs() */

/* END req_delete.c */

