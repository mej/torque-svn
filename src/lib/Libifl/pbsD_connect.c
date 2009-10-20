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

/* pbs_connect.c
 *
 * Open a connection with the TORQUE server.  At this point several
 * things are stubbed out, and other things are hard-wired.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <sys/param.h>
#if HAVE_SYS_UCRED_H
#include <sys/ucred.h>
#endif
#if HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include "libpbs.h"
#include "csv.h"
#include "dis.h"
#include "net_connect.h"



#define CNTRETRYDELAY 5



/* NOTE:  globals, must not impose per connection constraints */

static uid_t pbs_current_uid;               /* only one uid per requestor */

extern time_t pbs_tcp_timeout;              /* source? */

static unsigned int dflt_port = 0;

static char server_list[PBS_MAXSERVERNAME*3 + 1];
static char dflt_server[PBS_MAXSERVERNAME + 1];
static char fb_server[PBS_MAXSERVERNAME + 1];

static int got_dflt = FALSE;
static char server_name[PBS_MAXSERVERNAME + 1];  /* definite conflicts */
static unsigned int server_port;                 /* definite conflicts */
static const char *pbs_destn_file = PBS_DEFAULT_FILE;

char *pbs_server = NULL;


/**
 * Attempts to get a list of server names.  Trys first
 * to obtain the list from an envrionment variable PBS_DEFAULT.
 * If this is not set, it then trys to read the first line
 * from the file <b>server_name</b> in the <b>/var/spool/torque</b>
 * directory.
 * <p>
 * NOTE:  PBS_DEFAULT format:    <SERVER>[,<FBSERVER>]
 *        pbs_destn_file format: <SERVER>[,<FBSERVER>]
 * <p>
 * @return A pointer to the server list.
 * The side effect is
 * that the global variable <b>server_list</b> is set.  The one-shot
 * flag <b>got_dflt</b> is used to limit re-reading of the list.
 * @see pbs_default()
 * @see pbs_fbserver()
 */

char *pbs_get_server_list(void)
  {
  FILE *fd;
  char *pn;
  char *server;
  char tmp[1024];
  int len;

  if (got_dflt != TRUE)
    {
    memset(server_list, 0, sizeof(server_list));
    server = getenv("PBS_DEFAULT");

    if ((server == NULL) || (*server == '\0'))
      {
      server = getenv("PBS_SERVER");
      }

    if ((server == NULL) || (*server == '\0'))
      {
      fd = fopen(pbs_destn_file, "r");

      if (fd == NULL)
        {
        return(server_list);
        }

      if (fgets(tmp, sizeof(tmp), fd) == NULL)
        {
        fclose(fd);

        return(server_list);
        }

      strcpy(server_list, tmp);
      if ((pn = strchr(server_list, (int)'\n')))
        * pn = '\0';

      while(fgets(tmp, sizeof(tmp), fd))
        {
        strcat(server_list, ",");
        strcat(server_list, tmp);
        len = strlen(server_list);
        if(server_list[len-1] == '\n')
          {
          server_list[len-1] = '\0';
          }
        }

      fclose(fd);
      }
    else
      {
      strncpy(server_list, server, sizeof(server_list));
      }

    got_dflt = TRUE;
    }  /* END if (got_dflt != TRUE) */

  return(server_list);
  }





/**
 * The routine is called to get the name of the primary
 * server.  It can possibly trigger reading of the server name
 * list from the envrionment or the disk.
 * As a side effect, it set file local strings <b>dflt_server</b>
 * and <b>server_name</b>.  I am not sure if this is needed but
 * it seems in the spirit of the original routine.
 * @return A pointer to the default server name.
 * @see pbs_fbserver()
 */

char *pbs_default(void)
  {
  char *cp;

  pbs_get_server_list();
  server_name[0] = 0;
  cp = csv_nth(server_list, 0); /* get the first item from list */

  if (cp)
    {
    strcpy(dflt_server, cp);
    strcpy(server_name, cp);
    }

  return(server_name);
  }





/**
 * The routine is called to get the name of the fall-back
 * server.  It can possibly trigger reading of the server name
 * list from the envrionment or the disk.
 * As a side effect, it set file local strings <b>fb_server</b>
 * and <b>server_name</b>.  I am not sure if this is needed but
 * it seems in the spirit of the original routine.
 * @return A pointer to the fall-back server name.
 * @see pbs_default()
 */

