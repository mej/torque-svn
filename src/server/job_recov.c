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
 * job_recov.c - This file contains the functions to record a job
 *	data struture to disk and to recover it from disk.
 *
 *	The data is recorded in a file whose name is the job_id.
 *
 *	The following public functions are provided:
 *		job_save()   - save the disk image 
 *		job_recov()  - recover (read) job from disk
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "log.h"
#include "svrfunc.h"
#if __STDC__ != 1
#include <memory.h>
#endif


#define JOBBUFSIZE 2048
#define MAX_SAVE_TRIES 3

/* global data items */

extern char  *path_jobs;
extern const char *PJobSubState[];

extern time_t time_now;

/* data global only to this file */

const static unsigned int quicksize = sizeof(struct jobfix);

/*
 * job_save() - Saves (or updates) a job structure image on disk
 *
 *	Save does either - a quick update for state changes only,
 *			 - a full update for an existing file, or
 *			 - a full write for a new job
 *
 *	For a quick update, the data written is less than a disk block
 *	size and no size change occurs; so it is rewritten in place
 *	with O_SYNC.
 *
 *	For a full update (usually following modify job request), to
 *	insure no data is ever lost due to system crash:
 *	1. write (with O_SYNC) new image to a new file using a temp name
 *	2. unlink the old (image) file
 *	3. link the correct name to the new file
 *	4. unlink the temp name
 *
 *	For a new file write, first time, the data is written directly to
 *	the file.
 */

int job_save(

  job *pjob,		/* pointer to job structure */
  int  updatetype)	/* 0=quick, 1=full	    */

  {
  int	fds;
  int	i;
  char	namebuf1[MAXPATHLEN];
  char	namebuf2[MAXPATHLEN];
  int	openflags;
  int	redo;

  strcpy(namebuf1,path_jobs);	/* job directory path */
  strcat(namebuf1,pjob->ji_qs.ji_fileprefix);
  strcpy(namebuf2,namebuf1);	/* setup for later */
  strcat(namebuf1,JOB_FILE_SUFFIX);

  /* if ji_modified is set, ie an attribute changed, then update mtime */

  if (pjob->ji_modified) 
    {
    pjob->ji_wattr[JOB_ATR_mtime].at_val.at_long = time_now;
    }

  if (updatetype == SAVEJOB_QUICK) 
    {
    openflags = O_WRONLY | O_Sync;

    /* NOTE:  open, do not create */

    fds = open(namebuf1,openflags,0600);

    if (fds < 0) 
      {
      char tmpLine[1024];

      snprintf(tmpLine,sizeof(tmpLine),"cannot open file '%s' for job %s in state %s (%s)",
        namebuf1,
        pjob->ji_qs.ji_jobid,
        PJobSubState[pjob->ji_qs.ji_substate],
        (updatetype == 0) ? "quick" : "full");
 
      log_err(errno,"job_save",tmpLine);

      /* FAILURE */

      return(-1);
      }

    /* just write the "critical" base structure to the file */

    while ((i = write(fds,(char *)&pjob->ji_qs,quicksize)) != quicksize) 
      {
      if ((i < 0) && (errno == EINTR)) 
        {
        /* retry the write */

        if (lseek(fds,(off_t)0,SEEK_SET) < 0) 
          {
          log_err(errno,"job_save","lseek");

          close(fds);

          return(-1);
          }

        continue;
        } 
      else 
        {
        log_err(errno,"job_save","quickwrite");
 
        close(fds);

        /* FAILURE */

        return(-1);
        }
      }

    close(fds);
    } 
  else 
    {
    /*
     * write the whole structure to the file.
     * For a update, this is done to a new file to protect the
     * old against crashs.
     * The file is written in four parts:
     * (1) the job structure, 
     * (2) the attribtes in "encoded" form,
     * (3) the attributes in the "external" form, and last
     * (4) the dependency list.
     */
		
    strcat(namebuf2,JOB_FILE_COPY);

    openflags = O_CREAT | O_WRONLY | O_Sync;

    /* NOTE:  create file if required */

    if (updatetype == SAVEJOB_NEW)
      fds = open(namebuf1,openflags,0600);
    else
      fds = open(namebuf2,openflags,0600);

    if (fds < 0) 
      {
      log_err(errno,"job_save","open for full save");

      return(-1);
      }

    for (i = 0;i < MAX_SAVE_TRIES;++i) 
      {
      redo = 0;	/* try to save twice */

      save_setup(fds);

      if (save_struct((char *)&pjob->ji_qs,(size_t)quicksize) != 0) 
        {
        redo++;
        } 
      else if (save_attr(job_attr_def,pjob->ji_wattr,(int)JOB_ATR_LAST) != 0) 
        {
        redo++;
        } 
      else if (save_flush() != 0) 
        {
        redo++;
        }

      if (redo != 0) 
        {
        if (lseek(fds,(off_t)0,SEEK_SET) < 0) 
          {
          log_err(errno,"job_save","full lseek");	
          }
        } 
      else
        {
        break;
        }
      }  /* END for (i) */
 
    close(fds);

    if (i >= MAX_SAVE_TRIES) 
      {
      if (updatetype == SAVEJOB_FULL)
        unlink(namebuf2);

      return(-1);
      }

    if (updatetype == SAVEJOB_FULL) 
      {
      unlink(namebuf1);

      if (link(namebuf2,namebuf1) == -1) 
        {
        LOG_EVENT(
          PBSEVENT_ERROR|PBSEVENT_SECURITY,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          "Link in job_save failed");
        } 
      else 
        {
        unlink(namebuf2);
        }
      }

    pjob->ji_modified = 0;
    }  /* END (updatetype == SAVEJOB_QUICK) */

  return(0);
  }  /* END job_save() */





