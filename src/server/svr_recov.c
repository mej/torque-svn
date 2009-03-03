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

/**
 * @file svr_recov.c
 *
 * contains functions to save server state and recover
 *
 * Included functions are:
 *	svr_recov()
 *	svr_save()
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "queue.h"
#include "server.h"
#include "svrfunc.h"
#include "log.h"
#include "pbs_error.h"


/* Global Data Items: */

extern struct server server;
extern tlist_head svr_queues;
extern attribute_def svr_attr_def[];
extern char	    *path_svrdb;
extern char	    *path_svrdb_new;
extern char	    *path_priv;
extern time_t    time_now;
extern char	    *msg_svdbopen;
extern char	    *msg_svdbnosv;




/**
 * Recover server state from server database.
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_recov(

  char *svrfile,  /* I */
  int read_only)  /* I */

  {
  static char *this_function_name = "svr_recov";
  int i;
  int sdb;

  void recov_acl A_((attribute *,attribute_def *,char *,char *));

  sdb = open(svrfile,O_RDONLY,0);

  if (sdb < 0) 
    {
    log_err(errno, this_function_name, msg_svdbopen);

    return(-1);
    }

  /* read in server structure */

  i = read(sdb,(char *)&server.sv_qs,sizeof(struct server_qs));

  if (i != sizeof(struct server_qs)) 
    {
    if (i < 0) 
      log_err(errno, this_function_name, "read of serverdb failed");
    else
      log_err(errno, this_function_name, "short read of serverdb");

    close(sdb);

    return(-1);
    }

  /* Save the sv_jobidnumber field in case it is set by the attribute. */
  i = server.sv_qs.sv_jobidnumber;
	
  /* read in server attributes */

  if (recov_attr(
        sdb, 
        &server, 
        svr_attr_def, 
        server.sv_attr,
	(int)SRV_ATR_LAST,
        0,
        !read_only) != 0 ) 
    {
    log_err(errno, this_function_name, "error on recovering server attr");

    close(sdb);

    return(-1);
    }

  /* Restore the current job number and make it visible in qmgr print server commnad. */
  
  if (!read_only)
    {
    server.sv_qs.sv_jobidnumber = i;
    server.sv_attr[(int)SRV_ATR_NextJobNum].at_val.at_long = i;
    server.sv_attr[(int)SRV_ATR_NextJobNum].at_flags |= ATR_VFLAG_SET|ATR_VFLAG_MODIFY;
    }

  close(sdb);
	
  /* recover the server various acls from their own files */

  for (i = 0;i < SRV_ATR_LAST;i++) 
    {
    if (server.sv_attr[i].at_type == ATR_TYPE_ACL) 
      {
      recov_acl(
        &server.sv_attr[i], 
        &svr_attr_def[i],
        PBS_SVRACL, 
        svr_attr_def[i].at_name);

      if ((!read_only) && (svr_attr_def[i].at_action != (int (*)())0))
        {
        svr_attr_def[i].at_action(
          &server.sv_attr[i],
          &server,
          ATR_ACTION_RECOV);
        }
      }
    }    /* END for (i) */

  return(0);
  }  /* END svr_recov() */





/**
 * Save the state of the server (server structure).
 *
 * @param ps     A pointer to the server state structure.
 * @param mode   This is either SVR_SAVE_QUICK or SVR_SAVE_FULL.
 * @return       Zero on success or -1 on error.
 */

int svr_save(

  struct server *ps,
  int            mode)

  {
  static char *this_function_name = "svr_save";
  int i;
  int sdb;
  int save_acl A_((attribute *,attribute_def *,char *,char *));


  if (mode == SVR_SAVE_QUICK) 
    {
    sdb = open(path_svrdb, O_WRONLY | O_CREAT | O_Sync, 0600);

    if (sdb < 0) 
      {
      log_err(errno, this_function_name, msg_svdbopen);

      return(-1);
      }

    while ((i = write(
                  sdb,
		  &ps->sv_qs,
		  sizeof(struct server_qs))) != sizeof(struct server_qs)) 
      {
      if ((i == -1) && (errno == EINTR))
        continue;

      log_err(errno, this_function_name, msg_svdbnosv);

      return(-1);
      }

    close(sdb);
    } 
  else 
    {	
    /* SVR_SAVE_FULL Save */

    sdb = open(path_svrdb_new,O_WRONLY|O_CREAT|O_Sync,0600);

    if (sdb < 0) 
      {
      log_err(errno, this_function_name, msg_svdbopen);

      return(-1);
      }

    ps->sv_qs.sv_savetm = time_now;

    save_setup(sdb);

    if (save_struct((char *)&ps->sv_qs,sizeof(struct server_qs)) != 0) 
      {
      snprintf(log_buffer,1024,"cannot save data into server db, errno=%d (%s)",
        errno,
        pbs_strerror(errno));

      log_err(errno, this_function_name, log_buffer);

      close(sdb);

      return(-1);
      }

    if (save_attr(svr_attr_def,ps->sv_attr,(int)SRV_ATR_LAST) != 0) 
      {
      close(sdb);

      return(-1);
      }

    if (save_flush() != 0) 
      {
      close(sdb);

      return(-1);
      }

    /* new db successfully created, remove original db */

    close(sdb);
    unlink(path_svrdb);

    if (link(path_svrdb_new,path_svrdb) == -1)
      {
      snprintf(log_buffer,1024,"cannot move new database to default database location, errno=%d (%s)",
        errno,
        pbs_strerror(errno));

      log_err(errno, this_function_name, log_buffer);
      }
    else
      {
      unlink(path_svrdb_new);
      }

    /* save the server acls to their own files:	*/
    /* 	priv/svracl/(attr name)			*/

    for (i = 0;i < SRV_ATR_LAST;i++) 
      {
      if (ps->sv_attr[i].at_type == ATR_TYPE_ACL)
        save_acl(&ps->sv_attr[i],&svr_attr_def[i],
          PBS_SVRACL,svr_attr_def[i].at_name);
      }
    }    /* END else (mode == SVR_SAVE_QUICK) */

  return(0);
  }  /* END svr_save() */




