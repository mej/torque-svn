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
#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <pwd.h>
#include "libpbs.h"
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#ifndef __TOLDTTY
#include <pty.h>
#endif /* __TOLDTTY */

#include "list_link.h"
#include "log.h"
#include "server_limits.h"
#include "attribute.h"
#include "resource.h"
#include "job.h"
#include "mom_mach.h"
#include "mom_func.h"

/* Global Variables */

extern int	 exiting_tasks;
extern char	 mom_host[];
extern list_head svr_alljobs;
extern int	 termin_child;

extern int       LOGLEVEL;



/* Private variables */

/*
 * set_job - set up a new job session
 * 	Set session id and whatever else is required on this machine
 *	to create a new job.
 *
 *      Return: session/job id or if error:
 *		-1 - if setsid() fails
 *		-2 - if other, message in log_buffer
 */

int set_job(

  job                 *pjob,  /* I (not used) */
  struct startjob_rtn *sjr)   /* I (modified) */

  {
  sjr->sj_session = setsid();

  return(sjr->sj_session);
  }





/*
**	set_globid - set the global id for a machine type.
*/

void set_globid(

  job                 *pjob,  /* I (modified) */
  struct startjob_rtn *sjr)   /* I (not used) */

  {
  extern char noglobid[];

  if (pjob->ji_globid == NULL)
    pjob->ji_globid = strdup(noglobid);

  return;
  }  /* END set_globid() */




/*
 * set_mach_vars - setup machine dependent environment variables
 */

int set_mach_vars(

  job              *pjob,    /* pointer to the job	*/
  struct var_table *vtab)    /* pointer to variable table */

  {
  return(0);
  }





char *set_shell(

  job *pjob,
  struct passwd *pwdp)

  {
  char *cp;
  int   i;
  char *shell;
  struct array_strings *vstrs;

  /*
   * find which shell to use, one specified or the login shell
   */

  shell = pwdp->pw_shell;

  if ((pjob->ji_wattr[(int)JOB_ATR_shell].at_flags & ATR_VFLAG_SET) &&
      (vstrs = pjob->ji_wattr[(int)JOB_ATR_shell].at_val.at_arst)) 
    {
    for (i = 0; i < vstrs->as_usedptr; ++i) 
      {
      cp = strchr(vstrs->as_string[i], '@');

      if (cp != NULL) 
        {
        if (!strncmp(mom_host, cp+1, strlen(cp+1))) 
          {
          *cp = '\0';	/* host name matches */

          shell = vstrs->as_string[i];

          break;
          }
        } 
      else 
        {
        shell = vstrs->as_string[i];	/* wildcard */
        }
      }
    }

  return(shell);
  }






/* 
 * scan_for_terminated - scan the list of running jobs for one whose
 *	session id matches that of a terminated child pid.  Mark that
 *	job as Exiting.
 */

void scan_for_terminated()

  {
  static char	id[] = "scan_for_terminated";
  int	 exiteval = 0;
  pid_t	 pid;
  job	*pjob;
  task	*ptask = NULL;
  int	statloc;
  void	task_save A_((task *));

  /* update the latest intelligence about the running jobs;         */
  /* must be done before we reap the zombies, else we lose the info */

  termin_child = 0;

  if (mom_get_sample() == PBSE_NONE) 
    {
    pjob = (job *)GET_NEXT(svr_alljobs);

    while (pjob != NULL) 
      {
      mom_set_use(pjob);

      pjob = (job *)GET_NEXT(pjob->ji_alljobs);
      }
    }

  /* Now figure out which task(s) have terminated (are zombies) */

  while ((pid = waitpid(-1,&statloc,WNOHANG)) > 0) 
    {
    pjob = (job *)GET_NEXT(svr_alljobs);

    while (pjob != NULL) 
      {
      /*
      ** see if process was a child doing a special
      ** function for MOM
      */

      if (pid == pjob->ji_momsubt)
        break;

      /*
      ** look for task
      */

      ptask = (task *)GET_NEXT(pjob->ji_tasks);

      while (ptask != NULL) 
        {
        if (ptask->ti_qs.ti_sid == pid)
          break;

        ptask = (task *)GET_NEXT(ptask->ti_jobtask);
        }  /* END while (ptask) */

      if (ptask != NULL)
        break;

      pjob = (job *)GET_NEXT(pjob->ji_alljobs);
      }  /* END while (pjob != NULL) */

    if (pjob == NULL) 
      {
      if (LOGLEVEL >= 1)
        {
        sprintf(log_buffer,"pid %d not tracked, exitcode=%d\n",
          pid,
          statloc);

        log_record(
          PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          id,
          log_buffer);
        }

      continue;
      }

    if (pid == pjob->ji_momsubt) 
      {
      if (pjob->ji_mompost) 
        {
        pjob->ji_mompost(pjob,exiteval);
        pjob->ji_mompost = 0;
        }

      pjob->ji_momsubt = 0;

      job_save(pjob,SAVEJOB_QUICK);

      continue;
      }

    if (WIFEXITED(statloc))
      exiteval = WEXITSTATUS(statloc);
    else if (WIFSIGNALED(statloc))
      exiteval = WTERMSIG(statloc) + 0x100;
    else 
      exiteval = 1;

    if (LOGLEVEL >= 2)
      {
      sprintf(log_buffer,"for job %s, task %d, pid=%d, exitcode=%d\n",
        pjob->ji_qs.ji_jobid,
        ptask->ti_qs.ti_task,
        pid,
        exiteval);

      log_record(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        id,
        log_buffer);
      }

    kill_task(ptask,SIGKILL);

    ptask->ti_qs.ti_exitstat = exiteval;
    ptask->ti_qs.ti_status   = TI_STATE_EXITED;

    sprintf(log_buffer,"%s: job %s task %d terminated, sid %d",
      id, 
      pjob->ji_qs.ji_jobid,
      ptask->ti_qs.ti_task, 
      ptask->ti_qs.ti_sid);

    task_save(ptask);

    DBPRT(("%s\n",log_buffer));  /* REMOVEME */

    LOG_EVENT(
      PBSEVENT_DEBUG,
      PBS_EVENTCLASS_JOB,
      pjob->ji_qs.ji_jobid,
      log_buffer);

    exiting_tasks = 1;
    }  /* END while ((pid = waitpid(-1,&statloc,WNOHANG)) > 0) */

  return;
  }  /* END scan_for_terminated() */




