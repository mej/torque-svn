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

#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <stdio.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "server.h"
#include "pbs_error.h"
#include "svrfunc.h"
#include "log.h"
#ifdef _CRAY
#include <udb.h>
#endif /* _CRAY */

/* External Data */

extern char server_host[];

/*
 * geteusernam - get effective user name
 * Get the user name under which the job should be run on this host:
 *   1. from user_list use name with host name that matches this host
 *   2. from user_list use name with no host specification
 *   3. user owner name
 *
 * Returns pointer to the user name
 */

static char *geteusernam(

  job       *pjob,
  attribute *pattr) /* pointer to User_List attribute */

  {
  int  rule3 = 0;
  char *hit = 0;
  int  i;

  struct array_strings *parst;
  char *pn;
  char *ptr;
  static char username[PBS_MAXUSER + 1];

  /* search the user-list attribute */

  if ((pattr->at_flags & ATR_VFLAG_SET) &&
      (parst = pattr->at_val.at_arst))
    {
    for (i = 0;i < parst->as_usedptr;i++)
      {
      pn = parst->as_string[i];
      ptr = strchr(pn, '@');

      if (ptr != NULL)
        {
        /* has host specification */

        if (!strncmp(server_host, ptr + 1, strlen(ptr + 1)))
          {
          hit = pn; /* option 1. */

          break;
          }
        }
      else
        {
        /* wildcard host (null) */
        hit = pn; /* option 2. */
        }
      }
    }

  if (!hit)
    {
    /* default to the job owner ( 3.) */

    hit = pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str;

    rule3 = 1;
    }

  /* copy user name into return buffer, strip off host name */

  get_jobowner(hit, username);

  if (rule3)
    {
    ptr = site_map_user(username, get_variable(pjob, "PBS_O_HOST"));

    if (ptr != username)
      strcpy(username, ptr);
    }

  return(username);
  }  /* END geteusernam() */





/*
 * getegroup - get specified effective group name
 * Get the group name under which the job is specified to be run on
 * this host:
 *   1. from group_list use name with host name that matches this host
 *   2. from group_list use name with no host specification
 *   3. NULL, not specified.
 *
 * Returns pointer to the group name or null
 */

static char *getegroup(

  job       *pjob,  /* I */
  attribute *pattr) /* I group_list attribute */

  {
  char *hit = 0;
  int  i;

  struct array_strings *parst;
  char *pn;
  char *ptr;
  static char groupname[PBS_MAXUSER + 1];

  /* search the group-list attribute */

  if ((pattr->at_flags & ATR_VFLAG_SET) &&
      (parst = pattr->at_val.at_arst))
    {
    for (i = 0;i < parst->as_usedptr;i++)
      {
      pn = parst->as_string[i];
      ptr = strchr(pn, '@');

      if (ptr != NULL)
        {
        /* has host specification */

        if (!strncmp(server_host, ptr + 1, strlen(ptr + 1)))
          {
          hit = pn; /* option 1. */

          break;
          }
        }
      else
        {
        /* wildcard host (null) */

        hit = pn; /* option 2. */
        }
      }
    }

  if (!hit)   /* nothing specified, return null */
    {
    return(NULL);
    }

  /* copy group name into return buffer, strip off host name */
  /* get_jobowner() works for group as well, same size    */

  get_jobowner(hit, groupname);

  return(groupname);
  }  /* END getegroup() */





/*
 * set_jobexid - validate and set the execution user (JOB_ATR_euser) and
 * execution group (JOB_ATR_egroup) job attributes.
 *
 * 1.  Get the execution user name.
 * 1a. Get the corresponding password entry.
 * 1b. Uid of 0 (superuser) is not allowed, might cause root-rot
 * 1c. If job owner name is not being used, does the job owner have
 *     permission to map to the execution name?
 * 1d. Set JOB_ATR_euser to the execution user name.
 * 2.  Get the execution group name.
 * 2a. Is the execution user name a member of this group?
 * 2b. Set JOB_ATR_egroup to the execution group name.
 *
 * Returns: 0 if set, non-zero error number if any error
 */