/**
 * Save an Access Control List to its file.
 *
 * @param attr   A pointer to an acl (access control list) attribute.
 * @param pdef   A pointer to the attribute definition structure.
 * @param subdir The sub-directory path specifying where to write the file.
 * @param name   The parent object name which in this context becomes the file name.
 * @return       Zero on success (File may not be written if attribute is clean) or -1 on error.
 */

int save_acl(

  attribute     *attr,	 /* acl attribute */
  attribute_def *pdef,	 /* attribute def structure */
  char          *subdir, /* sub-directory path */
  char          *name)	 /* parent object name = file name */

  {
  static char *this_function_name = "save_acl";
  int  fds;
  char filename1[MAXPATHLEN];
  char filename2[MAXPATHLEN];
  tlist_head    head;
  int	     i;
  svrattrl    *pentry;

  if ((attr->at_flags & ATR_VFLAG_MODIFY) == 0)	
    {
    return(0);  	/* Not modified, don't bother */
    }

  attr->at_flags &= ~ATR_VFLAG_MODIFY;

  strcpy(filename1,path_priv);
  strcat(filename1,subdir);
  strcat(filename1,"/");
  strcat(filename1,name);

  if ((attr->at_flags & ATR_VFLAG_SET) == 0) 
    {
    /* has been unset, delete the file */

    unlink(filename1);

    return(0);
    }

  strcpy(filename2,filename1);
  strcat(filename2,".new");

  fds = open(filename2,O_WRONLY|O_CREAT|O_TRUNC|O_Sync,0600);

  if (fds < 0) 
    {
    snprintf(log_buffer,1024,"unable to open acl file '%s'",
      filename2);

    log_err(errno, this_function_name, log_buffer);

    return(-1);
    }
	
  CLEAR_HEAD(head);

  i = pdef->at_encode(attr,&head,pdef->at_name,(char *)0,ATR_ENCODE_SAVE);

  if (i < 0) 
    {
    log_err(-1, this_function_name, "unable to encode acl");

    close(fds);

    unlink(filename2);

    return(-1);
    }
	
  pentry = (svrattrl *)GET_NEXT(head);

  if (pentry != NULL) 
    {
    /* write entry, but without terminating null */

    while ((i = write(fds,pentry->al_value,pentry->al_valln - 1)) != pentry->al_valln - 1)
      {
      if ((i == -1) && (errno == EINTR))
        continue;

      log_err(errno, this_function_name, "wrote incorrect amount");	

      close(fds);

      unlink(filename2);

      return(-1);
      }

    free(pentry);
    }

  close(fds);
  unlink(filename1);

  if (link(filename2,filename1) < 0) 
    {
    log_err(errno, this_function_name, "unable to relink file");

    return(-1);
    }

  unlink(filename2);

  attr->at_flags &= ~ATR_VFLAG_MODIFY;	/* clear modified flag */

  return(0);
  }


/**
 * Reload an Access Control List from its file.
 *
 * @param attr   A pointer to an acl (access control list) attribute.
 * @param pdef   A pointer to the attribute definition structure.
 * @param subdir The sub-directory path specifying where to read the file.
 * @param name   The parent object name which in this context is the file name.
 * @return       Zero on success (File may not be written if attribute is clean) or -1 on error.
 */

void recov_acl(

  attribute     *pattr,	/* acl attribute */
  attribute_def *pdef,	/* attribute def structure */
  char          *subdir, /* directory path */
  char	        *name)	/* parent object name = file name */

  {
  static char *this_function_name = "recov_acl";
  char *buf;
  int   fds;
  char  filename1[MAXPATHLEN];
  struct stat sb;
  attribute tempat;

  errno = 0;

  strcpy(filename1,path_priv);

  if (subdir != NULL) 
    {
    strcat(filename1,subdir);
    strcat(filename1,"/");
    }

  strcat(filename1, name);

  fds = open(filename1,O_RDONLY,0600);

  if (fds < 0) 
    {
    if (errno != ENOENT) 
      {
      sprintf(log_buffer, "unable to open acl file %s",
        filename1);

      log_err(errno, this_function_name, log_buffer);
      }

    return;
    }

  if (fstat(fds,&sb) < 0) 
    {
    return;
    }

  if (sb.st_size == 0)
    {
    return;		/* no data */
    }

  buf = malloc((size_t)sb.st_size + 1);	/* 1 extra for added null */

  if (buf == NULL) 
    {
    close(fds);

    return;
    }

  if (read(fds,buf,(unsigned int)sb.st_size) != (int)sb.st_size) 
    {
    log_err(errno, this_function_name, "unable to read acl file");

    close(fds);
    free(buf);

    return;
    }

  close(fds);

  *(buf + sb.st_size) = '\0';

  clear_attr(&tempat, pdef);

  if (pdef->at_decode(&tempat,pdef->at_name,NULL,buf) < 0) 
    {
    sprintf(log_buffer,"decode of acl %s failed",
      pdef->at_name);

    log_err(errno, this_function_name, log_buffer);
    } 
  else if (pdef->at_set(pattr,&tempat,SET) != 0) 
    {
    sprintf(log_buffer,"set of acl %s failed",
      pdef->at_name);

    log_err(errno, this_function_name, log_buffer);
    }

  pdef->at_free(&tempat);

  free(buf);

  return;
  }  /* END recov_acl() */

/* END svr_recov.c  */

