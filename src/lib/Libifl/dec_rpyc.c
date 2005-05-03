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
 * decode_DIS_replyCmd() - decode a Batch Protocol Reply Structure for a Command
 * 
 *	This routine decodes a batch reply into the form used by commands.
 *	The only difference between this and the server version is on status
 *	replies.  For commands, the attributes are decoded into a list of
 *	attrl structure rather than the server's svrattrl.
 *
 * 	batch_reply structure defined in libpbs.h, it must be allocated
 *	by the caller.
 */
 
#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <stdlib.h>
#include "libpbs.h"
#include "dis.h"
#include "batch_request.h"



int decode_DIS_replyCmd(

  int                 sock,
  struct batch_reply *reply)

  {
  int		        ct;
  int		        i;
  struct brp_select    *psel;
  struct brp_select   **pselx;
  struct brp_cmdstat   *pstcmd;
  struct brp_cmdstat  **pstcx;
  int                   rc = 0;

  /* first decode "header" consisting of protocol type and version */

  i = disrui(sock,&rc);

  if (rc != 0) 
    {
    return(rc);
    }

  if (i != PBS_BATCH_PROT_TYPE) 
    {
    return(DIS_PROTO);
    }

  i = disrui(sock,&rc);

  if (rc != 0) 
    {
    return(rc);
    }

  if (i != PBS_BATCH_PROT_VER)
    {
    return(DIS_PROTO);
    }

  /* next decode code, auxcode and choice (union type identifier) */

  reply->brp_code = disrsi(sock,&rc);

  if (rc != 0)
    {
    return(rc);
    }

  reply->brp_auxcode = disrsi(sock,&rc);

  if (rc != 0)
    {
    return(rc);
    }

  reply->brp_choice = disrui(sock,&rc);

  if (rc != 0) 
    {
    return(rc);
    }

  switch (reply->brp_choice) 
    {
    case BATCH_REPLY_CHOICE_NULL:

      break;  /* no more to do */

    case BATCH_REPLY_CHOICE_Queue:
    case BATCH_REPLY_CHOICE_RdytoCom:
    case BATCH_REPLY_CHOICE_Commit:

      if ((rc = disrfst(sock,PBS_MAXSVRJOBID + 1,reply->brp_un.brp_jid)))
        {
        return(rc);
        }

      break;

    case BATCH_REPLY_CHOICE_Select:

      /* have to get count of number of strings first */

      reply->brp_un.brp_select = NULL;

      pselx = &reply->brp_un.brp_select;

      ct = disrui(sock,&rc);

      if (rc) 
        {
        return(rc);
        }

      while (ct--) 
        {
        psel = (struct brp_select *)malloc(sizeof (struct brp_select));

        if (psel == NULL) 
          {
          return(DIS_NOMALLOC);
          }

        psel->brp_next = NULL;

        psel->brp_jobid[0] = '\0';

        rc = disrfst(sock,PBS_MAXSVRJOBID + 1,psel->brp_jobid);

        if (rc) 
          {
          free(psel);

          return(rc);
          }

        *pselx = psel;

        pselx  = &psel->brp_next;
        }

      break;

    case BATCH_REPLY_CHOICE_Status:

      /* have to get count of number of status objects first */

      reply->brp_un.brp_statc = NULL;

      pstcx = &reply->brp_un.brp_statc;

      ct = disrui(sock,&rc);

      if (rc) 
        {
        return(rc);
        }

      while (ct--) 
        {
        pstcmd = (struct brp_cmdstat *)malloc(sizeof (struct brp_cmdstat));

        if (pstcmd == NULL) 
          {
          return(DIS_NOMALLOC);
          }

        pstcmd->brp_stlink = NULL;

        pstcmd->brp_objname[0] = '\0';

        pstcmd->brp_attrl = NULL;

        pstcmd->brp_objtype = disrui(sock,&rc);

        if (rc == 0) 
          {
          rc = disrfst(sock,PBS_MAXSVRJOBID + 1,pstcmd->brp_objname);
          }

        if (rc) 
          {
          free(pstcmd);

          return(rc);
          }

        rc = decode_DIS_attrl(sock, &pstcmd->brp_attrl);

        if (rc) 
          {
          free(pstcmd);

          return(rc);
          }

        *pstcx = pstcmd;

        pstcx  = &pstcmd->brp_stlink;
        }

      break;
		
    case BATCH_REPLY_CHOICE_Text:
		
      /* text reply */

      reply->brp_un.brp_txt.brp_str = disrcs(
        sock,
        (size_t*)&reply->brp_un.brp_txt.brp_txtlen,
        &rc);

      break;

    case BATCH_REPLY_CHOICE_Locate:

      /* Locate Job Reply */

      rc = disrfst(sock,PBS_MAXDEST + 1,reply->brp_un.brp_locate);

      break;

    case BATCH_REPLY_CHOICE_RescQuery:

      /* Resource Query Reply */

      reply->brp_un.brp_rescq.brq_avail = NULL;
      reply->brp_un.brp_rescq.brq_alloc = NULL;
      reply->brp_un.brp_rescq.brq_resvd = NULL;
      reply->brp_un.brp_rescq.brq_down  = NULL;

      ct = disrui(sock,&rc);

      if (rc) 
        break;

      reply->brp_un.brp_rescq.brq_number = ct;
      reply->brp_un.brp_rescq.brq_avail  = (int *)malloc(ct * sizeof (int));
      reply->brp_un.brp_rescq.brq_alloc  = (int *)malloc(ct * sizeof (int));
      reply->brp_un.brp_rescq.brq_resvd  = (int *)malloc(ct * sizeof (int));
      reply->brp_un.brp_rescq.brq_down   = (int *)malloc(ct * sizeof (int));

      for (i = 0;(i < ct) && (rc == 0);++i) 
        *(reply->brp_un.brp_rescq.brq_avail + i) = disrui(sock,&rc);

      for (i = 0;(i < ct) && (rc == 0);++i) 
        *(reply->brp_un.brp_rescq.brq_alloc + i) = disrui(sock,&rc);

      for (i = 0;(i < ct) && (rc == 0);++i) 
        *(reply->brp_un.brp_rescq.brq_resvd+i) = disrui(sock, &rc);

      for (i = 0;(i < ct) && (rc == 0);++i) 
        *(reply->brp_un.brp_rescq.brq_down+i)  = disrui(sock, &rc);

      break;

    default:

      return(-1);

      /*NOTREACHED*/

      break;
    }  /* END switch (reply->brp_choice) */

  return(rc);
  }  /* END decode_DIS_replyCmd() */

/* END dec_rpyc.c */

