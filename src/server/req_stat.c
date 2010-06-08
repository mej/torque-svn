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
 * svr_stat.c
 *
 * Functions relating to the Status Job, Status Queue, and
 *  Status Server Batch Requests.
 */
#include <pbs_config.h>   /* the master config generated by configure */

#define STAT_CNTL 1

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include "libpbs.h"
#include <ctype.h>
#include <stdint.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "server.h"
#include "queue.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "work_task.h"
#include "pbs_error.h"
#include "svrfunc.h"
#include "net_connect.h"
#include "pbs_nodes.h"
#include "log.h"
#include "u_tree.h"

/* Global Data Items: */

extern struct server   server;
extern tlist_head      svr_alljobs;
extern tlist_head      svr_queues;
extern char            server_name[];
extern attribute_def   svr_attr_def[];
extern attribute_def   que_attr_def[];
extern attribute_def   job_attr_def[];
extern attribute_def   node_attr_def[];   /* node attributes defs */
extern int        pbs_mom_port;
extern time_t        time_now;
extern char       *msg_init_norerun;

extern struct pbsnode *tfind_addr(const u_long, uint16_t);
extern int             LOGLEVEL;

/* Extern Functions */

int status_job(job *, struct batch_request *, svrattrl *, tlist_head *, int *);
int status_attrib(svrattrl *, attribute_def *, attribute *, int, int, tlist_head *, int *, int);
extern int  svr_connect(pbs_net_t, unsigned int, void (*)(int), enum conn_type);
extern int  status_nodeattrib(svrattrl *, attribute_def *, struct pbsnode *, int, int, tlist_head *, int*);
extern int  hasprop(struct pbsnode *, struct prop *);
extern void rel_resc(job*);

/* Private Data Definitions */

static int bad;

/* The following private support functions are included */

static void update_state_ct(attribute *, int *, char *);
static int  status_que(pbs_queue *, struct batch_request *, tlist_head *);
static int  status_node(struct pbsnode *, struct batch_request *, tlist_head *);
static void req_stat_job_step2(struct stat_cntl *);
static void stat_update(struct work_task *);

#ifndef TMAX_JOB
#define TMAX_JOB 999999999
#endif /* TMAX_JOB */




/**
 * req_stat_job - service the Status Job Request
 *
 * This request processes the request for status of a single job or
 * the set of jobs at a destination.
 * If SRV_ATR_PollJobs is not set or false (default), this takes three
 * steps because of running jobs being known to MOM:
 *   1. validate and setup the request (done here).
 *   2. for each candidate job which is running and for which there is no
 *      current status, ask MOM for an update.
 *   3. form the reply for each candidate job and return it to the client.
 *      If SRV_ATR_PollJobs is true, then we skip step 2.
 */

enum TJobStatTypeEnum
  {
  tjstNONE = 0,
  tjstJob,
  tjstQueue,
  tjstServer,
  tjstTruncatedQueue,
  tjstTruncatedServer,
  tjstLAST
  };

void req_stat_job(

  struct batch_request *preq) /* ptr to the decoded request   */

  {

  struct stat_cntl *cntl; /* see svrfunc.h  */
  char     *name;
  job     *pjob = NULL;
  pbs_queue    *pque = NULL;
  int      rc = 0;


  enum TJobStatTypeEnum type = tjstNONE;

  /*
   * first, validate the name of the requested object, either
   * a job, a queue, or the whole server.
   */

  /* FORMAT:  name = { <JOBID> | <QUEUEID> | '' } */

  name = preq->rq_ind.rq_status.rq_id;

  if (preq->rq_extend != NULL)
    {
    /* evaluate pbs_job_stat() 'extension' field */

    if (!strncasecmp(preq->rq_extend, "truncated", strlen("truncated")))
      {
      /* truncate response by 'max_report' */

      type = tjstTruncatedServer;
      }
    }    /* END if (preq->rq_extend != NULL) */

  if (isdigit((int)*name))
    {
    /* status a single job */

    type = tjstJob;

    if ((pjob = find_job(name)) == NULL)
      {
      rc = PBSE_UNKJOBID;
      }
    }
  else if (isalpha(name[0]))
    {
    if (type == tjstNONE)
      type = tjstQueue;
    else
      type = tjstTruncatedQueue;

    if ((pque = find_queuebyname(name)) == NULL)
      {
      rc = PBSE_UNKQUE;
      }
    }
  else if ((*name == '\0') || (*name == '@'))
    {
    /* status all jobs at server */

    if (type == tjstNONE)
      type = tjstServer;
    }
  else
    {
    rc = PBSE_IVALREQ;
    }

  if (rc != 0)
    {
    /* is invalid - an error */

    req_reject(rc, 0, preq, NULL, NULL);

    return;
    }

  preq->rq_reply.brp_choice = BATCH_REPLY_CHOICE_Status;

  CLEAR_HEAD(preq->rq_reply.brp_un.brp_status);

  cntl = (struct stat_cntl *)malloc(sizeof(struct stat_cntl));

  if (cntl == NULL)
    {
    req_reject(PBSE_SYSTEM, 0, preq, NULL, NULL);

    return;
    }

  cntl->sc_type   = (int)type;

  cntl->sc_conn   = -1;
  cntl->sc_pque   = pque;
  cntl->sc_origrq = preq;
  cntl->sc_post   = req_stat_job_step2;
  cntl->sc_jobid[0] = '\0'; /* cause "start from beginning" */

  if (server.sv_attr[(int)SRV_ATR_PollJobs].at_val.at_long)
    cntl->sc_post = 0; /* we're not going to make clients wait */

  req_stat_job_step2(cntl); /* go to step 2, see if running is current */

  return;
  }  /* END req_stat_job() */





