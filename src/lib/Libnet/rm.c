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

#if !defined(_BSD) && defined(_AIX)   /* this is needed by AIX */
#define _BSD 1
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pbs_ifl.h"
#include "pbs_error.h"
#include "net_connect.h"
#include "resmon.h"
#include "log.h"
#include "dis.h"
#include "dis_init.h"
#include "rm.h"

extern int pbs_errno;
static int full = 1;

/*
** This is the structure used to keep track of the resource
** monitor connections.  Each entry is linked into as list
** pointed to by "outs".  If len is -1, no
** request is active.  If len is -2, a request has been
** sent and is waiting to be read.  If len is > 0, the number
** indicates how much data is waiting to be sent.
*/

struct out
  {
  int stream;
  int len;

  struct out *next;
  };

#define HASHOUT 32

static struct out *outs[HASHOUT];

/*
** Create an "out" structure and put it in the hash table.
*/

static int addrm(

  int stream)  /* I */

  {

  struct out *op, **head;

  if ((op = (struct out *)calloc(1, sizeof(struct out))) == NULL)
    {
    return(errno);
    }

  head = &outs[stream % HASHOUT];

  op->stream = stream;
  op->len = -1;
  op->next = *head;
  *head = op;
  return 0;
  }




#define close_dis(x) close(x)
#define flush_dis(x) DIS_tcp_wflush(x)

char TRMEMsg[1024];  /* global rm error message */


/*
** Connects to a resource monitor and returns a file descriptor to
** talk to it.  If port is zero, use default port.
** Returns the stream handle (>i= 0) on success.
** Returns value < 0 on error.
*/

int openrm(

  char         *host,  /* I */
  unsigned int  port)  /* I (optional,0=DEFAULT) */

  {
  int                 stream;
  int                 rc;

  static unsigned int gotport = 0;

  if (port == 0)
    {
    if (gotport == 0)
      {
      gotport = get_svrport(PBS_MANAGER_SERVICE_NAME, "tcp",
                            PBS_MANAGER_SERVICE_PORT);
      }  /* END if (gotport == 0) */

    port = gotport;
    }

  if ((stream = socket(AF_INET, SOCK_STREAM, 0)) != -1)
    {
    int tryport = IPPORT_RESERVED;

    struct sockaddr_in  addr;
    struct addrinfo    *addr_info;

    if (getaddrinfo(host, NULL, NULL, &addr_info) != 0)
      {
      DBPRT(("host %s not found\n", host))

      return(ENOENT * -1);
      }

    memset(&addr, '\0', sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    while (--tryport > 0)
      {
      addr.sin_port = htons((u_short)tryport);

      rc = bind(stream, (struct sockaddr *)&addr, sizeof(addr));
      if (rc == 0)
        break;

      if ((errno == EADDRINUSE) || (errno == EADDRNOTAVAIL))
        {
        struct timespec rem;
        /* We can't get the port we want. Wait a bit and try again */
        rem.tv_sec = 0;
        rem.tv_nsec = 3000000;
        nanosleep(&rem, &rem);
        continue;
        }

      return(-1 * errno);
      }

    memset(&addr, '\0', sizeof(addr));

    addr.sin_addr = ((struct sockaddr_in *)addr_info->ai_addr)->sin_addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);

    freeaddrinfo(addr_info);

    if (connect(stream, (struct sockaddr *)&addr, sizeof(addr)) == -1)
      {
      close(stream);

      return(-1 * errno);
      }
    }    /* END if ((stream = socket(AF_INET,SOCK_STREAM,0)) != -1) */

  if (stream < 0)
    {
    return(-1 * errno);
    }

  if (addrm(stream) == -1)
    {
    close_dis(stream);

    return(-1 * errno);
    }

  return(stream);
  }  /* END openrm() */





/*
** Routine to close a connection to a resource monitor
** and free the "out" structure.
** Return 0 if all is well, -1 on error.
*/

