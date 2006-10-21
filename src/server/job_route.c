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
 * job_route.c - functions to route a job to another queue
 *
 * Included functions are:
 *
 *	job_route() - attempt to route a job to a new destination.
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "libpbs.h"
#include "pbs_error.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "work_task.h"
#include "server.h"
#include "log.h"
#include "queue.h"
#include "job.h"
#include "credential.h"
#include "batch_request.h"
#if __STDC__ != 1
#include <memory.h>
#endif

/* External functions called */
extern int svr_movejob A_((job *jobp, char *destination, struct batch_request  *req));

/* Local Functions */

int  job_route A_((job *));
void queue_route A_((pbs_queue *));

/* Global Data */

extern char	*msg_badstate;
extern char	*msg_routexceed;
extern char	*msg_routebad;
extern char	*msg_err_malloc;
extern time_t	 time_now;

/*
 * Add an entry to the list of bad destinations for a job.
 *
 *	Return: pointer to the new entry if it is added, NULL if not.
 */

void add_dest(jobp)
	job	*jobp;
{
	char	*id = "add_dest";
	badplace	*bp;
	char	*baddest = jobp->ji_qs.ji_destin;

	bp = (badplace *)malloc(sizeof (badplace));
        if (bp == (badplace *)0) {
                log_err(errno, id, msg_err_malloc);
                return;
        }
	CLEAR_LINK(bp->bp_link);

	strcpy(bp->bp_dest, baddest);
	
	append_link(&jobp->ji_rejectdest, &bp->bp_link, bp);
	return;
}

/*
 * Check the job for a match of dest in the list of rejected destinations.
 *
 *	Return: pointer if found, NULL if not.
 */

badplace * is_bad_dest(jobp, dest)
	job	*jobp;
	char	*dest;
{
	badplace	*bp;

	bp = (badplace *)GET_NEXT(jobp->ji_rejectdest);
	while (bp) {
		if (strcmp(bp->bp_dest, dest) == 0)
			break;
		bp = (badplace *)GET_NEXT(bp->bp_link);
	}
	return( bp );
}


/*
 * default_router - basic function for "routing" jobs.
 *	Does a round-robin attempt on the destinations as listed,
 *	job goes to first destination that takes it.
 *
 *	If no destination will accept the job, PBSE_ROUTEREJ is returned,
 *	otherwise 0 is returned.
 */

int default_router(jobp, qp, retry_time)
	job		 *jobp;
	struct pbs_queue *qp;
	long		  retry_time;
{
	struct array_strings *dest_attr = NULL;
	char		     *destination;
	int		      last;

	if (qp->qu_attr[(int)QR_ATR_RouteDestin].at_flags & ATR_VFLAG_SET) {
		dest_attr = qp->qu_attr[(int)QR_ATR_RouteDestin].at_val.at_arst;
		last = dest_attr->as_usedptr;
	} else
		last = 0;


	/* loop through all possible destinations */

	jobp->ji_retryok = 0;
	while (1) {
		if (jobp->ji_lastdest >= last) {
			jobp->ji_lastdest = 0;	/* have tried all */
			if (jobp->ji_retryok == 0) {
				log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, 
					  jobp->ji_qs.ji_jobid, msg_routebad);
				return (PBSE_ROUTEREJ);
			} else {
			
				/* set time to retry job */
				jobp->ji_qs.ji_un.ji_routet.ji_rteretry = retry_time;
				return (0);
			}
		}

		destination = dest_attr->as_string[jobp->ji_lastdest++];

		if (is_bad_dest(jobp, destination))
			continue;

		switch (svr_movejob(jobp, destination, NULL)) {

		    case -1:		/* permanent failure */
			add_dest(jobp);
			break;

		    case 0:		/* worked */
		    case 2:		/* deferred */
			return (0);

		    case 1:		/* failed, but try destination again */
			jobp->ji_retryok = 1;
			break;
		}
	}
}

/*
 * job_route - route a job to another queue
 *
 * This is only called for jobs in a routing queue.
 * Loop over all the possible destinations for the route queue.
 * Check each one to see if it is ok to try it.  It could have been
 * tried before and returned a rejection.  If so, skip to the next
 * destination.  If it is ok to try it, look to see if it is a local
 * queue.  If so, it is an internal procedure to try/do the move.
 * If not, a child process is created to deal with it in the
 * function net_route(), see svr_movejob.c.
 *
 *	Returns: 0 on success, non-zero (error number) on failure
 */