/*
 * req_stat_job_step2 - continue with statusing of jobs
 *
 *  This is re-entered after sending status requests to MOM.
 *
 *  NOTE:  because server job array and queue job arrays are basic linked
 *         lists, this routine will report jobs in jobid order.
 *
 *         if truncated listed are desired, should handle this by walking
 *         all queues and reporting max_report jobs/queue
 *
 *  Note, the funny initialization/advance of pjob in the "while" loop
 *  comes from the fact we want to look at the "next" job on re-entry.
 *
 * @see req_stat_job() - parent
 * @see status_job() - child - build job record
 */

static void req_stat_job_step2(

  struct stat_cntl *cntl)  /* I/O (freed on return) */

  {
  svrattrl        *pal;
  job         *pjob;
  job                  *cpjob;

  struct batch_request *preq;

  struct batch_reply   *preply;
  int          rc = 0;

  enum TJobStatTypeEnum type;

  pbs_queue            *pque = NULL;
  int                   exec_only = 0;

  int                   IsTruncated = 0;

  long                  DTime;  /* delta time - only report full attribute list if J->MTime > DTime */

  static svrattrl      *dpal = NULL;

  preq   = cntl->sc_origrq;
  type   = (enum TJobStatTypeEnum)cntl->sc_type;
  preply = &preq->rq_reply;

  /* See pbs_server_attributes(1B) for details on "poll_jobs" behaviour */

  /* NOTE:  If IsTruncated is true, should walk all queues and walk jobs in each queue
            until max_reported is reached (NYI) */

  if (dpal == NULL)
    {
    /* build 'delta' attribute list */

    svrattrl *tpal;

    tlist_head dalist;

    int aindex;

    int atrlist[] =
      {
      JOB_ATR_jobname,
      JOB_ATR_resc_used,
      JOB_ATR_LAST
      };

    CLEAR_LINK(dalist);

    for (aindex = 0;atrlist[aindex] != JOB_ATR_LAST;aindex++)
      {
      if ((tpal = attrlist_create("", "", 23)) == NULL)
        {
        return;
        }

      tpal->al_valln = atrlist[aindex];

      if (dpal == NULL)
        dpal = tpal;

      append_link(&dalist, &tpal->al_link, tpal);
      }
    }  /* END if (dpal == NULL) */

  if (!server.sv_attr[(int)SRV_ATR_PollJobs].at_val.at_long)
    {
    /* polljobs not set - indicates we may need to obtain fresh data from
       MOM */

    if (cntl->sc_jobid[0] == '\0')
      pjob = NULL;
    else
      pjob = find_job(cntl->sc_jobid);

    while (1)
      {
      if (pjob == NULL)
        {
        /* start from the first job */

        if (type == tjstJob)
          {
          pjob = find_job(preq->rq_ind.rq_status.rq_id);
          }
        else if (type == tjstQueue)
          {
          pjob = (job *)GET_NEXT(cntl->sc_pque->qu_jobs);
          }
        else
          {
          if ((type == tjstTruncatedServer) || (type == tjstTruncatedQueue))
            IsTruncated = TRUE;

          pjob = (job *)GET_NEXT(svr_alljobs);
          }
        }    /* END if (pjob == NULL) */
      else
        {
        /* get next job */

        if (type == tjstJob)
          break;

        if (type == tjstQueue)
          pjob = (job *)GET_NEXT(pjob->ji_jobque);
        else
          pjob = (job *)GET_NEXT(pjob->ji_alljobs);
        }

      if (pjob == NULL)
        break;

      /* PBS_RESTAT_JOB defaults to 30 seconds */

      if ((pjob->ji_qs.ji_substate == JOB_SUBSTATE_RUNNING) &&
          ((time_now - pjob->ji_momstat) > JobStatRate))
        {
        /* go to MOM for status */

        strcpy(cntl->sc_jobid, pjob->ji_qs.ji_jobid);

        if ((rc = stat_to_mom(pjob, cntl)) == PBSE_SYSTEM)
          {
          break;
          }

        if (rc != 0)
          {
          rc = 0;

          continue;
          }

        return; /* will pick up after mom replies */
        }
      }    /* END while(1) */

    if (cntl->sc_conn >= 0)
      svr_disconnect(cntl->sc_conn);  /* close connection to MOM */

    if (rc != 0)
      {
      free(cntl);

      reply_free(preply);

      req_reject(rc, 0, preq, NULL, "cannot get update from mom");

      return;
      }
    }    /* END if (!server.sv_attr[(int)SRV_ATR_PollJobs].at_val.at_long) */

  /*
   * now ready for part 3, building the status reply,
   * loop through again
   */

  if (type == tjstJob)
    pjob = find_job(preq->rq_ind.rq_status.rq_id);
  else if (type == tjstQueue)
    pjob = (job *)GET_NEXT(cntl->sc_pque->qu_jobs);
  else
    pjob = (job *)GET_NEXT(svr_alljobs);

  DTime = 0;

  if (preq->rq_extend != NULL)
    {
    char *ptr;

    /* FORMAT:  { EXECQONLY | DELTA:<EPOCHTIME> } */

    if (strstr(preq->rq_extend, EXECQUEONLY))
      exec_only = 1;

    ptr = strstr(preq->rq_extend, "DELTA:");

    if (ptr != NULL)
      {
      ptr += strlen("delta:");

      DTime = strtol(ptr, NULL, 10);
      }
    }

  free(cntl);

  if ((type == tjstTruncatedServer) || (type == tjstTruncatedQueue))
    {
    long sentJobCounter;
    long qjcounter;
    long qmaxreport;

    /* loop through all queues */

    for (pque = (pbs_queue *)GET_NEXT(svr_queues);
         pque != NULL;
         pque = (pbs_queue *)GET_NEXT(pque->qu_link))
      {
      qjcounter = 0;

      if ((exec_only == 1) &&
          (pque->qu_qs.qu_type != QTYPE_Execution))
        {
        /* ignore routing queues */

        continue;
        }

      if (((pque->qu_attr[QA_ATR_MaxReport].at_flags & ATR_VFLAG_SET) != 0) &&
          (pque->qu_attr[QA_ATR_MaxReport].at_val.at_long >= 0))
        {
        qmaxreport = pque->qu_attr[QA_ATR_MaxReport].at_val.at_long;
        }
      else
        {
        qmaxreport = TMAX_JOB;
        }

      if (LOGLEVEL >= 5)
        {
        sprintf(log_buffer,"giving scheduler up to %ld idle jobs in queue %s\n",
          qmaxreport,
          pque->qu_qs.qu_name);

        log_event(
          PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_QUEUE,
          pque->qu_qs.qu_name,
          log_buffer);
        }

      sentJobCounter = 0;

      /* loop through jobs in queue */

      for (pjob = (job *)GET_NEXT(pque->qu_jobs);
           pjob != NULL;
           pjob = (job *)GET_NEXT(pjob->ji_jobque))
        {
        if ((qjcounter >= qmaxreport) &&
            (pjob->ji_qs.ji_state == JOB_STATE_QUEUED))
          {
          /* max_report of queued jobs reached for queue */

          continue;
          }

        pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

        rc = status_job(
               pjob,
               preq,
               (pjob->ji_wattr[(int)JOB_ATR_mtime].at_val.at_long >= DTime) ? pal : dpal,
               &preply->brp_un.brp_status,
               &bad);

        if ((rc != 0) && (rc != PBSE_PERM))
          {
          req_reject(rc, bad, preq, NULL, NULL);

          return;
          }

        sentJobCounter++;

        if (pjob->ji_qs.ji_state == JOB_STATE_QUEUED)
          qjcounter++;
        }    /* END for (pjob) */

      if (LOGLEVEL >= 5)
        {
        sprintf(log_buffer,"sent scheduler %ld total jobs for queue %s\n",
          sentJobCounter,
          pque->qu_qs.qu_name);

        log_event(
          PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_QUEUE,
          pque->qu_qs.qu_name,
          log_buffer);
        }
      }      /* END for (pque) */

    reply_send(preq);

    return;
    }        /* END if ((type == tjstTruncatedServer) || ...) */

  while (pjob != NULL)
    {
    /* go ahead and build the status reply for this job */

    if (exec_only)
      {
      pque = find_queuebyname(pjob->ji_qs.ji_queue);

      if (pque->qu_qs.qu_type != QTYPE_Execution)
        goto nextjob;
      }

    pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

    rc = status_job(
           pjob,
           preq,
           pal,
           &preply->brp_un.brp_status,
           &bad);

    if ((rc != 0) && (rc != PBSE_PERM))
      {
      req_reject(rc, bad, preq, NULL, NULL);

      return;
      }

    /* get next job */

nextjob:

    if (type == tjstJob)
      break;

    if (type == tjstQueue)
      pjob = (job *)GET_NEXT(pjob->ji_jobque);
    else
      pjob = (job *)GET_NEXT(pjob->ji_alljobs);

    rc = 0;
    }  /* END while (pjob != NULL) */

  /* add completed jobs */

  cpjob = NULL;  /* disable completed job display for now */

  while (cpjob != NULL)
    {
    /* go ahead and build the status reply for this job */

    pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

    rc = status_job(pjob, preq, pal, &preply->brp_un.brp_status, &bad);

    if (rc && (rc != PBSE_PERM))
      {
      req_reject(rc, bad, preq, NULL, NULL);

      return;
      }

    /* get next job */

    if (type == tjstJob)
      break;

    if (type == tjstQueue)
      pjob = (job *)GET_NEXT(pjob->ji_jobque);
    else
      pjob = (job *)GET_NEXT(pjob->ji_alljobs);

    rc = 0;
    }  /* END while (cpjob != NULL) */

  reply_send(preq);

  return;
  }  /* END req_stat_job_step2() */





