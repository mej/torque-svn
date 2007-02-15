/*
 * Copyright 1994 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (C) 2003, 2004 by the Massachusetts Institute of Technology.
 * All rights reserved.
 *
 * Export of this software from the United States of America may
 *   require a specific license from the United States Government.
 *   It is the responsibility of any person or organization contemplating
 *   export to obtain such a license before exporting.
 * 
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of M.I.T. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  Furthermore if you modify this software you must label
 * your software as modified software and not distribute it in such a
 * fashion that it might be confused with the original M.I.T. software.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 */

/* clipped largely from gss-misc.c in the krb5 distribution.
   
   The pbsgss functions work over DIS tcp sockets; they use the dis library 
   to read from the socket, but not to write.  They do flush the DIS buffer 
   before writing, though, so there shouldn't be ordering issues.

   The callers do need to ensure that DIS has been setup on the socket, eg
     DIS_tcp_setup(sock);

*/

#include <stdio.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <time.h>
#include <stdlib.h>

#include <krb5.h>

#include "portability.h"
#include "pbsgss.h"
#include "log.h"

#include "dis.h"
#include "dis_init.h"

static int retry = 0;

gss_buffer_desc empty_token_buf = { 0, (void *) "" };
gss_buffer_t empty_token = &empty_token_buf;

static void display_status_1(char *m, OM_uint32 code, int type);

int write_all(int fildes, char *buf, unsigned int nbyte) {
  int i;
  i = (*dis_puts)(fildes, buf, nbyte);
  (*disw_commit)(fildes,1);
  DIS_tcp_wflush(fildes);
  return i;
}

int write_all_orig(int fildes, char *buf, unsigned int nbyte) {
  int ret;
  char *ptr;

  for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
    ret = send(fildes, ptr, nbyte, 0);
    if (ret < 0) {
      if (errno == EINTR)
	continue;
      return(ret);
    } else if (ret == 0) {
      return(ptr-buf);
    }
  }
  return(ptr-buf);
}

static int read_all(int fildes, char *buf, unsigned int nbyte) {
  return (*dis_gets)(fildes,buf,nbyte);
}
 
static  int read_all_orig(int fildes, char *buf, unsigned int nbyte) {
  int ret;
  char *ptr;
  fd_set rfds;
  struct timeval tv;

  FD_ZERO(&rfds);
  FD_SET(fildes, &rfds);
  tv.tv_sec = 10;
  tv.tv_usec = 0;

  for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
    if (select(FD_SETSIZE, &rfds, NULL, NULL, &tv) <= 0
	|| !FD_ISSET(fildes, &rfds)) {
      return(ptr-buf);
    }
    ret = recv(fildes, ptr, nbyte, 0);
    if (ret < 0) {
      if (errno == EINTR)
	continue;
      return(ret);
    } else if (ret == 0) {
      return(ptr-buf);
    }
  }

  return(ptr-buf);
}

/*
 * Function: send_token
 *
 * Purpose: Writes a token to a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(r) the flags to write
 * 	tok		(r) the token to write
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 *
 * If the flags are non-null, send_token writes the token flags (a
 * single byte, even though they're passed in in an integer). Next,
 * the token length (as a network long) and then the token data are
 * written to the file descriptor s.  It returns 0 on success, and -1
 * if an error occurs or if it could not write all the data.
 */
int pbsgss_send_token(s, flags, tok)
     int s;
     int flags;
     gss_buffer_t tok;
{
     int ret;
     unsigned char char_flags = (unsigned char) flags;
     unsigned char lenbuf[4];

     DIS_tcp_wflush(s);

     if (char_flags) {
	 ret = write_all(s, (char *)&char_flags, 1);
	 if (ret != 1) {
	     perror("sending token flags");
	     return -1;
	 }
     }
     if (tok->length > 0xffffffffUL)
	 abort();
     lenbuf[0] = (tok->length >> 24) & 0xff;
     lenbuf[1] = (tok->length >> 16) & 0xff;
     lenbuf[2] = (tok->length >> 8) & 0xff;
     lenbuf[3] = tok->length & 0xff;

     ret = write_all(s, lenbuf, 4);
     if (ret < 0) {
	  perror("sending token length");
	  return -1;
     } 

     ret = write_all(s, tok->value, tok->length);
     if (ret < 0) {
	  perror("sending token data");
	  return -1;
     } else if (ret != tok->length) {	 
	 return -1;
     }
     return 0;
}

