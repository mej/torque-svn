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
/* pbs_runjob.c

*/

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"


int pbs_runjob(

  int   c,
  char *jobid,
  char *location,
  char *extend)

  {
  int rc;

  struct batch_reply   *reply;
  unsigned int resch = 0;
  int sock;

  /* NOTE:  routes over to req_runjob() on server side
       DIS_tcp_wflush  -----> wait_request()                       (net_server.c)
                                process_request()                  (server/process_request.c)
                                  dis_request_read()               (server/dis_read.c)
                                    decode_DIS_RunJob()            (lib/Libifl/dec_RunJob.c)
                                dispatch_request()                 (server/process_request.c)
                                  req_runjob()                     (server/req_runjob.c)
                                    svr_startjob()                 (server/req_runjob.c)
                                      assign_hosts()               (server/req_runjob.c)
                                        set_nodes()                (server/node_manager.c)
                                          node_spec()              (server/node_manager.c)
                                            search()               (server/node_manager.c)
                                      svr_strtjob2()               (server/req_runjob.c)
                                        send_job(jobp,hostaddr)    (server/svr_movejob.c)
                                          fork()
                                          PARENT:
                                            set_task()             (server/svr_task.c)
                                          CHILD:
                                            svr_connect()
                                            PBSD_queuejob()           (lib/libifl/PBSD_submit.c)
                                              encode_DIS_QueueJob()
                                              DIS_tcp_wflush()
                                              PBSD_rdrpy(c)           (lib/Libifl/PBSD_rdrpy.c)
      NOTE: sock = connection[c].ch_socket      decode_DIS_replyCmd(sock) (Libifl/dec_rpyc.c)
      NOTE: stream=sock                           disrul(stream)      (Libdis/disrul.c)
                                                    disrsl_(stream)   (Libdis/disrsl_.c)
      NOTE: fd=stream                                 tcp_getc(fd)    (Libifl/tcp_dis.c)
                                                        tcp_read(fd)  (Libifl/tcp_dis.c)
                                                          select(pbs_tcp_timeout)
                                                          read()
                                            svr_disconnect()
                                            exit()
                                dispatch_task()                    (server/svr_task.c)
      NOTE: exit code eval        post_sendmom()                   (server/req_runjob.c)
                                    XXX
                                      reply_text()                 (server/reply_send.c)
                                        reply_send()               (server/reply_send.c)
   *   PBSD_rdrpy      <-----
   */

  /* NOTE:  set_task sets WORK_Deferred_Child : request remains until child terminates */

  if ((c < 0) || (jobid == NULL) || (*jobid == '\0'))
    {
    return(PBSE_IVALREQ);
    }

  if (location == NULL)
    {
    location = "";
    }

  pthread_mutex_lock(connection[c].ch_mutex);

  sock = connection[c].ch_socket;

  /* setup DIS support routines for following DIS calls */

  DIS_tcp_setup(sock);

  /* send run request */

  if ((rc = encode_DIS_ReqHdr(sock, PBS_BATCH_RunJob, pbs_current_user)) ||
      (rc = encode_DIS_RunJob(sock, jobid, location, resch)) ||
      (rc = encode_DIS_ReqExtend(sock, extend)))
    {
    connection[c].ch_errtxt = strdup(dis_emsg[rc]);

    pthread_mutex_unlock(connection[c].ch_mutex);

    return(PBSE_PROTOCOL);
    }

  if (DIS_tcp_wflush(sock))
    {
    pthread_mutex_unlock(connection[c].ch_mutex);

    return(PBSE_PROTOCOL);
    }

  /* get reply */

  reply = PBSD_rdrpy(c);

  rc = connection[c].ch_errno;

  pthread_mutex_unlock(connection[c].ch_mutex);

  PBSD_FreeReply(reply);

  return(rc);
  }  /* END pbs_runjob() */


