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

#include <assert.h>
#include <float.h>
#include <stddef.h>
#include <stdlib.h>

#include "dis.h"

unsigned dis_lmx10 = 0;
dis_long_double_t *dis_lp10 = NULL;
dis_long_double_t *dis_ln10 = NULL;

/*
 *	Allocate and fill tables with all powers of 10 that fit the forms:
 *
 *				  n
 *				 2
 *		*dis_lp10[n] = 10
 *
 *				  ( n)
 *				 -(2 )
 *		*dis_ln10[n] = 10
 *
 *	For all values of n supported by the floating point format.  Set
 *	dis_lmx10 equal to the largest value of n that fits the format.
 */

void disi10l_() {
	unsigned	i;
	unsigned long	ul;
	dis_long_double_t	accum;
	size_t		tabsize;

	assert(dis_lp10 == NULL);
	assert(dis_ln10 == NULL);
	assert(dis_lmx10 == 0);

#if LDBL_MAX_10_EXP + LDBL_MIN_10_EXP > 0
	ul = LDBL_MAX_10_EXP;
#else
	ul = -LDBL_MIN_10_EXP;
#endif
	while (ul >>= 1)
		dis_lmx10++;
	tabsize = (dis_lmx10 + 1) * sizeof(dis_long_double_t);
	dis_lp10 = (dis_long_double_t *)malloc(tabsize);
	assert(dis_lp10 != NULL);
	dis_ln10 = (dis_long_double_t *)malloc(tabsize);
	assert(dis_ln10 != NULL);
	assert(dis_lmx10 > 0);
	dis_lp10[0] = accum = 10.0L;
	dis_ln10[0] = 1.0L / accum;
	for (i = 1; i <= dis_lmx10; i++) {
		accum *= accum;
		dis_lp10[i] = accum;
		dis_ln10[i] = 1.0L / accum;
	}
}
