#include "license_pbs.h" /* See here for the software license */
/*
 *
 * qmove - (PBS) move batch job
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
#include <pbs_config.h>   /* the master config generated by configure */


int main(int argc, char **argv) /* qmove */
  {
  int any_failed = 0;

  char job_id[PBS_MAXCLTJOBID];  /* from the command line */
  char destination[PBS_MAXSERVERNAME]; /* from the command line */
  char *q_n_out, *s_n_out;

  char job_id_out[PBS_MAXCLTJOBID];
  char server_out[MAXSERVERNAME];
  char rmt_server[MAXSERVERNAME];

  if (argc < 3)
    {
    static char usage[] = "usage: qmove destination job_identifier...\n";
    fprintf(stderr,"%s", usage);
    exit(2);
    }

  strcpy(destination, argv[1]);

  if (parse_destination_id(destination, &q_n_out, &s_n_out))
    {
    fprintf(stderr, "qmove: illegally formed destination: %s\n", destination);
    exit(2);
    }

  for (optind = 2; optind < argc; optind++)
    {
    int connect;
    int stat = 0;
    int located = FALSE;

    strcpy(job_id, argv[optind]);

    if (get_server(job_id, job_id_out, server_out))
      {
      fprintf(stderr, "qmove: illegally formed job identifier: %s\n", job_id);
      any_failed = 1;
      continue;
      }

cnt:

    connect = cnt2server(server_out);

    if (connect <= 0)
      {
      any_failed = -1 * connect;

      fprintf(stderr, "qmove: cannot connect to server %s (errno=%d) %s\n",
        pbs_server, any_failed, pbs_strerror(any_failed));
      continue;
      }

    stat = pbs_movejob_err(connect, job_id_out, destination, NULL, &any_failed);

    if (stat &&
        (any_failed != PBSE_UNKJOBID))
      {
      prt_job_err("qmove", connect, job_id_out);
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

      prt_job_err("qmove", connect, job_id_out);
      }

    pbs_disconnect(connect);
    }

  exit(any_failed);
  }