char *pbs_fbserver(void)
  {
  char *cp;

  pbs_get_server_list();
  server_name[0] = 0;
  cp = csv_nth(server_list, 1); /* get the second item from list */

  if (cp)
    {
    strcpy(fb_server, cp);
    strcpy(server_name, cp);
    }

  return(server_name);
  }



static char *PBS_get_server(

  char         *server,  /* I (NULL|'\0' for not set,modified) */
  unsigned int *port)    /* O */

  {
  int   i;
  char *pc;

  for (i = 0;i < PBS_MAXSERVERNAME + 1;i++)
    {
    /* clear global server_name */

    server_name[i] = '\0';
    }

  if (dflt_port == 0)
    {
    dflt_port = get_svrport(
                  PBS_BATCH_SERVICE_NAME,
                  "tcp",
                  PBS_BATCH_SERVICE_PORT_DIS);
    }

  /* first, get the "net.address[:port]" into 'server_name' */

  if ((server == (char *)NULL) || (*server == '\0'))
    {
    if (pbs_default() == NULL)
      {
      return(NULL);
      }
    }
  else
    {
    strncpy(server_name, server, PBS_MAXSERVERNAME);
    }

  /* now parse out the parts from 'server_name' */

  if ((pc = strchr(server_name, (int)':')))
    {
    /* got a port number */

    *pc++ = '\0';

    *port = atoi(pc);
    }
  else
    {
    *port = dflt_port;
    }

  return(server_name);
  }  /* END PBS_get_server() */





/*
 * PBS_authenticate - call pbs_iff(1) to authenticate use to the PBS server.
 */

static int PBSD_authenticate(

  int psock)  /* I */

  {
  char   cmd[PBS_MAXSERVERNAME + 80];
  int    cred_type;
  int    i;
  int    j;
  FILE *piff;
  char  *ptr;

  struct stat buf;

  static char iffpath[1024];

  int    rc;

  /* use pbs_iff to authenticate me */

  if (iffpath[0] == '\0')
    {
    if ((ptr = getenv("PBSBINDIR")) != NULL)
      {
      snprintf(iffpath, sizeof(iffpath), "%s/pbs_iff",
               ptr);
      }
    else
      {
      strcpy(iffpath, IFF_PATH);
      }

    rc = stat(iffpath, &buf);

    if (rc == -1)
      {
      /* cannot locate iff in default location - search PATH */

      if ((ptr = getenv("PATH")) != NULL)
        {
        ptr = strtok(ptr, ";");

        while (ptr != NULL)
          {
          snprintf(iffpath, sizeof(iffpath), "%s/pbs_iff",
                   ptr);

          rc = stat(iffpath, &buf);

          if (rc != -1)
            break;

          ptr = strtok(NULL, ";");
          }  /* END while (ptr != NULL) */
        }    /* END if ((ptr = getenv("PATH")) != NULL) */

      if (rc == -1)
        {
        /* FAILURE */

        if (getenv("PBSDEBUG"))
          {
          fprintf(stderr, "ALERT:  cannot verify file '%s', errno=%d (%s)\n",
                  cmd,
                  errno,
                  strerror(errno));
          }

        /* cannot locate iff in default location - search PATH */

        iffpath[0] = '\0';

        return(-1);
        }
      }
    }    /* END if (iffpath[0] == '\0') */

  snprintf(cmd, sizeof(cmd), "%s %s %u %d",
           iffpath,
           server_name,
           server_port,
           psock);

  piff = popen(cmd, "r");

  if (piff == NULL)
    {
    /* FAILURE */

    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "ALERT:  cannot open pipe, errno=%d (%s)\n",
              errno,
              strerror(errno));
      }

    return(-1);
    }

  i = read(fileno(piff), &cred_type, sizeof(int));

  if ((i != sizeof(int)) || (cred_type != PBS_credentialtype_none))
    {
    /* FAILURE */

    if (getenv("PBSDEBUG"))
      {
      if (i != sizeof(int))
        {
        fprintf(stderr, "ALERT:  cannot read pipe, rc=%d, errno=%d (%s)\n",
                i,
                errno,
                strerror(errno));
        }
      else
        {
        fprintf(stderr, "ALERT:  invalid cred type %d reported\n",
                cred_type);
        }
      }

    pclose(piff);

    return(-1);
    }  /* END if ((i != sizeof(int)) || ...) */

  j = pclose(piff);

  if (j != 0)
    {
    /* FAILURE */

    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "ALERT:  cannot close pipe, errno=%d (%s)\n",
              errno,
              strerror(errno));
      }

    /* report failure but do not fail (CRI) */

    /* return(-1); */
    }

  /* SUCCESS */

  return(0);
  }  /* END PBSD_authenticate() */