/*
 * Function: recv_token
 *
 * Purpose: Reads a token from a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(w) the read flags
 * 	tok		(w) the read token
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * recv_token reads the token flags (a single byte, even though
 * they're stored into an integer, then reads the token length (as a
 * network long), allocates memory to hold the data, and then reads
 * the token data from the file descriptor s.  It blocks to read the
 * length and data, if necessary.  On a successful return, the token
 * should be freed with gss_release_buffer.  It returns 0 on success,
 * and -1 if an error occurs, or -2 if it couldn't read enough data (eg, retryable)
 */
int pbsgss_recv_token(s, flags, tok)
     int s;
     int *flags;
     gss_buffer_t tok;
{
  int ret;
  unsigned char char_flags;
  unsigned char lenbuf[4];

  ret = read_all(s, (char *) &char_flags, 1);
  if (ret < 0) {
    perror("reading token flags");
    return -1;
  } else if (! ret) {
    (*disr_commit)(s,0);      
    return -2;
  } else {
    *flags = (int) char_flags;
  }

  if (char_flags == 0 ) {
    lenbuf[0] = 0;
    ret = read_all(s, &lenbuf[1], 3);
    if (ret < 0) {
      perror("reading token length");
      return -1;
    }  else if (ret != 3) {
      (*disr_commit)(s,0);      
      return -2;
    }
  } else {
    ret = read_all(s, lenbuf, 4);
    if (ret < 0) {
      perror("reading token length");
      return -1;
    } else if (ret != 4) {
      (*disr_commit)(s,0);
      return -2;
    }
  }
  tok->length = ((lenbuf[0] << 24)
		 | (lenbuf[1] << 16)
		 | (lenbuf[2] << 8)
		 | lenbuf[3]);
  if (tok->length == 0) {
    tok->value = NULL;
    return 0;
  }
  tok->value = (char *) malloc(tok->length ? tok->length : 1);
  if (tok->length && tok->value == NULL) {
    tok->length = 0;
    return -6;
  }

  ret = read_all(s, (char *) tok->value, tok->length);
  if (ret < 0) {
    perror("reading token data");
    free(tok->value);
    tok->length = 0;
    return -1;
  } else if (ret != tok->length) {
    free(tok->value);
    tok->length = 0;
    (*disr_commit)(s,0);
    return -2;
  }
  (*disr_commit)(s,1);      
  return 0;
}

static void display_status_1(m, code, type)
     char *m;
     OM_uint32 code;
     int type;
{
     OM_uint32 maj_stat, min_stat;
     gss_buffer_desc msg;
     OM_uint32 msg_ctx;
     
     msg_ctx = 0;
     do {
       maj_stat = gss_display_status(&min_stat, code,
				     type, GSS_C_NULL_OID,
				     &msg_ctx, &msg);
       fprintf(stderr,"%s : %.*s\n",m,
	       (int)msg.length,
	       (char *)msg.value);
	  (void) gss_release_buffer(&min_stat, &msg);

     } while (msg_ctx != 0);
}

/*
 * Function: display_status
 *
 * Purpose: displays GSS-API messages
 *
 * Arguments:
 *
 * 	msg		a string to be displayed with the message
 * 	maj_stat	the GSS-API major status code
 * 	min_stat	the GSS-API minor status code
 *
 * Effects:
 *
 * The GSS-API messages associated with maj_stat and min_stat are
 * displayed on stderr, each preceeded by "GSS-API error <msg>: " and
 * followed by a newline.
 */
void pbsgss_display_status(msg, maj_stat, min_stat)
     char *msg;
     OM_uint32 maj_stat;
     OM_uint32 min_stat;
{
     display_status_1(msg, maj_stat, GSS_C_GSS_CODE);
     display_status_1(msg, min_stat, GSS_C_MECH_CODE);
}


