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

#include <sys/types.h>
#include <stdio.h>
#include <signal.h>
#include "libpbs.h"
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "job.h"
#include "queue.h"
#include "pbs_error.h"
#include "acct.h"
#include "log.h"
#include "svrfunc.h"


/* Global Data Items: */

extern char *msg_deletejob;
extern char *msg_delrunjobsig;
extern char *msg_manager;
extern char *msg_unkjobid;
extern char *msg_permlog;
extern char *msg_badstate;
tlist_head	svr_alljobs;           /* list of all jobs in server       */

extern struct server server;
extern time_t time_now;
extern int   LOGLEVEL;

/* Private Functions in this file */

static void post_delete_route A_((struct work_task *));
static void post_delete_mom1 A_((struct work_task *));
static void post_delete_mom2 A_((struct work_task *));
static int forced_jobpurge A_((struct batch_request *));
static void job_delete_nanny A_((struct work_task *));
static void post_job_delete_nanny A_((struct work_task *));
static void purge_completed_jobs A_((struct batch_request *));

/* Public Functions in this file */

struct work_task *apply_job_delete_nanny A_((struct job *, int));
int has_job_delete_nanny A_((struct job *));

/* Private Data Items */

static char *deldelaystr = DELDELAY;
static char *delpurgestr = DELPURGE;

/* Extern Functions */

extern void set_resc_assigned(job *, enum batch_op);


/*
 * remove_stagein() - request that mom delete staged-in files for a job
 * used when the job is to be purged after files have been staged in
 */

void remove_stagein(

  job *pjob)  /* I */

  {

  struct batch_request *preq = 0;

  preq = cpy_stage(preq, pjob, JOB_ATR_stagein, 0);

  if (preq != NULL)
    {
    /* have files to delete  */

    /* change the request type from copy to delete  */

    preq->rq_type = PBS_BATCH_DelFiles;

    preq->rq_extra = NULL;

    if (relay_to_mom(
          pjob->ji_qs.ji_un.ji_exect.ji_momaddr,
          preq,
          release_req) == 0)
      {
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

      free_br(preq);
      }
    }

  return;
  }  /* END remove_stagein() */





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
  job              *pjob;

  struct work_task *pwtold;

  struct work_task *pwtnew;

  int               rc;
  char             *sigt = "SIGTERM";

  char             *Msg = NULL;

  /* check if we are getting a purgecomplete from scheduler */
  if ((preq->rq_extend != NULL) && 
        !strncmp(preq->rq_extend,PURGECOMP,strlen(PURGECOMP)))
    {

    /*
     * purge_completed_jobs will respond with either an ack or reject
     */
    purge_completed_jobs(preq);

    return;
    }

  /* The way this is implemented, if the user enters the command "qdel -p <jobid>",
   * they can then delete jobs other than their own since the authorization
   * checks are made below in chk_job_request. This should be fixed.
   */

  if (forced_jobpurge(preq) != 0)
    {
    return;
    }

  /* NOTE:  should support rq_objname={<JOBID>|ALL|<name:<JOBNAME>} */

  /* NYI */

  pjob = chk_job_request(preq->rq_ind.rq_delete.rq_objname, preq);

  if (pjob == NULL)
    {
    /* NOTE:  chk_job_request() will issue req_reject() */

    return;
    }

  if (preq->rq_extend != NULL)
    {
    if (strncmp(preq->rq_extend, deldelaystr, strlen(deldelaystr)) &&
        strncmp(preq->rq_extend, delpurgestr, strlen(delpurgestr)))
      {
      /* have text message in request extension, add it */

      Msg = preq->rq_extend;

      /*
       * Message capability is only for operators and managers.
       * Check if request is authorized
      */

      if ((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR |
                            ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) == 0)
        {
        req_reject(PBSE_PERM, 0, preq, NULL,
                   "must have operator or manager privilege to use -m parameter");
        return;
        }
      }
    }

  if (pjob->ji_qs.ji_state == JOB_STATE_TRANSIT)
    {
    /*
     * Find pid of router from existing work task entry,
     * then establish another work task on same child.
     * Next, signal the router and wait for its completion;
     */

    pwtold = (struct work_task *)GET_NEXT(pjob->ji_svrtask);

    while (pwtold != NULL)
      {
      if ((pwtold->wt_type == WORK_Deferred_Child) ||
          (pwtold->wt_type == WORK_Deferred_Cmp))
        {
        pwtnew = set_task(
                   pwtold->wt_type,
                   pwtold->wt_event,
                   post_delete_route,
                   preq);

        if (pwtnew != NULL)
          {
          /*
           * reset type in case the SIGCHLD came
           * in during the set_task;  it makes
           * sure that next_task() will find the
           * new entry.
           */

          pwtnew->wt_type = pwtold->wt_type;
          pwtnew->wt_aux = pwtold->wt_aux;

          kill((pid_t)pwtold->wt_event, SIGTERM);

          pjob->ji_qs.ji_substate = JOB_SUBSTATE_ABORT;

          return; /* all done for now */
          }
        else
          {
          req_reject(PBSE_SYSTEM, 0, preq, NULL, NULL);

          return;
          }
        }

      pwtold = (struct work_task *)GET_NEXT(pwtold->wt_linkobj);
      }

    /* should never get here ...  */

    log_err(-1, "req_delete", "Did not find work task for router");

    req_reject(PBSE_INTERNAL, 0, preq, NULL, NULL);

    return;
    }

  if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN)
    {
    /* being sent to MOM, wait till she gets it going */
    /* retry in one second                            */

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

    sprintf(log_buffer, "job cannot be deleted, state=PRERUN, requeuing delete request");

    log_event(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      log_buffer);

    pwtnew = set_task(
               WORK_Timed,
               time_now + 1,
               post_delete_route,
               preq);

    if (pwtnew == 0)
      req_reject(PBSE_SYSTEM, 0, preq, NULL, NULL);

    return;
    }  /* END if (pjob->ji_qs.ji_substate == JOB_SUBSTATE_PRERUN) */

