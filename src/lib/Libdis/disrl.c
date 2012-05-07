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
 * Synopsis:
 *  long double disrl(int stream, int *retval)
 *
 * Gets a Data-is-Strings floating point number from <stream> and converts
 * it into a long double and returns it.  The number from <stream> consists
 * of two consecutive signed integers.  The first is the coefficient, with
 * its implied decimal point at the low-order end.  The second is the
 * exponent as a power of 10.
 *
 * *<retval> gets DIS_SUCCESS if everything works well.  It gets an error
 * code otherwise.  In case of an error, the <stream> character pointer is
 * reset, making it possible to retry with some other conversion strategy.
 *
 * By fiat of the author, neither loss of significance nor underflow are
 * errors.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "dis.h"
#include "lib_dis.h"
#include "rpp.h"
#include "tcp.h"

/* to work around a problem in a compiler */
#if SIZEOF_LONG_DOUBLE == SIZEOF_DOUBLE

#ifdef LDBL_MAX
#undef LDBL_MAX
#endif /* LDBL_MAX */

#define LDBL_MAX DBL_MAX

#endif /* SIZEOF_LONG_DOUBLE == SIZEOF_DOUBLE */

#if defined(__GNUC__) && defined(__DBL_MAX__)

#ifdef LDBL_MAX
#undef LDBL_MAX
#endif /* LDBL_MAX */

#define LDBL_MAX __DBL_MAX__


#ifdef HUGE_VAL
#undef HUGE_VAL
#endif
#define HUGE_VAL LDBL_MAX

#endif



dis_long_double_t disrl(

  struct tcp_chan *chan,
  int *retval)

  {
  int  expon;
  unsigned uexpon;
  int  locret;
  int  negate;
  unsigned ndigs;
  unsigned nskips;
  dis_long_double_t  ldval;

  assert(retval != NULL);

  ldval = 0.0L;
  locret = disrl_(chan, &ldval, &ndigs, &nskips, LDBL_DIG, 1);

  if (locret == DIS_SUCCESS)
    {
    locret = disrsi_(chan, &negate, &uexpon, 1);

    if (locret == DIS_SUCCESS)
      {
      expon = negate ? nskips - uexpon : nskips + uexpon;

      if (expon + (int)ndigs > LDBL_MAX_10_EXP)
        {
        if (expon + (int)ndigs > LDBL_MAX_10_EXP + 1)
          {
          ldval = ldval < 0.0L ?
                  -HUGE_VAL : HUGE_VAL;
          locret = DIS_OVERFLOW;
          }
        else
          {
          ldval *= disp10l_(expon - 1);

          if (ldval > LDBL_MAX / 10.0L)
            {
            ldval = ldval < 0.0L ?
                    -HUGE_VAL : HUGE_VAL;
            locret = DIS_OVERFLOW;
            }
          else
            ldval *= 10.0L;
          }
        }
      else
        {
        if (expon < LDBL_MIN_10_EXP)
          {
          ldval *= disp10l_(expon + (int)ndigs);
          ldval /= disp10l_((int)ndigs);
          }
        else
          ldval *= disp10l_(expon);
        }
      }
    }

  if (tcp_rcommit(chan, locret == DIS_SUCCESS) < 0)
    locret = DIS_NOCOMMIT;

  *retval = locret;

  return(ldval);
  }

