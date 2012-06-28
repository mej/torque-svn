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
 * dis_read.c - contains function to read and decode the DIS
 * encoded requests and replies.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "dis.h"
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "pbs_error.h"
#include "credential.h"
#include "batch_request.h"
#include "net_connect.h"
#include "tcp.h" /* tcp_chan */



/* External Global Data */

extern int       LOGLEVEL;


/*
 * dis_request_read - read in an DIS encoded request from the network
 * and decodes it:
 * Read and decode the request into the request structures
 *
 * returns: 0 if request read ok, batch_request pointed to by request is
 *     updated.
 *  -1 if EOF (no request but no error)
 *  >0 if errors ( a PBSE_ number)
 */

int dis_request_read(

  struct tcp_chan      *chan,
  struct batch_request *request) /* server internal structure */

  {
  int   proto_type;
  int   proto_ver;
  int   rc;  /* return code */
  char  log_buf[LOCAL_LOG_BUF_SIZE];

#ifdef PBS_MOM
  /* NYI: talk to Ken about this. This is necessary due to the changes to 
   * decode_DIS_ReqHdr */
  proto_type = disrui(chan, &rc);
#endif

  /* Decode the Request Header, that will tell the request type */

  if ((rc = decode_DIS_ReqHdr(chan, request, &proto_type, &proto_ver)))
    {
    if (rc == DIS_EOF)
      {
      return(EOF);
      }

    sprintf(log_buf, "req header bad, dis error %d (%s), type=%s",
      rc,
      dis_emsg[rc],
      reqtype_to_txt(request->rq_type));

    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, __func__, log_buf);

    return(PBSE_DISPROTO);
    }

  if (proto_ver != PBS_BATCH_PROT_VER)
    {
    sprintf(log_buf, "conflicting version numbers, %d detected, %d expected",
            proto_ver,
            PBS_BATCH_PROT_VER);

    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, __func__, log_buf);

    return(PBSE_DISPROTO);
    }

  if ((request->rq_type < 0) || (request->rq_type >= PBS_BATCH_CEILING))
    {
    sprintf(log_buf, "invalid request type: %d", request->rq_type);

    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, __func__, log_buf);

    return(PBSE_DISPROTO);
    }

  /* Decode the Request Body based on the type */

  if (LOGLEVEL >= 5)
    {
    sprintf(log_buf, "decoding command %s from %s",
      reqtype_to_txt(request->rq_type),
      request->rq_user);

    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, __func__, log_buf);
    }

  switch (request->rq_type)
    {

    case PBS_BATCH_Disconnect:

      return(PBSE_SOCKET_CLOSE);  /* set EOF return */

      /*NOTREACHED*/

      break;

    case PBS_BATCH_QueueJob:

      CLEAR_HEAD(request->rq_ind.rq_queuejob.rq_attr);

      rc = decode_DIS_QueueJob(chan, request);

      break;

    case PBS_BATCH_JobCred:

      rc = decode_DIS_JobCred(chan, request);

      break;

    case PBS_BATCH_jobscript:

    case PBS_BATCH_MvJobFile:

      rc = decode_DIS_JobFile(chan, request);

      break;

    case PBS_BATCH_RdytoCommit:

    case PBS_BATCH_Commit:

    case PBS_BATCH_Rerun:

      rc = decode_DIS_JobId(chan, request->rq_ind.rq_commit);

      break;

    case PBS_BATCH_DeleteJob:

    case PBS_BATCH_HoldJob:

    case PBS_BATCH_CheckpointJob:

    case PBS_BATCH_ModifyJob:

    case PBS_BATCH_AsyModifyJob:

      rc = decode_DIS_Manage(chan, request);

      break;

    case PBS_BATCH_MessJob:

      rc = decode_DIS_MessageJob(chan, request);

      break;

    case PBS_BATCH_Shutdown:

      rc = decode_DIS_ShutDown(chan, request);

      break;

    case PBS_BATCH_SignalJob:

      rc = decode_DIS_SignalJob(chan, request);

      break;

    case PBS_BATCH_StatusJob:

      rc = decode_DIS_Status(chan, request);

      break;

    case PBS_BATCH_GpuCtrl:

      rc = decode_DIS_GpuCtrl(chan, request);

      break;

#ifndef PBS_MOM

    case PBS_BATCH_LocateJob:

      rc = decode_DIS_JobId(chan, request->rq_ind.rq_locate);

      break;

    case PBS_BATCH_Manager:

    case PBS_BATCH_ReleaseJob:

      rc = decode_DIS_Manage(chan, request);

      break;

    case PBS_BATCH_MoveJob:

    case PBS_BATCH_OrderJob:

      rc = decode_DIS_MoveJob(chan, request);

      break;

    case PBS_BATCH_RunJob:

    case PBS_BATCH_AsyrunJob:

    case PBS_BATCH_StageIn:

      rc = decode_DIS_RunJob(chan, request);

      break;

    case PBS_BATCH_SelectJobs:

    case PBS_BATCH_SelStat:

      CLEAR_HEAD(request->rq_ind.rq_select);

      rc = decode_DIS_svrattrl(chan, &request->rq_ind.rq_select);

      break;

    case PBS_BATCH_StatusNode:

    case PBS_BATCH_StatusQue:

    case PBS_BATCH_StatusSvr:
      /* DIAGTODO: add PBS_BATCH_StatusDiag */

      rc = decode_DIS_Status(chan, request);

      break;

    case PBS_BATCH_TrackJob:

      rc = decode_DIS_TrackJob(chan, request);

      break;

    case PBS_BATCH_Rescq:

    case PBS_BATCH_ReserveResc:

    case PBS_BATCH_ReleaseResc:

      rc = decode_DIS_Rescl(chan, request);

      break;

    case PBS_BATCH_RegistDep:

      rc = decode_DIS_Register(chan, request);

      break;
      
    case PBS_BATCH_AuthenUser:
      
      rc = decode_DIS_Authen(chan, request);
      
      break;
      
    case PBS_BATCH_AltAuthenUser:
      
      rc = decode_DIS_AltAuthen(chan, request);
      
      break;

	case PBS_BATCH_AltAuthenUser:

	  rc = decode_DIS_AltAuthen(chan, request);

	  break;

    case PBS_BATCH_JobObit:

      rc = decode_DIS_JobObit(chan, request);

      break;

#else  /* PBS_MOM */
      
    /* pbs_mom services */
    
    case PBS_BATCH_DeleteReservation:

      /* NO-OP: this one is just a header and an extension string */

      break;
      
    case PBS_BATCH_ReturnFiles:
      
      rc = decode_DIS_ReturnFiles(chan, request);

      break;

    case PBS_BATCH_CopyFiles:

    case PBS_BATCH_DelFiles:

      rc = decode_DIS_CopyFiles(chan, request);

      break;

#endif /* PBS_MOM */

    default:

      sprintf(log_buf, "%s: %d from %s",
        pbse_to_txt(PBSE_NOSUP),
        request->rq_type,
        request->rq_user);

      log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, __func__, log_buf);

      rc = PBSE_UNKREQ;

      break;
    }  /* END switch (request->rq_type) */

  if (rc == DIS_SUCCESS)
    {
    /* Decode the Request Extension, if present */
    rc = decode_DIS_ReqExtend(chan, request);

    if (rc != 0)
      {
      sprintf(log_buf, "req extension bad, dis error %d (%s), type=%s",
        rc,
        dis_emsg[rc],
        reqtype_to_txt(request->rq_type));

      log_event(PBSEVENT_DEBUG,PBS_EVENTCLASS_REQUEST,"?",log_buf);

      rc = PBSE_DISPROTO;
      }
    }
  else if (rc != PBSE_UNKREQ)
    {
    sprintf(log_buf, "req body bad, dis error %d (%s), type=%s",
      rc,
      dis_emsg[rc],
      reqtype_to_txt(request->rq_type));

    log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_REQUEST, "?", log_buf);

    rc = PBSE_DISPROTO;
    }

  return(rc);
  }  /* END dis_request_read() */





/*
 * DIS_reply_read - read and decode DIS based batch reply
 *
 * Returns  0 - reply read and decoded ok
 *  !0 - otherwise
 */


int DIS_reply_read(

  struct tcp_chan *chan,
  struct batch_reply *preply)  /* I (modified) */

  {
  return(decode_DIS_replySvr(chan, preply));
  }

/* END dis_read.c */