jump:

  /*
   * Log delete and if requesting client is not job owner, send mail.
   */

  sprintf(log_buffer, "requestor=%s@%s",
          preq->rq_user,
          preq->rq_host);


  /* NOTE:  should annotate accounting record with extend message (NYI) */

  account_record(PBS_ACCT_DEL, pjob, log_buffer);

  sprintf(log_buffer, msg_manager,
          msg_deletejob,
          preq->rq_user,
          preq->rq_host);

  log_event(
    PBSEVENT_JOB,
    PBS_EVENTCLASS_JOB,
    pjob->ji_qs.ji_jobid,
    log_buffer);

  /* NOTE:  should incorporate job delete message */

  if (Msg != NULL)
    {
    /* have text message in request extension, add it */

    strcat(log_buffer, "\n");
    strcat(log_buffer, Msg);
    }

  if ((svr_chk_owner(preq, pjob) != 0) &&
      !has_job_delete_nanny(pjob))
    {
    /* only send email if owner did not delete job and job deleted
       has not been previously attempted */

    svr_mailowner(pjob, MAIL_DEL, MAIL_FORCE, log_buffer);
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

  if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /*
     * setup a nanny task to make sure the job is actually deleted (see the
     * comments at job_delete_nanny()).
     */

    if (has_job_delete_nanny(pjob))
      {
      req_reject(PBSE_IVALREQ, 0, preq, NULL, "job cancel in progress");

      return;
      }

    apply_job_delete_nanny(pjob, time_now + 60);

    /*
     * Send signal request to MOM.  The server will automagically
     * pick up and "finish" off the client request when MOM replies.
     */

    if ((rc = issue_signal(pjob, sigt, post_delete_mom1, preq)))
      {
      /* cant send to MOM */

      req_reject(rc, 0, preq, NULL, NULL);
      }

    /* normally will ack reply when mom responds */

    sprintf(log_buffer, msg_delrunjobsig,
            sigt);

    LOG_EVENT(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      log_buffer);

    return;
    }  /* END if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING) */

  if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHKPT) != 0)
    {
    /* job has restart file at mom, do end job processing */

    svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_EXITING);

    pjob->ji_momhandle = -1;

    /* force new connection */

    pwtnew = set_task(WORK_Immed, 0, on_job_exit, (void *)pjob);

    if (pwtnew)
      {
      append_link(&pjob->ji_svrtask, &pwtnew->wt_linkobj, pwtnew);
      }
    }
  else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_StagedIn) != 0)
    {
    /* job has staged-in file, should remove them */

    remove_stagein(pjob);

    job_abt(&pjob, Msg);
    }
  else
    {
    /*
     * the job is not transitting (though it may have been) and
     * is not running, so abort it.
     */

    job_abt(&pjob, Msg);
    }  /* END else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_CHKPT) != 0) */

  reply_ack(preq);

  return;
  }  /* END req_deletejob() */





/*
 * post_delete_route - complete the task of deleting a job which was
 * being routed at the time the delete request was received.
 *
 * Just recycle the delete request, the job will either be here or not.
 */