static int delrm(

  int stream)

  {

  struct out *op, *prev = NULL;

  for (op = outs[stream % HASHOUT];op;op = op->next)
    {
    if (op->stream == stream)
      break;

    prev = op;
    }  /* END for (op) */

  if (op != NULL)
    {
    close_dis(stream);

    if (prev != NULL)
      prev->next = op->next;
    else
      outs[stream % HASHOUT] = op->next;

    free(op);

    return(0);
    }

  return(-1);
  }  /* END delrm() */




/*
** Internal routine to find the out structure for a stream number.
** Return non NULL if all is well, NULL on error.
*/

static struct out *findout(

  int *local_errno,
  int  stream)

  {

  struct out *op;

  for (op = outs[stream % HASHOUT];op;op = op->next)
    {
    if (op->stream == stream)
      break;
    }

  if (op == NULL)
    *local_errno = ENOTTY;

  return(op);
  }




static int startcom(

  int  stream,      /* I */
  int *local_errno, /* O */
  int  com)         /* I */

  {
  int ret;

  ret = diswsi(stream, RM_PROTOCOL);

  if (ret == DIS_SUCCESS)
    {
    ret = diswsi(stream, RM_PROTOCOL_VER);

    if (ret == DIS_SUCCESS)
      {
      ret = diswsi(stream, com);
      }
    }

  if (ret != DIS_SUCCESS)
    {
    /* NOTE:  cannot resolve log_err */

    /* log_err(ret,"startcom - diswsi error",(char *)dis_emsg[ret]); */

    *local_errno = errno;
    }

  return(ret);
  }  /* END startcom() */





/*
** Internal routine to compose and send a "simple" command.
** This means anything with a zero length body.
** Return 0 if all is well, -1 on error.
*/

static int simplecom(

  int  stream,
  int *local_errno,
  int  com)

  {
  struct out *op;

  if ((op = findout(local_errno, stream)) == NULL)
    {
    return(-1);
    }

  op->len = -1;

  if (startcom(stream, local_errno, com) != DIS_SUCCESS)
    {
    close_dis(stream);

    return(-1);
    }

  if (flush_dis(stream) == -1)
    {
    *local_errno = errno;

    DBPRT(("simplecom: flush error %d (%s)\n",
           *local_errno, pbs_strerror(*local_errno)))

    close_dis(stream);

    return(-1);
    }

  return(0);
  }  /* END simplecom() */




/*
** Internal routine to read the return value from a command.
** Return 0 if all is well, -1 on error.
*/

static int simpleget(

  int *local_errno,
  int  stream)

  {
  int ret, num;


  num = disrsi(stream, &ret);

  if (ret != DIS_SUCCESS)
    {
    /* NOTE:  cannot resolve log_err */

    /* log_err(ret,"simpleget",(char *)dis_emsg[ret]); */

    *local_errno = errno ? errno : EIO;

    close_dis(stream);

    return(-1);
    }

  if (num != RM_RSP_OK)
    {
#ifdef ENOMSG
    *local_errno = ENOMSG;
#else
    *local_errno = EINVAL;
#endif

    return(-1);
    }

  return(0);
  }  /* END simpleget() */





/*
** Close connection to resource monitor.  Return result 0 if
** all is ok or -1 if not (return errno).
*/

int closerm_err(

  int *local_errno,
  int  stream)

  {
  simplecom(stream, local_errno, RM_CMD_CLOSE);

  if (delrm(stream) == -1)
    {
    return(ENOTTY);
    }

  return(0);
  }  /* END closerm_err() */




int closerm(

  int stream)

  {
  return(closerm_err(&pbs_errno, stream));
  } /* END closerm() */




/*
** Shutdown the resource monitor.  Return result 0 if
** all is ok or -1 if not.
*/

int downrm(

  int *local_errno,
  int  stream)  /* I */

  {
  if (simplecom(stream, local_errno, RM_CMD_SHUTDOWN))
    {
    return(-1);
    }

  if (simpleget(local_errno, stream))
    {
    return(-1);
    }

  delrm(stream);

  return(0);
  }  /* END downrm() */




/*
** Cause the resource monitor to read the file named.
** Return the result 0 if all is ok or -1 if not.
*/

