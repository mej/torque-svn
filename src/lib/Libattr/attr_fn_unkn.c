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
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"

/*
 * This file contains functions for manipulating attributes of an
 * unknown (unrecognized) name (and therefore unknown type).
 * It is a collection point for all "other" attributes, other than
 * the types with specific definition and meaning.
 *
 * Because the type is unknown, it cannot be decoded into a native
 * form.  Thus the attribute is maintained in the attrlist form.
 * Any attribute/value located here will be sent to the Scheduler and it
 * within its rules may do as it choses with them.
 *
 * The prototypes are declared in "attribute.h"
 *
 * ----------------------------------------------------------------------------
 * Attribute functions for attributes with value type "unknown"
 * ----------------------------------------------------------------------------
 */

/* External Global Items */


/* private functions */


/*
 * decode_unkn - decode a pair of strings (name and value) into the Unknown
 * type attribute/resource which is maintained as a "svrattrl", a
 * linked list of structures containing strings.
 *
 * Returns: 0 if ok,
 *  >0 error number if error,
 *  *patr members set
 */

int
decode_unkn(
  struct attribute *patr,  /* May be Modified on Return */
  char *name,
  char *rescn,
  char *value
)
  {
  svrattrl *entry;
  size_t      valln;


  if (patr == (attribute *)0)
    return (PBSE_INTERNAL);

  if (!(patr->at_flags & ATR_VFLAG_SET))
    CLEAR_HEAD(patr->at_val.at_list);

  if (name == (char *)0)
    return (PBSE_INTERNAL);

  if (value == (char *)0)
    valln = 0;
  else
    valln = strlen(value) + 1;

  entry = attrlist_create(name, rescn, valln);

  if (entry == (svrattrl *)0)
    return (PBSE_SYSTEM);

  if (valln)
    memcpy(entry->al_value, value, valln);

  append_link(&patr->at_val.at_list, &entry->al_link, entry);

  patr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY;

  return (0);
  }


/*
 * encode_unkn - encode attr of unknown type into attrlist form
 *
 * Here things are different from the typical attribute.  Most have a
 * single value to be encoded.  But "the unknown" attribute may have a whole
 * list.
 *
 * This function does not use the parent attribute name, after all "_other_"
 * is rather meaningless.  In addition, each unknown already is in an
 * attrlist form.
 *
 * Thus for each entry in the list, encode_unkn duplicates the existing
 * attrlist struct and links the copy into the list.
 *
 * Returns:>=0 if ok, the total encoded size of all resources.
 *  -1 if encoded value would not fit into buffer
 *  -2 if error
 */
/*ARGSUSED*/

int
encode_unkn(
  attribute *attr,   /* ptr to attribute to encode */
  tlist_head *phead,   /* list to place entry in */
  char *atname,  /* attribute name, not used here */
  char *rsname,  /* resource name, not used here */
  int mode   /* encode mode, unused here */
)
  {
  svrattrl *plist;
  svrattrl *pnew;

  if (!attr)
    return (-2);

  plist = (svrattrl *)GET_NEXT(attr->at_val.at_list);

  if (plist == (svrattrl *)0)
    return (0);

  while (plist != (svrattrl *)0)
    {
    pnew = (svrattrl *)malloc(plist->al_tsize);

    if (pnew == (svrattrl *)0)
      return (-1);

    CLEAR_LINK(pnew->al_link);

    pnew->al_tsize = plist->al_tsize;

    pnew->al_nameln = plist->al_nameln;

    pnew->al_rescln = plist->al_rescln;

    pnew->al_valln  = plist->al_valln;

    pnew->al_flags   = plist->al_flags;

    pnew->al_name  = (char *)pnew + sizeof(svrattrl);

    (void)memcpy(pnew->al_name, plist->al_name, plist->al_nameln);

    if (plist->al_rescln)
      {
      pnew->al_resc = pnew->al_name + pnew->al_nameln;
      (void)memcpy(pnew->al_resc, plist->al_resc,
                   plist->al_rescln);
      }
    else
      {
      pnew->al_resc = (char *)0;
      }

    if (plist->al_valln)
      {
      pnew->al_value = pnew->al_name + pnew->al_nameln +
                       pnew->al_rescln;
      (void)memcpy(pnew->al_value, plist->al_value,
                   pnew->al_valln);
      }

    append_link(phead, &pnew->al_link, pnew);

    plist = (svrattrl *)GET_NEXT(plist->al_link);
    }

  return (1);
  }



/*
 * set_unkn - set value of attribute of unknown type  to another
 *
 * Each entry in the list headed by the "new" attribute is appended
 * to the list headed by "old".
 *
 * All operations, set, incr, and decr, map to append.
 * Returns: 0 if ok
 *  >0 if error
 */

/*ARGSUSED*/
int
set_unkn(struct attribute *old, struct attribute *new, enum batch_op op)
  {
  svrattrl *plist;
  svrattrl *pnext;

  assert(old && new && (new->at_flags & ATR_VFLAG_SET));

  plist = (svrattrl *)GET_NEXT(new->at_val.at_list);

  while (plist != (svrattrl *)0)
    {
    pnext = (svrattrl *)GET_NEXT(plist->al_link);
    delete_link(&plist->al_link);
    append_link(&old->at_val.at_list, &plist->al_link, plist);
    plist = pnext;
    }

  old->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY;

  return (0);
  }

/*
 * comp_unkn - compare two attributes of type ATR_TYPE_RESR
 *
 * How do you compare something when you don't know what it is...
 * So, always returns +1
 *
 */

int
comp_unkn(struct attribute *attr, struct attribute *with)
  {
  return (1);
  }

/*
 * free_unkn - free space associated with attribute value
 *
 * For each entry in the list, it is delinked, and freed.
 */

void
free_unkn(attribute *pattr)
  {
  svrattrl *plist;

  if (pattr->at_flags & ATR_VFLAG_SET)
    {
    while ((plist = (svrattrl *)GET_NEXT(pattr->at_val.at_list)) !=
           (svrattrl *)0)
      {
      delete_link(&plist->al_link);
      (void)free(plist);
      }
    }

  CLEAR_HEAD(pattr->at_val.at_list);

  pattr->at_flags &= ~ATR_VFLAG_SET;
  }