static void post_delete_route(

  struct work_task *pwt)

  {
  req_deletejob(
    (struct batch_request *)pwt->wt_parm1);

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
  int         delay = 0;
  int        dellen = strlen(deldelaystr);
  job       *pjob;

  struct work_task   *pwtnew;
  pbs_queue      *pque;

  struct batch_request *preq_sig;  /* signal request to MOM */

  struct batch_request *preq_clt;  /* original client request */
  int        rc;

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
    pque = pjob->ji_qhdr;

    delay = attr_ifelse_long(&pque->qu_attr[(int)QE_ATR_KillDelay],
                             &server.sv_attr[(int)SRV_ATR_KillDelay],
                             2);
    }

  pwtnew = set_task(WORK_Timed, delay + time_now, post_delete_mom2, pjob);

  if (pwtnew)
    {
    /* insure that work task will be removed if job goes away */

    append_link(&pjob->ji_svrtask, &pwtnew->wt_linkobj, pwtnew);
    }

  /*
   * Since the first signal has succeeded, let's reschedule the
   * nanny to be 1 minute after the second phase.
   */

  apply_job_delete_nanny(pjob, time_now + delay + 60);

  return;
  }  /* END post_delete_mom1() */





static void post_delete_mom2(

  struct work_task *pwt)

  {
  job  *pjob;
  char *sigk = "SIGKILL";

  pjob = (job *)pwt->wt_parm1;

  if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    issue_signal(pjob, sigk, release_req, 0);

    sprintf(log_buffer, msg_delrunjobsig, sigk);

    LOG_EVENT(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      log_buffer);
    }

  return;
  }  /* END post_delete_mom2() */





/*
 * forced_jobpurge - possibly forcibly purge a job
 */

static int forced_jobpurge(

  struct batch_request *preq)

  {
  job *pjob;

  if ((pjob = find_job(preq->rq_ind.rq_delete.rq_objname)) == NULL)
    {
    log_event(
      PBSEVENT_DEBUG,
      PBS_EVENTCLASS_JOB,
      preq->rq_ind.rq_delete.rq_objname,
      msg_unkjobid);

    req_reject(PBSE_UNKJOBID, 0, preq, NULL, NULL);

    return(-1);
    }

  /* check about possibly purging the job */

  if (preq->rq_extend != NULL)
    {
    if (!strncmp(preq->rq_extend, delpurgestr, strlen(delpurgestr)))
      {
      if (((preq->rq_perm & (ATR_DFLAG_OPRD | ATR_DFLAG_OPWR | ATR_DFLAG_MGRD | ATR_DFLAG_MGWR)) != 0) ||
          ((svr_chk_owner(preq, pjob) == 0) && (server.sv_attr[(int)SRV_ATR_OwnerPurge].at_val.at_long)))
        {
        sprintf(log_buffer, "purging job without checking MOM");

        log_event(
          PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          log_buffer);

        reply_ack(preq);

        free_nodes(pjob);

        set_resc_assigned(pjob, DECR);

        job_purge(pjob);

        return(1);
        }
      else
        {
        /* FAILURE */

        req_reject(PBSE_PERM, 0, preq, NULL, NULL);

        return(-1);
        }
      }
    }

  return(0);
  }  /* END forced_jobpurge() */




/* has_job_delete_nanny - return true if job has a job delete nanny
 *
 * This means someone has already tried to cancel this job, and
 * the nanny is taking care of things now.
 */

int has_job_delete_nanny(

  struct job *pjob)

  {

  struct work_task *pwtiter;

  pwtiter = (struct work_task *)GET_NEXT(pjob->ji_svrtask);

  while (pwtiter != NULL)
    {
    if (pwtiter->wt_func == job_delete_nanny)
      {
      return(1);
      }

    pwtiter = (struct work_task *)GET_NEXT(pwtiter->wt_linkobj);
    }

  return(0);
  }  /* END has_job_delete_nanny() */





/* remove_job_delete_nanny - remove all nannies on a job */

void remove_job_delete_nanny(

  struct job *pjob)

  {

  struct work_task *pwtiter, *pwtdel;

  pwtiter = (struct work_task *)GET_NEXT(pjob->ji_svrtask);

  while (pwtiter != NULL)
    {
    if (pwtiter->wt_func == job_delete_nanny)
      {
      pwtdel = pwtiter;
      pwtiter = (struct work_task *)GET_NEXT(pwtiter->wt_linkobj);
      delete_task(pwtdel);
      }
    else
      {
      pwtiter = (struct work_task *)GET_NEXT(pwtiter->wt_linkobj);
      }
    }

  return;
  }  /* END has_job_delete_nanny() */






/* apply_job_delete_nanny - setup the job delete nanny on a job
 *
 * Only 1 nanny will be allowed at a time.  Before adding the new
 * nanny, we'll remove any existing nannies.
 */

