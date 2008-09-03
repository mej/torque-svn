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
 * req_shutdown.c - contains the functions to shutdown the server
 */
#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include "libpbs.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "log.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "queue.h"
#include "pbs_error.h"
#include "svrfunc.h"
#include "csv.h"

/* Private Fuctions Local to this File */

static int shutdown_checkpoint A_((job *));
static void post_checkpoint A_((struct work_task *));
static void rerun_or_kill A_((job *, char *text));

/* Private Data Items */

static struct batch_request *pshutdown_request = 0;

/* Global Data Items: */

extern tlist_head svr_alljobs;
extern char *msg_abort_on_shutdown;
extern char *msg_daemonname;
extern char *msg_init_queued;
extern char *msg_shutdown_op;
extern char *msg_shutdown_start;
extern char *msg_leftrunning;
extern char *msg_stillrunning;
extern char *msg_on_shutdown;
extern char *msg_job_abort;

extern tlist_head task_list_event;

extern struct server server;
extern attribute_def svr_attr_def[];
extern int    LOGLEVEL;




/*
 * svr_shutdown() - Perform (or start of) the shutdown of the server
 */

void svr_shutdown(

  int type) /* I */

  {
  attribute *pattr;
  job     *pjob;
  job     *pnxt;
  long     *state;

  /* Lets start by logging shutdown and saving everything */

  state = &server.sv_attr[(int)SRV_ATR_State].at_val.at_long;

  strcpy(log_buffer, msg_shutdown_start);

  if (*state == SV_STATE_SHUTIMM)
    {
    /* if already shuting down, another Immed/sig will force it */

    if ((type == SHUT_IMMEDIATE) || (type == SHUT_SIG))
      {
      *state = SV_STATE_DOWN;

      strcat(log_buffer, "Forced");

      log_event(
        PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
        PBS_EVENTCLASS_SERVER,
        msg_daemonname,
        log_buffer);

      return;
      }
    }

  if (type == SHUT_IMMEDIATE)
    {
    *state = SV_STATE_SHUTIMM;

    strcat(log_buffer, "Immediate");
    }
  else if (type == SHUT_DELAY)
    {
    *state = SV_STATE_SHUTDEL;

    strcat(log_buffer, "Delayed");
    }
  else if (type == SHUT_QUICK)
    {
    *state = SV_STATE_DOWN; /* set to down to brk pbsd_main loop */

    strcat(log_buffer, "Quick");
    }
  else
    {
    *state = SV_STATE_SHUTIMM;

    strcat(log_buffer, "By Signal");
    }

  log_event(

    PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
    PBS_EVENTCLASS_SERVER,
    msg_daemonname,
    log_buffer);

  if ((type == SHUT_QUICK) || (type == SHUT_SIG)) /* quick, leave jobs as are */
    {
    return;
    }

  svr_save(&server, SVR_SAVE_QUICK);

  pnxt = (job *)GET_NEXT(svr_alljobs);

  while ((pjob = pnxt) != NULL)
    {
    pnxt = (job *)GET_NEXT(pjob->ji_alljobs);

    if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
      {
      pjob->ji_qs.ji_svrflags |= JOB_SVFLG_HOTSTART | JOB_SVFLG_HASRUN;

      pattr = &pjob->ji_wattr[(int)JOB_ATR_checkpoint];

      if ((pattr->at_flags & ATR_VFLAG_SET) &&
          ((csv_find_string(pattr->at_val.at_str, "s") != NULL) ||
           (csv_find_string(pattr->at_val.at_str, "c") != NULL) ||
           (csv_find_string(pattr->at_val.at_str, "shutdown") != NULL)))
        {
        /* do checkpoint of job */

        if (shutdown_checkpoint(pjob) == 0)
          continue;
        }

      /* if no checkpoint (not supported, not allowed, or fails */
      /* rerun if possible, else kill job */

      rerun_or_kill(pjob, msg_on_shutdown);
      }
    }

  return;
  }  /* END svr_shutdown() */






/*
 * shutdown_ack - acknowledge the shutdown (terminate) request
 *  if there is one.  This is about the last thing the server does
 * before going away.
 */

void
shutdown_ack(void)

  {
  if (pshutdown_request)
    {
    reply_ack(pshutdown_request);

    pshutdown_request = 0;
    }

  return;
  }




/*
 * req_shutdown - process request to shutdown the server.
 *
 * Must have operator or administrator privilege.
 */

void req_shutdown(

  struct batch_request *preq)

  {
  if ((preq->rq_perm &
       (ATR_DFLAG_MGWR | ATR_DFLAG_MGRD | ATR_DFLAG_OPRD | ATR_DFLAG_OPWR)) == 0)
    {
    req_reject(PBSE_PERM, 0, preq, NULL, NULL);

    return;
    }

  sprintf(log_buffer, msg_shutdown_op,

          preq->rq_user,
          preq->rq_host);

  log_event(
    PBSEVENT_SYSTEM | PBSEVENT_ADMIN | PBSEVENT_DEBUG,
    PBS_EVENTCLASS_SERVER,
    msg_daemonname,
    log_buffer);

  pshutdown_request = preq;    /* save for reply from main() when done */

  svr_shutdown(preq->rq_ind.rq_shutdown);

  return;
  }  /* END req_shutdown() */





