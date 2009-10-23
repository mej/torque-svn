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
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>

#if defined(FD_SET_IN_SYS_SELECT_H)
#  include <sys/select.h>
#endif

#include "dis.h"
#include "dis_.h"
#include "dis_init.h"
#include "log.h"

#ifdef HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif

#define MAX_INT_LEN 256;

static struct tcp_chan **tcparray = NULL;
static int    tcparraymax = 0;

static char *ulmax;
static unsigned ulmaxdigs = 0;


time_t pbs_tcp_timeout = 20;  /* reduced from 60 to 20 (CRI - Nov/03/2004) */



void DIS_tcp_settimeout(

  long timeout)  /* I */

  {
  pbs_tcp_timeout = timeout;

  return;
  }  /* END DIS_tcp_settimeout() */





int DIS_tcp_istimeout(

  int sock)

  {
  if (tcparray == NULL)
    {
    return(0);
    }

  return(tcparray[sock]->IsTimeout);
  }  /* END DIS_tcp_istimeout() */




/*
 * tcp_pack_buff - pack existing data into front of buffer
 *
 * Moves "uncommited" data to front of buffer and adjusts pointers.
 * Does a character by character move since data may over lap.
 */

static void tcp_pack_buff(

  struct tcpdisbuf *tp)

  {
  size_t amt;
  size_t start;
  size_t i;

  start = tp->tdis_trailp - tp->tdis_thebuf;

  if (start != 0)
    {
    amt  = tp->tdis_eod - tp->tdis_trailp;

    for (i = 0;i < amt;++i)
      {
      *(tp->tdis_thebuf + i) = *(tp->tdis_thebuf + i + start);
      }

    tp->tdis_leadp  -= start;

    tp->tdis_trailp -= start;
    tp->tdis_eod    -= start;
    }

  return;
  }  /* END tcp_pack_buff() */




/*
 * tcp_read - read data from tcp stream to "fill" the buffer
 * Update the various buffer pointers.
 *
 * Return: >0 number of characters read
 *   0 if EOD (no data currently avalable)
 *  -1 if error
 *  -2 if EOF (stream closed)
 */

static int tcp_read(

  int fd)  /* I */

  {
  int               i;
#ifdef HAVE_POLL

  struct pollfd pollset;
  int timeout;
#else
  fd_set            readset;

  struct timeval    timeout;
#endif

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->readbuf;

  /* must compact any uncommitted data into bottom of buffer */

  tcp_pack_buff(tp);

  /*
   * we don't want to be locked out by an attack on the port to
   * deny service, so we time out the read, the network had better
   * deliver promptly
   */

  tcparray[fd]->IsTimeout = 0;
  tcparray[fd]->SelectErrno = 0;
  tcparray[fd]->ReadErrno = 0;

  do
    {
#ifdef HAVE_POLL
    /* poll()'s timeout is only a signed int, must be careful not to overflow */

    if (INT_MAX / 1000 > pbs_tcp_timeout)
      timeout = pbs_tcp_timeout * 1000;
    else
      timeout = INT_MAX;

    pollset.fd = fd;

    pollset.events = POLLIN | POLLHUP;

    i = poll(&pollset, 1, timeout);

#else
    timeout.tv_sec = pbs_tcp_timeout;

    timeout.tv_usec = 0;

    FD_ZERO(&readset);

    FD_SET(fd, &readset);

    i = select(
          fd + 1,
          &readset,
          NULL,
          NULL,
          &timeout);

#endif
    }
  while ((i == -1) && (errno == EINTR));

  if (i == 0)
    {
    /* timeout has occurred */

    tcparray[fd]->IsTimeout = 1;

    return(0);
    }
  else if (i < 0)
    {
    /* select failed */

    tcparray[fd]->SelectErrno = errno;

    return(-1);
    }

  while ((i = read(
                fd,
                tp->tdis_eod,
                tp->tdis_thebuf + tp->tdis_bufsize - tp->tdis_eod)) == -1)
    {
    if (errno != EINTR)
      break;
    }

  if (i < 0)
    {
    /* FAILURE - read failed */

    tcparray[fd]->ReadErrno = errno;

    return(-1);
    }
  else if (i == 0)
    {
    /* FAILURE - no data read */

    return(-2);
    }

  /* SUCCESS */

  tp->tdis_eod += i;

  return(i);
  }  /* END tcp_read() */





/*
 * DIS_tcp_wflush - flush tcp/dis write buffer
 *
 * Writes "committed" data in buffer to file discriptor,
 * packs remaining data (if any), resets pointers
 * Returns: 0 on success, -1 on error
 *      NOTE:  does not close fd
 *
 */