#ifdef ENABLE_UNIX_SOCKETS
ssize_t    send_unix_creds(int sd)
  {

  struct iovec    vec;

  struct msghdr   msg;

  struct cmsghdr  *cmsg;
  char dummy = 'm';
  char buf[CMSG_SPACE(sizeof(struct ucred))];

  struct ucred *uptr;


  memset(&msg, 0, sizeof(msg));
  vec.iov_base = &dummy;
  vec.iov_len = 1;
  msg.msg_iov = &vec;
  msg.msg_iovlen = 1;
  msg.msg_control = buf;
  msg.msg_controllen = sizeof(buf);
  cmsg = CMSG_FIRSTHDR(&msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_CREDS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(struct ucred));
  uptr = (struct ucred *)CMSG_DATA(cmsg);
  SPC_PEER_UID(uptr) = getuid();
  SPC_PEER_GID(uptr) = getgid();
#ifdef linux
  uptr->pid = getpid();
#endif
  msg.msg_controllen = cmsg->cmsg_len;

  return (sendmsg(sd, &msg, 0) != -1);

  }

#endif /* END ENABLE_UNIX_SOCKETS */


/* returns socket descriptor or negative value (-1) on failure */

/* NOTE:  cannot use globals or static information as API
   may be used to connect a single server to multiple TORQUE
   interfaces */

/* NOTE:  0 is not a valid return value */

int pbs_original_connect(

  char *server)  /* I (FORMAT:  NULL | '\0' | HOSTNAME | HOSTNAME:PORT )*/

  {
  struct sockaddr_in server_addr;

  struct hostent *hp;
  int out;
  int i;

  struct passwd *pw;
  int use_unixsock = 0;
#ifdef ENABLE_UNIX_SOCKETS

  struct sockaddr_un unserver_addr;
  char hnamebuf[256];
#endif

  char  *ptr;

  /* reserve a connection state record */

  out = -1;

  for (i = 1;i < NCONNECTS;i++)
    {
    if (connection[i].ch_inuse)
      continue;

    out = i;

    connection[out].ch_inuse  = 1;

    connection[out].ch_errno  = 0;

    connection[out].ch_socket = -1;

    connection[out].ch_errtxt = NULL;

    break;
    }

  if (out < 0)
    {
    pbs_errno = PBSE_NOCONNECTS;

    if (getenv("PBSDEBUG"))
      fprintf(stderr, "ALERT:  cannot locate free channel\n");

    /* FAILURE */

    return(-1);
    }

  /* get server host and port */

  server = PBS_get_server(server, &server_port);

  if (server == NULL)
    {
    connection[out].ch_inuse = 0;
    pbs_errno = PBSE_NOSERVER;

    if (getenv("PBSDEBUG"))
      fprintf(stderr, "ALERT:  PBS_get_server() failed\n");

    return(-1);
    }

  /* determine who we are */

  pbs_current_uid = getuid();

  if ((pw = getpwuid(pbs_current_uid)) == NULL)
    {
    pbs_errno = PBSE_SYSTEM;

    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "ALERT:  cannot get password info for uid %ld\n",
              (long)pbs_current_uid);
      }

    return(-1);
    }

  strcpy(pbs_current_user, pw->pw_name);

  pbs_server = server;    /* set for error messages from commands */