/*
 * job_recov() - recover (read in) a job from its save file
 *
 *	This function is only needed upon server start up.
 *
 *	The job structure, its attributes strings, and its dependencies
 *	are recovered from the disk.  Space to hold the above is
 *	malloc-ed as needed.  
 *
 *	Returns: job pointer to new job structure or a
 *		 null pointer on an error.
*/

job *job_recov(

  char *filename) /* I */   /* pathname to job save file */

  {
  int	 fds;
  job	*pj;
  char	*pn;
  char	 namebuf[MAXPATHLEN];

  pj = job_alloc();	/* allocate & initialize job structure space */

  if (pj == NULL) 
    {
    /* FAILURE - cannot alloc memory */

    return(NULL);
    }

  strcpy(namebuf,path_jobs);	/* job directory path */
  strcat(namebuf,filename);

  fds = open(namebuf,O_RDONLY,0);

  if (fds < 0) 
    {
    log_err(errno,"job_recov","open of job file");

    free((char *)pj);

    /* FAILURE - cannot open job file */

    return(NULL);
    }

  /* read in job quick save sub-structure */

  if (read(fds,(char *)&pj->ji_qs,quicksize) != quicksize) 
    {
    log_err(errno,"job_recov","read");

    free((char *)pj);

    close(fds);

    return (NULL);
    }

  /* Does file name match the internal name? */
  /* This detects ghost files */

  pn = strrchr(namebuf, (int)'/') + 1;

  if (strncmp(pn,pj->ji_qs.ji_fileprefix,strlen(pj->ji_qs.ji_fileprefix)) != 0) 
    {
    /* mismatch, discard job */

    sprintf(log_buffer,"Job Id %s does not match file name for %s",
      pj->ji_qs.ji_jobid,
      namebuf);

    log_err(-1,"job_recov",log_buffer);

    free((char *)pj);

    close(fds);

    return(NULL);
    }

  /* read in working attributes */

  if (recov_attr(
        fds,
        pj,
        job_attr_def,
        pj->ji_wattr,
        (int)JOB_ATR_LAST,
        (int)JOB_ATR_UNKN) != 0) 
    {
    log_err(errno,"job_recov","err from recov_attr");

    job_free(pj);

    close(fds);

    return(NULL);
    }

  close(fds);

  /* all done recovering the job */

  return(pj);
  }  /* END job_recov() */