/*
 * stat_to_mom - send a Job Status to MOM to obtain latest status.
 * Used by req_stat_job()/stat_step_2()
 *
 * Returns PBSE_SYSTEM if out of memory, PBSE_NORELYMOM if the MOM
 * is down, offline, or deleted.  Otherwise returns result of MOM
 * contact request.
 *
 * NOTE: called by qstat if poll_jobs == False
 */

int stat_to_mom(

  job              *pjob,  /* I */
  struct stat_cntl *cntl)  /* I/O */

  {

  struct batch_request *newrq;
  int      rc;
  ulong    addr;

  struct work_task     *pwt = 0;

  struct pbsnode       *node;

  if ((newrq = alloc_br(PBS_BATCH_StatusJob)) == NULL)
    {
    return(PBSE_SYSTEM);
    }

  /* set up status request, save address of cntl in request for later */

  newrq->rq_extra = (void *)cntl;

  if (cntl->sc_type == 1)
    strcpy(newrq->rq_ind.rq_status.rq_id, pjob->ji_qs.ji_jobid);
  else
    newrq->rq_ind.rq_status.rq_id[0] = '\0';  /* get stat of all */

  CLEAR_HEAD(newrq->rq_ind.rq_status.rq_attr);

  /* if MOM is down just return stale information */

  addr = pjob->ji_qs.ji_un.ji_exect.ji_momaddr;

  if (((node = tfind_addr(addr, pjob->ji_qs.ji_un.ji_exect.ji_momport)) != NULL) &&
      (node->nd_state & (INUSE_DELETED | INUSE_DOWN)))
    {
    if (LOGLEVEL >= 6)
      {
      sprintf(log_buffer, "node '%s' is allocated to job but in state '%s'",
              node->nd_name,
              (node->nd_state & INUSE_DELETED) ? "deleted" : "down");

      log_event(
        PBSEVENT_SYSTEM,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        log_buffer);
      }

    return(PBSE_NORELYMOM);
    }

  /* get connection to MOM */

  cntl->sc_conn = svr_connect(
                    pjob->ji_qs.ji_un.ji_exect.ji_momaddr,
                    pjob->ji_qs.ji_un.ji_exect.ji_momport,
                    process_Dreply,
                    ToServerDIS);

  if ((rc = cntl->sc_conn) >= 0)
    rc = issue_Drequest(cntl->sc_conn, newrq, stat_update, &pwt);

  if (rc != 0)
    {
    /* request failed */

    if (pwt)
      delete_task(pwt);

    free_br(newrq);

    if (cntl->sc_conn >= 0)
      svr_disconnect(cntl->sc_conn);
    }  /* END if (rc != NULL) */

  return(rc);
  }  /* END stat_to_mom() */