int DIS_tcp_wflush(

  int fd)  /* I */

  {
  size_t ct;
  int  i;
  char *pb;

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->writebuf;
  pb = tp->tdis_thebuf;

  ct = tp->tdis_trailp - tp->tdis_thebuf;

  while ((i = write(fd, pb, ct)) != (ssize_t)ct)
    {
    if (i == -1)
      {
      if (errno == EINTR)
        {
        continue;
        }

      /* FAILURE */

      if (getenv("PBSDEBUG") != NULL)
        {
        fprintf(stderr, "TCP write of %d bytes (%.32s) failed, errno=%d (%s)\n",
                (int)ct,
                pb,
                errno,
                strerror(errno));
        }

      return(-1);
      }  /* END if (i == -1) */

    ct -= i;

    pb += i;
    }  /* END while (i) */

  /* SUCCESS */

  tp->tdis_eod = tp->tdis_leadp;

  tcp_pack_buff(tp);

  return(0);
  }  /* END DIS_tcp_wflush() */





/*
 * DIS_tcp_clear - reset tpc/dis buffer to empty
 */

static void DIS_tcp_clear(

  struct tcpdisbuf *tp)

  {
  tp->tdis_leadp  = tp->tdis_thebuf;
  tp->tdis_trailp = tp->tdis_thebuf;
  tp->tdis_eod    = tp->tdis_thebuf;

  return;
  }





void DIS_tcp_reset(

  int fd,
  int i)

  {

  struct tcp_chan *tcp;

  tcp = tcparray[fd];

  if (i == 0)
    DIS_tcp_clear(&tcp->readbuf);
  else
    DIS_tcp_clear(&tcp->writebuf);

  return;
  }  /* END DIS_tcp_reset() */





/*
 * tcp_rskip - tcp/dis support routine to skip over data in read buffer
 *
 * Returns: 0 on success, -1 on error
 */

static int tcp_rskip(

  int    fd,
  size_t ct)

  {

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->readbuf;

  if (tp->tdis_leadp - tp->tdis_eod < (ssize_t)ct)
    {
    /* this isn't the best thing to do, but this isn't used, so */

    return(-1);
    }

  tp->tdis_leadp += ct;

  return(0);
  }





/*
 * tcp_getc - tcp/dis support routine to get next character from read buffer
 *
 * Return: >0 number of characters read
 *  -1 if EOD or error
 *  -2 if EOF (stream closed)
 */

static int tcp_getc(

  int fd)

  {
  int x;

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->readbuf;

  if (tp->tdis_leadp >= tp->tdis_eod)
    {
    /* not enough data, try to get more */

    x = tcp_read(fd);

    if (x <= 0)
      {
      return((x == -2) ? -2 : -1); /* Error or EOF */
      }
    }

  return((int)*tp->tdis_leadp++);
  }  /* END tcp_getc() */





/*
 * tcp_gets - tcp/dis support routine to get a string from read buffer
 *
 * Return: >0 number of characters read
 *   0 if EOD (no data currently avalable)
 *  -1 if error
 *  -2 if EOF (stream closed)
 */

static int tcp_gets(

  int     fd,
  char   *str,
  size_t  ct)

  {
  int             x;

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->readbuf;

  while (tp->tdis_eod - tp->tdis_leadp < (ssize_t)ct)
    {
    /* not enough data, try to get more */

    x = tcp_read(fd);

    if (x <= 0)
      {
      return(x);  /* Error or EOF */
      }
    }

  memcpy((char *)str, tp->tdis_leadp, ct);

  tp->tdis_leadp += ct;

  return((int)ct);
  }  /* END tcp_gets() */




int PConnTimeout(

  int sock)  /* I */

  {
  if ((tcparray == NULL) || (tcparray[sock] == NULL))
    {
    return(0);
    }

  if (tcparray[sock]->IsTimeout == 1)
    {
    /* timeout occurred, report 'TRUE' */

    return(1);
    }

  return(0);
  }  /* END PConnTimeout() */




int TConnGetReadErrno(

  int sock)  /* I */

  {
  if ((tcparray == NULL) || (tcparray[sock] == NULL))
    {
    return(0);
    }

  return(tcparray[sock]->ReadErrno);
  }  /* END TConnGetReadErrno() */





int TConnGetSelectErrno(

  int sock)  /* I */

  {
  if ((tcparray == NULL) || (tcparray[sock] == NULL))
    {
    return(0);
    }

  return(tcparray[sock]->SelectErrno);
  }  /* END TConnGetSelectErrno() */





