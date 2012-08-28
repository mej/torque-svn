#include "license_pbs.h" /* See here for the software license */
/*
 *
 * qhold - (PBS) hold batch job
 *
 * Authors:
 *      Terry Heidelberg
 *      Livermore Computing
 *
 *      Bruce Kelly
 *      National Energy Research Supercomputer Center
 *
 *      Lawrence Livermore National Laboratory
 *      University of California
 */

#include "cmds.h"
#include "net_cache.h"
#include <pbs_config.h>   /* the master config generated by configure */


int main(
    
  int    argc,
  char **argv) /* qhold */
  
  {
  int c;
  int errflg = 0;
  int any_failed = 0;
  int u_cnt, o_cnt, s_cnt;
  char *pc;
  char  extend[1024];

  char job_id[PBS_MAXCLTJOBID];       /* from the command line */

  char job_id_out[PBS_MAXCLTJOBID];
  char server_out[MAXSERVERNAME] = "";
  char rmt_server[MAXSERVERNAME] = "";

#define MAX_HOLD_TYPE_LEN 32
  char hold_type[MAX_HOLD_TYPE_LEN+1];

#define GETOPT_ARGS "h:t:"

  hold_type[0] = '\0';
  extend[0] = '\0';

  initialize_network_info();

  while ((c = getopt(argc, argv, GETOPT_ARGS)) != EOF)
    switch (c)
      {

      case 'h':

        while (isspace((int)*optarg)) optarg++;

        if (strlen(optarg) == 0)
          {
          fprintf(stderr, "qhold: illegal -h value\n");
          errflg++;
          break;
          }

        pc = optarg;

        u_cnt = o_cnt = s_cnt = 0;

        while (*pc)
          {
          if (*pc == 'u')
            u_cnt++;
          else if (*pc == 'o')
            o_cnt++;
          else if (*pc == 's')
            s_cnt++;
          else
            {
            fprintf(stderr, "qhold: illegal -h value\n");
            errflg++;
            break;
            }

          pc++;
          }

        strcpy(hold_type, optarg);

        break;

      case 't':

        pc = optarg;

        if (strlen(pc) == 0)
          {
          fprintf(stderr, "qhold: illegal -t value (array range cannot be zero length)\n");

          errflg++;

          break;
          }

        snprintf(extend,sizeof(extend),"%s%s",
          ARRAY_RANGE,
          pc);

        break;

      default :
        errflg++;
      }

  if (errflg || optind >= argc)
    {
    static char usage[] = "usage: qhold [-h hold_list] [-t array_range] job_identifier...\n";
    fprintf(stderr,"%s", usage);
    exit(2);
    }

  for (; optind < argc; optind++)
    {
    int connect;
    int stat = 0;
    int located = FALSE;

    strcpy(job_id, argv[optind]);

    if (get_server(job_id, job_id_out, server_out))
      {
      fprintf(stderr, "qhold: illegally formed job identifier: %s\n", job_id);
      any_failed = 1;
      continue;
      }

cnt:

    connect = cnt2server(server_out);

    if (connect <= 0)
      {
      any_failed = -1 * connect;

      if (server_out[0] != 0)
        fprintf(stderr, "qhold: cannot connect to server %s (errno=%d) %s\n",
              server_out, any_failed, pbs_strerror(any_failed));
      else
        fprintf(stderr, "qhold: cannot connect to server %s (errno=%d) %s\n",
              pbs_server, any_failed, pbs_strerror(any_failed));
      
      continue;
      }

    if (extend[0] == '\0')
      stat = pbs_holdjob_err(connect, job_id_out, hold_type, NULL, &any_failed);
    else
      stat = pbs_holdjob_err(connect, job_id_out, hold_type, extend, &any_failed);

    if (stat &&
        (any_failed != PBSE_UNKJOBID))
      {
      prt_job_err("qhold", connect, job_id_out);
      }
    else if (stat && 
             (any_failed != PBSE_UNKJOBID) &&
             !located)
      {
      located = TRUE;

      if (locate_job(job_id_out, server_out, rmt_server))
        {
        pbs_disconnect(connect);
        strcpy(server_out, rmt_server);
        goto cnt;
        }

      prt_job_err("qhold", connect, job_id_out);
      }

    pbs_disconnect(connect);
    }

  exit(any_failed);
  }