int set_jobexid(

  job       *pjob,    /* I */
  attribute *attrry,  /* I */
  char      *EMsg)    /* O (optional,minsize=1024) */

  {
  int   addflags = 0;
  attribute *pattr;
  char        **pmem;

  struct group *gpent;
  char  *puser = NULL;

  struct passwd *pwent = NULL;
  char  *pgrpn;
  char   gname[PBS_MAXGRPN + 1];
#ifdef _CRAY

  struct udb    *pudb;
#endif

  char           tmpLine[1024 + 1];

  int            CheckID;  /* boolean */

  if (EMsg != NULL)
    EMsg[0] = '\0';

  /* use the passed User_List if set, may be a newly modified one     */
  /* if not set, fall back to the job's actual User_List, may be same */

  if (server.sv_attr[(int)SRV_ATR_DisableServerIdCheck].at_val.at_long)
    CheckID = 0;
  else
    CheckID = 1;

  if (CheckID == 0)
    {
    /* NOTE: use owner, not userlist - should this be changed? */
    /* Yes, changed 10/17/2007 */

    if (pjob->ji_wattr[JOB_ATR_job_owner].at_val.at_str != NULL)
      {
      /* start of change to use userlist instead of owner 10/17/2007 */

      if ((attrry + (int)JOB_ATR_userlst)->at_flags & ATR_VFLAG_SET)
        pattr = attrry + (int)JOB_ATR_userlst;
      else
        pattr = &pjob->ji_wattr[(int)JOB_ATR_userlst];

      if ((puser = geteusernam(pjob, pattr)) == NULL)
        {
        if (EMsg != NULL)
          snprintf(EMsg, 1024, "cannot locate user name in job");

        return(PBSE_BADUSER);
        }

      sprintf(tmpLine, "%s",

              puser);

      /* end of change to use userlist instead of owner 10/17/2007 */
      }
    else
      {
      strcpy(tmpLine, "???");
      }
    }  /* END if (CheckID == 0) */
  else
    {
    if ((attrry + (int)JOB_ATR_userlst)->at_flags & ATR_VFLAG_SET)
      pattr = attrry + (int)JOB_ATR_userlst;
    else
      pattr = &pjob->ji_wattr[(int)JOB_ATR_userlst];

    if ((puser = geteusernam(pjob, pattr)) == NULL)
      {
      if (EMsg != NULL)
        snprintf(EMsg, 1024, "cannot locate user name in job");

      return(PBSE_BADUSER);
      }

    pwent = getpwnam(puser);

    if (pwent == NULL)
      {
      log_err(errno, "set_jobexid", "getpwnam failed");

      if (EMsg != NULL)
        snprintf(EMsg, 1024, "user does not exist in server password file");

      return(PBSE_BADUSER);
      }

    if (pwent->pw_uid == 0)
      {
      if (server.sv_attr[(int)SRV_ATR_AclRoot].at_flags & ATR_VFLAG_SET)
        {
        if (acl_check(
              &server.sv_attr[(int)SRV_ATR_AclRoot],
              pjob->ji_wattr[(int)JOB_ATR_job_owner].at_val.at_str,
              ACL_User) == 0)
          {
          if (EMsg != NULL)
            snprintf(EMsg, 1024, "root user %s fails ACL check",
                     puser);

          return(PBSE_BADUSER); /* root not allowed */
          }
        }
      else
        {
        if (EMsg != NULL)
          snprintf(EMsg, 1024, "root user %s not allowed",
                   puser);

        return(PBSE_BADUSER); /* root not allowed */
        }
      }    /* END if (pwent->pw_uid == 0) */

    if (site_check_user_map(pjob, puser, EMsg) == -1)
      {
      return(PBSE_BADUSER);
      }

    strncpy(tmpLine, puser, sizeof(tmpLine));
    }  /* END else (CheckID == 0) */

  pattr = attrry + (int)JOB_ATR_euser;

  job_attr_def[(int)JOB_ATR_euser].at_free(pattr);

  job_attr_def[(int)JOB_ATR_euser].at_decode(pattr, NULL, NULL, tmpLine);

#ifdef _CRAY

  /* on cray check UDB (user data base) for permission to batch it */

  if ((pwent != NULL) && (puser != NULL))
    {
    pudb = getudbuid(pwent->pw_uid);

    endudb();

    if (pudb == UDB_NULL)
      {
      if (EMsg != NULL)
        snprintf(EMsg, 1024, "user %s not located in user data base",
                 puser);

      return(PBSE_BADUSER);
      }

    if (pudb->ue_permbits & (PERMBITS_NOBATCH | PERMBITS_RESTRICTED))
      {
      return(PBSE_QACESS);
      }

    /* if account (qsub -A) not specified, set default from UDB */

    pattr = attrry + (int)JOB_ATR_account;

    if ((pattr->at_flags & ATR_VFLAG_SET) == 0)
      {
      job_attr_def[(int)JOB_ATR_account].at_decode(
        pattr,
        NULL,
        NULL,
        (char *)acid2nam(pudb->ue_acids[0]));
      }
    }    /* END if ((pwent != NULL) && (puser != NULL)) */

#endif /* _CRAY */

  /*
   * now figure out the group name under which the job should execute
   * PBS requires that each group have an entry in the group file,
   * see the admin guide for the reason why...
   *
   * use the passed group_list if set, may be a newly modified one
   * if not set, fall back to the job's actual group_list, may be same
   */

  if ((attrry + (int)JOB_ATR_grouplst)->at_flags & ATR_VFLAG_SET)
    pattr = attrry + (int)JOB_ATR_grouplst;
  else
    pattr = &pjob->ji_wattr[(int)JOB_ATR_grouplst];

  /* extract user-specified egroup if it exists */

  pgrpn = getegroup(pjob, pattr);

  if (pgrpn == NULL)
    {
    if ((pwent != NULL) || ((pwent = getpwnam(puser)) != NULL))
      {
      /* egroup not specified - use user login group */

      gpent = getgrgid(pwent->pw_gid);

      if (gpent != NULL)
        {
        pgrpn = gpent->gr_name;           /* use group name */
        }
      else
        {
        sprintf(gname, "%ld",
                (long)pwent->pw_gid);

        pgrpn = gname;            /* turn gid into string */
        }
      }
    else if (CheckID == 0)
      {
      strcpy(gname, "???");

      pgrpn = gname;
      }
    else
      {
      log_err(errno, "set_jobexid", "getpwnam failed");

      if (EMsg != NULL)
        snprintf(EMsg, 1024, "user does not exist in server password file");

      return(PBSE_BADUSER);
      }

    /*
     * setting the DEFAULT flag is a "kludgy" way to keep MOM from
     * having to do an unneeded look up of the group file.
     * We needed to have JOB_ATR_egroup set for the server but
     * MOM only wants it if it is not the login group, so there!
     */

    addflags = ATR_VFLAG_DEFLT;
    }  /* END if ((pgrpn = getegroup(pjob,pattr))) */
  else if (CheckID == 0)
    {
    /* egroup specified - do not validate group within server */

    /* NO-OP */
    }
  else
    {
    /* user specified a group, group must exist and either */
    /* must be user's primary group or the user must be in it */

    gpent = getgrnam(pgrpn);

    if (gpent == NULL)
      {
      if (CheckID == 0)
        {
        strcpy(gname, "???");

        pgrpn = gname;
        }
      else
        if (EMsg != NULL)
          snprintf(EMsg, 1024, "cannot locate group %s in server group file",
                   pgrpn);

      return(PBSE_BADGRP);  /* no such group */
      }

    if (gpent->gr_gid != pwent->pw_gid)
      {
      /* not primary group */

      pmem = gpent->gr_mem;

      while (*pmem != NULL)
        {
        if (!strcmp(puser, *pmem))
          break;

        ++pmem;
        }

      if (*pmem == NULL)
        {
        /* requested group not allowed */

        if (EMsg != NULL)
          snprintf(EMsg, 1024, "user %s not member of group %s in server password file",
                   puser,
                   pgrpn);

        return(PBSE_BADGRP); /* user not in group */
        }
      }
    }    /* END if ((pgrpn = getegroup(pjob,pattr))) */

  /* set new group */

  pattr = attrry + (int)JOB_ATR_egroup;

  job_attr_def[(int)JOB_ATR_egroup].at_free(pattr);

  job_attr_def[(int)JOB_ATR_egroup].at_decode(pattr, NULL, NULL, pgrpn);

  pattr->at_flags |= addflags;

  /* SUCCESS */

  return(0);
  }  /* END set_jobexid() */

/* END geteusernam.c */