/*
 * tcp_puts - tcp/dis support routine to put a counted string of characters
 * into the write buffer.
 *
 * Returns: >= 0, the number of characters placed
 *   -1 if error
 */

static int tcp_puts(

  int         fd,  /* I */
  const char *str, /* I */
  size_t      ct)  /* I */

  {
#ifndef NDEBUG
  char *id = "tcp_puts";
#endif

  struct tcp_chan *tcp;
  struct tcpdisbuf *tp;
  char *temp;
  int leadpct, trailpct; 
  size_t newbufsize;

  tcp = tcparray[fd];

  /* NOTE:  currently, failures may occur if THE_BUF_SIZE is not large enough */
  /*        this should be changed to allow proper operation with degraded    */
  /*        performance (how?) */

  tp = &tcp->writebuf;
  if ((tp->tdis_thebuf + tp->tdis_bufsize - tp->tdis_leadp) < (ssize_t)ct)
    {
    /* not enough room, reallocate the buffer */
    leadpct = (int)(tp->tdis_thebuf - tp->tdis_leadp);
    trailpct = (int)(tp->tdis_thebuf - tp->tdis_trailp);
    newbufsize = tp->tdis_bufsize + THE_BUF_SIZE;
    temp = (char *)malloc(newbufsize);
    if(!temp)
      {
      /* FAILURE */

      DBPRT(("%s: error!  out of space in buffer and cannot realloc message buffer (bufsize=%ld, buflen=%d, ct=%d)\n",
             id,
             tp->tdis_bufsize,
             (int)(tp->tdis_leadp - tp->tdis_thebuf),
             (int)ct))

      return(-1);
      }

    memcpy(temp, tp->tdis_thebuf, tp->tdis_bufsize);
    free(tp->tdis_thebuf);
    tp->tdis_thebuf = temp;
    tp->tdis_bufsize = newbufsize;
    tp->tdis_leadp = tp->tdis_thebuf - leadpct;
    tp->tdis_trailp = tp->tdis_thebuf - trailpct;

    }

  memcpy(tp->tdis_leadp, (char *)str, ct);

  tp->tdis_leadp += ct;

  return(ct);
  }  /* END tcp_puts() */




/*
 * tcp_rcommit - tcp/dis support routine to commit/uncommit read data
 */

static int tcp_rcommit(

  int fd,
  int commit_flag)

  {

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->readbuf;

  if (commit_flag)
    {
    /* commit by moving trailing up */

    tp->tdis_trailp = tp->tdis_leadp;
    }
  else
    {
    /* uncommit by moving leading back */

    tp->tdis_leadp = tp->tdis_trailp;
    }

  return(0);
  }  /* END tcp_rcommit() */





/*
 * tcp_wcommit - tcp/dis support routine to commit/uncommit write data
 */

static int tcp_wcommit(

  int fd,
  int commit_flag)

  {

  struct tcpdisbuf *tp;

  tp = &tcparray[fd]->writebuf;

  if (commit_flag)
    {
    /* commit by moving trailing up */

    tp->tdis_trailp = tp->tdis_leadp;
    }
  else
    {
    /* uncommit by moving leading back */

    tp->tdis_leadp = tp->tdis_trailp;
    }

  return(0);
  }




void
DIS_tcp_funcs(void)

  {
  if (dis_getc == tcp_getc)
    {
    return;
    }

  dis_getc = tcp_getc;

  dis_puts = tcp_puts;
  dis_gets = tcp_gets;
  disr_skip = tcp_rskip;
  disr_commit = tcp_rcommit;
  disw_commit = tcp_wcommit;

  return;
  }




/*
 * DIS_tcp_setup - setup supports routines for dis, "data is strings", to
 * use tcp stream I/O.  Also initializes an array of pointers to
 * buffers and a buffer to be used for the given fd.
 * 
 * NOTE:  tmpArray is global
 *
 * NOTE:  does not return FAILURE - FIXME
 */