/*
 * Function: server_acquire_creds
 *
 * Purpose: imports a service name and acquires credentials for it
 *
 * Arguments:
 *
 * 	service_name	(r) the ASCII service name
 * 	server_creds	(w) the GSS-API service credentials
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 *
 * The service name is imported with gss_import_name, and service
 * credentials are acquired with gss_acquire_cred.  If either opertion
 * fails, an error message is displayed and -1 is returned; otherwise,
 * 0 is returned.
p */
 int pbsgss_server_acquire_creds(service_name, server_creds)
     char *service_name;
     gss_cred_id_t *server_creds;
{
  gss_buffer_desc name_buf;
  gss_name_t server_name;
  OM_uint32 maj_stat, min_stat;

  name_buf.value = service_name;
  name_buf.length = strlen(name_buf.value) + 1;
  maj_stat = gss_import_name(&min_stat, &name_buf, 
			     (gss_OID) gss_nt_service_name, &server_name);

  if (maj_stat != GSS_S_COMPLETE) {
    pbsgss_display_status("importing name", maj_stat, min_stat);
    return -1;
  }

  maj_stat = gss_acquire_cred(&min_stat, server_name, 0,
			      GSS_C_NULL_OID_SET, GSS_C_ACCEPT,
			      server_creds, NULL, NULL);


  if (maj_stat != GSS_S_COMPLETE) {
    pbsgss_display_status("pbsgss_server_acquire_creds", maj_stat, min_stat);
    (void) gss_release_name(&min_stat, &server_name);
    return -1;
  }
  (void) gss_release_name(&min_stat, &server_name);
  return 0;
}


/* Purpose: establishses a GSS-API context as a specified service with
 * an incoming client, and returns the context handle and associated
 * client name
 *
 * Arguments:
 *
 * 	s		(r) an established TCP connection to the client
 * 	service_creds	(r) server credentials, from gss_acquire_cred
 * 	context		(w) the established GSS-API context
 * 	client_name	(w) the client's ASCII name
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 *
 * Any valid client request is accepted.  If a context is established,
 * its handle is returned in context and the client name is returned
 * in client_name and 0 is returned.  If unsuccessful, an error
 * message is displayed and -1 is returned.
 */
 int pbsgss_server_establish_context(s, server_creds, client_creds, context, client_name, 
				    ret_flags)
     int s;
     gss_cred_id_t server_creds, *client_creds;
     gss_ctx_id_t *context;
     gss_buffer_t client_name;
     OM_uint32 *ret_flags;
{
  gss_buffer_desc send_tok, recv_tok;
  gss_name_t client;
  gss_OID doid;
  OM_uint32 maj_stat, min_stat, acc_sec_min_stat;
  gss_buffer_desc	oid_name;
  int token_flags;

  *context = GSS_C_NO_CONTEXT;
  recv_tok.value = NULL; recv_tok.length = 0;
  do {
    if (pbsgss_recv_token(s, &token_flags, &recv_tok) < 0) {
      if (recv_tok.value) {
	free(recv_tok.value);
      }	
      if (retry < 3) {
	retry++;
	return pbsgss_server_establish_context(s,server_creds,client_creds,context,client_name,ret_flags);
      } else {
	retry = 0;
	return -1;
      }
    }

    maj_stat =
      gss_accept_sec_context(&acc_sec_min_stat,
			     context,
			     server_creds,
			     &recv_tok,
			     GSS_C_NO_CHANNEL_BINDINGS,
			     &client,
			     &doid,
			     &send_tok,
			     ret_flags,
			     NULL, 	/* ignore time_rec */
			     client_creds); 	/* ignore del_cred_handle */

    if(recv_tok.value) {
      free(recv_tok.value);
      recv_tok.value = NULL;
    }

    if (send_tok.length != 0) {
      if (pbsgss_send_token(s, TOKEN_CONTEXT, &send_tok) < 0) {
	return -104;
      }

      (void) gss_release_buffer(&min_stat, &send_tok);
    }
    if (maj_stat!=GSS_S_COMPLETE && maj_stat!=GSS_S_CONTINUE_NEEDED) {
      pbsgss_display_status("accepting context", maj_stat,
			    acc_sec_min_stat);
      if (*context != GSS_C_NO_CONTEXT)
	gss_delete_sec_context(&min_stat, &context,
			       GSS_C_NO_BUFFER);
      return -105;
    }
 
  } while (maj_stat == GSS_S_CONTINUE_NEEDED);

  maj_stat = gss_display_name(&min_stat, client, client_name, &doid);
  if (maj_stat != GSS_S_COMPLETE) {
    pbsgss_display_status("displaying name", maj_stat, min_stat);
    return -106;
  }
  maj_stat = gss_release_name(&min_stat, &client);
  if (maj_stat != GSS_S_COMPLETE) {
    pbsgss_display_status("releasing name", maj_stat, min_stat);
    return -107;
  }
  retry = 0;
  DIS_tcp_setup(s);
  return 0;
}

