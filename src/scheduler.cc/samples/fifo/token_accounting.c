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
#include "log.h"
#include "queue.h"
#include "token_acct.h"

/* Local Data */

static FILE     *acctfile;  /* open stream for log file */
static volatile int  acct_opened = 0;
static int      acct_opened_day;
static int      acct_auto_switch = 0;

/* Global Data */

extern char     path_acct[];
extern int      resc_access_perm;

/*
 * token_acct_open() - open the acct file for append.
 *
 * Opens a (new) acct file.
 * If a acct file is already open, and the new file is successfully opened,
 * the old file is closed.  Otherwise the old file is left open.
 */

int token_acct_open(char *filename)
  {
  char  filen[_POSIX_PATH_MAX];
  char  logmsg[_POSIX_PATH_MAX+80];
  FILE *newacct;
  time_t now;

  struct tm *ptm;

  if (filename == (char *)0)   /* go with default */
    {
    now = time(0);
    ptm = localtime(&now);
    (void)sprintf(filen, "%s/%04d%02d%02d",
                  path_acct,
                  ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    filename = filen;
    acct_auto_switch = 1;
    acct_opened_day = ptm->tm_yday;
    }
  else if (*filename == '\0')   /* a null name is not an error */
    {
    return (0);  /* turns off account logging.  */
    }
  else if (*filename != '/')
    {
    return (-1);  /* not absolute */
    }

  if ((newacct = fopen(filename, "a")) == NULL)
    {
    fprintf(stderr, "In token_acct_open filed to open file %s\n", filename);
    perror("acct_open");
    return (-1);
    }

  setbuf(newacct, NULL);  /* set no buffering */

  if (acct_opened > 0)   /* if acct was open, close it */
    (void)fclose(acctfile);

  acctfile = newacct;

  acct_opened = 1;   /* note that file is open */

  (void)sprintf(logmsg, "Account file %s opened", filename);

  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, "TokenAct", logmsg);

  return (0);
  }

/*
 * acct_close - close the current open log file
 */

void token_acct_close()
  {
  if (acct_opened == 1)
    {
    (void)fclose(acctfile);
    acct_opened = 0;
    }
  }

/*
 * account_record - write basic accounting record
 */

void token_account_record(int acctype, char *job_id, char *text)
  {
  time_t now;
  char jobid[1024];

  struct tm *ptm;

  time(&now);
  memset(jobid, 0, 1024);

  if (job_id != NULL)
    {
    strncpy(jobid, job_id, 1023);
    }

  ptm = localtime(&now);

  if (acctfile != NULL)
    {
    (void)fprintf(acctfile,
                  "%02d/%02d/%04d %02d:%02d:%02d;%c;%s;%s\n",
                  ptm->tm_mon + 1, ptm->tm_mday, ptm->tm_year + 1900,
                  ptm->tm_hour, ptm->tm_min, ptm->tm_sec,
                  (char)acctype,
                  jobid,
                  text);
    }
  }

