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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portability.h"
#include "log.h"

/*
 * setup_env - setup the daemon's environment
 *
 *	To provide a "safe and secure" environment, the daemons replace their
 *	inherited one with one from a file.  Each line in the file is
 *	either a comment line, starting with '#' or ' ', or is a line of 
 *	the forms:	variable=value
 *			variable
 *	In the second case the value is obtained from the current environment.
 *
 *	Returns: number of variables placed or -1 on error
 */

#define PBS_ENVP_STR     64
#define PBS_ENV_CHUNCK 1024

int setup_env(

  char *filen)

  {
  char	     buf[256];
  int	     evbufsize = 0;
  FILE	    *efile;
  char        *envbuf = NULL;
  static char *envp[PBS_ENVP_STR + 1];
  int	     len;
  int	     nstr = 0;
  char	    *pequal;
  char	    *pval = NULL;
  extern char **environ;

  envp[0] = NULL;

  if ((filen == NULL) || (*filen == '\0'))
    {
    environ = envp;
    return(0);
    }

  efile = fopen(filen, "r");

  if (efile != NULL) 
    {
    while (fgets(buf,255,efile)) 
      {
      if ((buf[0] != '#') && (buf[0] != ' ') && (buf[0] != '\n')) 
        {
        /* remove newline */

	len = strlen(buf);
	buf[len - 1] = '\0';

	if ((pequal = strchr(buf,(int)'=')) == NULL) 
          {
          if ((pval = getenv(buf)) == NULL)
            continue;

          len += strlen(pval) + 1;
          }
	
        if (evbufsize < len) 
          {
          if ((envbuf = malloc(PBS_ENV_CHUNCK)) == NULL)
            goto err;

          evbufsize = PBS_ENV_CHUNCK;
          }

        (void)strcpy(envbuf,buf);

        if (pequal == NULL) 
          {
          (void)strcat(envbuf,"=");
          (void)strcat(envbuf,pval);
          }

        envp[nstr++] = envbuf;

        if (nstr == PBS_ENVP_STR)
          goto err;

        envp[nstr] = NULL;
        envbuf += len;
        evbufsize -= len;
        }  /* END if (Buf[0]) */
      }    /* END while (fgets(buf,255,efile)) */

    /* copy local environment */

    if ((pval = getenv("PBSDEBUG")) != NULL)
      {
      sprintf(envbuf,"PBSDEBUG=%s",
        pval);

      len = strlen(envbuf);

      envp[nstr++] = envbuf;

      if (nstr == PBS_ENVP_STR)
        goto err;

      envp[nstr] = NULL;
      envbuf += len;
      evbufsize -= len;
      }

    fclose(efile);

    environ = envp;
    }  /* END if (efile != NULL) */ 
  else if (errno != ENOENT) 
    {
    goto err;
    }

  return (nstr);

  err:	

  log_err(-1,"setup_env","could not set up the environment");
  return(-1);
  }  /* END setup_env() */