#ifdef ENABLE_UNIX_SOCKETS
  /* determine if we want to use unix domain socket */

  if (!strcmp(server, "localhost"))
    use_unixsock = 1;
  else if ((gethostname(hnamebuf, sizeof(hnamebuf) - 1) == 0) && !strcmp(hnamebuf, server))
    use_unixsock = 1;

  /* NOTE: if any part of using unix domain sockets fails,
   * we just cleanup and try again with inet sockets */

  /* get socket */

  if (use_unixsock)
    {
    connection[out].ch_socket = socket(AF_UNIX, SOCK_STREAM, 0);

    if (connection[out].ch_socket < 0)
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot create socket:  errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      connection[out].ch_inuse = 0;

      pbs_errno = PBSE_PROTOCOL;

      use_unixsock = 0;
      }
    }

  /* and connect... */

  if (use_unixsock)
    {
    unserver_addr.sun_family = AF_UNIX;
    strcpy(unserver_addr.sun_path, TSOCK_PATH);

    if (connect(
          connection[out].ch_socket,
          (struct sockaddr *)&unserver_addr,
          (strlen(unserver_addr.sun_path) + sizeof(unserver_addr.sun_family))) < 0)
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = 0;
      pbs_errno = errno;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server, errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      use_unixsock = 0;  /* will try again with inet socket */
      }
    }

  if (use_unixsock)
    {
    if (!send_unix_creds(connection[out].ch_socket))
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot send unix creds to pbs_server:  errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      close(connection[out].ch_socket);

      connection[out].ch_inuse = 0;
      pbs_errno = PBSE_PROTOCOL;

      use_unixsock = 0;  /* will try again with inet socket */
      }
    }

#endif /* END ENABLE_UNIX_SOCKETS */

  if (!use_unixsock)
    {

    /* at this point, either using unix sockets failed, or we determined not to
     * try */

    connection[out].ch_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (connection[out].ch_socket < 0)
      {
      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server \"%s\", errno=%d (%s)\n",
                server,
                errno,
                strerror(errno));
        }

      connection[out].ch_inuse = 0;

      pbs_errno = PBSE_PROTOCOL;

      return(-1);
      }

    server_addr.sin_family = AF_INET;

    hp = NULL;
    hp = gethostbyname(server);

    if (hp == NULL)
      {
      close(connection[out].ch_socket);
      connection[out].ch_inuse = 0;
      pbs_errno = PBSE_BADHOST;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot get servername (%s) errno=%d (%s)\n",
                (server != NULL) ? server : "NULL",
                errno,
                strerror(errno));
        }

      return(-1);
      }

    memcpy((char *)&server_addr.sin_addr, hp->h_addr_list[0], hp->h_length);

    server_addr.sin_port = htons(server_port);

    if (connect(
          connection[out].ch_socket,
          (struct sockaddr *)&server_addr,
          sizeof(server_addr)) < 0)
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = 0;
      pbs_errno = errno;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot connect to server, errno=%d (%s)\n",
                errno,
                strerror(errno));
        }

      return(-1);
      }

    /* FIXME: is this necessary?  Contributed by one user that fixes a problem,
       but doesn't fix the same problem for another user! */
#if 0
#if defined(__hpux)
    /*HP-UX : avoiding socket caching */
    send(connection[out].ch_socket, '?', 1, MSG_OOB);

#endif
#endif

    /* Have pbs_iff authenticate connection */

    if ((ENABLE_TRUSTED_AUTH == FALSE) && (PBSD_authenticate(connection[out].ch_socket) != 0))
      {
      close(connection[out].ch_socket);

      connection[out].ch_inuse = 0;

      pbs_errno = PBSE_PERM;

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "ERROR:  cannot authenticate connection to server \"%s\", errno=%d (%s)\n",
                server,
                errno,
                strerror(errno));
        }

      return(-1);
      }
    } /* END if !use_unixsock */

  /* setup DIS support routines for following pbs_* calls */

  DIS_tcp_setup(connection[out].ch_socket);

  if ((ptr = getenv("PBSAPITIMEOUT")) != NULL)
    {
    pbs_tcp_timeout = strtol(ptr, NULL, 0);

    if (pbs_tcp_timeout <= 0)
      {
      pbs_tcp_timeout = 10800;      /* set for 3 hour time out */
      }
    }
  else
    {
    pbs_tcp_timeout = 10800;      /* set for 3 hour time out */
    }

  return(out);
  }  /* END pbs_original_connect() */





