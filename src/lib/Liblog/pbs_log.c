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
 * pbs_log.c - contains functions to log error and event messages to
 * the log file.
 *
 * Functions included are:
 * log_open()
 * log_err()
 * log_ext()
 * log_record()
 * log_close()
 * log_roll()
 * log_size()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "portability.h"
#include "pbs_error.h"

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "log.h"
#if SYSLOG
#include <syslog.h>
#endif


/* Global Data */
char log_buffer[LOG_BUF_SIZE];
char log_directory[_POSIX_PATH_MAX/2];
char log_host[1024];
char log_suffix[1024];

char *msg_daemonname = "unset";

/* Local Data */

static int      log_auto_switch = 0;
static int      log_open_day;
static FILE     *logfile;  /* open stream for log file */
static char     *logpath = NULL;
static volatile int  log_opened = 0;
#if SYSLOG
static int      syslogopen = 0;
#endif /* SYSLOG */

/*
 * the order of these names MUST match the defintions of
 * PBS_EVENTCLASS_* in log.h
 */
static char *class_names[] =
  {
  "n/a",
  "Svr",
  "Que",
  "Job",
  "Req",
  "Fil",
  "Act",
  "node"
  };

/* External functions called */

/* local prototypes */
const char *log_get_severity_string(int);


/*
 * mk_log_name - make the log name used by MOM
 * based on the date: yyyymmdd
 */

static char *mk_log_name(

  char *pbuf)     /* O (minsize=1024) */

  {

  struct tm *ptm;
  struct tm  tmpPtm;
  time_t time_now;

  time_now = time((time_t *)0);
  ptm = localtime_r(&time_now,&tmpPtm);

  if (log_suffix[0] != '\0')
    {
    if (!strcasecmp(log_suffix, "%h"))
      {
      sprintf(pbuf, "%s/%04d%02d%02d.%s",
              log_directory,
              ptm->tm_year + 1900,
              ptm->tm_mon + 1,
              ptm->tm_mday,
              (log_host[0] != '\0') ? log_host : "localhost");
      }
    else
      {
      sprintf(pbuf, "%s/%04d%02d%02d.%s",
              log_directory,
              ptm->tm_year + 1900,
              ptm->tm_mon + 1,
              ptm->tm_mday,
              log_suffix);
      }
    }
  else
    {
    sprintf(pbuf, "%s/%04d%02d%02d",
            log_directory,
            ptm->tm_year + 1900,
            ptm->tm_mon + 1,
            ptm->tm_mday);
    }

  log_open_day = ptm->tm_yday; /* Julian date log opened */

  return(pbuf);
  }  /* END mk_log_name() */






int log_init(

  char *suffix,    /* I (optional) */
  char *hostname)  /* I (optional) */

  {
  if (suffix != NULL)
    strncpy(log_suffix, suffix, sizeof(log_suffix));

  if (hostname != NULL)
    strncpy(log_host, hostname, sizeof(log_host));

  return(0);
  }  /* END log_init() */




/*
 * log_open() - open the log file for append.
 *
 * Opens a (new) log file.
 * If a log file is already open, and the new file is successfully opened,
 * the old file is closed.  Otherwise the old file is left open.
 */

