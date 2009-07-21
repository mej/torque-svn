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
 * accounting.c - contains functions to record accounting information
 *
 * Functions included are:
 * acct_open()
 * acct_record()
 * acct_close()
 */


#include <pbs_config.h>   /* the master config generated by configure */
#include "portability.h"
#include <sys/param.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "job.h"
#include "queue.h"
#include "log.h"
#include "acct.h"

/* Local Data */

static FILE     *acctfile;  /* open stream for log file */
static volatile int  acct_opened = 0;
static int      acct_opened_day;
static int      acct_auto_switch = 0;

/* Global Data */

extern attribute_def job_attr_def[];
extern char     *path_acct;
extern int      resc_access_perm;
extern time_t      time_now;
extern int       LOGLEVEL;



#define EXTRA_PAD 1000 /* Used to bad the account buffer string */

int AdjustAcctBufSize(char **Buf, unsigned int *BufSiz, int newStringLen, job *pjob);


/*
 * acct_job - build common data for start/end job accounting record
 * Used by account_jobstr() and account_jobend()
 */

static char *acct_job(

  job  *pjob,    /* I */
  char **Buf,     /* O - buffer in which data is to be placed */
  unsigned int   *BufSize) /* I */

  {
  char *ptr;
  int   Len;
  int  runningBufSize = *BufSize;
  int  newStringLen;

  tlist_head attrlist;
  svrattrl *pal;

  if (pjob == NULL)
    {
    return(*Buf);
    }

  CLEAR_HEAD(attrlist);

  ptr = *Buf;

  /* user */

	/* acct_job is only called from account_jobstr and account_jobend. BufSize should be
	 	 PBS_ACCT_MAX_RCD + 1 in size. We will make the assumption that the following
	 	 strncat calls have ample buffer space to complete successfully */
  sprintf(ptr, "user=%s ",
          pjob->ji_wattr[(int)JOB_ATR_euser].at_val.at_str);

  ptr += strlen(ptr);

  /* group */

  sprintf(ptr, "group=%s ",
          pjob->ji_wattr[(int)JOB_ATR_egroup].at_val.at_str);

  ptr += strlen(ptr);

  /* account */

  if (pjob->ji_wattr[(int)JOB_ATR_account].at_flags & ATR_VFLAG_SET)
    {
    sprintf(ptr, "account=%s ",
            pjob->ji_wattr[(int)JOB_ATR_account].at_val.at_str);

    ptr += strlen(ptr);
    }

  /* job name */

  sprintf(ptr, "jobname=%s ",
          pjob->ji_wattr[(int)JOB_ATR_jobname].at_val.at_str);

  ptr += strlen(ptr);

  /* queue name */

  sprintf(ptr, "queue=%s ",
          pjob->ji_qhdr->qu_qs.qu_name);

  ptr += strlen(ptr);

  /* create time */

  sprintf(ptr, "ctime=%ld ",
          pjob->ji_wattr[(int)JOB_ATR_ctime].at_val.at_long);

  ptr += strlen(ptr);

  /* queued time */

  sprintf(ptr, "qtime=%ld ",
          pjob->ji_wattr[(int)JOB_ATR_qtime].at_val.at_long);

  ptr += strlen(ptr);

  /* eligible time, how long ready to run */

  sprintf(ptr, "etime=%ld ",
          pjob->ji_wattr[(int)JOB_ATR_etime].at_val.at_long);

  ptr += strlen(ptr);

  /* execution start time */

  sprintf(ptr, "start=%ld ",
          (long)pjob->ji_qs.ji_stime);

  ptr += strlen(ptr);

  /* user */

  sprintf(ptr, "owner=%s ",
          pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str);


 
  /* For large clusters strings can get pretty long. We need to see if there
     is a need to allocate a bigger buffer */

  runningBufSize -= strlen(*Buf);

  newStringLen = strlen("exec_host=")+strlen(pjob->ji_wattr[(int)JOB_ATR_exec_host].at_val.at_str);
  if(runningBufSize <= newStringLen+1)
  {
    Len = AdjustAcctBufSize(Buf, BufSize, newStringLen, pjob);
    if(Len == 0)
      return(ptr);
    runningBufSize += newStringLen+EXTRA_PAD;
  }

  ptr = *Buf;
  ptr += strlen(*Buf);

  /* execution host name */
  snprintf(ptr, runningBufSize, "exec_host=%s ",
           pjob->ji_wattr[(int)JOB_ATR_exec_host].at_val.at_str);

  Len = strlen(ptr);

	runningBufSize -= Len;

/*  if (BufSize <= 100)
    {
    char tmpLine[1024];

    sprintf(tmpLine, "account record for job %s too long, not fully recorded - increase PBS_ACCT_MAX_RCD",
            pjob->ji_qs.ji_jobid);

    log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, "Act", tmpLine);

    return(ptr);
    }*/

  ptr += Len;

  /* now encode the job's resource_list attribute */

  resc_access_perm = READ_ONLY;

  job_attr_def[(int)JOB_ATR_resource].at_encode(
    &pjob->ji_wattr[(int)JOB_ATR_resource],
    &attrlist,
    job_attr_def[(int)JOB_ATR_resource].at_name,
    NULL,
    ATR_ENCODE_CLIENT);

  while ((pal = GET_NEXT(attrlist)) != NULL)
    {
		/* exec_host can use a lot of buffer space. We can't assume
		 	 we still have enough to make these small strncpy calls */

		newStringLen = strlen(pal->al_name);
		if(runningBufSize <= newStringLen+1)
			{
			Len = AdjustAcctBufSize(Buf, BufSize, newStringLen, pjob);
			if(Len == 0)
				return(ptr);

			runningBufSize += newStringLen+EXTRA_PAD;
			}

    strncat(ptr, pal->al_name,runningBufSize);
		runningBufSize -= strlen(ptr);
		ptr += strlen(ptr);

    if (pal->al_resc != NULL)
      {
			if(runningBufSize <= (int)(strlen(pal->al_resc)+2)) /*one for 0 and one for the '.' for 2 */
				{
				Len = AdjustAcctBufSize(Buf, BufSize, newStringLen, pjob);
				if(Len == 0)
					return(ptr);

				runningBufSize += newStringLen+EXTRA_PAD;
				}
      strncat(ptr, ".",runningBufSize);
      strncat(ptr, pal->al_resc,runningBufSize);
			runningBufSize -= strlen(ptr);
			ptr += strlen(ptr);
      }

		if(runningBufSize <= 2)
			{
			Len = AdjustAcctBufSize(Buf, BufSize, newStringLen, pjob);
			if(Len == 0)
				return(ptr);

			runningBufSize += newStringLen+EXTRA_PAD;
			}

		strncat(ptr, "=",runningBufSize);

    runningBufSize -= strlen(ptr);
    newStringLen = strlen(pal->al_value);
    if(runningBufSize <= newStringLen+1)
    {
      Len = AdjustAcctBufSize(Buf, BufSize, newStringLen, pjob);
      if(Len == 0)
        return(ptr);

      runningBufSize += newStringLen+EXTRA_PAD;
    }

    strncat(ptr, pal->al_value, runningBufSize);
    strncat(ptr, " ", runningBufSize);

    delete_link(&pal->al_link);

    free(pal);

    Len = strlen(ptr);

    runningBufSize -= Len;

    ptr += Len;

   /*  if (BufSize <= 100)
      {
      char tmpLine[1024];

      sprintf(tmpLine, "account record for job %s too long, not fully recorded - increase PBS_ACCT_MAX_RCD",
              pjob->ji_qs.ji_jobid);

      log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, "Act", tmpLine);

      return(ptr);
      }*/
    }  /* END while (pal != NULL) */

#ifdef ATTR_X_ACCT

  /* x attributes */

  if (pjob->ji_wattr[(int)JOB_SITE_ATR_x].at_flags & ATR_VFLAG_SET)
    {
    sprintf(ptr, "x=%s ",
            pjob->ji_wattr[(int)JOB_SITE_ATR_x].at_val.at_str);

    ptr += strlen(ptr);
    }

#endif

  /* SUCCESS */

  return(ptr);
  }  /* END acct_job() */