/*
 * stat_update - take reply to status request from MOM and update job status
 */

static void stat_update(

  struct work_task *pwt)

  {

  struct stat_cntl     *cntl;
  job                  *pjob;

  struct batch_request *preq;

  struct batch_reply   *preply;

  struct brp_status    *pstatus;
  svrattrl        *sattrl;
  int    oldsid;

  preq = pwt->wt_parm1;
  preply = &preq->rq_reply;
  cntl = preq->rq_extra;

  if (preply->brp_choice == BATCH_REPLY_CHOICE_Status)
    {
    pstatus = (struct brp_status *)GET_NEXT(preply->brp_un.brp_status);

    while (pstatus != NULL)
      {
      if ((pjob = find_job(pstatus->brp_objname)))
        {
        sattrl = (svrattrl *)GET_NEXT(pstatus->brp_attr);

        oldsid = pjob->ji_wattr[(int)JOB_ATR_session_id].at_val.at_long;

        modify_job_attr(
          pjob,
          sattrl,
          ATR_DFLAG_MGWR | ATR_DFLAG_SvWR,
          &bad);

        if (oldsid != pjob->ji_wattr[(int)JOB_ATR_session_id].at_val.at_long)
          {
          /* first save since running job (or the sid has changed), */
          /* must save session id    */

          job_save(pjob, SAVEJOB_FULL, 0);

          svr_mailowner(pjob, MAIL_BEGIN, MAIL_NORMAL, NULL);
          }

#ifdef USESAVEDRESOURCES
        else
          {
          /* save so we can recover resources used */
          job_save(pjob, SAVEJOB_FULL);
          }
#endif    /* USESAVEDRESOURCES */

        pjob->ji_momstat = time_now;
        }

      pstatus = (struct brp_status *)GET_NEXT(pstatus->brp_stlink);
      }  /* END while (pstatus != NULL) */
    }    /* END if (preply->brp_choice == BATCH_REPLY_CHOICE_Status) */
  else
    {
    if (preply->brp_code == PBSE_UNKJOBID)
      {
      /* we sent a stat request, but mom says it doesn't know anything about
         the job */
      if ((pjob = find_job(preq->rq_ind.rq_status.rq_id)))
        {
        /* job really isn't running any more - mom doesn't know anything about it
           this can happen if a diskless node reboots and the mom_priv/jobs
           directory is cleared, set its state to queued so job_abt doesn't
           think it is still running */
        svr_setjobstate(pjob, JOB_STATE_QUEUED, JOB_SUBSTATE_ABORT);
        rel_resc(pjob);
        job_abt(&pjob, "Job does not exist on node");

        /* TODO, if the job is rerunnable we should set its state back to queued */

        }
      }
    }

  release_req(pwt);

  cntl->sc_conn = -1;

  if (cntl->sc_post)
    cntl->sc_post(cntl); /* continue where we left off */
  else
    free(cntl); /* a bit of a kludge but its saves an extra func */

  return;
  }  /* END stat_update() */