int configrm(

  int   stream,      /* I */
  int  *local_errno, /* O */
  char *file)        /* I */

  {
  int         ret;
  size_t      len;

  struct out *op;

  if ((op = findout(local_errno, stream)) == NULL)
    {
    return(-1);
    }

  op->len = -1;

  /* NOTE:  remove absolute job path check to allow config file staging (CRI) */

  /* NOTE:  remove filename size check (was 'MAXPATHLEN') */

  if ((len = strlen(file)) > (size_t)65536)
    {
    return(-1 * EINVAL);
    }

  if (startcom(stream, local_errno, RM_CMD_CONFIG) != DIS_SUCCESS)
    {
    return(-1);
    }

    ret = diswcs(stream, file, len);


  if (ret != DIS_SUCCESS)
    {
#if defined(ECOMM)
    *local_errno = ECOMM;
#elif defined(ENOCONNECT)
    *local_errno = ENOCONNECT;
#else
    *local_errno = ETXTBSY;
#endif

    DBPRT(("configrm: diswcs %s\n",
           dis_emsg[ret]))

    return(-1);
    }

  if (flush_dis(stream) == -1)
    {
    DBPRT(("configrm: flush error %d (%s)\n",
           errno, pbs_strerror(errno)))

    return(-1 * errno);
    }

  if (simpleget(local_errno, stream))
    {
    return(-1);
    }

  return(0);
  }  /* END configrm() */





/*
** Begin a new message to the resource monitor if necessary.
** Add a line to the body of an outstanding command to the resource
** monitor.
** Return the result 0 if all is ok or -1 if not.
*/

static int doreq(

  struct out *op,
  char       *line)

  {
  int ret;
  int local_errno = 0;

  if (op->len == -1)
    {
    /* start new message */

    if (startcom(op->stream, &local_errno, RM_CMD_REQUEST) != DIS_SUCCESS)
      {
      if (local_errno != 0)
        return(-1 * local_errno);
      else
        return(-1);
      }

    op->len = 1;
    }

  ret = diswcs(op->stream, line, strlen(line));


  if (ret != DIS_SUCCESS)
    {
#if defined(ECOMM)
    local_errno = ECOMM;
#elif defined(ENOCONNECT)
    local_errno = ENOCONNECT;
#else
    local_errno = ETXTBSY;
#endif

    DBPRT(("doreq: diswcs %s\n",
           dis_emsg[ret]))

    return(-1 * local_errno);
    }

  return(0);
  }  /* END doreq() */





/*
** Add a request to a single stream.
*/

int addreq_err(

  int   stream,
  int  *local_errno, 
  char *line)

  {
  struct out *op;

  if ((op = findout(local_errno, stream)) == NULL)
    {
    return(-1);
    }

  if (doreq(op, line) == -1)
    {
    delrm(stream);

    return(-1);
    }

  return(0);
  }  /* END addreq_err() */




int addreq(

  int   stream,
  char *line)

  {
  return(addreq_err(stream, &pbs_errno, line));
  } /* END addreq() */





/*
** Add a request to every stream.
** Return the number of streams acted upon.
*/

int allreq(

  char *line)

  {

  struct out *op, *prev;
  int         i, num;

  num = 0;

  for (i = 0; i < HASHOUT;i++)
    {
    prev = NULL;

    op = outs[i];

    while (op != NULL)
      {
      if (doreq(op, line) == -1)
        {

        struct out *hold = op;

        close_dis(op->stream);

        if (prev)
          prev->next = op->next;
        else
          outs[i] = op->next;

        op = op->next;

        free(hold);
        }
      else
        {
        prev = op;
        op = op->next;
        num++;
        }
      }
    }

  return(num);
  }  /* END allreq() */





/*
** Finish (and send) any outstanding message to the resource monitor.
** Return a pointer to the next response line or a NULL if
** there are no more or an error occured. 
*/

