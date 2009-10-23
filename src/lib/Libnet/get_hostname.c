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
#include <netinet/in.h>
#include "portability.h"
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

#include "portability.h"

/*
 * get_fullhostname - get the fully qualified name of a host
 *
 * Returns: 0 - if success
 *  host name in character buffer pointed to by namebuf
 *    -1 - if error
 */

/* NOTE:  make response lower case */

int get_fullhostname(

	char *shortname,  /* I */
	char *namebuf,    /* O */
	int   bufsize,    /* I */
	char *EMsg)       /* O (optional,minsize=1024) */

{
	char *pbkslh = NULL;
	char *pcolon = NULL;
#ifdef TORQUE_WANT_IPV6
	struct addrinfo hints, *res;
    int error;
#else
	struct hostent *phe;
	struct in_addr  ina;

	int   index;
#endif


	if ((NULL == shortname) || ('\0' == shortname[0])) {
		/* FAILURE */

		if (EMsg != NULL)
			strcpy(EMsg,"host name not specified");

		return(-1);
	}

	if ((pcolon = strchr(shortname,':')) != NULL) {
		*pcolon = '\0';

		if ('\\' == *(pcolon - 1)) {
			pbkslh = pcolon - 1;
			*pbkslh = '\0';
		}
	}

#ifdef TORQUE_WANT_IPV6 /* IPv6/4 enabled version, works correct for BOTH versions */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	error = getaddrinfo(shortname, NULL, &hints, &res);

	if (NULL != pcolon) {
		*pcolon = ':';	/* restore the colon */

		if (pbkslh)
			*pbkslh = '\\';
	}

	if (0 == error) {
		if (strlen(res->ai_canonname) > (size_t)bufsize) {
			/* FAILURE - name too long */
			if (NULL != EMsg) {
				snprintf(EMsg, 1024,
							"hostname (%.32s...) is too long (> %d chars)",
							res->ai_canonname, bufsize);
			}

			return(-1);
		}
		strncpy(namebuf, res->ai_canonname, bufsize);

		namebuf[bufsize - 1] = '\0';	/* ensure null terminated */

		/* lowercase the name */
		for (char *p = namebuf; *p != '\0'; ++p)
			*p = tolower(*p);

		/* free resources */
		freeaddrinfo(res);
	}
	else {
		/* on error, return the error string of getaddrinfo */
		snprintf(EMsg, 1024, gai_strerror(error));

		return(-1);
	}

#else  /* IPv4 enabled version */

	phe = gethostbyname(shortname);

	if (pcolon != NULL) {
		*pcolon = ':';	/* replace the colon */

		if (pbkslh)
			*pbkslh = '\\';
	}

	if (NULL == phe) {
		/* FAILURE - cannot gethostbyname() */

		if (EMsg != NULL)
			snprintf(EMsg, 1024, "gethostbyname(%s) failed, h_errno=%d", 
						shortname, h_errno);
		return(-1);
	}

	memcpy((char *)&ina,*phe->h_addr_list,phe->h_length);

	phe = gethostbyaddr((char *)&ina,phe->h_length,phe->h_addrtype);

	if (NULL == phe) {
		if (HOST_NOT_FOUND == h_errno) {
			fprintf(stderr,
					"Unable to lookup host '%s' by address (check /etc/hosts or DNS reverse name lookup)\n",
					shortname); 
		}

		if (EMsg != NULL)
			snprintf(EMsg,1024,"gethostbyname(%s) failed, h_errno=%d",
						shortname, h_errno);

		/* FAILURE - cannot get host by address */

		return(-1);
	}

	if ((size_t)bufsize < strlen(phe->h_name)) {
	/* FAILURE - name too long */

	if (EMsg != NULL)
		snprintf(EMsg,1024,"hostname (%.32s...) is too long (> %d chars)",
					phe->h_name,
					bufsize);

		return(-1);
	}

	strncpy(namebuf,phe->h_name,bufsize);

	namebuf[bufsize - 1] = '\0';	/* ensure null terminated */

	for (index = 0;index < bufsize;index++) {
		if (namebuf[index] == '\0')
			break;

		namebuf[index] = tolower(namebuf[index]);
	}  /* END for (index) */
#endif

	/* SUCCESS */

	return(0);
}  /* END get_fullhostname() */

/* END get_hostname.c */