void DIS_tcp_setup(

  int fd)

  {

  struct tcp_chan *tcp;
  struct tcpdisbuf *tp;

  /* check for bad file descriptor */

  if (fd < 0)
    {
    return;
    }

  /* set DIS function pointers to tcp routines */

  DIS_tcp_funcs();

  if (fd >= tcparraymax)
    {
    int hold = tcparraymax;

    tcparraymax = fd + 10;

    if (tcparray == NULL)
      {
      tcparray = (struct tcp_chan **)calloc(
        tcparraymax,
        sizeof(struct tcp_chan *));

      if (tcparray == NULL)
        {
        /* FAILURE */

        log_err(errno,"DIS_tcp_setup","calloc failure");

        return;
        }
      }
    else
      {
      struct tcp_chan **tmpTA;

      tmpTA = (struct tcp_chan **)realloc(
        tcparray,
        tcparraymax * sizeof(struct tcp_chan *));

      if (tmpTA == NULL)
        {
        /* FAILURE */

        log_err(errno,"DIS_tcp_setup","realloc failure");

        return;
        }

      tcparray = tmpTA;

      memset(&tcparray[hold], '\0', (tcparraymax - hold) * sizeof(struct tcp_chan *));
      }
    }    /* END if (fd >= tcparraymax) */

  tcp = tcparray[fd];

  if (tcp == NULL)
    {
    tcparray[fd] = (struct tcp_chan *)malloc(sizeof(struct tcp_chan));

    tcp = tcparray[fd];

    if (tcp == NULL)
      {
      log_err(errno, "DIS_tcp_setup", "malloc failure");

      return;
      }

    tp = &tcp->readbuf;

    tp->tdis_thebuf = (char *)malloc(THE_BUF_SIZE);
    if(tp->tdis_thebuf == NULL)
      {
      log_err(errno,"DIS_tcp_setup","malloc failure");

      return;
      }
    tp->tdis_bufsize = THE_BUF_SIZE;

    tp = &tcp->writebuf;

    tp->tdis_thebuf = (char *)malloc(THE_BUF_SIZE);
    if(tp->tdis_thebuf == NULL)
      {
      log_err(errno,"DIS_tcp_setup","malloc failure");

      return;
      }
    tp->tdis_bufsize = THE_BUF_SIZE;


    }

  /* initialize read and write buffers */

  DIS_tcp_clear(&tcp->readbuf);

  DIS_tcp_clear(&tcp->writebuf);

  return;
  }  /* END DIS_tcp_setup() */


/* With larger systems (over 20000 cores) THE_BUF_SIZE is no longer large enough to handle
   buffer sizes needed to transfer all of the information of a job. These dis functions
   which have been shared between tcp and udp are now begin strictly set for tcp so
   the transfer buffer size can grow dynamically for huge jobs.
 
   The reason for putting these functions here is to take advantage of tcparray which is a static
   variable. We could make it public if it seems better to keep these functions with their
   udp sisters */
/*
 * Allocate and fill a counted string containing the constant, UINT_MAX,
 * expressed as character codes of decimal digits.
 *
 *      dis_umaxd = number of digits in UINT_MAX
 *
 * dis_umax[0] through dis_umax[dis_umaxd - 1] = the digits, in order.
 */

void tcp_disiui_(char *buf)
  {
  char  *cp;
  char  scratch[DIS_BUFSIZ+1];

  assert(dis_umax == NULL);
  assert(dis_umaxd == 0);

  memset(scratch, 0, DIS_BUFSIZ+1);
  cp = discui_(&scratch[DIS_BUFSIZ], UINT_MAX, &dis_umaxd);
  assert(dis_umaxd > 0);
  dis_umax = (char *)malloc(dis_umaxd);
  assert(dis_umax != NULL);
  memcpy(dis_umax, cp, dis_umaxd);
  }


int tcp_disrsi_(

  int       stream,
  int      *negate,
  unsigned *value,
  unsigned  count,
  char      *buf)

  {
  int  c;
  unsigned locval;
  unsigned ndigs;
  char  *cp;

  assert(negate != NULL);
  assert(value != NULL);
  assert(count);
  assert(stream >= 0);
  assert(dis_getc != NULL);
  assert(dis_gets != NULL);

  if (dis_umaxd == 0)
    disiui_();

  switch (c = (*dis_getc)(stream))
    {

    case '-':

    case '+':

      *negate = c == '-';

      if ((*dis_gets)(stream, buf, count) != (int)count)
        {
        return(DIS_EOD);
        }

      if (count >= dis_umaxd)
        {
        if (count > dis_umaxd)
          goto overflow;

        if (memcmp(buf, dis_umax, dis_umaxd) > 0)
          goto overflow;
        }

      cp = buf;

      locval = 0;

      do
        {
        if (((c = *cp++) < '0') || (c > '9'))
          {
          return(DIS_NONDIGIT);
          }

        locval = 10 * locval + c - '0';
        }
      while (--count);

      *value = locval;

      return (DIS_SUCCESS);

      break;

    case '0':

      return (DIS_LEADZRO);

      break;

    case '1':

    case '2':

    case '3':

    case '4':

    case '5':

    case '6':

    case '7':

    case '8':

    case '9':

      ndigs = c - '0';

      if (count > 1)
        {
        if ((*dis_gets)(stream, buf + 1, count - 1) != (int)count - 1)
          {
          return(DIS_EOD);
          }

        cp = buf;

        if (count >= dis_umaxd)
          {
          if (count > dis_umaxd)
            break;

          *cp = c;

          if (memcmp(buf, dis_umax, dis_umaxd) > 0)
            break;
          }

        while (--count)
          {
          if (((c = *++cp) < '0') || (c > '9'))
            {
            return(DIS_NONDIGIT);
            }

          ndigs = 10 * ndigs + c - '0';
          }
        }    /* END if (count > 1) */

      return(tcp_disrsi_(stream, negate, value, ndigs, buf));

      /*NOTREACHED*/

      break;

    case - 1:

      return(DIS_EOD);

      /*NOTREACHED*/

      break;

    case -2:

      return(DIS_EOF);

      /*NOTREACHED*/

      break;

    default:

      return(DIS_NONDIGIT);

      /*NOTREACHED*/

      break;
    }

  *negate = FALSE;

overflow:

  *value = UINT_MAX;

  return(DIS_OVERFLOW);
  }  /* END tcp_disrsi_() */



