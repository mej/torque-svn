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
 * req_rerun.c - functions dealing with a Rerun Job Request
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <sys/types.h>
#include "libpbs.h"
#include <signal.h>
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "pbs_error.h"
#include "log.h"
#include "acct.h"
#include "svrfunc.h"

/* Private Function local to this file */

/* Global Data Items: */

extern char *msg_manager;
extern char *msg_jobrerun;
extern time_t time_now;

extern void rel_resc(job *);

/*
 * post_rerun - handler for reply from mom on signal_job sent in req_rerunjob
 * If mom acknowledged the signal, then all is ok.
 * If mom rejected the signal for unknown jobid, then force local requeue.
 */

static void post_rerun(

  struct work_task *pwt)

  {
  int  newstate;
  int  newsub;
  job *pjob;

  struct batch_request *preq;

  preq = (struct batch_request *)pwt->wt_parm1;

  if (preq->rq_reply.brp_code != 0)
    {
    sprintf(log_buffer, "rerun signal reject by mom: %d",
            preq->rq_reply.brp_code);

    log_event(
      PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      preq->rq_ind.rq_signal.rq_jid,
      log_buffer);

    if ((pjob = find_job(preq->rq_ind.rq_signal.rq_jid)))
      {
      svr_evaljobstate(pjob, &newstate, &newsub, 1);

      svr_setjobstate(pjob, newstate, newsub);
      }
    }

  release_req(pwt);

  return;
  }  /* END post_rerun() */





/**
 * req_rerunjob - service the Rerun Job Request
 *
 * This request Reruns a job by:
 * sending to MOM a signal job request with SIGKILL
 * marking the job as being rerun by setting the substate.
 *
 * NOTE:  can be used to requeue active jobs or completed jobs.
 */

