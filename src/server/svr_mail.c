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
 * svr_mail.c - send mail to mail list or owner of job on
 * job begin, job end, and/or job abort
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "pbs_ifl.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "pbs_job.h"
#include "log.h"
#include "server.h"
#include "rpp.h"


/* External Functions Called */

extern void net_close (int);
extern void svr_format_job (FILE *, job *, char *, int, char *);

/* Global Data */

extern struct server server;

extern int LOGLEVEL;

void svr_mailowner(

  job   *pjob,       /* I */
  int   mailpoint,  /* note, single character  */
  int    force,      /* if set to MAIL_FORCE, force mail delivery */
  char *text)      /* (optional) additional message text */

  {
  char *cmdbuf;
  int    i;
  char *mailfrom;
  char  mailto[1024];
  char *bodyfmt, *subjectfmt;
  char bodyfmtbuf[1024];
  FILE *outmail;

  struct array_strings *pas;

  if ((server.sv_attr[(int)SRV_ATR_MailDomain].at_flags & ATR_VFLAG_SET) &&
      (server.sv_attr[(int)SRV_ATR_MailDomain].at_val.at_str != NULL) &&
      (!strcasecmp("never", server.sv_attr[(int)SRV_ATR_MailDomain].at_val.at_str)))
    {
    /* never send user mail under any conditions */

    return;
    }

  if (LOGLEVEL >= 3)
    {
    char tmpBuf[LOG_BUF_SIZE];

    snprintf(tmpBuf, LOG_BUF_SIZE, "sending '%c' mail for job %s to %s (%.64s)\n",
             (char)mailpoint,
             pjob->ji_qs.ji_jobid,
             pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str,
             (text != NULL) ? text : "---");

    log_event(
      PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      tmpBuf);
    }

  /* if force is true, force the mail out regardless of mailpoint */

  if (force != MAIL_FORCE)
    {
    /* see if user specified mail of this type */

    if (pjob->ji_wattr[(int)JOB_ATR_mailpnts].at_flags & ATR_VFLAG_SET)
      {
      if (strchr(
            pjob->ji_wattr[(int)JOB_ATR_mailpnts].at_val.at_str,
            mailpoint) == NULL)
        {
        /* do not send mail */
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          "Not sending email: User does not want mail of this type.\n");

        return;
        }
      }
    else if (mailpoint != MAIL_ABORT) /* not set, default to abort */
      {
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        "Not sending email: Default mailpoint does not include this type.\n");

      return;
      }
    }

  /*
   * ok, now we will fork a process to do the mailing to not
   * hold up the server's other work.
   */

  if (fork())
    {
    return;  /* its all up to the child now */
    }

  /*
   * From here on, we are a child process of the server.
   * Fix up file descriptors and signal handlers.
   */

  rpp_terminate();

  net_close(-1);

  /* Who is mail from, if SRV_ATR_mailfrom not set use default */

  if ((mailfrom = server.sv_attr[(int)SRV_ATR_mailfrom].at_val.at_str) == NULL)
    {
    if (LOGLEVEL >= 5)
      {
      char tmpBuf[LOG_BUF_SIZE];

      snprintf(tmpBuf,sizeof(tmpBuf),
        "Updated mailto from user list: '%s'\n",
        mailto);
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        tmpBuf);
      }
    mailfrom = PBS_DEFAULT_MAIL;
    }

  /* Who does the mail go to?  If mail-list, them; else owner */

  *mailto = '\0';

  if (pjob->ji_wattr[(int)JOB_ATR_mailuser].at_flags & ATR_VFLAG_SET)
    {
    /* has mail user list, send to them rather than owner */

    pas = pjob->ji_wattr[(int)JOB_ATR_mailuser].at_val.at_arst;

    if (pas != NULL)
      {
      for (i = 0;i < pas->as_usedptr;i++)
        {
        if ((strlen(mailto) + strlen(pas->as_string[i]) + 2) < sizeof(mailto))
          {
          strcat(mailto, pas->as_string[i]);
          strcat(mailto, " ");
          }
        }
      }
    }
  else
    {
    /* no mail user list, just send to owner */

    if ((server.sv_attr[(int)SRV_ATR_MailDomain].at_flags & ATR_VFLAG_SET) &&
        (server.sv_attr[(int)SRV_ATR_MailDomain].at_val.at_str != NULL))
      {
      strcpy(mailto, pjob->ji_wattr[(int)JOB_ATR_euser].at_val.at_str);
      strcat(mailto, "@");
      strcat(mailto, server.sv_attr[(int)SRV_ATR_MailDomain].at_val.at_str);

      if (LOGLEVEL >= 5) 
        {
        char tmpBuf[LOG_BUF_SIZE];

        snprintf(tmpBuf,sizeof(tmpBuf),
          "Updated mailto from job owner and mail domain: '%s'\n",
          mailto);
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          tmpBuf);
        }
      }
    else
      {
#ifdef TMAILDOMAIN
      strcpy(mailto, pjob->ji_wattr[(int)JOB_ATR_euser].at_val.at_str);
      strcat(mailto, "@");
      strcat(mailto, TMAILDOMAIN);
#else /* TMAILDOMAIN */
      strcpy(mailto, pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str);
#endif /* TMAILDOMAIN */

      if (LOGLEVEL >= 5)
        {
        char tmpBuf[LOG_BUF_SIZE];

        snprintf(tmpBuf,sizeof(tmpBuf),
          "Updated mailto from job owner: '%s'\n",
          mailto);
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          tmpBuf);
        }
      }
    }

  /* mail subject line formating statement */

  if ((server.sv_attr[(int)SRV_ATR_MailSubjectFmt].at_flags & ATR_VFLAG_SET) &&
      (server.sv_attr[(int)SRV_ATR_MailSubjectFmt].at_val.at_str != NULL))
    {
    subjectfmt = server.sv_attr[(int)SRV_ATR_MailSubjectFmt].at_val.at_str;
    }
  else
    {
    subjectfmt = "PBS JOB %i";
    }

  /* mail body formating statement */

  if ((server.sv_attr[(int)SRV_ATR_MailBodyFmt].at_flags & ATR_VFLAG_SET) &&
      (server.sv_attr[(int)SRV_ATR_MailBodyFmt].at_val.at_str != NULL))
    {
    bodyfmt = server.sv_attr[(int)SRV_ATR_MailBodyFmt].at_val.at_str;
    }
  else
    {
    bodyfmt =  strcpy(bodyfmtbuf, "PBS Job Id: %i\n"
                                  "Job Name:   %j\n");
    if (pjob->ji_wattr[(int)JOB_ATR_exec_host].at_flags & ATR_VFLAG_SET)
      {
      strcat(bodyfmt, "Exec host:  %h\n");
      }

    strcat(bodyfmt, "%m\n");

    if (text != NULL)
      {
      strcat(bodyfmt, "%d\n");
      }
    }
  /* setup sendmail command line with -f from_whom */

  i = strlen(SENDMAIL_CMD) + strlen(mailfrom) + strlen(mailto) + 6;

  if ((cmdbuf = malloc(i)) == NULL)
    {
    char tmpBuf[LOG_BUF_SIZE];

    snprintf(tmpBuf,sizeof(tmpBuf),
      "Unable to popen() command '%s' for writing: '%s' (error %d)\n",
      cmdbuf,
      strerror(errno),
      errno);
    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      tmpBuf);

    exit(1);
    }

  sprintf(cmdbuf, "%s -f %s %s",

          SENDMAIL_CMD,
          mailfrom,
          mailto);

  outmail = (FILE *)popen(cmdbuf, "w");

  if (outmail == NULL)
    {
    exit(1);
    }

  /* Pipe in mail headers: To: and Subject: */

  fprintf(outmail, "To: %s\n",
          mailto);

  fprintf(outmail, "Subject: ");
  svr_format_job(outmail, pjob, subjectfmt, mailpoint, text);
  fprintf(outmail, "\n");

  /* Set "Precedence: bulk" to avoid vacation messages, etc */

  fprintf(outmail, "Precedence: bulk\n\n");

  /* Now pipe in the email body */
  svr_format_job(outmail, pjob, bodyfmt, mailpoint, text);

  errno = 0;
  if ((i = pclose(outmail)) != 0)
    {
    char tmpBuf[LOG_BUF_SIZE];

    snprintf(tmpBuf,sizeof(tmpBuf),
      "Email '%c' to %s failed: Child process '%s' %s %d (errno %d:%s)\n",
      mailpoint,
      mailto,
      cmdbuf,
      ((WIFEXITED(i)) ? ("returned") : ((WIFSIGNALED(i)) ? ("killed by signal") : ("croaked"))),
      ((WIFEXITED(i)) ? (WEXITSTATUS(i)) : ((WIFSIGNALED(i)) ? (WTERMSIG(i)) : (i))),
      errno,
      strerror(errno));
    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      tmpBuf);
    }
  else if (LOGLEVEL >= 4)
    {
    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      "Email sent successfully\n");
    }

  exit(0);

  /*NOTREACHED*/

  return;
  }  /* END svr_mailowner() */

/* END svr_mail.c */
