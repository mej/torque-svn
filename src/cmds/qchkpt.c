/*
 * Copyright ClusterResources 2008
 *
 * qchkpt - checkpoint a batch job
 *
 * Author:
 *      Steven Snelgrove
 *      Cluster Resources
 */

#include "cmds.h"
#include <pbs_config.h>   /* the master config generated by configure */

int main(int argc, char **argv) /* qchkpt */
  {
  int any_failed = 0;
  static char *usage = "Usage: qchkpt job_id ...\n";

  char job_id[PBS_MAXCLTJOBID];       /* from the command line */

  char job_id_out[PBS_MAXCLTJOBID];
  char server_out[MAXSERVERNAME];
  char rmt_server[MAXSERVERNAME];

  if (argc == 1)
    {
    fprintf(stderr, usage);
    return 1;
    }


  for (optind = 1; optind < argc; optind++)
    {
    int connect;
    int stat = 0;
    int located = FALSE;

    strcpy(job_id, argv[optind]);

    if (get_server(job_id, job_id_out, server_out))
      {
      fprintf(stderr, "qchkpt: illegally formed job identifier: %s\n", job_id);
      any_failed = 1;
      continue;
      }

cnt:

    connect = cnt2server(server_out);

    if (connect <= 0)
      {
      fprintf(stderr, "qchkpt: cannot connect to server %s (errno=%d)\n",
              pbs_server, pbs_errno);
      any_failed = pbs_errno;
      continue;
      }

    stat = pbs_checkpointjob(connect, job_id_out, NULL);

    if (stat && (pbs_errno != PBSE_UNKJOBID))
      {
      prt_job_err("qchkpt", connect, job_id_out);
      any_failed = pbs_errno;
      }
    else if (stat && (pbs_errno == PBSE_UNKJOBID) && !located)
      {
      located = TRUE;

      if (locate_job(job_id_out, server_out, rmt_server))
        {
        pbs_disconnect(connect);
        strcpy(server_out, rmt_server);
        goto cnt;
        }

      prt_job_err("qchkpt", connect, job_id_out);

      any_failed = pbs_errno;
      }

    pbs_disconnect(connect);
    }

  return any_failed;

  }