/*
 * acct_open() - open the acct file for append.
 *
 * Opens a (new) acct file.
 * If a acct file is already open, and the new file is successfully opened,
 * the old file is closed.  Otherwise the old file is left open.
 */

int acct_open(

  char *filename)  /* abs pathname or NULL */

  {
  char  filen[_POSIX_PATH_MAX];
  char  logmsg[_POSIX_PATH_MAX + 80];
  FILE *newacct;
  time_t now;

  struct tm *ptm;

  struct tm tmpPtm;

  if (filename == NULL)
    {
    /* go with default */

    now = time(0);

    ptm = localtime_r(&now, &tmpPtm);

    sprintf(filen, "%s%04d%02d%02d",
            path_acct,
            ptm->tm_year + 1900,
            ptm->tm_mon + 1,
            ptm->tm_mday);

    filename = filen;

    acct_auto_switch = 1;

    acct_opened_day = ptm->tm_yday;
    }
  else if (*filename == '\0')
    {
    /* a null name is not an error */

    return(0);  /* turns off account logging.  */
    }
  else if (*filename != '/')
    {
    /* not absolute */

    return(-1);
    }

  if ((newacct = fopen(filename, "a")) == NULL)
    {
    log_err(errno, "acct_open", filename);

    return(-1);
    }

  setbuf(newacct, NULL);        /* set no buffering */

  if (acct_opened > 0)          /* if acct was open, close it */
    fclose(acctfile);

  acctfile = newacct;

  acct_opened = 1;  /* note that file is open */

  sprintf(logmsg, "Account file %s opened",
          filename);

  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, "Act", logmsg);

  return(0);
  }  /* END acct_open() */