void req_rerunjob(

  struct batch_request *preq) /* I */

  {
  job *pjob;
  extern struct work_task *apply_job_delete_nanny(struct job *,int);

  struct work_task	*pwt;

  int  Force;

  int  rc;

  int  MgrRequired = TRUE;

  /* check if requestor is admin, job owner, etc */

  if ((pjob = chk_job_request(preq->rq_ind.rq_rerun, preq)) == 0)
    {
    /* FAILURE */

    /* chk_job_request calls req_reject() */

    return;
    }

  /* the job must be running or completed */

  if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
    {
    if (pjob->ji_wattr[(int)JOB_ATR_checkpoint_name].at_flags & ATR_VFLAG_SET)
      {
      /* allow end-users to rerun checkpointed jobs */

      MgrRequired = FALSE;
      }
    }
  else if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /* job is running */

    /* NO-OP */
    }
  else
    {
    /* FAILURE - job is in bad state */

    req_reject(PBSE_BADSTATE, 0, preq, NULL, NULL);

    return;
    }

  if ((MgrRequired == TRUE) &&
      ((preq->rq_perm & (ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)) == 0))
    {
    /* FAILURE */

    req_reject(PBSE_PERM, 0, preq, NULL, NULL);

    return;
    }

  /* the job must be rerunnable */

  if (pjob->ji_wattr[(int)JOB_ATR_rerunable].at_val.at_long == 0)
    {
    /* NOTE:  should force override this constraint? maybe (???) */
    /*          no, the user is saying that the job will break, and
                IEEE Std 1003.1 specifically says rerun is to be rejected
                if rerunable==FALSE -garrick */

    req_reject(PBSE_NORERUN, 0, preq, NULL, NULL);

    return;
    }

  if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    apply_job_delete_nanny(pjob,time_now + 60);

    /* ask MOM to kill off the job if it is running */

    rc = issue_signal(pjob, "SIGTERM", post_rerun, 0);
    }
  else
    { 
    if (pjob->ji_wattr[(int)JOB_ATR_hold].at_val.at_long == HOLD_n)
      {
      svr_setjobstate(pjob, JOB_STATE_QUEUED, JOB_SUBSTATE_QUEUED);
      }
    else
      {
      svr_setjobstate(pjob, JOB_STATE_HELD, JOB_SUBSTATE_HELD);
      }

    /* reset some job attributes */
    
    pjob->ji_wattr[(int)JOB_ATR_comp_time].at_flags &= ~ATR_VFLAG_SET;
    pjob->ji_wattr[(int)JOB_ATR_reported].at_flags &= ~ATR_VFLAG_SET;

    /*
     * delete any work task entries associated with the job
     * there may be tasks for keep_completed proccessing
     */

    while ((pwt = (struct work_task *)GET_NEXT(pjob->ji_svrtask)) != NULL) 
      {
      delete_task(pwt);
      }

    set_statechar(pjob);

    rc = -1;
    }

  if (preq->rq_extend && !strncasecmp(preq->rq_extend, RERUNFORCE, strlen(RERUNFORCE)))
    Force = 1;
  else
    Force = 0;

  switch (rc)
    {

    case - 1:

      /* completed job was requeued */

      /* clear out job completion time if there is one */
      break;

    case 0:

      /* requeue request successful */

      pjob->ji_qs.ji_substate = JOB_SUBSTATE_RERUN;

      break;

    case PBSE_SYSTEM:

      req_reject(PBSE_SYSTEM, 0, preq, NULL, "cannot allocate memory");

      return;

      /*NOTREACHED*/

      break;

    default:

      if (Force == 0)
        {
        req_reject(PBSE_MOMREJECT, 0, preq, NULL, NULL);

        return;
        }
      else
        {
        int newstate, newsubst;
        unsigned int dummy;

        /* Cannot communicate with MOM, forcibly requeue job.
           This is a relatively disgusting thing to do */

        sprintf(log_buffer, "rerun req to %s failed (rc=%d), forcibly requeueing job",
                parse_servername(pjob->ji_wattr[(int)JOB_ATR_exec_host].at_val.at_str, &dummy),
                rc);

        log_event(
          PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          log_buffer);

        log_err(-1, "req_rerunjob", log_buffer);

        strcat(log_buffer, ", previous output files may be lost");

        svr_mailowner(pjob, MAIL_OTHER, MAIL_FORCE, log_buffer);

        svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_RERUN3);

        rel_resc(pjob); /* free resc assigned to job */

        if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_HOTSTART) == 0)
          {
          /* in case of server shutdown, don't clear exec_host */
          /* will use it on hotstart when next comes up        */

          job_attr_def[(int)JOB_ATR_exec_host].at_free(
            &pjob->ji_wattr[(int)JOB_ATR_exec_host]);

          job_attr_def[(int)JOB_ATR_session_id].at_free(
            &pjob->ji_wattr[(int)JOB_ATR_session_id]);
          }

        pjob->ji_modified = 1;    /* force full job save */

        pjob->ji_momhandle = -1;
        pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_StagedIn;

        svr_evaljobstate(pjob, &newstate, &newsubst, 0);

        svr_setjobstate(pjob, newstate, newsubst);
        }

      break;
    }  /* END switch (rc) */

  /* So job has run and is to be rerun (not restarted) */

  pjob->ji_qs.ji_svrflags = (pjob->ji_qs.ji_svrflags &
      ~(JOB_SVFLG_CHECKPOINT_FILE |JOB_SVFLG_CHECKPOINT_MIGRATEABLE |
       JOB_SVFLG_CHECKPOINT_COPIED)) | JOB_SVFLG_HASRUN;

  sprintf(log_buffer, msg_manager,
          msg_jobrerun,
          preq->rq_user,
          preq->rq_host);

  log_event(
    PBSEVENT_JOB,
    PBS_EVENTCLASS_JOB,
    pjob->ji_qs.ji_jobid,
    log_buffer);

  reply_ack(preq);

  /* note in accounting file */

  account_record(PBS_ACCT_RERUN, pjob, NULL);
  }  /* END req_rerunjob() */


/* END req_rerun.c */