/*
 * shutdown_checkpoint - perform checkpoint of job by issuing a hold request to mom
 */

static int shutdown_checkpoint(

  job *pjob)

  {

  struct batch_request *phold;
  attribute        temp;

  phold = alloc_br(PBS_BATCH_HoldJob);

  if (phold == NULL)
    {
    return(PBSE_SYSTEM);
    }

  temp.at_flags = ATR_VFLAG_SET;

  temp.at_type  = job_attr_def[(int)JOB_ATR_hold].at_type;
  temp.at_val.at_long = HOLD_s;

  phold->rq_perm = ATR_DFLAG_MGRD | ATR_DFLAG_MGWR;

  strcpy(phold->rq_ind.rq_hold.rq_orig.rq_objname, pjob->ji_qs.ji_jobid);

  CLEAR_HEAD(phold->rq_ind.rq_hold.rq_orig.rq_attr);

  if (job_attr_def[(int)JOB_ATR_hold].at_encode(
        &temp,
        &phold->rq_ind.rq_hold.rq_orig.rq_attr,
        job_attr_def[(int)JOB_ATR_hold].at_name,
        NULL,
        ATR_ENCODE_CLIENT) < 0)
    {
    return(PBSE_SYSTEM);
    }

  if (relay_to_mom(pjob->ji_qs.ji_un.ji_exect.ji_momaddr, phold, post_checkpoint) != 0)
    {
    /* FAILURE */

    return(-1);
    }

  pjob->ji_qs.ji_substate = JOB_SUBSTATE_RERUN;

  pjob->ji_qs.ji_svrflags |= JOB_SVFLG_HASRUN | JOB_SVFLG_CHECKPOINT_FILE;

  if (LOGLEVEL >= 1)
    {
    log_event(
      PBSEVENT_SYSTEM | PBSEVENT_JOB | PBSEVENT_DEBUG,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      "shutting down with active checkpointable job");
    }

  job_save(pjob, SAVEJOB_QUICK);

  return(0);
  }  /* END shutdown_checkpoint() */




/*
 * post-checkpoint - clean up after shutdown_checkpoint
 * This is called on the reply from MOM to a Hold request made in
 * shutdown_checkpoint().  If the request succeeded, then record in job.
 * If the request failed, then we fall back to rerunning or aborting
 * the job.
 */

static void post_checkpoint(

  struct work_task *ptask)

  {
  job                  *pjob;

  struct batch_request *preq;

  preq = (struct batch_request *)ptask->wt_parm1;
  pjob = find_job(preq->rq_ind.rq_hold.rq_orig.rq_objname);

  if (preq->rq_reply.brp_code == 0)
    {
    /* checkpointed ok */
    if (preq->rq_reply.brp_auxcode) /* checkpoint can be moved */
      {
      pjob->ji_qs.ji_svrflags =
        (pjob->ji_qs.ji_svrflags & ~JOB_SVFLG_CHECKPOINT_FILE) |
        JOB_SVFLG_HASRUN | JOB_SVFLG_CHECKPOINT_MIGRATEABLE;

      }
    }
  else
    {
    /* need to try rerun if possible or just abort the job */

    if (pjob)
      {
      pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_CHECKPOINT_FILE;
      pjob->ji_qs.ji_substate = JOB_SUBSTATE_RUNNING;

      if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
        rerun_or_kill(pjob, msg_on_shutdown);
      }
    }

  release_req(ptask);
  }  /* END post_checkpoint() */





/* NOTE:  pjob may be free with dangling pointer */

static void rerun_or_kill(

  job  *pjob,  /* I (modified/freed) */
  char *text)  /* I */

  {
  long server_state = server.sv_attr[(int)SRV_ATR_State].at_val.at_long;

  if (pjob->ji_wattr[(int)JOB_ATR_rerunable].at_val.at_long)
    {
    /* job is rerunable, mark it to be requeued */

    issue_signal(pjob, "SIGKILL", release_req, 0);

    pjob->ji_qs.ji_substate  = JOB_SUBSTATE_RERUN;

    strcpy(log_buffer, msg_init_queued);
    strcat(log_buffer, pjob->ji_qhdr->qu_qs.qu_name);
    strcat(log_buffer, text);
    }
  else if (server_state != SV_STATE_SHUTDEL)
    {
    /* job not rerunable, immediate shutdown - kill it off */

    strcpy(log_buffer, msg_job_abort);
    strcat(log_buffer, text);

    /* need to record log message before purging job */

    log_event(
      PBSEVENT_SYSTEM | PBSEVENT_JOB | PBSEVENT_DEBUG,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      log_buffer);

    job_abt(&pjob, log_buffer);

    return;
    }
  else
    {
    /* delayed shutdown, leave job running */

    strcpy(log_buffer, msg_leftrunning);
    strcat(log_buffer, text);
    }

  log_event(PBSEVENT_SYSTEM | PBSEVENT_JOB | PBSEVENT_DEBUG,

            PBS_EVENTCLASS_JOB, pjob->ji_qs.ji_jobid,
            log_buffer);

  return;
  }  /* END rerun_or_kill() */