/*
 * acct_close - close the current open log file
 */

void
acct_close(void)

  {
  if (acct_opened == 1)
    {
    fclose(acctfile);

    acct_opened = 0;
    }

  return;
  }  /* END acct_close() */





/*
 * account_record - write basic accounting record
 */

void account_record(

  int   acctype, /* accounting record type */
  job  *pjob,
  char *text)  /* text to log, may be null */

  {

  struct tm *ptm;

  struct tm tmpPtm;

  if (acct_opened == 0)
    {
    /* file not open, don't bother */

    return;
    }

  ptm = localtime_r(&time_now, &tmpPtm);

  /* Do we need to switch files */

  if ((acct_auto_switch != 0) && (acct_opened_day != ptm->tm_yday))
    {
    acct_close();

    acct_open(NULL);
    }

  if (text == NULL)
    text = "";

  fprintf(acctfile, "%02d/%02d/%04d %02d:%02d:%02d;%c;%s;%s\n",
          ptm->tm_mon + 1,
          ptm->tm_mday,
          ptm->tm_year + 1900,
          ptm->tm_hour,
          ptm->tm_min,
          ptm->tm_sec,
          (char)acctype,
          pjob->ji_qs.ji_jobid,
          text);

  return;
  }  /* END account_record() */






/*
 * account_jobstr - write a job start record
 */

void account_jobstr(

  job *pjob)

  {
  /*char buf[PBS_ACCT_MAX_RCD + 1];*/
  char *buf;
  unsigned int  bufSize = PBS_ACCT_MAX_RCD + 1;

  /* pack in general information about the job */

  buf = (char *)malloc(bufSize);
  if(!buf)
    return;
  acct_job(pjob, &buf, &bufSize);

  buf[bufSize] = '\0';

  account_record(PBS_ACCT_RUN, pjob, buf);

  free(buf);

  return;
  }  /* END account_jobstr() */






/*
 * account_jobend - write a job termination/resource usage record
 */

void account_jobend(

  job *pjob,
  char *used) /* job usage information, see req_jobobit() */

  {
  /*char   buf[PBS_ACCT_MAX_RCD + 1];*/
  char  *buf;
  unsigned int   bufSize = PBS_ACCT_MAX_RCD + 1;
  char  *pb;

  /* pack in general information about the job */

  buf = (char *)malloc(bufSize);
  if(!buf)
    return;

  pb = acct_job(pjob, &buf, &bufSize);

  /* session */

  sprintf(pb, "session=%ld ",
          pjob->ji_wattr[(int)JOB_ATR_session_id].at_val.at_long);

  pb += strlen(pb);

  /* Alternate id if present */

  if (pjob->ji_wattr[(int)JOB_ATR_altid].at_flags & ATR_VFLAG_SET)
    {
    sprintf(pb, "alt_id=%s ",
            pjob->ji_wattr[(int)JOB_ATR_altid].at_val.at_str);

    pb += strlen(pb);
    }

  /* add the execution end time */

  sprintf(pb, "end=%ld ",
          (long)time_now);

  pb += strlen(pb);

  /* finally add on resources used from req_jobobit() */

  strncat(pb, used, PBS_ACCT_MAX_RCD - (pb - buf));

  buf[PBS_ACCT_MAX_RCD] = '\0';

  account_record(PBS_ACCT_END, pjob, buf);

  free(buf);
  return;
  }  /* END account_jobend() */

/*
 * acct_cleanup - remove the old accounting files
 */

void acct_cleanup(

  long	days_to_keep)	/* Number of days to keep accounting files */

  {
  char *id = "acct_cleanup";

  if (log_remove_old(path_acct,(days_to_keep * SECS_PER_DAY)) != 0)
    {
    log_err(-1,id,"failure occurred when checking for old accounting logs");
    }

  return;
  }  /* END acct_cleanup() */


/* AdjustAcctBufSize - Increase the size of the current size plus
   newStringLen + EXTRA_PAD. Return newStringLen on success or 0 if the
   realloc fails */
 int AdjustAcctBufSize(char **Buf, unsigned int *BufSize, int newStringLen, job *pjob)
 {
	 char *newBuf;

   newBuf = (char *)realloc(*Buf, *BufSize+newStringLen+EXTRA_PAD); /* add 1000 so we don't have to realloc for the small strings */
   if(newBuf == NULL)
     {
     char tmpLine[1024];

     sprintf(tmpLine, "account record for job %s too long and realloc failed. The job is not fully recorded.",
                     pjob->ji_qs.ji_jobid);

     log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, "Act", tmpLine);

     return(0);

     }

   *BufSize += newStringLen+EXTRA_PAD;

	 *Buf = newBuf;

   return(newStringLen);
 }


/* END accounting.c */