/* END tcp_dis.c */

unsigned tcp_disrui(

  int  stream,  /* I */
  int *retval)  /* O */

  {
  int       locret;
  int       negate;
  unsigned  value;
  struct tcpdisbuf *tp;

  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;
  locret = tcp_disrsi_(stream, &negate, &value, 1, tp->tdis_thebuf);

  if (locret != DIS_SUCCESS)
    {
    value = 0;
    }
  else if (negate)
    {
    value = 0;

    locret = DIS_BADSIGN;
    }

  *retval = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

            DIS_NOCOMMIT : locret;

  return(value);
  }
/* end of tcp_disrui */


int tcp_disrsi(

  int  stream,
  int *retval)

  {
  int  locret;
  int  negate;
  int  value;
  unsigned uvalue;
  struct tcpdisbuf *tp;

  assert(retval != NULL);
  assert(disr_commit != NULL);

  value = 0;

  tp = &tcparray[stream]->readbuf;

  switch (locret = tcp_disrsi_(stream, &negate, &uvalue, 1, tp->tdis_thebuf))
    {

    case DIS_SUCCESS:

      if (negate ? uvalue <= (unsigned) - (INT_MIN + 1) + 1 : uvalue <= (unsigned)INT_MAX)
        {
        value = negate ? -uvalue : uvalue;

        break;
        }
      else
        {
        locret = DIS_OVERFLOW;
        }

    case DIS_OVERFLOW:

      value = negate ? INT_MIN : INT_MAX;
    }

  *retval = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

            DIS_NOCOMMIT : locret;

  return(value);
  } /* end tcp_disrsi */