int log_open(

  char *filename,  /* abs filename or NULL */
  char *directory) /* normal log directory */

  {
  char  buf[_POSIX_PATH_MAX];
  int   fds;

  if (log_opened > 0)
    {
    return(-1); /* already open */
    }

  if (log_directory != directory)  /* some calls pass in log_directory */
    {
    strncpy(log_directory, directory, (_POSIX_PATH_MAX) / 2 - 1);
    }

  if ((filename == NULL) || (*filename == '\0'))
    {
    filename = mk_log_name(buf);

    log_auto_switch = 1;
    }
  else if (*filename != '/')
    {
    return(-1); /* must be absolute path */
    }

  if ((fds = open(filename, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0)
    {
    log_opened = -1; /* note that open failed */

    return(-1);
    }

  if (fds < 3)
    {
    log_opened = fcntl(fds, F_DUPFD, 3); /* overload variable */

    if (log_opened < 0)
      {
      return(-1);
      }

    close(fds);

    fds = log_opened;
    }

  /* save the path of the last opened logfile for log_roll */
  if (logpath != filename)
    {
    if (logpath != NULL)
      free(logpath);

    logpath = strdup(filename);
    }

  logfile = fdopen(fds, "a");

  setvbuf(logfile, NULL, _IOLBF, 0); /* set line buffering */

  log_opened = 1;   /* note that file is open */

  log_record(
    PBSEVENT_SYSTEM,
    PBS_EVENTCLASS_SERVER,
    "Log",
    "Log opened");

  return(0);
  }  /* END log_open() */




/*
 * log_err - log an internal error
 * The error is recorded to the pbs log file and to syslogd if it is
 * available.  If the error file has not been opened and if syslog is
 * not defined, then the console is opened.
 */

void log_err(

  int   errnum,  /* I (errno or PBSErrno) */
  char *routine, /* I */
  char *text)    /* I */

  {
  log_ext(errnum,routine,text,LOG_ERR);

  return;
  }  /* END log_err() */




/*
 *  log_ext (log extended) - log message to syslog (if available) and to the TORQUE log.
 *
 *  The error is recorded to the TORQUE log file and to syslogd if it is
 *	available.  If the error file has not been opened and if syslog is
 *	not defined, then the console is opened.

 *  Note that this function differs from log_err in that you can specify a severity
 *  level that will accompany the message in the syslog (see 'manpage syslog' for a list
 *  of severity/priority levels). This function, is in fact, used by log_err--it just uses
 *  a severity level of LOG_ERR
 */

void log_ext(

  int   errnum,   /* I (errno or PBSErrno) */
  char *routine,  /* I */
  char *text,     /* I */
  int   severity) /* I */

  {
  char  buf[LOG_BUF_SIZE];

  char *EPtr = NULL;

  char  EBuf[1024];

  char  tmpLine[2048];

  const char *SeverityText = NULL;

  tmpLine[0] = '\0';

  EBuf[0] = '\0';

  if (errnum == -1)
    {
    buf[0] = '\0';
    }
  else
    {
    /* NOTE:  some strerror() routines return "Unknown error X" w/bad errno */

    if (errnum >= 15000)
      {
      EPtr = pbse_to_txt(errnum);
      }
    else
      {
      EPtr = strerror(errnum);
      }

    if (EPtr == NULL)
      {
      sprintf(EBuf, "unexpected error %d",
              errnum);

      EPtr = EBuf;
      }

    sprintf(tmpLine,"%s (%d) in ", 
            EPtr,
            errnum);
    }

  SeverityText = log_get_severity_string(severity);

  snprintf(buf,sizeof(buf),"%s::%s%s, %s",
    SeverityText,
    tmpLine,
    routine,
    text);

  buf[LOG_BUF_SIZE - 1] = '\0';

  if (log_opened == 0)
    {
#if !SYSLOG
    log_open("/dev/console", log_directory);
#endif /* not SYSLOG */
    }

  if (isatty(2))
    {
    fprintf(stderr, "%s: %s\n",
            msg_daemonname,
            buf);
    }

  if (log_opened > 0)
    {
    log_record(
      PBSEVENT_ERROR | PBSEVENT_FORCE,
      PBS_EVENTCLASS_SERVER,
      msg_daemonname,
      buf);
    }

#if SYSLOG
  if (syslogopen == 0)
    {
    openlog(msg_daemonname, LOG_NOWAIT, LOG_DAEMON);

    syslogopen = 1;
    }

  syslog(severity|LOG_DAEMON,"%s",buf);

#endif /* SYSLOG */

  return;
  }  /* END log_ext() */



/**
 * Returns a string to represent the syslog severity-level given.
 */

const char *log_get_severity_string(

  int severity)

  {
  const char *result = NULL;

  /* We can't just index into an array to get the strings, as we don't always
   * have control over what the value of the LOG_* consts are */

  switch (severity)
    {
    case LOG_EMERG:
      result = "LOG_EMERGENCY";
      break;
    
    case LOG_ALERT:
      result = "LOG_ALERT";
      break;

    case LOG_CRIT:
      result = "LOG_CRITICAL";
      break;

    case LOG_ERR:
      result = "LOG_ERROR";
      break;

    case LOG_WARNING:
      result = "LOG_WARNING";
      break;

    case LOG_NOTICE:
      result = "LOG_NOTICE";
      break;

    case LOG_DEBUG:
      result = "LOG_DEBUG";
      break;

    case LOG_INFO:
    default:
      result = "LOG_INFO";
      break;
    }

  return(result);
  }  /* END log_get_severity_string() */




/*
 * log_record - log a message to the log file
 * The log file must have been opened by log_open().
 *
 * NOTE:  do not use in pbs_mom spawned children - does not write to syslog!!!
 *
 * The caller should ensure proper formating of the message if "text"
 * is to contain "continuation lines".
 */

void log_record(

  int   eventtype,  /* I */
  int   objclass,   /* I */
  char *objname,    /* I */
  char *text)       /* I */

  {
  int tryagain = 2;
  time_t now;

  struct tm *ptm;
  struct tm  tmpPtm;
  int    rc = 0;
  FILE  *savlog;
  char  *start = NULL, *end = NULL;
  size_t nchars;

  if (log_opened < 1)
    {
    return;
    }

  now = time((time_t *)0); /* get time for message */

  ptm = localtime_r(&now,&tmpPtm);

  /* Do we need to switch the log? */

  if (log_auto_switch && (ptm->tm_yday != log_open_day))
    {
    log_close(1);

    log_open(NULL, log_directory);

    if (log_opened < 1)
      {
      return;
      }
    }

  /*
   * Looking for the newline characters and splitting the output message
   * on them.  Sequence "\r\n" is mapped to the single newline.
   */
  start = text;

  while (1)
    {
    for (end = start; *end != '\n' && *end != '\r' && *end != '\0'; end++)
      ;

    nchars = end - start;

    if (*end == '\r' && *(end + 1) == '\n')
      end++;

    while (tryagain)
      {
      rc = fprintf(logfile,
                   "%02d/%02d/%04d %02d:%02d:%02d;%04x;%10.10s;%s;%s;%s%.*s\n",
                   ptm->tm_mon + 1,
                   ptm->tm_mday,
                   ptm->tm_year + 1900,
                   ptm->tm_hour,
                   ptm->tm_min,
                   ptm->tm_sec,
                   (eventtype & ~PBSEVENT_FORCE),
                   msg_daemonname,
                   class_names[objclass],
                   objname,
                   (text == start ? "" : "[continued]"),
                   (int)nchars, start);

      if ((rc < 0) &&
          (errno == EPIPE) &&
          (tryagain == 2))
        {
        /* the log file descriptor has been changed--it now points to a socket!
         * reopen log and leave the previous file descriptor alone--do not close it */

        log_opened = 0;
        log_open(NULL, log_directory);
        tryagain--;
        }
      else
        {
        tryagain = 0;
        }
      }

    if (rc < 0)
      break;

    if (*end == '\0')
      break;

    start = end + 1;
    }

  fflush(logfile);

  if (rc < 0)
    {
    rc = errno;
    clearerr(logfile);
    savlog = logfile;
    logfile = fopen("/dev/console", "w");
    log_err(rc, "log_record", "PBS cannot write to its log");
    fclose(logfile);
    logfile = savlog;
    }

  return;
  }  /* END log_record() */




/*
 * log_close - close the current open log file
 */

void log_close(

  int msg)  /* BOOLEAN - write close message */

  {
  if (log_opened == 1)
    {
    log_auto_switch = 0;

    if (msg)
      {
      log_record(
        PBSEVENT_SYSTEM,
        PBS_EVENTCLASS_SERVER,
        "Log",
        "Log closed");
      }

    fclose(logfile);

    log_opened = 0;
    }

#if SYSLOG

  if (syslogopen)
    closelog();

#endif /* SYSLOG */

  return;
  }  /* END log_close() */





void log_roll(

  int max_depth)

  {
  int i, suffix_size, file_buf_len, as;
  int err = 0;
  char *source  = NULL;
  char *dest    = NULL;

  if (!log_opened)
    {
    return;
    }

  /* save value of log_auto_switch */

  as = log_auto_switch;

  log_close(1);

  /* find out how many characters the suffix could be. (save in suffix_size)
     start at 1 to account for the "."  */

  for (i = max_depth, suffix_size = 1;i > 0;suffix_size++, i /= 10);

  /* allocate memory for rolling */

  file_buf_len = sizeof(char) * (strlen(logpath) + suffix_size + 1);

  source = (char*)malloc(file_buf_len);

  dest   = (char*)malloc(file_buf_len);

  /* call unlink to delete logname.max_depth - it doesn't matter if it
     doesn't exist, so we'll ignore ENOENT */

  sprintf(dest, "%s.%d",
          logpath,
          max_depth);

  if ((unlink(dest) != 0) && (errno != ENOENT))
    {
    err = errno;
    goto done_roll;
    }

  /* logname.max_depth is gone, so roll the rest of the log files */

  for (i = max_depth - 1;i >= 0;i--)
    {
    if (i == 0)
      {
      strcpy(source, logpath);
      }
    else
      {
      sprintf(source, "%s.%d",
              logpath,
              i);
      }

    sprintf(dest, "%s.%d",
            logpath,
            i + 1);

    /* rename file if it exists */

    if ((rename(source, dest) != 0) && (errno != ENOENT))
      {
      err = errno;
      goto done_roll;
      }
    }

done_roll:

  if (as)
    {
    log_open(NULL, log_directory);
    }
  else
    {
    log_open(logpath, log_directory);
    }

  free(source);

  free(dest);

  if (err != 0)
    {
    log_err(err, "log_roll", "error while rollng logs");
    }
  else
    {
    log_record(
      PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      "Log",
      "Log Rolled");
    }

  return;
  } /* END log_roll() */



/* return size of log file in kilobytes */

long log_size(void)

  {
#if defined(HAVE_STRUCT_STAT64) && defined(HAVE_STAT64)

  struct stat64 file_stat;
#else

  struct stat file_stat;
#endif

#if defined(HAVE_STRUCT_STAT64) && defined(HAVE_STAT64)

  if (log_opened && (fstat64(fileno(logfile), &file_stat) != 0))
#else
  if (log_opened && (fstat(fileno(logfile), &file_stat) != 0))
#endif
    {
    /* FAILURE */

    log_err(errno, "log_size", "PBS cannot fstat logfile");

    return(0);
    }

  return(file_stat.st_size / 1024);
  }

/* END pbs_log.c */
