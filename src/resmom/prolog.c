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

#define PBS_MOM 1
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <grp.h>
#include "libpbs.h"
#include "list_link.h"
#include "server_limits.h"
#include "attribute.h"
#include "job.h"
#include "log.h"
#include "mom_mach.h"
#include "mom_func.h"
#include "resource.h"
#include "pbs_proto.h"
#include "net_connect.h"


#define PBS_PROLOG_TIME 300

extern char PBSNodeMsgBuf[];
extern int  MOMPrologTimeoutCount;
extern int  MOMPrologFailureCount;

extern int  LOGLEVEL;
extern int  DEBUGMODE;

extern int  lockfds;
extern char *path_aux;

unsigned int pe_alarm_time = PBS_PROLOG_TIME;
static pid_t child;
static int   run_exit;

/* external prototypes */

extern int pe_input A_((char *));
extern int TTmpDirName A_((job *, char *));
extern void encode_used A_((job *, tlist_head *));

/* END extern prototypes */

const char *PPEType[] =
  {
  "NONE",
  "prolog",
  "epilog",
  "userprolog",
  "userepilog",
  NULL
  };



/*
 * resc_to_string - convert resources_[list or used] to a single string
 */

static char *resc_to_string(

  job       *pjob,      /* I (optional - if specified, report total job resources) */
  int        aindex,    /* I which attribute to convert */
  char      *buf, /* O the buffer into which to convert */
  int      buflen) /* I the length of the above buffer */

  {
  int        need;
  svrattrl  *patlist;
  tlist_head  svlist;
  attribute *pattr;

  int       isfirst = 1;

  CLEAR_HEAD(svlist);

  *buf = '\0';

  pattr = &pjob->ji_wattr[aindex];

  /* pack the list of resources into svlist */

  if (aindex == JOB_ATR_resource)
    {
    if (encode_resc(pattr, &svlist, "x", NULL, ATR_ENCODE_CLIENT) <= 0)
      {
      return(buf);
      }
    }
  else if (aindex == JOB_ATR_resc_used)
    {
    encode_used(pjob, &svlist);
    }
  else
    {
    return(buf);
    }

  /* unpack svlist into a comma-delimited string */

  patlist = (svrattrl *)GET_NEXT(svlist);

  while (patlist != NULL)
    {
    need = strlen(patlist->al_resc) + strlen(patlist->al_value) + 3;

    if (need >= buflen)
      {
      patlist = (svrattrl *)GET_NEXT(patlist->al_link);

      continue;
      }

    if (LOGLEVEL >= 7)
      {
      fprintf(stderr, "Epilog:  %s=%s\n",
              patlist->al_resc,
              patlist->al_value);
      }

    if (isfirst == 1)
      {
      isfirst = 0;
      }
    else
      {
      strcat(buf, ",");
      buflen--;
      }

    strcat(buf, patlist->al_resc);

    strcat(buf, "=");
    strcat(buf, patlist->al_value);

    buflen -= need;

    patlist = (svrattrl *)GET_NEXT(patlist->al_link);
    }  /* END while (patlist != NULL) */

  free_attrlist(&svlist);

  return(buf);
  }  /* END resc_to_string() */





/*
 * pelog_err - record error for run_pelog()
 */

static int pelog_err(

  job  *pjob,  /* I */
  char *file,  /* I */
  int   n,     /* I - exit code */
  char *text)  /* I */

  {
  sprintf(log_buffer, "prolog/epilog failed, file: %s, exit: %d, %s",
          file,
          n,
          text);

  sprintf(PBSNodeMsgBuf, "ERROR: %s",
          log_buffer);

  log_err(-1, "run_pelog", log_buffer);

  return(n);
  }  /* END pelog_err() */





/*
 * pelogalm() - alarm handler for run_pelog()
 */

static void pelogalm(

  int sig)  /* I */

  {
  /* child is global */

  errno = 0;

  kill(child, SIGKILL);

  run_exit = -4;

  return;
  }  /* END pelogalm() */





/*
 * run_pelog() - Run the Prologue/Epilogue script
 *
 * Script is run under uid of root, prologue and the epilogue have:
 *  - argv[1] is the jobid
 *  - argv[2] is the user's name
 *  - argv[3] is the user's group name
 *              - argv[4] is the job name
 *  - the input file is an architecture-dependent file
 *  - the output and error are the job's output and error
 * The epilogue also has:
 *  - argv[5] is the session id
 *  - argv[6] is the list of resource limits specified
 *  - argv[7] is the list of resources used
 *  - argv[8] is the queue in which the job resides
 *  - argv[9] is the account under which the job run
 *      The prologue also has:
 *              - argv[5] is the list of resource limits specified
 *              - argv[6] is the queue in which the job resides
 *              - argv[7] is the account under which the job is run
 */