struct work_task *apply_job_delete_nanny(

        struct job *pjob,
        int         delay)  /* I */

  {

  struct work_task *pwtnew;
  enum work_type tasktype;

  /* short-circuit if nanny isn't enabled */

  if (!server.sv_attr[(int)SRV_ATR_JobNanny].at_val.at_long)
    {
    remove_job_delete_nanny(pjob); /* in case it was recently disabled */

    return(NULL);
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
    log_err(-1, "apply_job_delete_nanny", "negative delay requested for nanny");

    return(NULL);
    }

  /* first, surgically remove any existing nanny tasks */

  remove_job_delete_nanny(pjob);

  /* second, add a nanny task at the requested time */

  pwtnew = set_task(tasktype, delay, job_delete_nanny, (void *)pjob);

  if (pwtnew)
    {
    /* insure that work task will be removed if job goes away */

    append_link(&pjob->ji_svrtask, &pwtnew->wt_linkobj, pwtnew);
    }

  return(pwtnew);
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
  job *pjob;
  char *sigk = "SIGKILL";

  struct batch_request *newreq;

  /* short-circuit if nanny isn't enabled */

  if (!server.sv_attr[(int)SRV_ATR_JobNanny].at_val.at_long)
    {
    release_req(pwt);

    return;
    }

  pjob = (job *)pwt->wt_parm1;

  sprintf(log_buffer, "exiting job '%s' still exists, sending a SIGKILL",
          pjob->ji_qs.ji_jobid);

  log_err(-1, "job nanny", log_buffer);

  /* build up a Signal Job batch request */

  if ((newreq = alloc_br(PBS_BATCH_SignalJob)) != NULL)
    {
    strcpy(newreq->rq_ind.rq_signal.rq_jid, pjob->ji_qs.ji_jobid);
    strncpy(newreq->rq_ind.rq_signal.rq_signame, sigk, PBS_SIGNAMESZ);
    }

  issue_signal(pjob, sigk, post_job_delete_nanny, newreq);


  apply_job_delete_nanny(pjob, time_now + 60);

  return;
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

  struct batch_request *preq_sig;                /* signal request to MOM */

  int   rc;
  job  *pjob;

  preq_sig = pwt->wt_parm1;
  rc       = preq_sig->rq_reply.brp_code;


  if (!server.sv_attr[(int)SRV_ATR_JobNanny].at_val.at_long)
    {
    /* the admin disabled nanny within the last minute or so */

    release_req(pwt);

    return;
    }

  /* extract job id from task */

  pjob = find_job(preq_sig->rq_ind.rq_signal.rq_jid);

  if (pjob == NULL)
    {
    sprintf(log_buffer, "job delete nanny: the job disappeared (this is a BUG!)");

    LOG_EVENT(
      PBSEVENT_ERROR,
      PBS_EVENTCLASS_JOB,
      preq_sig->rq_ind.rq_signal.rq_jid,
      log_buffer);
    }
  else if (rc == PBSE_UNKJOBID)
    {
    sprintf(log_buffer, "job delete nanny returned, but does not exist on mom");

    LOG_EVENT(
      PBSEVENT_ERROR,
      PBS_EVENTCLASS_JOB,
      preq_sig->rq_ind.rq_signal.rq_jid,
      log_buffer);

    free_nodes(pjob);

    set_resc_assigned(pjob, DECR);

    job_purge(pjob);
    }

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
  char *id = "purge_completed_jobs";
  job  *pjob;
  char *time_str;
  time_t purge_time = 0;

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
    
  if (LOGLEVEL >= 4)
    {
    sprintf(log_buffer,"Received purge completed jobs command, purge time is %ld (%s)",
      purge_time, preq->rq_extend);

    LOG_EVENT(
      PBSEVENT_SYSTEM, 
      PBS_EVENTCLASS_REQUEST,
      id,
      log_buffer);
    }

  for (pjob = (job *)GET_NEXT(svr_alljobs);
        pjob != NULL;
        pjob = (job *)GET_NEXT(pjob->ji_alljobs)) 
    {
    if ((pjob->ji_qs.ji_substate == JOB_SUBSTATE_COMPLETE) &&
      (pjob->ji_wattr[(int)JOB_ATR_comp_time].at_val.at_long <= purge_time) &&
      ((pjob->ji_wattr[(int)JOB_ATR_reported].at_flags & ATR_VFLAG_SET) != 0) &&
      (pjob->ji_wattr[(int)JOB_ATR_reported].at_val.at_long == 0))
      {
      if (LOGLEVEL >= 4)
        {
        sprintf(log_buffer,"Reported job is COMPLETED (%ld), setting reported to TRUE",
          pjob->ji_wattr[(int)JOB_ATR_comp_time].at_val.at_long);
          
        log_event(
          PBSEVENT_JOB, 
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          log_buffer);
        }

      pjob->ji_wattr[(int)JOB_ATR_reported].at_val.at_long = 1;
      pjob->ji_wattr[(int)JOB_ATR_reported].at_flags =
        ATR_VFLAG_SET | ATR_VFLAG_MODIFY;
          
      job_save(pjob,SAVEJOB_FULL); 
      }
    }


  reply_ack(preq);
  return;
  } /* END purge_completed_jobs() */

/* END req_delete.c */