int tcp_disrfst(

  int     stream,
  size_t  achars,
  char   *value)

  {
  int  locret;
  struct tcpdisbuf *tp;
  /*int           rc;*/

  int  negate;
  unsigned count;

  assert(value != NULL);
  assert(dis_gets != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  locret = tcp_disrsi_(stream, &negate, &count, 1, tp->tdis_thebuf);

  if (locret == DIS_SUCCESS)
    {
    if (negate)
      locret = DIS_BADSIGN;
    else if (count > achars)
      locret = DIS_OVERFLOW;
    else if ((*dis_gets)(stream, value, (size_t)count) != (int)count)
      locret = DIS_PROTO;

#ifndef NDEBUG
    else if (memchr(value, 0, (size_t)count))
      locret = DIS_NULLSTR;

#endif
    else
      value[count] = '\0';
    }

  /*
    if (locret == DIS_SUCCESS)
      rc = (*disr_commit)(stream,TRUE);
    else
      rc = (*disr_commit)(stream,FALSE);

    locret = (rc != 0) ? rc : DIS_NOCOMMIT;
  */

  locret = (*disr_commit)(stream, locret == DIS_SUCCESS) ?
           DIS_NOCOMMIT :
           locret;

  if (locret != DIS_SUCCESS)
    *value = '\0';

  return(locret);
  }  /* END tcp_disrfst() */

char *tcp_disrcs(int stream, size_t *nchars, int *retval)
  {
  int  locret;
  int  negate;
  unsigned count = 0;
  char  *value = NULL;
  struct tcpdisbuf *tp;

  assert(nchars != NULL);
  assert(retval != NULL);
  assert(dis_gets != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  locret = tcp_disrsi_(stream, &negate, &count, 1, tp->tdis_thebuf);
  locret = negate ? DIS_BADSIGN : locret;

  if (locret == DIS_SUCCESS)
    {
    if (negate)
      locret = DIS_BADSIGN;
    else
      {
      value = (char *)malloc((size_t)count + 1);

      if (value == NULL)
        locret = DIS_NOMALLOC;
      else
        {
        if ((*dis_gets)(stream, value,
                        (size_t)count) != (int)count)
          locret = DIS_PROTO;
        else
          value[count] = '\0';
        }
      }
    }

  locret = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

           DIS_NOCOMMIT : locret;

  if ((*retval = locret) != DIS_SUCCESS && value != NULL)
    {
    count = 0;
    free(value);
    value = NULL;
    }

  *nchars = count;

  return (value);
  } /* end tcp_disrcs */

char *tcp_disrst(

  int  stream,
  int *retval)

  {
  int        locret;
  int        negate;
  unsigned int count;

  char *value = NULL;
  struct tcpdisbuf *tp;

  assert(retval != NULL);
  assert(dis_gets != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  locret = tcp_disrsi_(stream, &negate, &count, 1, tp->tdis_thebuf);

  if (locret == DIS_SUCCESS)
    {
    if (negate)
      {
      locret = DIS_BADSIGN;
      }
    else
      {
      value = (char *)malloc((size_t)count + 1);

      if (value == NULL)
        {
        locret = DIS_NOMALLOC;
        }
      else
        {
        if ((*dis_gets)(stream, value, (size_t)count) != (int)count)
          locret = DIS_PROTO;

#ifndef NDEBUG
        else if (memchr(value, 0, (size_t)count))
          locret = DIS_NULLSTR;

#endif
        else
          value[count] = '\0';
        }
      }
    }

  locret = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

           DIS_NOCOMMIT : locret;

  *retval = locret;

  if ((locret != DIS_SUCCESS) && (value != NULL))
    {
    free(value);

    value = NULL;
    }

  return(value);
  }  /* END tcp_disrst() */

int tcp_disrsl_(

  int   stream,
  int  *negate,
  unsigned long *value,
  unsigned long  count,
  char *buf)
  {
  int  c;
  unsigned long locval;
  unsigned long ndigs;
  char  *cp;
  struct tcpdisbuf *tp;

  assert(negate != NULL);
  assert(value != NULL);
  assert(count);
  assert(stream >= 0);
  assert(dis_getc != NULL);
  assert(dis_gets != NULL);

  tp = &tcparray[stream]->readbuf;

  if (ulmaxdigs == 0)
    {
    cp = discul_(tp->tdis_thebuf + DIS_BUFSIZ, ULONG_MAX, &ulmaxdigs);

    ulmax = (char *)malloc(ulmaxdigs);

    if (ulmax == NULL)
      {
      return(DIS_NOMALLOC);
      }

    memcpy(ulmax, cp, ulmaxdigs);

    if (dis_umaxd == 0)
      tcp_disiui_(tp->tdis_thebuf);
    }

  c = (*dis_getc)(stream);

  /* FORMAT:  +2+1+0+0+64+2079+22+251175826.teva.westgrid.ubc2+362+21+8Job_Name+02+11run32_.2557+02+ ... */

  switch (c)
    {

    case '-':

    case '+':

      *negate = (c == '-');

      if ((*dis_gets)(stream, buf, count) != (int)count)
        {
        return(DIS_EOD);
        }

      if (count >= ulmaxdigs)
        {
        if (count > ulmaxdigs)
          goto overflow;

        if (memcmp(buf, ulmax, ulmaxdigs) > 0)
          goto overflow;
        }

      cp = buf;

      locval = 0;

      do
        {
        if ((c = *cp++) < '0' || c > '9')
          {
          return(DIS_NONDIGIT);
          }

        locval = 10 * locval + c - '0';
        }
      while (--count);

      *value = locval;

      return(DIS_SUCCESS);

      /*NOTREACHED*/

      break;

    case '0':

      return(DIS_LEADZRO);

      /*NOTREACHED*/

      break;

    case '1':

    case '2':

    case '3':

    case '4':

    case '5':

    case '6':

    case '7':

    case '8':

    case '9':

      ndigs = c - '0';

      if (count > 1)
        {
        if ((*dis_gets)(stream, buf + 1, count - 1) != (int)count - 1)
          {
          /* FAILURE */

          return(DIS_EOD);
          }

        cp = buf;

        if (count >= ulmaxdigs)
          {
          if (count > ulmaxdigs)
            break;

          *cp = c;

          if (memcmp(buf, ulmax, ulmaxdigs) > 0)
            break;
          }

        while (--count)
          {
          if (((c = *++cp) < '0') || (c > '9'))
            {
            /* FAILURE */

            return(DIS_NONDIGIT);
            }

          ndigs = 10 * ndigs + c - '0';
          }
        }

      return(tcp_disrsl_(stream, negate, value, ndigs, buf));

      /*NOTREACHED*/

      break;

    case - 1:

      /* FAILURE */

      return(DIS_EOD);

      /*NOTREACHED*/

      break;

    case -2:

      /* FAILURE */

      return(DIS_EOF);

      /*NOTREACHED*/

      break;

    default:

      /* FAILURE */

      return(DIS_NONDIGIT);

      /*NOTREACHED*/

      break;
    }  /* END switch (c) */

  *negate = FALSE;

overflow:

  *value = ULONG_MAX;

  return(DIS_OVERFLOW);
  }  /* END tcp_disrsl_() */

long tcp_disrsl(

  int  stream,
  int *retval)

  {
  int  locret;
  int  negate;
  long  value;
  unsigned long uvalue;
  struct tcpdisbuf *tp;

  assert(retval != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  value = 0;

  switch (locret = tcp_disrsl_(stream, &negate, &uvalue, 1, tp->tdis_thebuf))
    {

    case DIS_SUCCESS:

      if (negate ? uvalue <= (unsigned long) - (LONG_MIN + 1) + 1 :
          uvalue <= LONG_MAX)
        {
        value = negate ? -uvalue : uvalue;
        break;
        }
      else
        locret = DIS_OVERFLOW;

    case DIS_OVERFLOW:
      value = negate ? LONG_MIN : LONG_MAX;
    }

  *retval = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

            DIS_NOCOMMIT : locret;

  return (value);
  } /* end tcp_disrsl */

int tcp_disrfcs(int stream, size_t  *nchars, size_t  achars, char  *value)
  {
  int  locret;
  int  negate;
  unsigned count = 0;
  struct tcpdisbuf *tp;

  assert(nchars != NULL);
  assert(value != NULL);
  assert(dis_gets != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  locret = tcp_disrsi_(stream, &negate, &count, 1, tp->tdis_thebuf);

  if (locret == DIS_SUCCESS)
    {
    if (negate)
      locret = DIS_BADSIGN;
    else if ((*nchars = count) > achars)
      locret = DIS_OVERFLOW;
    else if ((*dis_gets)(stream, value, *nchars) != (int)*nchars)
      locret = DIS_PROTO;
    }

  locret = (*disr_commit)(stream, locret == DIS_SUCCESS) ?

           DIS_NOCOMMIT : locret;

  if (locret != DIS_SUCCESS)
    *nchars = 0;

  return (locret);
  } /* end tcp_disrfcs */

unsigned char tcp_disruc(int stream, int *retval)
  {
  int  locret;
  int  negate;
  unsigned value;
  struct tcpdisbuf *tp;

  assert(retval != NULL);
  assert(disr_commit != NULL);

  tp = &tcparray[stream]->readbuf;

  locret = tcp_disrsi_(stream, &negate, &value, 1, tp->tdis_thebuf);

  if (locret != DIS_SUCCESS)
    {
    value = 0;
    }
  else if (negate)
    {
    value = 0;
    locret = DIS_BADSIGN;
    }
  else if (value > UCHAR_MAX)
    {
    value = UCHAR_MAX;
    locret = DIS_OVERFLOW;
    }

  *retval = ((*disr_commit)(stream, locret == DIS_SUCCESS) < 0) ?

            DIS_NOCOMMIT : locret;
  return ((unsigned char)value);
  } /* end tcp_disruc */

/* start of the write dis functions */

int tcp_diswui_(int stream, unsigned value, char *buf)
  {
  unsigned ndigs;
  char  *cp;
  char  scratch[DIS_BUFSIZ+1];

  assert(stream >= 0);
  assert(dis_puts != NULL);

  memset(scratch, 0, DIS_BUFSIZ+1);

  cp = discui_(&scratch[DIS_BUFSIZ], value, &ndigs);
  *--cp = '+';

  while (ndigs > 1)
    cp = discui_(cp, ndigs, &ndigs);

  if ((*dis_puts)(stream, cp, strlen(cp)) < 0)
    return(DIS_PROTO);

  return (DIS_SUCCESS);
  }


int tcp_diswcs(

  int          stream,
  const char  *value,
  size_t       nchars)

  {
  int retval;
  struct tcpdisbuf *tp;

  assert(disw_commit != NULL);
  assert(dis_puts != NULL);

  tp = &tcparray[stream]->writebuf;

  retval = tcp_diswui_(stream, (unsigned)nchars, tp->tdis_thebuf);

  if ((retval == DIS_SUCCESS) &&
      (nchars > 0) &&
      ((*dis_puts)(stream, value, nchars) != (int)nchars))
    {
    retval = DIS_PROTO;
    }

  return (((*disw_commit)(stream, retval == DIS_SUCCESS) < 0) ?  DIS_NOCOMMIT : retval);
  }

int tcp_diswul(

  int           stream,  /* I */
  unsigned long value)   /* I */

  {
  int  retval;
  unsigned ndigs;
  char         *cp;
  struct tcpdisbuf *tp;
  char  scratch[DIS_BUFSIZ+1];

  int           rc;

  assert(stream >= 0);
  assert(dis_puts != NULL);
  assert(disw_commit != NULL);

  memset(scratch, 0, DIS_BUFSIZ+1);
  tp = &tcparray[stream]->writebuf;

  cp = discul_(&scratch[DIS_BUFSIZ], value, &ndigs);

  *--cp = '+';

  while (ndigs > 1)
    cp = discui_(cp, ndigs, &ndigs);

  retval = (*dis_puts)(stream, cp, strlen(cp)) < 0 ?
           DIS_PROTO :
           DIS_SUCCESS;

  rc = (*disw_commit)(stream, retval == DIS_SUCCESS);

  if (rc < 0)
    {
    return(DIS_NOCOMMIT);
    }

  return(retval);
  }  /* END tcp_diswul() */

int tcp_diswsi(

  int stream,
  int value)

  {
  int       retval;
  unsigned  ndigs;
  unsigned  uval;
  char      c;
  char     *cp;
  char      scratch[DIS_BUFSIZ+1];

  int       rc;

  struct tcpdisbuf *tp;

  assert(stream >= 0);
  assert(dis_puts != NULL);
  assert(disw_commit != NULL);

  memset(scratch, 0, DIS_BUFSIZ+1);
  tp = &tcparray[stream]->writebuf;

  if (value < 0)
    {
    uval = (unsigned) - (value + 1) + 1;

    c = '-';
    }
  else
    {
    uval = value;

    c = '+';
    }

  cp = discui_(&scratch[DIS_BUFSIZ], uval, &ndigs);

  *--cp = c;

  while (ndigs > 1)
    cp = discui_(cp, ndigs, &ndigs);

  retval = (*dis_puts)(
             stream,
             cp,
             strlen(cp)) < 0 ?  DIS_PROTO : DIS_SUCCESS;

  /* disw_commit -> rpp_wcommit */

  rc = ((*disw_commit)(stream, retval == DIS_SUCCESS) < 0) ?
       DIS_NOCOMMIT : retval;

  return(rc);
  } /* end tcp_diswsi */

int tcp_diswsl(

  int  stream,
  long value)

  {
  int  retval;
  unsigned ndigs;
  unsigned long ulval;
  char  c;
  char  *cp;
  struct tcpdisbuf *tp;
  char  scratch[DIS_BUFSIZ+1];

  assert(stream >= 0);
  assert(dis_puts != NULL);
  assert(disw_commit != NULL);

  memset(scratch, 0, DIS_BUFSIZ+1);
  tp = &tcparray[stream]->writebuf;

  if (value < 0)
    {
    ulval = (unsigned long) - (value + 1) + 1;
    c = '-';
    }
  else
    {
    ulval = value;
    c = '+';
    }

  cp = discul_(&scratch[DIS_BUFSIZ], ulval, &ndigs);

  *--cp = c;

  while (ndigs > 1)
    cp = discui_(cp, ndigs, &ndigs);

  retval = (*dis_puts)(stream, cp,
                       strlen(cp)) < 0 ?
           DIS_PROTO : DIS_SUCCESS;

  return (((*disw_commit)(stream, retval == DIS_SUCCESS) < 0) ?
          DIS_NOCOMMIT : retval);
  } /* end tcp_diswsl */


int tcp_diswui(int stream, unsigned value)
  {
  int  retval;
  struct tcpdisbuf *tp;

  assert(disw_commit != NULL);

  tp = &tcparray[stream]->writebuf;

  retval = tcp_diswui_(stream, value, tp->tdis_thebuf);

  return (((*disw_commit)(stream, retval == DIS_SUCCESS) < 0) ?
          DIS_NOCOMMIT : retval);
  } /* end tcp_diswui */

