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
 * qenable
 *  The qenable command directs that a destination should no longer accept
 *  batch jobs.
 *
 * Synopsis:
 *  qenable destination ...
 *
 * Arguments:
 *  destination ...
 *      A list of destinations.  A destination has one of the following
 *      three forms:
 *          queue
 *          @server
 *          queue@server
 *      If queue is specified, the request is to enable the queue at
 *      the default server.  If @server is given, the request is to
 *      enable the default queue at the server.  If queue@server is
 *      used, the request is to enable the named queue at the named
 *      server.
 *
 * Written by:
 *  Bruce Kelly
 *  National Energy Research Supercomputer Center
 *  Livermore, CA
 *  May, 1993
 */

#include "cmds.h"
#include <pbs_config.h>   /* the master config generated by configure */


int exitstatus = 0; /* Exit Status */
static void execute (char *, char *);


int main(

  int    argc,
  char **argv)

  {
  /*
   *  This routine sends a Manage request to the batch server specified by
   * the destination.  The ENABLED queue attribute is set to {True}.  If the
   * batch request is accepted, the server will accept Queue Job requests for
   * the specified queue.
   */

    int dest;		/* Index into the destination array (argv) */
    char *queue;	/* Queue name part of destination */
    char *server;	/* Server name part of destination */
    
    if ( argc == 1 )
    {
        fprintf(stderr, "Usage: qenable [queue][@server] ...\n");
        exit(1);
    }
    else if ( argc > 1 && argv[1][0] == '-' )
    {
		/* make it look like some kind of help is available */
        fprintf(stderr, "Usage: qenable [queue][@server] ...\n");
        exit(1);
    }
    
    for ( dest=1; dest<argc; dest++ )
        if ( parse_destination_id(argv[dest], &queue, &server) == 0 )
		execute(queue, server);
        else {
            fprintf(stderr, "qenable: illegally formed destination: %s\n",
                    argv[dest]);
            exitstatus = 1;
        }
    exit (exitstatus);
}


/*
 * void execute( char *queue, char *server )
 *
 * queue    The name of the queue to disable.
 * server   The name of the server that manages the queue.
 *
 * Returns:
 *  None
 *
 * File Variables:
 *  exitstatus  Set to two if an error occurs.
 */

static void execute( 

  char *queue,
  char *server)

  {
  int ct;         /* Connection to the server */
  int merr;       /* Error return from pbs_manager */
  char *errmsg;   /* Error message from pbs_manager */
                    /* The disable request */

  static struct attropl attr = {NULL, "enabled", NULL, "TRUE", SET};
    
    if ( (ct = cnt2server(server)) > 0 ) {
        merr = pbs_manager(ct,MGR_CMD_SET,MGR_OBJ_QUEUE,queue,&attr,NULL);
        if ( merr != 0 ) {
            errmsg = pbs_geterrmsg(ct);
            if ( errmsg != NULL ) {
                fprintf(stderr, "qenable: %s ", errmsg);
            } else {
                fprintf(stderr, "qenable: Error (%d) enabling queue ", pbs_errno);
            }
            if ( notNULL(queue) )
                fprintf(stderr, "%s", queue);
            if ( notNULL(server) )
                fprintf(stderr, "@%s", server);
            fprintf(stderr, "\n");
            exitstatus = 2;
        }
        pbs_disconnect(ct);
    } else {
        fprintf(stderr, "qenable: could not connect to server %s (%d)\n", server, pbs_errno);
        exitstatus = 2;
    }
}