int run_pelog(

  int   which,      /* I (one of PE_*) */
  char *specpelog,  /* I - script path */
  job  *pjob,       /* I - associated job */
  int   pe_io_type) /* I */

  {
  char *id = "run_pelog";

  struct sigaction act, oldact;
  char  *arg[11];
  int   fds1 = 0;
  int   fds2 = 0;
  int   fd_input;
  char  resc_list[2048];
  char  resc_used[2048];

  struct stat sbuf;
  char   sid[20];
  int    waitst;
  int    isjoined;  /* boolean */
  char   buf[MAXPATHLEN + 1024];
  char   pelog[MAXPATHLEN + 1024];

  int    jobtypespecified = 0;

  resource      *r;

  char          *EmptyString = "";

  int            LastArg;
  int            aindex;

  int            rc;

  char          *ptr;

  if ((pjob == NULL) || (specpelog == NULL) || (specpelog[0] == '\0'))
    {
    return(0);
    }

  ptr = pjob->ji_wattr[(int)JOB_ATR_jobtype].at_val.at_str;

  if (ptr != NULL)
    {
    jobtypespecified = 1;

    snprintf(pelog,sizeof(pelog),"%s.%s",
      specpelog,
      ptr);
    }
  else
    {
    strncpy(pelog,specpelog,sizeof(pelog));
    }

  rc = stat(pelog,&sbuf);

  if ((rc == -1) && (jobtypespecified == 1))
    {
    strncpy(pelog,specpelog,sizeof(pelog));

    rc = stat(pelog,&sbuf);
    }

  if (rc == -1)
    {
    if (errno == ENOENT)
      {
      /* prolog script does not exist */

      if (LOGLEVEL >= 5)
        {
        static char tmpBuf[1024];

        sprintf(log_buffer, "%s script '%s' does not exist (cwd: %s)",
          PPEType[which],
          (pelog != NULL) ? pelog : "NULL",
          getcwd(tmpBuf, sizeof(tmpBuf)));

        log_record(PBSEVENT_SYSTEM, 0, id, log_buffer);
        }

      return(0);
      }

    return(pelog_err(pjob, pelog, errno, "cannot stat"));
    }

  if (LOGLEVEL >= 5)
    {
    sprintf(log_buffer, "running %s script '%s'",
      PPEType[which],
      (pelog != NULL) ? pelog : "NULL");

    log_record(PBSEVENT_SYSTEM, 0, id, log_buffer);
    }

  /* script must be owned by root, be regular file, read and execute by user *
   * and not writeable by group or other */

  if ((sbuf.st_uid != 0) ||
      (!S_ISREG(sbuf.st_mode)) ||
      ((sbuf.st_mode & (S_IRUSR | S_IXUSR)) != (S_IRUSR | S_IXUSR)) ||
      (sbuf.st_mode & (S_IWGRP | S_IWOTH)))
    {
    return(pelog_err(pjob, pelog, -1, "permission Error"));
    }

  if ((which == PE_PROLOGUSER) || (which == PE_EPILOGUSER))
    {
    /* script must also be read and execute by other */

    if ((sbuf.st_mode & (S_IROTH | S_IXOTH)) != (S_IROTH | S_IXOTH))
      {
      return(pelog_err(pjob, pelog, -1, "permission Error"));
      }
    }

  fd_input = pe_input(pjob->ji_qs.ji_jobid);

  if (fd_input < 0)
    {
    return(pelog_err(pjob, pelog, -2, "no pro/epilogue input file"));
    }

  run_exit = 0;

  child = fork();

  if (child > 0)
    {
    int KillSent = FALSE;

    /* parent - watch for prolog/epilog to complete */

    close(fd_input);

    act.sa_handler = pelogalm;

    sigemptyset(&act.sa_mask);

    act.sa_flags = 0;

    sigaction(SIGALRM, &act, &oldact);

    /* it would be nice if the harvest routine could block for 5 seconds,
       and if the prolog is not complete in that time, mark job as prolog
       pending, append prolog child, and continue */

    /* main loop should attempt to harvest prolog in non-blocking mode.
       If unsuccessful after timeout, job should be terminated, and failure
       reported.  If successful, mom should unset prolog pending, and
       continue with job start sequence.  Mom should report job as running
       while prologpending flag is set.  (NOTE:  must track per job prolog
       start time)
    */

    alarm(pe_alarm_time);

    while (waitpid(child, &waitst, 0) < 0)
      {
      if (errno != EINTR)
        {
        /* exit loop. non-alarm based failure occurred */

        run_exit = -3;

        MOMPrologFailureCount++;

        break;
        }

      if (run_exit == -4)
        {
        if (KillSent == FALSE)
          {
          MOMPrologTimeoutCount++;

          /* timeout occurred */

          KillSent = TRUE;

          /* NOTE:  prolog/epilog may be locked in KERNEL space and unkillable */

          alarm(5);
          }
        else
          {
          /* cannot kill prolog/epilog, give up */

          run_exit = -5;

          break;
          }
        }
      }    /* END while (wait(&waitst) < 0) */

    /* epilog/prolog child completed */

    alarm(0);

    /* restore the previous handler */

    sigaction(SIGALRM, &oldact, 0);

    if (run_exit == 0)
      {
      if (WIFEXITED(waitst))
        {
        run_exit = WEXITSTATUS(waitst);
        }
      }
    }
  else
    {
    /* child - run script */

    log_close(0);

    if (lockfds >= 0)
      {
      close(lockfds);

      lockfds = -1;
      }

    net_close(-1);

    if ((which == PE_PROLOGUSER) || (which == PE_EPILOGUSER))
      {
      if (setgroups(
           pjob->ji_grpcache->gc_ngroup,
           (gid_t *)pjob->ji_grpcache->gc_groups) != 0)
        {
        snprintf(log_buffer,sizeof(log_buffer),"setgroups() for UID = %lu failed: %s\n",
          (unsigned long)pjob->ji_qs.ji_un.ji_momt.ji_exuid,
          strerror(errno));

        log_err(errno, id, log_buffer);

        exit(255);
        }

      if (setgid(pjob->ji_qs.ji_un.ji_momt.ji_exgid) != 0)
        {
        snprintf(log_buffer,sizeof(log_buffer),"setgid(%lu) for UID = %lu failed: %s\n",
          (unsigned long)pjob->ji_qs.ji_un.ji_momt.ji_exgid,
          (unsigned long)pjob->ji_qs.ji_un.ji_momt.ji_exuid,
          strerror(errno));

        log_err(errno, id, log_buffer);

        exit(255);
        }

      if (setuid(pjob->ji_qs.ji_un.ji_momt.ji_exuid) != 0)
        {
        snprintf(log_buffer,sizeof(log_buffer),"setuid(%lu) failed: %s\n",
          (unsigned long)pjob->ji_qs.ji_un.ji_momt.ji_exuid,
          strerror(errno));

        log_err(errno, id, log_buffer);

        exit(255);
        }
      }

    if (fd_input != 0)
      {
      close(0);

      if (dup(fd_input) == -1) {}

      close(fd_input);
      }

    if (pe_io_type == PE_IO_TYPE_NULL)
      {
      /* no output, force to /dev/null */

      fds1 = open("/dev/null",O_WRONLY,0600);
      fds2 = open("/dev/null",O_WRONLY,0600);
      }
    else if (pe_io_type == PE_IO_TYPE_STD)
      {
      /* open job standard out/error */

      /*
       * We need to know if files are joined or not.
       * If they are then open the correct file and duplicate it to the other
      */

      isjoined = is_joined(pjob);

      switch (isjoined)
        {
        case -1:

          fds2 = open_std_file(pjob, StdErr, O_WRONLY | O_APPEND,
                               pjob->ji_qs.ji_un.ji_momt.ji_exgid);

          fds1 = dup(fds2);

          break;

        case 1:

          fds1 = open_std_file(
            pjob, 
            StdOut, 
            O_WRONLY | O_APPEND,
            pjob->ji_qs.ji_un.ji_momt.ji_exgid);

          fds2 = dup(fds1);

          break;

        default:

          fds1 = open_std_file(pjob, StdOut, O_WRONLY | O_APPEND,
                               pjob->ji_qs.ji_un.ji_momt.ji_exgid);

          fds2 = open_std_file(pjob, StdErr, O_WRONLY | O_APPEND,
                               pjob->ji_qs.ji_un.ji_momt.ji_exgid);
          break;
        }
      }

    if (pe_io_type != PE_IO_TYPE_ASIS)
      {
      /* If PE_IO_TYPE_ASIS, leave as is, already open to job */

      if (fds1 != 1)
        {
        close(1);

        if (dup(fds1) == -1) {}

        close(fds1);
        }

      if (fds2 != 2)
        {
        close(2);

        if (dup(fds2) == -1) {}

        close(fds2);
        }
      }

    if ((which == PE_PROLOGUSER) || (which == PE_EPILOGUSER))
      {
      if (chdir(pjob->ji_grpcache->gc_homedir) != 0)
        {
        /* warn only, no failure */

        sprintf(log_buffer,"PBS: chdir to %s failed: %s (running user %s in current directory)",
          pjob->ji_grpcache->gc_homedir,
          strerror(errno),
          which == PE_PROLOGUSER ? "prologue" : "epilogue");

        if (write(2,log_buffer,strlen(log_buffer)) == -1) {}

        fsync(2);
        }
      }

    /* for both prolog and epilog */

    if (DEBUGMODE == 1)
      {
      fprintf(stderr, "PELOGINFO:  script:'%s'  jobid:'%s'  euser:'%s'  egroup:'%s'  jobname:'%s' SSID:'%ld'  RESC:'%s'\n",
              pelog,
              pjob->ji_qs.ji_jobid,
              pjob->ji_wattr[(int)JOB_ATR_euser].at_val.at_str,
              pjob->ji_wattr[(int)JOB_ATR_egroup].at_val.at_str,
              pjob->ji_wattr[(int)JOB_ATR_jobname].at_val.at_str,
              pjob->ji_wattr[(int)JOB_ATR_session_id].at_val.at_long,
              resc_to_string(pjob, (int)JOB_ATR_resource, resc_list, sizeof(resc_list)));
      }

    arg[0] = pelog;

    arg[1] = pjob->ji_qs.ji_jobid;
    arg[2] = pjob->ji_wattr[(int)JOB_ATR_euser].at_val.at_str;
    arg[3] = pjob->ji_wattr[(int)JOB_ATR_egroup].at_val.at_str;
    arg[4] = pjob->ji_wattr[(int)JOB_ATR_jobname].at_val.at_str;

    /* NOTE:  inside child */

    if (which == PE_EPILOG)
      {
      /* for epilog only */

      sprintf(sid, "%ld",
              pjob->ji_wattr[(int)JOB_ATR_session_id].at_val.at_long);

      arg[5] = sid;
      arg[6] = resc_to_string(pjob, (int)JOB_ATR_resource, resc_list, sizeof(resc_list));
      arg[7] = resc_to_string(pjob, (int)JOB_ATR_resc_used, resc_used, sizeof(resc_used));
      arg[8] = pjob->ji_wattr[(int)JOB_ATR_in_queue].at_val.at_str;
      arg[9] = pjob->ji_wattr[(int)JOB_ATR_account].at_val.at_str;
      arg[10] = NULL;

      LastArg = 10;
      }
    else
      {
      /* prolog */

      arg[5] = resc_to_string(pjob, (int)JOB_ATR_resource, resc_list, sizeof(resc_list));
      arg[6] = pjob->ji_wattr[(int)JOB_ATR_in_queue].at_val.at_str;
      arg[7] = pjob->ji_wattr[(int)JOB_ATR_account].at_val.at_str;
      arg[8] = NULL;

      LastArg = 8;
      }

    for (aindex = 0;aindex < LastArg;aindex++)
      {
      if (arg[aindex] == NULL)
        arg[aindex] = EmptyString;
      }  /* END for (aindex) */

    /*
     * Pass Resource_List.nodes request in environment
     * to allow pro/epi-logue setup/teardown of system
     * settings.  --pw, 2 Jan 02
     * Fixed to use putenv for sysV compatibility.
     *  --troy, 11 jun 03
     *
     */

    r = find_resc_entry(
          &pjob->ji_wattr[(int)JOB_ATR_resource],
          find_resc_def(svr_resc_def, "nodes", svr_resc_size));

    if (r != NULL)
      {
      /* setenv("PBS_RESOURCE_NODES",r->rs_value.at_val.at_str,1); */

      const char *envname = "PBS_RESOURCE_NODES=";
      char *envstr;

      envstr = malloc(
                 (strlen(envname) + strlen(r->rs_value.at_val.at_str) + 1) * sizeof(char));

      if (envstr != NULL)
        {
        strcpy(envstr,envname);

        strcat(envstr,r->rs_value.at_val.at_str);

        /* do _not_ free the string when using putenv */

        putenv(envstr);
        }
      }  /* END if (r != NULL) */

    r = find_resc_entry(
          &pjob->ji_wattr[(int)JOB_ATR_resource],
          find_resc_def(svr_resc_def,"gres",svr_resc_size));

    if (r != NULL)
      {
      /* setenv("PBS_RESOURCE_NODES",r->rs_value.at_val.at_str,1); */

      const char *envname = "PBS_RESOURCE_GRES=";
      char *envstr;

      envstr = malloc(
                 (strlen(envname) + strlen(r->rs_value.at_val.at_str) + 1) * sizeof(char));

      if (envstr != NULL)
        {
        strcpy(envstr,envname);

        strcat(envstr,r->rs_value.at_val.at_str);

        /* do _not_ free the string when using putenv */

        putenv(envstr);
        }
      }  /* END if (r != NULL) */

    if (TTmpDirName(pjob,buf))
      {
      const char *envname = "TMPDIR=";
      char *envstr;

      envstr = malloc(
                 (strlen(envname) + strlen(buf) + 1) * sizeof(char));

      if (envstr != NULL)
        {
        strcpy(envstr,envname);

        strcat(envstr,buf);

        /* do _not_ free the string when using putenv */

        putenv(envstr);
        }
      }  /* END if (TTmpDirName(pjob,&buf)) */

    /* Set PBS_SCHED_HINT */

      {
      char *envname = "PBS_SCHED_HINT";
      char *envval;
      char *envstr;

      if ((envval = get_job_envvar(pjob,envname)) != NULL)
        {
        envstr = malloc((strlen(envname) + strlen(envval) + 2) * sizeof(char));

        if (envstr != NULL)
          {
          sprintf(envstr,"%s=%s",
            envname,
            envval);

          putenv(envstr);
          }
        }
      }

    /* Set PBS_NODENUM */
      {
      char *envname = "PBS_NODENUM";
      char *envstr;

      sprintf(buf,"%d",
        pjob->ji_nodeid);

      envstr = malloc((strlen(envname) + strlen(buf) + 2) * sizeof(char));

      if (envstr != NULL)
        {
        sprintf(envstr,"%s=%d",
          envname,
          pjob->ji_nodeid);

        putenv(envstr);
        }
      }

    /* Set PBS_MSHOST */
      {
      char *envname = "PBS_MSHOST";
      char *envstr;

      if ((pjob->ji_vnods[0].vn_host != NULL) && (pjob->ji_vnods[0].vn_host->hn_host != NULL))
        {
        envstr = malloc((strlen(envname) + strlen(pjob->ji_vnods[0].vn_host->hn_host) + 2) * sizeof(char));

        if (envstr != NULL)
          {
          sprintf(envstr,"%s=%s",
            envname,
            pjob->ji_vnods[0].vn_host->hn_host);

          putenv(envstr);
          }
        }
      }

    /* Set PBS_NODEFILE */

    {
    char *envname = "PBS_NODEFILE";
    char *envstr;

    if (pjob->ji_flags & MOM_HAS_NODEFILE)
      {
      sprintf(buf, "%s/%s",
        path_aux,
        pjob->ji_qs.ji_jobid);

      envstr = malloc((strlen(envname) + strlen(buf) + 2) * sizeof(char));

      if (envstr != NULL)
        {
        sprintf(envstr,"%s=%s",
          envname,
          buf);

        putenv(envstr);
        }
      }
    }

    /* SET BEOWULF_JOB_MAP */

      {
      struct array_strings *vstrs;

      int VarIsSet = 0;
      int j;

      vstrs = pjob->ji_wattr[(int)JOB_ATR_variables].at_val.at_arst;

      for (j = 0;j < vstrs->as_usedptr;++j)
        {
        if (!strncmp(
              vstrs->as_string[j],
              "BEOWULF_JOB_MAP=",
              strlen("BEOWULF_JOB_MAP=")))
          {
          VarIsSet = 1;

          break;
          }
        }

      if (VarIsSet == 1)
        {
        char *envstr;

        envstr = malloc((strlen(vstrs->as_string[j])) * sizeof(char));

        if (envstr != NULL)
          {
          strcpy(envstr,vstrs->as_string[j]);

          putenv(envstr);
          }
        }
      }

    execv(pelog,arg);

    sprintf(log_buffer,"execv of %s failed: %s\n",
      pelog,
      strerror(errno));

    if (write(2,log_buffer,strlen(log_buffer)) == -1) 
      {
      /* FAILURE */
      }
    
    fsync(2);

    exit(255);
    }  /* END else () */

  switch (run_exit)

    {
    case 0:

      /* NO-OP */

      break;

    case - 3:

      pelog_err(pjob, pelog, run_exit, "child wait interrupted");

      break;

    case - 4:

      pelog_err(pjob, pelog, run_exit, "prolog/epilog timeout occurred, child cleaned up");

      break;

    case - 5:

      pelog_err(pjob, pelog, run_exit, "prolog/epilog timeout occurred, cannot kill child");

      break;

    default:

      pelog_err(pjob, pelog, run_exit, "nonzero p/e exit status");

      break;
    }  /* END switch (run_exit) */

  return(run_exit);
  }  /* END run_pelog() */