int job_route(jobp)
	job	*jobp;		/* job to route */
{
	int			 bad_state = 0;
	char			*id = "job_route";
	time_t			 life;
	struct pbs_queue	*qp;
	long			 retry_time;

	/* see if the job is able to be routed */

	switch (jobp->ji_qs.ji_state) {

	    case JOB_STATE_TRANSIT:
		return (0);		/* already going, ignore it */

	    case JOB_STATE_QUEUED:
		break;			/* ok to try */

	    case JOB_STATE_HELD:
		bad_state = !jobp->ji_qhdr->qu_attr[QR_ATR_RouteHeld].at_val.at_long;
		break;

	    case JOB_STATE_WAITING:
		bad_state = !jobp->ji_qhdr->qu_attr[QR_ATR_RouteWaiting].at_val.at_long;
		break;

	    default:
		(void)sprintf(log_buffer, "%s %d", msg_badstate,
						   jobp->ji_qs.ji_state);
		(void)strcat(log_buffer, id);
		log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_JOB, 
			  jobp->ji_qs.ji_jobid, log_buffer);
		return (0);
	}

	/* check the queue limits, can we route any (more) */

	qp = jobp->ji_qhdr;
	if (qp->qu_attr[(int)QA_ATR_Started].at_val.at_long == 0)
		return (0);	/* queue not started - no routing */

	if ((qp->qu_attr[(int)QA_ATR_MaxRun].at_flags & ATR_VFLAG_SET) &&
	    (qp->qu_attr[(int)QA_ATR_MaxRun].at_val.at_long <=
					qp->qu_njstate[JOB_STATE_TRANSIT]))
			return(0);	/* max number of jobs being routed */

	/* what is the retry time and life time of a job in this queue */

	if (qp->qu_attr[(int)QR_ATR_RouteRetryTime].at_flags & ATR_VFLAG_SET)
		retry_time = (long)time_now +
	 	         qp->qu_attr[(int)QR_ATR_RouteRetryTime].at_val.at_long;
	else
		retry_time = (long)time_now + PBS_NET_RETRY_TIME;
		
	if (qp->qu_attr[(int)QR_ATR_RouteLifeTime].at_flags & ATR_VFLAG_SET)
		
		life = jobp->ji_qs.ji_un.ji_routet.ji_quetime + 
			 qp->qu_attr[(int)QR_ATR_RouteLifeTime].at_val.at_long;
	else
		life = 0;	/* forever */

	if (life && (life < time_now)) {
		log_event(PBSEVENT_JOB, PBS_EVENTCLASS_JOB, 
			  jobp->ji_qs.ji_jobid, msg_routexceed);
		return (PBSE_ROUTEEXPD);   /* job too long in queue */
	}

	if (bad_state) 	/* not currently routing this job */
		return (0);		   /* else ignore this job */

	if (qp->qu_attr[(int)QR_ATR_AltRouter].at_val.at_long == 0)
		return ( default_router(jobp, qp, retry_time) );
	else
		return ( site_alt_router(jobp, qp, retry_time) );
}


/*
 * queue_route - route any "ready" jobs in a specific queue
 *
 *	look for any job in the queue whose route retry time has
 *	passed.

 *	If the queue is "started" and if the number of jobs in the
 *	Transiting state is less than the max_running limit, then
 *	attempt to route it.
 */

void queue_route(

  pbs_queue *pque)

  {
  job *nxjb;
  job *pjob;
  int  rc;

  pjob = (job *)GET_NEXT(pque->qu_jobs);

  while (pjob != NULL) 
    {
    nxjb = (job *)GET_NEXT(pjob->ji_jobque);

    if (pjob->ji_qs.ji_un.ji_routet.ji_rteretry <= time_now) 
      {
      if ((rc = job_route(pjob)) == PBSE_ROUTEREJ)
        job_abt(&pjob,msg_routebad);
      else if (rc == PBSE_ROUTEEXPD)
        job_abt(&pjob,msg_routexceed);
      }

    pjob = nxjb;
    }

  return;
  }