/* returns 1 if we can get creds and 0 otherwise */
int pbsgss_can_get_creds() {
  OM_uint32 gss_flags, ret_flags, maj_stat, min_stat;
  gss_cred_id_t creds;

  maj_stat = gss_acquire_cred(&min_stat,
			      GSS_C_NO_NAME,
			      GSS_C_INDEFINITE,
			      GSS_C_NO_OID_SET,
			      GSS_C_INITIATE,
			      &creds,
			      NULL,
			      NULL);
  if (maj_stat == GSS_S_COMPLETE) {
    if (creds != NULL) {
      gss_release_cred(&min_stat,&creds);
    }
  }
  return (maj_stat == GSS_S_COMPLETE);
}

/*
 * Function: client_establish_context
 *
 * Purpose: establishes a GSS-API context with a specified service and
 * returns the context handle
 *
 * Arguments:
 *
 * 	s		    (r) an established TCP connection to the service
 * 	service_name(r) the ASCII service name of the service
 *	gss_flags	(r) GSS-API delegation flag (if any)
 *	auth_flag	(r) whether to actually do authentication
 *	oid		    (r) OID of the mechanism to use
 * 	context		(w) the established GSS-API context
 *	ret_flags	(w) the returned flags from init_sec_context
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * service_name is imported as a GSS-API name and a GSS-API context is
 * established with the corresponding service; the service should be
 * listening on the TCP connection s.  The default GSS-API mechanism
 * is used, and mutual authentication and replay detection are
 * requested.
 * 
 * If successful, the context handle is returned in context.  If
 * unsuccessful, the GSS-API error messages are displayed on stderr
 * and -1 is returned.  -2 is returned on errors that should be retried.
 */
int pbsgss_client_establish_context(s, service_name, creds, oid, gss_flags,
				    gss_context, ret_flags)
     int s;
     char *service_name;
     gss_cred_id_t creds;
     gss_OID oid;
     OM_uint32 gss_flags;
     gss_ctx_id_t *gss_context;
     OM_uint32 *ret_flags;

{
  gss_buffer_desc send_tok, recv_tok, *token_ptr;
  gss_name_t target_name;
  OM_uint32 maj_stat, min_stat, init_sec_min_stat;
  int token_flags, status;

  /*
   * Import the name into target_name.  Use send_tok to save
   * local variable space.
   */
  send_tok.value = service_name;
  send_tok.length = strlen(service_name) ;
  maj_stat = gss_import_name(&min_stat, &send_tok,
			     (gss_OID) gss_nt_service_name, &target_name);
  if (maj_stat != GSS_S_COMPLETE) {
    pbsgss_display_status("parsing name", maj_stat, min_stat);
    return -1;
  }    

  send_tok.value = NULL; send_tok.length = 0;

  /*
   * Perform the context-establishement loop.
   *
   * On each pass through the loop, token_ptr points to the token
   * to send to the server (or GSS_C_NO_BUFFER on the first pass).
   * Every generated token is stored in send_tok which is then
   * transmitted to the server; every received token is stored in
   * recv_tok, which token_ptr is then set to, to be processed by
   * the next call to gss_init_sec_context.
   * 
   * GSS-API guarantees that send_tok's length will be non-zero
   * if and only if the server is expecting another token from us,
   * and that gss_init_sec_context returns GSS_S_CONTINUE_NEEDED if
   * and only if the server has another token to send us.
   */
     
  token_ptr = GSS_C_NO_BUFFER;
  *gss_context = GSS_C_NO_CONTEXT;

  do {
    maj_stat =
      gss_init_sec_context(&init_sec_min_stat,
			   creds ? creds : GSS_C_NO_CREDENTIAL,
			   gss_context,
			   target_name,
			   oid,
			   gss_flags,
			   0,
			   NULL,	/* no channel bindings */
			   token_ptr,
			   NULL,	/* ignore mech type */
			   &send_tok,
			   ret_flags,
			   NULL);	/* ignore time_rec */
     
    if (token_ptr != GSS_C_NO_BUFFER && token_ptr->length && token_ptr->value) {
      //      fprintf(stderr,"Freeing token 2 %p\n",token_ptr->value);
      free(token_ptr->value);
      token_ptr->value = NULL;
      token_ptr->length = 0;
    }

    if (send_tok.length != 0) {
      if (pbsgss_send_token(s, TOKEN_CONTEXT, &send_tok) < 0) {
	(void) gss_release_buffer(&min_stat, &send_tok);
	(void) gss_release_name(&min_stat, &target_name);
	return -1;
      }
    } 
    if (maj_stat!=GSS_S_COMPLETE && maj_stat!=GSS_S_CONTINUE_NEEDED) {
      pbsgss_display_status("pbsgss_client_establish_context.gss_init_set_context", maj_stat,
			    init_sec_min_stat);
      (void) gss_release_name(&min_stat, &target_name);
      if (*gss_context != GSS_C_NO_CONTEXT)
	gss_delete_sec_context(&min_stat, &gss_context,
			       GSS_C_NO_BUFFER);
      return -1;
    }   
   (void) gss_release_buffer(&min_stat, &send_tok);

 	  
    if (maj_stat == GSS_S_CONTINUE_NEEDED) {
      status = pbsgss_recv_token(s, &token_flags, &recv_tok);
      if ( status < 0) {
	gss_delete_sec_context(&min_stat, &gss_context,
			       GSS_C_NO_BUFFER);
	(void) gss_release_name(&min_stat, &target_name);
	return status;
      }
      token_ptr = &recv_tok;
    }
  } while (maj_stat == GSS_S_CONTINUE_NEEDED);
  if (token_ptr != GSS_C_NO_BUFFER && token_ptr->length && token_ptr->value) {
    free(token_ptr->value);
    token_ptr->value = NULL;
    token_ptr->length = 0;
  }

  (void) gss_release_name(&min_stat, &target_name);
  DIS_tcp_setup(s);
  return 0;
}