/*
 * stat_mom_job - status a single job running under a MOM
 * This is used after sending a job to mom to get the session id
 */

void stat_mom_job(

  job *pjob)

  {

  struct stat_cntl *cntl;

  cntl = (struct stat_cntl *)malloc(sizeof(struct stat_cntl));

  if (cntl == NULL)
    {
    return;
    }

  cntl->sc_type   = 1;

  cntl->sc_conn   = -1;
  cntl->sc_pque   = (pbs_queue *)0;
  cntl->sc_origrq = (struct batch_request *)0;
  cntl->sc_post   = 0;  /* tell stat_update() to free cntl */
  cntl->sc_jobid[0] = '\0'; /* cause "start from beginning" */

  if (stat_to_mom(pjob, cntl) != 0)
    {
    free(cntl);
    }

  /* if not an error, cntl freed in stat_update() */

  return;
  }  /* END stat_mom_job() */



/**
 * poll_job_task
 *
 * The invocation of this routine is triggered from
 * the pbs_server main_loop code.  The check of
 * SRV_ATR_PollJobs appears to be redundant.
 */
void poll_job_task(

  struct work_task *ptask)

  {

  job *pjob;

  pjob = (job *)ptask->wt_parm1;

  if (pjob == NULL)
    {
    /* FAILURE */

    return;
    }

  if (server.sv_attr[(int)SRV_ATR_PollJobs].at_val.at_long &&
      (pjob->ji_qs.ji_state == JOB_STATE_RUNNING))
    {
    stat_mom_job(pjob);
    }

  return;
  }  /* END poll_job_task() */




