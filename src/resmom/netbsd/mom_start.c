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

#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include "portability.h"
#include "libpbs.h"
#include "list_link.h"
#include "log.h"
#include "server_limits.h"
#include "attribute.h"
#include "resource.h"
#include "job.h"
#include "mom_mach.h"
#include "mom_func.h"

static char ident[] = "@(#) $RCSfile$ $Revision$";

/* Global Variables */

extern int	 exiting_tasks;
extern char	 mom_host[];
extern list_head svr_alljobs;
extern int	 termin_child;

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

int set_job(pjob, sjr)
	job *pjob;
	struct startjob_rtn *sjr;
{
	return (sjr->sj_session = setsid());
}

/*
**	set_globid - set the global id for a machine type.
*/
void
set_globid(pjob, sjr)
	job *pjob;
	struct startjob_rtn *sjr;
{
	extern	char	noglobid[];

	if (pjob->ji_globid == NULL)
		pjob->ji_globid = strdup(noglobid);
}

/*
 * set_mach_vars - setup machine dependent environment variables
 */

int
set_mach_vars(pjob, vtab)
	job   		 *pjob;    /* pointer to the job	*/
	struct var_table *vtab;    /* pointer to variable table */
{
	return 0;
}

char *set_shell(pjob, pwdp)
	job	      *pjob;
	struct passwd *pwdp;
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
	    (vstrs = pjob->ji_wattr[(int)JOB_ATR_shell].at_val.at_arst)) {
		for (i = 0; i < vstrs->as_usedptr; ++i) {
			cp = strchr(vstrs->as_string[i], '@');
			if (cp) {
				if (!strncmp(mom_host, cp+1, strlen(cp+1))) {
					*cp = '\0';	/* host name matches */
					shell = vstrs->as_string[i];
					break;
				}
			} else {
				shell = vstrs->as_string[i];	/* wildcard */
			}
		}
	}
	return (shell);
}

/* 
 * scan_for_terminated - scan the list of runnings jobs for one whose
 *	session id matched that of a terminated child pid.  Mark that
 *	job as Exiting.
 */

void scan_for_terminated()
{
	static	char	id[] = "scan_for_terminated";
	int		exiteval;
	pid_t		pid;
	job		*pjob;
	task		*ptask;
	int		statloc;

	/* update the latest intelligence about the running jobs;         */
	/* must be done before we reap the zombies, else we lose the info */

	termin_child = 0;

	if (mom_get_sample() == PBSE_NONE) {
		pjob = (job *)GET_NEXT(svr_alljobs);
		while (pjob) {
			mom_set_use(pjob);
			pjob = (job *)GET_NEXT(pjob->ji_alljobs);
		}
	}

	/* Now figure out which task(s) have terminated (are zombies) */

	while ((pid = waitpid(-1, &statloc, WNOHANG)) != 0) {
		if ((pid == -1) && (errno != EINTR))
			break;
		pjob = (job *)GET_NEXT(svr_alljobs);
		while (pjob) {
			ptask = (task *)GET_NEXT(pjob->ji_tasks);
			while (ptask) {
				if (ptask->ti_qs.ti_sid == pid)
					break;
				ptask = (task *)GET_NEXT(ptask->ti_jobtask);
			}
			if (ptask != NULL)
				break;
			pjob = (job *)GET_NEXT(pjob->ji_alljobs);
		}
		if (pjob == NULL) {
			DBPRT(("%s: pid %d not a task\n", id, pid))
			continue;
		}
		/*
		** We found task within the job which has exited.
		*/
		if (WIFEXITED(statloc))
			exiteval = WEXITSTATUS(statloc);
		else if (WIFSIGNALED(statloc))
			exiteval = WTERMSIG(statloc) + 10000;
		else 
			exiteval = 1;
		DBPRT(("%s: task %d pid %d exit value %d\n", id,
				ptask->ti_qs.ti_task, pid, exiteval))
		kill_task(ptask, SIGKILL);
		ptask->ti_qs.ti_exitstat = exiteval;
		ptask->ti_qs.ti_status = TI_STATE_EXITED;
		pjob->ji_qs.ji_un.ji_momt.ji_exitstat = exiteval;
		task_save(ptask);
		sprintf(log_buffer, "task %d terminated", ptask->ti_qs.ti_task);
		LOG_EVENT(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB,
			pjob->ji_qs.ji_jobid, log_buffer);

		exiting_tasks = 1;
	}
}

/*
 * creat the master pty, this particular
 * piece of code does it the hard way, it searches
 */

#define PTY_SIZE 12

int open_master(rtn_name)
	char **rtn_name;	/* RETURN name of slave pts */
{
	char 	       *pc1;
	char 	       *pc2;
	int		ptc;	/* master file descriptor */
	static char	ptcchar1[] = "pqrs";
	static char	ptcchar2[] = "0123456789abcdef";
	static char	pty_name[PTY_SIZE+1];	/* "/dev/[pt]tyXY" */

	(void)strncpy(pty_name, "/dev/ptyXY", PTY_SIZE);
	for (pc1 = ptcchar1; *pc1 != '\0'; ++pc1) {
	    pty_name[8] = *pc1;
	    for (pc2 = ptcchar2; *pc2 != '\0'; ++pc2) {
		pty_name[9] = *pc2;
		if ((ptc = open(pty_name, O_RDWR | O_NOCTTY, 0)) >= 0) {
		    /* Got a master, fix name to matching slave */
		    pty_name[5] = 't';	
		    *rtn_name = pty_name;
		    return (ptc);
		
		} else if (errno == ENOENT)
			return (-1);	/* tried all entries, give up */
	    }
	}
	return (-1);	/* tried all entries, give up */
}

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
	{ "EMT", SIGEMT },
	{ "FPE", SIGFPE },
	{ "KILL", SIGKILL },
	{ "BUS", SIGBUS },
	{ "SEGV", SIGSEGV },
	{ "SYS", SIGSYS },
	{ "PIPE", SIGPIPE },
	{ "ALRM", SIGALRM },
	{ "TERM", SIGTERM },
	{ "URG", SIGURG },
	{ "STOP", SIGSTOP },
	{ "suspend", SIGSTOP },
	{ "TSTP", SIGTSTP },
	{ "CONT", SIGCONT },
	{ "resume", SIGCONT },
	{ "CHLD", SIGCHLD },
	{ "TTIN", SIGTTIN },
	{ "TTOU", SIGTTOU },
	{ "IO", SIGIO },
	{ "XCPU", SIGXCPU },
	{ "XFSZ", SIGXFSZ },
	{ "VTALRM", SIGVTALRM },
	{ "PROF", SIGPROF },
	{ "WINCH", SIGWINCH },
	{ "USR1", SIGUSR1 },
	{ "USR2", SIGUSR2 },
	{ (char *)0, -1 }
};