int pbsgss_save_creds (gss_cred_id_t client_creds,
		       char *principal,
		       char *ccname) {
  
  krb5_context kcontext;
  krb5_auth_context authcontext;
  krb5_error_code retval;
  krb5_ccache ccache;
  krb5_principal princ;
  OM_uint32 maj_status, min_status;
    
  if (retval = krb5_init_context(&kcontext)) {
    return -1;
  }
    
  if (retval = krb5_cc_resolve(kcontext,
			       ccname,
			       &ccache)) {
    krb5_free_context(kcontext);
    return -2;
  }

  /* Check to see if principal is defined */
  if(principal) {
     if (retval = krb5_parse_name(kcontext,
   			       principal,
   			       &princ)) {
       krb5_cc_destroy(kcontext,ccache);
       krb5_free_context(kcontext);
       return -3;
     }
  } else {
	  return -3;
  }

  if (retval = krb5_cc_initialize(kcontext,ccache,princ)) {
    krb5_free_principal(kcontext,princ);
    krb5_cc_destroy(kcontext,ccache);
    krb5_free_context(kcontext);
    return -4;
  }
  krb5_free_principal(kcontext,princ);
  if (maj_status = gss_krb5_copy_ccache(&min_status,
					client_creds,
					ccache)) {
    krb5_cc_destroy(kcontext,ccache);
    krb5_free_context(kcontext);
    return -5;
  }
  krb5_free_context(kcontext);
  return 0;
}

int pbsgss_renew_creds (char *jobname, char *prefix) {
  char *cmd, *ccname;
  ccname = ccname_for_job(jobname,prefix);
  cmd = malloc(sizeof(char) * (strlen(ccname) + strlen("/usr/bin/kinit -R -c ") + 10));
  if (cmd == NULL) {
    free(ccname);
    return 1;
  }
  sprintf(cmd,"/usr/bin/kinit -R -c %s",ccname);
  return system(cmd);
}