/*
 * req_stat_que - service the Status Queue Request
 *
 * This request processes the request for status of a single queue or
 * the set of queues at a destinaion.
 */

void req_stat_que(

  struct batch_request *preq) /* ptr to the decoded request   */

  {
  char     *name;
  pbs_queue    *pque = NULL;

  struct batch_reply *preply;
  int      rc   = 0;
  int      type = 0;

  /*
   * first, validate the name of the requested object, either
   * a queue, or null for all queues
   */

  name = preq->rq_ind.rq_status.rq_id;

  if ((*name == '\0') || (*name == '@'))
    {
    type = 1;
    }
  else
    {
    pque = find_queuebyname(name);

    if (pque == NULL)
      {
      req_reject(PBSE_UNKQUE, 0, preq, NULL, "cannot locate queue");

      return;
      }
    }

  preply = &preq->rq_reply;

  preply->brp_choice = BATCH_REPLY_CHOICE_Status;

  CLEAR_HEAD(preply->brp_un.brp_status);

  if (type == 0)
    {
    /* get status of the named queue */

    rc = status_que(pque, preq, &preply->brp_un.brp_status);
    }
  else
    {
    /* get status of all queues */

    pque = (pbs_queue *)GET_NEXT(svr_queues);

    while (pque != NULL)
      {
      rc = status_que(pque, preq, &preply->brp_un.brp_status);

      if (rc != 0)
        {
        if (rc != PBSE_PERM)
          break;

        rc = 0;
        }

      pque = (pbs_queue *)GET_NEXT(pque->qu_link);
      }
    }

  if (rc != 0)
    {
    reply_free(preply);

    req_reject(rc, bad, preq, NULL, "status_queue failed");
    }
  else
    {
    reply_send(preq);
    }

  return;
  }  /* END req_stat_que() */





/*
 * status_que - Build the status reply for a single queue.
 */

static int status_que(

  pbs_queue            *pque,     /* ptr to que to status */
  struct batch_request *preq,
  tlist_head           *pstathd)  /* head of list to append status to */

  {

  struct brp_status *pstat;
  svrattrl          *pal;

  if ((preq->rq_perm & ATR_DFLAG_RDACC) == 0)
    {
    return(PBSE_PERM);
    }

  /* ok going to do status, update count and state counts from qu_qs */

  pque->qu_attr[(int)QA_ATR_TotalJobs].at_val.at_long = pque->qu_numjobs;

  pque->qu_attr[(int)QA_ATR_TotalJobs].at_flags |= ATR_VFLAG_SET;

  update_state_ct(
    &pque->qu_attr[(int)QA_ATR_JobsByState],
    pque->qu_njstate,
    pque->qu_jobstbuf);

  /* allocate status sub-structure and fill in header portion */

  pstat = (struct brp_status *)malloc(sizeof(struct brp_status));

  if (pstat == NULL)
    {
    return(PBSE_SYSTEM);
    }

  pstat->brp_objtype = MGR_OBJ_QUEUE;

  strcpy(pstat->brp_objname, pque->qu_qs.qu_name);

  CLEAR_LINK(pstat->brp_stlink);
  CLEAR_HEAD(pstat->brp_attr);

  append_link(pstathd, &pstat->brp_stlink, pstat);

  /* add attributes to the status reply */

  bad = 0;

  pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

  if (status_attrib(
        pal,
        que_attr_def,
        pque->qu_attr,
        QA_ATR_LAST,
        preq->rq_perm,
        &pstat->brp_attr,
        &bad,
        1) != 0)   /* IsOwner == TRUE */
    {
    return(PBSE_NOATTR);
    }

  return(0);
  }  /* END stat_que() */





#ifdef NUMA_SUPPORT
/* instead of getting the status on a node with numa nodes, report
 * the status of all the numa nodes
 *
 * @param pnode - the node to report on
 * @param preq - the batch request
 * @param pstathd - the list to add this response to
 *
 * @return - 0 on SUCCESS, error code otherwise
 */
