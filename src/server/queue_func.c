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
 * queue_func.c - various functions dealing with queues
 *
 * Included functions are:
 * que_alloc() - allocacte and initialize space for queue structure
 * que_free() - free queue structure
 * que_purge() - remove queue from server
 * find_queuebyname() - find a queue with a given name
 * get_dfltque() - get default queue
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include <memory.h>
#include <stdlib.h>
#include "pbs_ifl.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "list_link.h"
#include "log.h"
#include "attribute.h"
#include "server_limits.h"
#include "server.h"
#include "queue.h"
#include "pbs_job.h"
#include "pbs_error.h"
#if __STDC__ != 1
#include <memory.h>
#endif
#include <pthread.h>

/* Global Data */

extern char     *msg_err_unlink;
extern char *path_queues;

extern struct    server server;
extern all_queues svr_queues;

/*
 * que_alloc - allocate space for a queue structure and initialize
 * attributes to "unset"
 *
 * Returns: pointer to structure or null is space not available.
 */

pbs_queue *que_alloc(

  char *name)

  {
  static char *id = "que_alloc";
  static char *mem_err = "no memory";

  int        i;
  pbs_queue *pq;

  pq = (pbs_queue *)malloc(sizeof(pbs_queue));

  if (pq == NULL)
    {
    log_err(errno, id, mem_err);

    return(NULL);
    }

  memset((char *)pq, (int)0, (size_t)sizeof(pbs_queue));

  pq->qu_qs.qu_type = QTYPE_Unset;

  pq->qu_mutex = malloc(sizeof(pthread_mutex_t));
  pq->qu_jobs = malloc(sizeof(struct all_jobs));
  pq->qu_jobs_array_sum = malloc(sizeof(struct all_jobs));
  
  if ((pq->qu_mutex == NULL) ||
      (pq->qu_jobs == NULL) ||
      (pq->qu_jobs_array_sum == NULL))
    {
    log_err(ENOMEM,id,mem_err);
    return(NULL);
    }

  initialize_all_jobs_array(pq->qu_jobs);
  initialize_all_jobs_array(pq->qu_jobs_array_sum);
  pthread_mutex_init(pq->qu_mutex,NULL);
  pthread_mutex_lock(pq->qu_mutex);

  strncpy(pq->qu_qs.qu_name, name, PBS_MAXQUEUENAME);

  insert_queue(&svr_queues,pq);
    
  server.sv_qs.sv_numque++;

  /* set the working attributes to "unspecified" */

  for (i = 0; i < QA_ATR_LAST; i++)
    {
    clear_attr(&pq->qu_attr[i], &que_attr_def[i]);
    }

  return(pq);
  }  /* END que_alloc() */




/*
 * que_free - free queue structure and its various sub-structures
 */

void que_free(

  pbs_queue *pq)

  {
  int   i;
  attribute *pattr;
  attribute_def *pdef;

  /* remove any malloc working attribute space */

  for (i = 0;i < QA_ATR_LAST;i++)
    {
    pdef  = &que_attr_def[i];
    pattr = &pq->qu_attr[i];

    pdef->at_free(pattr);

    /* remove any acl lists associated with the queue */

    if (pdef->at_type == ATR_TYPE_ACL)
      {
      pattr->at_flags |= ATR_VFLAG_MODIFY;

      save_acl(pattr, pdef, pdef->at_name, pq->qu_qs.qu_name);
      }
    }

  /* now free the main structure */
  pthread_mutex_lock(server.sv_qs_mutex);

  server.sv_qs.sv_numque--;
  
  pthread_mutex_unlock(server.sv_qs_mutex);

  remove_queue(&svr_queues,pq);

  free((char *)pq);

  return;
  }  /* END que_free() */




/*
 * que_purge - purge queue from system
 *
 * The queue is dequeued, the queue file is unlinked.
 * If the queue contains any jobs, the purge is not allowed.
 */