int pbsgss_client_authenticate(char *hostname, int psock, int delegate) {
  char *service_name;
  OM_uint32 gss_flags, ret_flags, maj_stat, min_stat;
  gss_OID oid;
  gss_ctx_id_t gss_context;
  gss_cred_id_t creds;
  int retval;
  gss_name_t name = GSS_C_NO_NAME;
 
  maj_stat = gss_acquire_cred(&min_stat,
			      GSS_C_NO_NAME,
			      GSS_C_INDEFINITE,
			      GSS_C_NO_OID_SET,
			      GSS_C_INITIATE,
			      &creds,
			      NULL,
			      NULL);
  if (maj_stat != GSS_S_COMPLETE) {
    if (geteuid() == 0) {
      struct utsname buf;
      char *princname;
      gss_buffer_desc name_buf;
      if (uname(&buf) != 0) {
	//fprintf(stderr,"couldn't uname");
	return -1;
      }
      princname = malloc(sizeof(char) * (1 + strlen("host@") + strlen(buf.nodename)));      
      if (princname) {
	sprintf(princname,"host@%s",buf.nodename);
	putenv("KRB5CCNAME=FILE:/tmp/krb5cc_pbs_server");
	name_buf.value = princname;
	name_buf.length = strlen(princname) + 1;
	maj_stat = gss_import_name(&min_stat,&name_buf,(gss_OID)gss_nt_service_name,&name);
	if (maj_stat == GSS_S_COMPLETE) {
	  maj_stat = gss_acquire_cred(&min_stat,
				      name,
				      GSS_C_INDEFINITE,
				      GSS_C_NO_OID_SET,
				      GSS_C_INITIATE,
				      &creds,
				      NULL,
				      NULL);
	  if (maj_stat != GSS_S_COMPLETE) {
	    pbsgss_display_status("pbsgss_client_authenticate/gss_acquire_cred (host princ)",maj_stat,min_stat);
	    creds = NULL;
	  } 
	  free(princname);
	} else {
	  pbsgss_display_status("importing name", maj_stat, min_stat);
	  creds = NULL;
	}
      } else {
	creds = NULL;
      }
    } else {
      creds = NULL;
    }
  } 
  if (creds == NULL) {
    return -1;
  }
  
  service_name = malloc(sizeof(char) * (1 + strlen(hostname) + strlen("host@")));
  sprintf(service_name,"host@%s",hostname);

  gss_flags = GSS_C_MUTUAL_FLAG | (delegate ? GSS_C_DELEG_FLAG : 0);
  oid = GSS_C_NULL_OID;
  retval = pbsgss_client_establish_context(psock,
					   service_name,
					   creds,
					   oid,
					   gss_flags,
					   &gss_context,
					   &ret_flags);
  free(service_name);
  if (creds != NULL) {
    gss_release_cred(&min_stat,&creds);
  }
  if (name != GSS_C_NO_NAME) {
    gss_release_name(&min_stat,&name);
  }
  gss_delete_sec_context(&min_stat,&gss_context,GSS_C_NO_BUFFER);
  if (retval < 0) {    
    if (retry < 3) {
      retry++;
      DIS_tcp_setup(psock);
      return pbsgss_client_authenticate(hostname, psock, delegate);      
    } else {
      return retval;
    }
  }
  retry = 0;
  return retval;
}

char *ccname_for_job(char *jobname, char *prefix) {
  char *ccname;
  int i;
  i = strlen(prefix) + 
    strlen("/krb5cc_") + 
    strlen(jobname) + 1;  
  ccname = malloc(sizeof(char)*i);
  if (!ccname) 
    {
      return NULL;
    }
  sprintf(ccname,"%s/krb5cc_%s",prefix,jobname);
  return ccname;
}

/* assumes it's running as the mom, because server doesn't need to call aklog */
int authenticate_as_job(char *ccname,
			int setpag) {
  if (setenv("KRB5CCNAME",ccname,1) != 0) {
    return -1;
  }
  if (setpag) {
    system("/usr/bin/aklog -setpag");   
  } else {
    system("/usr/bin/aklog");
  }
  return 0;
}

int clear_krb5ccname() {
  setenv("KRB5CCNAME","",1);
}

/* returns the full principal name for the current host, eg
   host/foo.bar.com@BAR.COM 
*/
char *pbsgss_get_host_princname() {
  char *service_name, *princname;
  gss_cred_id_t creds;
  OM_uint32 min_stat;
  gss_name_t name;
  gss_buffer_desc buffer;
  struct utsname buf;  
  gss_OID name_type;
  if (uname(&buf) != 0) {
    return "NOUNAME";
  }
  service_name = malloc(sizeof(buf.nodename) + 6);
  if (service_name == NULL) {return NULL;}
  sprintf(service_name,"host@%s",buf.nodename);
  if (pbsgss_server_acquire_creds(service_name,&creds) < 0) {
    free(service_name);
    return NULL;
  }
  if(gss_inquire_cred(&min_stat,
		      creds,
		      &name,
		      NULL,
		      NULL,
		      NULL) != GSS_S_COMPLETE) {
    gss_release_cred(&min_stat,&creds);
    free(service_name);
    return NULL;
  }
  if (gss_display_name(&min_stat,
		       name,
		       &buffer,
		       &name_type) != GSS_S_COMPLETE) {
    gss_release_name(&min_stat,&name);
    gss_release_cred(&min_stat,&creds);
    free(service_name);
    return NULL;
  }
  princname = malloc(sizeof(char) * (buffer.length + 2));
  if (princname) {
    strncpy(princname,buffer.value,buffer.length);
    princname[buffer.length] = '\0';
  } else {
    princname = NULL;
  }
  
  gss_release_buffer(&min_stat,&buffer);
  gss_release_name(&min_stat,&name);
  gss_release_cred(&min_stat,&creds);
  free(service_name);
  return princname;
}
