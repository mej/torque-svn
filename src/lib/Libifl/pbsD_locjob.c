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
/* pbs_locjob.c

 This function does the LocateJob request.
*/

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "log.h"

char * pbs_locjob(
    
  int   c,
  char *jobid,
  char *extend)

  {
  int rc;

  struct batch_reply *reply;
  char       *ploc = (char *)0;
  int sock;


  if ((jobid == (char *)0) || (*jobid == '\0'))
    {
    pbs_errno = PBSE_IVALREQ;
    return (ploc);
    }

  pthread_mutex_lock(connection[c].ch_mutex);

  sock = connection[c].ch_socket;

  /* setup DIS support routines for following DIS calls */

  DIS_tcp_setup(sock);

  if ((rc = encode_DIS_ReqHdr(sock, PBS_BATCH_LocateJob, pbs_current_user)) ||
      (rc = encode_DIS_JobId(sock, jobid))    ||
      (rc = encode_DIS_ReqExtend(sock, extend)))
    {
    connection[c].ch_errtxt = strdup(dis_emsg[rc]);

    pthread_mutex_unlock(connection[c].ch_mutex);

    pbs_errno = PBSE_PROTOCOL;
    return(NULL);
    }

  /* write data over tcp stream */

  if (DIS_tcp_wflush(sock))
    {
    pthread_mutex_unlock(connection[c].ch_mutex);

    pbs_errno = PBSE_PROTOCOL;
    return(NULL);
    }

  /* read reply from stream */

  reply = PBSD_rdrpy(c);

  if (reply == NULL)
    {
    pbs_errno = PBSE_PROTOCOL;
    }
  else if (reply->brp_choice != BATCH_REPLY_CHOICE_NULL &&
           reply->brp_choice != BATCH_REPLY_CHOICE_Text &&
           reply->brp_choice != BATCH_REPLY_CHOICE_Locate)
    {
#ifndef NDEBUG
    fprintf(stderr, "advise: pbs_locjob\tUnexpected reply choice\n\n");
#endif /* NDEBUG */
    pbs_errno = PBSE_PROTOCOL;
    }
  else if (connection[c].ch_errno == 0)
    {
    ploc = strdup(reply->brp_un.brp_locate);
    }

  PBSD_FreeReply(reply);

  pthread_mutex_unlock(connection[c].ch_mutex);

  return(ploc);
  } /* END pbs_locjob() */