int pbs_disconnect(

  int connect)  /* I (socket descriptor) */

  {
  int  sock;
  static char x[THE_BUF_SIZE / 4];

  /* send close-connection message */

  sock = connection[connect].ch_socket;

  DIS_tcp_setup(sock);

  if ((tcp_encode_DIS_ReqHdr(sock, PBS_BATCH_Disconnect, pbs_current_user) == 0) &&
      (DIS_tcp_wflush(sock) == 0))
    {
    int atime;

    struct sigaction act;

    struct sigaction oldact;

    /* set alarm to break out of potentially infinite read */

    act.sa_handler = SIG_IGN;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, &oldact);

    atime = alarm(pbs_tcp_timeout);

    /* NOTE:  alarm will break out of blocking read even with sigaction ignored */

    while (1)
      {
      /* wait for server to close connection */

      /* NOTE:  if read of 'sock' is blocking, request below may hang forever */

      if (read(sock, &x, sizeof(x)) < 1)
        break;
      }

    alarm(atime);

    sigaction(SIGALRM, &oldact, NULL);
    }

  close(sock);

  if (connection[connect].ch_errtxt != (char *)NULL)
    free(connection[connect].ch_errtxt);

  connection[connect].ch_errno = 0;

  connection[connect].ch_inuse = 0;

  return(0);
  }  /* END pbs_disconnect() */





/**
 * This is a new version of this function that allows
 * connecting to a list of servers.  It is backwards
 * compatible with the previous version in that it
 * will accept a single server name.
 *
 * @param server_name_ptr A pointer to a server name or server name list.
 * @returns A file descriptor number.
 */

int pbs_connect(char *server_name_ptr)    /* I (optional) */
  {
  int connect = -1;
  int i, list_len;
  char server_name_list[PBS_MAXSERVERNAME*3+1];
  char current_name[PBS_MAXSERVERNAME+1];
  char *tp;

  memset(server_name_list, 0, sizeof(server_name_list));

  /* If a server name is passed in, use it, otherwise use the list from server_name file. */

  if (server_name_ptr && server_name_ptr[0])
    {
    strncpy(server_name_list, server_name_ptr, sizeof(server_name_list) - 1);

    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "pbs_connect called with explicit server name \"%s\"\n",
        server_name_list);
      }
    }
  else
    {
    strncpy(server_name_list, pbs_get_server_list(), sizeof(server_name_list) - 1);

    if (getenv("PBSDEBUG"))
      {
      fprintf(stderr, "pbs_connect using default server name list \"%s\"\n",
        server_name_list);
      }
    }

  list_len = csv_length(server_name_list);

  for (i = 0; i < list_len; i++)  /* Try all server names in the list. */
    {
    tp = csv_nth(server_name_list, i);

    if (tp && tp[0])
      {
      memset(current_name, 0, sizeof(current_name));
      strncpy(current_name, tp, sizeof(current_name) - 1);

      if (getenv("PBSDEBUG"))
        {
        fprintf(stderr, "pbs_connect attempting connection to server \"%s\"\n",
                current_name);
        }

      if ((connect = pbs_original_connect(current_name)) >= 0)
        {
        if (getenv("PBSDEBUG"))
          {
          fprintf(stderr, "pbs_connect: Successful connection to server \"%s\", fd = %d\n",
                  current_name, connect);
          }

        return(connect);  /* Success, we have a connection, return it. */
        }
      }
    }

  return(connect);
  }


/**
 * This routine is not used but was implemented to
 * support qsub.
 *
 * @param server_name_ptr A pointer to a server name or server name list.
 * @param retry_seconds The period of time for which retrys should be attempted.
 * @returns A file descriptor number.
 */
int pbs_connect_with_retry(char *server_name_ptr, int retry_seconds)
  {
  int n_times_to_try = retry_seconds / CNTRETRYDELAY;
  int connect = -1;
  int n;

  for (n = 0; n < n_times_to_try; n++)  /* This is the retry loop */
    {
    if ((connect = pbs_connect(server_name_ptr)) >= 0)
      return(connect);  /* Success, we have a connection, return it. */

    sleep(CNTRETRYDELAY);
    }

  return(connect);
  }


int
pbs_query_max_connections(void)

  {
  return(NCONNECTS - 1);
  }


/* END pbsD_connect.c */