int get_numa_statuses(

  struct pbsnode       *pnode,    /* ptr to node receiving status query */
  struct batch_request *preq,
  tlist_head            *pstathd)  /* head of list to append status to  */

  {
  int i;
  int rc;

  struct pbsnode *pn;

  if (pnode->num_numa_nodes == 0)
    {
    /* no numa nodes, just return the status for this node */
    rc = status_node(pnode,preq,pstathd);

    return(rc);
    }

  for (i = 0; i < pnode->num_numa_nodes; i++)
    {
    pn = AVL_find(i,pnode->nd_mom_port,pnode->numa_nodes);

    if (pn == NULL)
      continue;

    if ((rc = status_node(pn, preq, pstathd)) != 0)
      return(rc);
    }

  return(rc);
  } /* END get_numa_statuses() */
#endif /* NUMA_SUPPORT */





/*
 * req_stat_node - service the Status Node Request
 *
 * This request processes the request for status of a single node or
 * set of nodes at a destination.
 */

void req_stat_node(

  struct batch_request *preq) /* ptr to the decoded request   */

  {
  char    *name;

  struct pbsnode *pnode = NULL;

  struct batch_reply *preply;
  svrattrl *pal;
  int     rc   = 0;
  int     type = 0;
  int     i;

  struct prop props;


  char     *id = "req_stat_node";

  /*
   * first, check that the server indeed has a list of nodes
   * and if it does, validate the name of the requested object--
   * either name is that of a specific node, or name[0] is null/@
   * meaning request is for all nodes in the server's jurisdiction
   */

  if (LOGLEVEL >= 6)
    {
    log_record(
      PBSEVENT_SCHED,
      PBS_EVENTCLASS_REQUEST,
      id,
      "entered");
    }

  if ((pbsndmast == NULL) || (svr_totnodes <= 0))
    {
    req_reject(PBSE_NONODES, 0, preq, NULL, "node list is empty - check 'server_priv/nodes' file");

    return;
    }

  name = preq->rq_ind.rq_status.rq_id;

  if ((*name == '\0') || (*name == '@'))
    {
    type = 1;
    }
  else if ((*name == ':') && (*(name + 1) != '\0'))
    {
    if (!strcmp(name + 1, "ALL"))
      {
      type = 1;  /* psuedo-group for all nodes */
      }
    else
      {
      type = 2;
      props.name = name + 1;
      props.mark = 1;
      props.next = NULL;
      }
    }
  else
    {
    pnode = find_nodebyname(name);

    if (pnode == NULL)
      {
      req_reject(PBSE_UNKNODE, 0, preq, NULL, "cannot locate specified node");

      return;
      }
    }

  preply = &preq->rq_reply;

  preply->brp_choice = BATCH_REPLY_CHOICE_Status;

  CLEAR_HEAD(preply->brp_un.brp_status);

  if (type == 0)
    {
    /* get status of the named node */

#ifdef NUMA_SUPPORT
    /* get the status on all of the numa nodes */
    rc = get_numa_statuses(pnode,preq,&preply->brp_un.brp_status);
#else
    rc = status_node(pnode, preq, &preply->brp_un.brp_status);
#endif /* NUMA_SUPPORT */
    }
  else
    {
    /* get status of all or several nodes */

    for (i = 0;i < svr_totnodes;i++)
      {
      pnode = pbsndmast[i];

      if ((type == 2) && !hasprop(pnode, &props))
        continue;
#ifdef NUMA_SUPPORT
      /* get the status on all of the numa nodes */
      rc = get_numa_statuses(pnode,preq,&preply->brp_un.brp_status);
#else
      if ((rc = status_node(pnode, preq, &preply->brp_un.brp_status)) != 0)
#endif /* NUMA_SUPPORT */
        break;
      }
    }

  if (!rc)
    {
    /* SUCCESS */

    reply_send(preq);
    }
  else
    {
    if (rc != PBSE_UNKNODEATR)
      {
      req_reject(rc, 0, preq, NULL, NULL);
      }
    else
      {
      pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

      reply_badattr(rc, bad, pal, preq);
      }
    }

  return;
  }  /* END req_stat_node() */





/*
 * status_node - Build the status reply for a single node.
 */