/* SNL patch to use openpty() system call rather than search /dev */

/*
 * create the master pty, this particular
 * piece of code depends on multiplexor /dev/ptc aka /dev/ptmx
 */

#define PTY_SIZE 64

#ifndef __TOLDTTY

int open_master(

  char **rtn_name)	/* RETURN name of slave pts */

  {
  int master;
  int slave;
  static char slave_name[PTY_SIZE];

  int status = openpty(&master,&slave,slave_name,0,0);

  if (status < 0)
    {
    log_err(errno,"open_master", 
      "failed in openpty()");

    return(-1);
    }

  close(slave); 

  /* open_master has no way to return this, must return slave_name instead */

  *rtn_name = slave_name;

  return(master);
  }  /* END open_master() */

#else /* __TOLDTTY */

int open_master(

  char **rtn_name)      /* RETURN name of slave pts */

  {
  char 	       *pc1;
  char 	       *pc2;
  int		ptc;	/* master file descriptor */
  static char	ptcchar1[] = "pqrs";
  static char	ptcchar2[] = "0123456789abcdef";
  static char	pty_name[PTY_SIZE + 1];	/* "/dev/[pt]tyXY" */

  strncpy(pty_name,"/dev/ptyXY",PTY_SIZE);

  for (pc1 = ptcchar1;*pc1 != '\0';++pc1) 
    {
    pty_name[8] = *pc1;

    for (pc2 = ptcchar2;*pc2 != '\0';++pc2) 
      {
      pty_name[9] = *pc2;

      if ((ptc = open(pty_name,O_RDWR|O_NOCTTY,0)) >= 0) 
        {
        /* got a master, fix name to matching slave */

        pty_name[5] = 't';	

        *rtn_name = pty_name;

        return(ptc);
        } 
      else if (errno == ENOENT)
        {
        return(-1);	/* tried all entries, give up */
        }
      }
    }

  return(-1);	/* tried all entries, give up */
  }  /* END open_master() */

#endif /* __TOLDTTY */



/*
 * struct sig_tbl = map of signal names to numbers,
 * see req_signal() in ../requests.c
 */
struct sig_tbl sig_tbl[] = {
	{ "NULL", 0 },
	{ "HUP", SIGHUP },
	{ "INT", SIGINT },
	{ "QUIT", SIGQUIT },
	{ "ILL",  SIGILL },
	{ "TRAP", SIGTRAP },
	{ "IOT", SIGIOT },
	{ "ABRT",SIGABRT },
	{ "FPE", SIGFPE },
	{ "KILL", SIGKILL },
	{ "BUS", SIGBUS },
	{ "SEGV", SIGSEGV },
	{ "PIPE", SIGPIPE },
	{ "ALRM", SIGALRM },
	{ "TERM", SIGTERM },
	{ "URG", SIGURG },
	{ "STOP", SIGSTOP },
	/* { "suspend", SIGSTOP }, - NOTE: changed for MPI jobs - NORWAY */
        { "suspend", SIGTSTP },
	{ "TSTP", SIGTSTP },
	{ "CONT", SIGCONT },
	{ "resume", SIGCONT },
	{ "CHLD", SIGCHLD },
	{ "CLD",  SIGCHLD },
	{ "TTIN", SIGTTIN },
	{ "TTOU", SIGTTOU },
	{ "IO", SIGIO },
	{ "POLL", SIGPOLL },
	{ "XCPU", SIGXCPU },
	{ "XFSZ", SIGXFSZ },
	{ "VTALRM", SIGVTALRM },
	{ "PROF", SIGPROF },
	{ "WINCH", SIGWINCH },
	{ "USR1", SIGUSR1 },
	{ "USR2", SIGUSR2 },
	{ (char *)0, -1 }
};