char *getreq_err(

  int *local_errno,
  int  stream)  /* I */

  {
  char *startline;

  struct out *op;
  int ret;

  if ((op = findout(local_errno, stream)) == NULL)
    {
    return(NULL);
    }

  if (op->len >= 0)
    {
    /* there is a message to send */

    if (flush_dis(stream) == -1)
      {
      DBPRT(("getreq: flush error %d (%s)\n",
             errno, pbs_strerror(errno)))

      delrm(stream);

      return(NULL);
      }

    op->len = -2;

    }

  if (op->len == -2)
    {
    if (simpleget(local_errno, stream) == -1)
      {
      return(NULL);
      }

    op->len = -1;
    }


  startline = disrst(stream, &ret);

  if (ret == DIS_EOF)
    {
    return(NULL);
    }

  if (ret != DIS_SUCCESS)
    {
    if (!errno)
      errno = EIO;

    DBPRT(("getreq: cannot read string %s\n",
           dis_emsg[ret]))

    return(NULL);
    }

  if (!full)
    {
    char *cc, *hold;
    int   indent = 0;

    for (cc = startline;*cc;cc++)
      {
      if (*cc == '[')
        indent++;
      else if (*cc == ']')
        indent--;
      else if ((*cc == '=') && (indent == 0))
        {
        hold = strdup(cc + 1);

        free(startline);

        startline = hold;

        break;
        }
      }
    }    /* END if (!full) */

  return(startline);
  }  /* END getreq_err() */




char *getreq(

  int stream)

  {
  return(getreq_err(&pbs_errno, stream));
  } /* END getreq() */




/*
** Finish and send any outstanding messages to all resource monitors.
** Return the number of messages flushed.
*/

int flushreq(void)

  {

  struct out *op, *prev;
  int         did, i;

  did = 0;

  for (i = 0;i < HASHOUT;i++)
    {
    for (op = outs[i];op != NULL;op = op->next)
      {
      if (op->len <= 0) /* no message to send */
        continue;

      if (flush_dis(op->stream) == -1)
        {
        DBPRT(("flushreq: flush error %d (%s)\n",
               errno, pbs_strerror(errno)))

        close_dis(op->stream);

        op->stream = -1;

        continue;
        }

      op->len = -2;

      did++;
      }  /* END for (op) */

    prev = NULL;

    op = outs[i];

    while (op != NULL)
      {
      /* get rid of bad streams */

      if (op->stream != -1)
        {
        prev = op;

        op = op->next;

        continue;
        }

      if (prev == NULL)
        {
        outs[i] = op->next;

        free(op);

        op = outs[i];
        }
      else
        {
        prev->next = op->next;

        free(op);

        op = prev->next;
        }
      }
    }

  return(did);
  }  /* END flushreq() */





/*
** Return the stream number of the next stream with something
** to read or a negative number (the return from rpp_poll)
** if there is no stream to read.
*/

int activereq(void)

  {
#ifndef NDEBUG
  static char id[] = "activereq";
#endif

  int            i, num;

  struct timeval tv;

  fd_set *FDSet;

  int MaxNumDescriptors = 0;

  flushreq();

  MaxNumDescriptors = get_max_num_descriptors();
  FDSet = (fd_set *)calloc(1,sizeof(char) * get_fdset_size());

  for (i = 0; i < HASHOUT; i++)
    {

    struct out *op;

    op = outs[i];

    while (op)
      {
			FD_SET(op->stream, FDSet);
      op = op->next;
      }
    }

  tv.tv_sec = 15;

  tv.tv_usec = 0;

  num = select(MaxNumDescriptors, FDSet, NULL, NULL, &tv);

  if (num == -1)
    {
    DBPRT(("%s: select %d %s\n", id, errno, pbs_strerror(errno)))
    free(FDSet);
    return -1;
    }
  else if (num == 0)
    {
    free(FDSet);
		return -2;
    }

  for (i = 0; i < HASHOUT; i++)
    {

    struct out *op;

    op = outs[i];

    while (op)
      {
			if (FD_ISSET(op->stream, FDSet))
        {
        free(FDSet);
				return op->stream;
        }

      op = op->next;
      }
    }

  free(FDSet);

  return(-2);
  }  /* END activereq() */





/*
** If flag is true, turn on "full response" mode where getreq
** returns a pointer to the beginning of a line of response.
** This makes it possible to examine the entire line rather
** than just the answer following the equal sign.
*/
void fullresp(
    
  int flag)

  {
  full = flag;

  return;
  }