static int status_node(

  struct pbsnode       *pnode,    /* ptr to node receiving status query */
  struct batch_request *preq,
  tlist_head            *pstathd)  /* head of list to append status to  */

  {
  int       rc = 0;

  struct brp_status *pstat;
  svrattrl          *pal;

  if (pnode->nd_state & INUSE_DELETED)  /*node no longer valid*/
    {
    return(0);
    }

  if ((preq->rq_perm & ATR_DFLAG_RDACC) == 0)
    {
    return(PBSE_PERM);
    }

  /* allocate status sub-structure and fill in header portion */

  pstat = (struct brp_status *)malloc(sizeof(struct brp_status));

  if (pstat == NULL)
    {
    return(PBSE_SYSTEM);
    }

  pstat->brp_objtype = MGR_OBJ_NODE;

  strcpy(pstat->brp_objname, pnode->nd_name);

  CLEAR_LINK(pstat->brp_stlink);
  CLEAR_HEAD(pstat->brp_attr);

  /*add this new brp_status structure to the list hanging off*/
  /*the request's reply substructure                         */

  append_link(pstathd, &pstat->brp_stlink, pstat);

  /*point to the list of node-attributes about which we want status*/
  /*hang that status information from the brp_attr field for this  */
  /*brp_status structure                                           */

  bad = 0;                                    /*global variable*/

  pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

  rc = status_nodeattrib(
         pal,
         node_attr_def,
         pnode,
         ND_ATR_LAST,
         preq->rq_perm,
         &pstat->brp_attr,
         &bad);

  return(rc);
  }  /* END status_node() */





/*
 * req_stat_svr - service the Status Server Request
 *
 * This request processes the request for status of the Server
 */

void req_stat_svr(

  struct batch_request *preq) /* ptr to the decoded request */

  {
  svrattrl    *pal;

  struct batch_reply *preply;

  struct brp_status  *pstat;
  int *nc;
  static char nc_buf[128];

  /* update count and state counts from sv_numjobs and sv_jobstates */

  server.sv_attr[(int)SRV_ATR_TotalJobs].at_val.at_long = server.sv_qs.sv_numjobs;
  server.sv_attr[(int)SRV_ATR_TotalJobs].at_flags |= ATR_VFLAG_SET;

  update_state_ct(
    &server.sv_attr[(int)SRV_ATR_JobsByState],
    server.sv_jobstates,
    server.sv_jobstbuf);

  nc = netcounter_get();
  sprintf(nc_buf, "%d %d %d", *nc, *(nc + 1), *(nc + 2));
  server.sv_attr[(int)SRV_ATR_NetCounter].at_val.at_str = nc_buf;
  server.sv_attr[(int)SRV_ATR_NetCounter].at_flags |= ATR_VFLAG_SET;

  /* allocate a reply structure and a status sub-structure */

  preply = &preq->rq_reply;
  preply->brp_choice = BATCH_REPLY_CHOICE_Status;

  CLEAR_HEAD(preply->brp_un.brp_status);

  pstat = (struct brp_status *)malloc(sizeof(struct brp_status));

  if (pstat == NULL)
    {
    reply_free(preply);

    req_reject(PBSE_SYSTEM, 0, preq, NULL, NULL);

    return;
    }

  CLEAR_LINK(pstat->brp_stlink);

  strcpy(pstat->brp_objname, server_name);

  pstat->brp_objtype = MGR_OBJ_SERVER;

  CLEAR_HEAD(pstat->brp_attr);

  append_link(&preply->brp_un.brp_status, &pstat->brp_stlink, pstat);

  /* add attributes to the status reply */

  bad = 0;

  pal = (svrattrl *)GET_NEXT(preq->rq_ind.rq_status.rq_attr);

  if (status_attrib(
        pal,
        svr_attr_def,
        server.sv_attr,
        SRV_ATR_LAST,
        preq->rq_perm,
        &pstat->brp_attr,
        &bad,
        1))    /* IsOwner == TRUE */
    {
    reply_badattr(PBSE_NOATTR, bad, pal, preq);
    }
  else
    {
    reply_send(preq);
    }

  return;
  }  /* END req_stat_svr() */

/* DIAGTODO: write req_stat_diag() */



/*
 * update-state_ct - update the count of jobs per state (in queue and server
 * attributes.
 */

static void
update_state_ct(attribute *pattr, int *ct_array, char *buf)
  {
  static char *statename[] = { "Transit", "Queued", "Held",
                               "Waiting", "Running", "Exiting"
                             };
  int  index;

  buf[0] = '\0';

  for (index = 0; index < PBS_NUMJOBSTATE; index++)
    {
    sprintf(buf + strlen(buf), "%s:%d ", statename[index],
            *(ct_array + index));
    }

  pattr->at_val.at_str = buf;

  pattr->at_flags |= ATR_VFLAG_SET;
  }