int que_purge(

  pbs_queue *pque)

  {
  char     namebuf[MAXPATHLEN];
  char     log_buf[LOCAL_LOG_BUF_SIZE];

  if (pque->qu_numjobs != 0)
    {
    return(PBSE_QUEBUSY);
    }

  strcpy(namebuf, path_queues); /* delete queue file */

  strcat(namebuf, pque->qu_qs.qu_name);

  if (unlink(namebuf) < 0)
    {
    sprintf(log_buf, msg_err_unlink, "Queue", namebuf);

    log_err(errno, "queue_purge", log_buf);
    }

  que_free(pque);

  return(0);
  }





/*
 * find_queuebyname() - find a queue by its name
 */

pbs_queue *find_queuebyname(

  char *quename) /* I */

  {
  char  *pc;
  pbs_queue *pque = NULL;
  char   qname[PBS_MAXDEST + 1];
  int    i;

  strncpy(qname, quename, PBS_MAXDEST);

  qname[PBS_MAXDEST] = '\0';

  pc = strchr(qname, (int)'@'); /* strip off server (fragment) */

  if (pc != NULL)
    *pc = '\0';

  pthread_mutex_lock(svr_queues.allques_mutex);

  i = get_value_hash(svr_queues.ht,qname);

  if (i >= 0)
    {
    pque = svr_queues.ra->slots[i].item;

    pthread_mutex_lock(pque->qu_mutex);
    }

  pthread_mutex_unlock(svr_queues.allques_mutex);

  if (pc != NULL)
    *pc = '@'; /* restore '@' server portion */

  return(pque);
  }  /* END find_queuebyname() */





/*
 * get_dfltque - get the default queue (if declared)
 */

pbs_queue *get_dfltque(void)

  {
  pbs_queue *pq = NULL;

  if (server.sv_attr[SRV_ATR_dflt_que].at_flags & ATR_VFLAG_SET)
    {
    pq = find_queuebyname(server.sv_attr[SRV_ATR_dflt_que].at_val.at_str);
    }

  return(pq);
  }  /* END get_dfltque() */




void initialize_allques_array(

  all_queues *aq)

  {
  aq->ra = initialize_resizable_array(INITIAL_QUEUE_SIZE);
  aq->ht = create_hash(INITIAL_HASH_SIZE);

  aq->allques_mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(aq->allques_mutex,NULL);
  } /* END initialize_all_ques_array() */




int insert_queue(

  all_queues *aq,
  pbs_queue  *pque)

  {
  static char *id = "insert_queue";
  int          rc;

  pthread_mutex_lock(aq->allques_mutex);

  if ((rc = insert_thing(aq->ra,pque)) == -1)
    {
    rc = ENOMEM;
    log_err(rc,id,"No memory to resize the array");
    }
  else
    {
    add_hash(aq->ht,rc,pque->qu_qs.qu_name);
    rc = PBSE_NONE;
    }

  pthread_mutex_unlock(aq->allques_mutex);

  return(rc);
  } /* END insert_queue() */





int remove_queue(

  all_queues *aq,
  pbs_queue  *pque)

  {
  int rc = PBSE_NONE;
  int index;

  if (pthread_mutex_trylock(aq->allques_mutex))
    {
    pthread_mutex_unlock(pque->qu_mutex);
    pthread_mutex_lock(aq->allques_mutex);
    pthread_mutex_lock(pque->qu_mutex);
    }

  if ((index = get_value_hash(aq->ht,pque->qu_qs.qu_name)) < 0)
    rc = THING_NOT_FOUND;
  else
    {
    remove_thing_from_index(aq->ra,index);
    remove_hash(aq->ht,pque->qu_qs.qu_name);
    }

  pthread_mutex_unlock(aq->allques_mutex);

  return(rc);
  } /* END remove_queue() */





pbs_queue *next_queue(

  all_queues *aq,
  int        *iter)

  {
  pbs_queue *pque;

  pthread_mutex_lock(aq->allques_mutex);

  pque = (pbs_queue *)next_thing(aq->ra,iter);

  pthread_mutex_unlock(aq->allques_mutex);

  if (pque != NULL)
    pthread_mutex_lock(pque->qu_mutex);

  return(pque);
  } /* END next_queue() */




/* END queue_func.c */

