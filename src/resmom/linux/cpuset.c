#include <pbs_config.h>

#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <sys/param.h>

#include "libpbs.h"
#include "attribute.h"
#include "resource.h"
#include "server_limits.h"
#include "pbs_job.h"
#include "log.h"

/* NOTE: move these three things to utils when lib is checked in */
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif /* MAXPATHLEN */
#ifndef FAILURE
#define FAILURE 0
#endif /* FAILURE */
#ifndef SUCCESS
#define SUCCESS 1
#endif /* SUCCESS */

#define TTORQUECPUSET_PATH "/dev/cpuset/torque"
#define TROOTCPUSET_PATH   "/dev/cpuset"

/* FIXME: TODO:  TTORQUECPUSET_PATH, enabling cpuset support, and correct error
 * checking need a run-time config */

int           *VPToCPUMap = NULL;  /* map of virtual processors to cpus (alloc) */

extern char    mom_host[];
extern char    cpus_str[];
extern char    mem_str[];
extern int     num_cpus;
extern int     num_mems;
extern int     cpu_offset;
extern int     LOGLEVEL;

extern char    mom_short_name[];


/* private functions */
void remove_defunct_cpusets();
int get_cpu_string(job *pjob,char *);
int get_cpuset_strings(job *pjob,char *,char *);
int create_vnodesets(job *,char *path,char *,mode_t);
int init_jobset(char *,job *,mode_t,char *);
/* end private functions */


/**
 * deletes a cpuset
 *
 * @param cpusetname - name of cpuset to be deleted
 * @return -1 on failure, 0 otherwise
 */
int cpuset_delete(

  char *cpusetname)  /* I */

  {
  char   path[MAXPATHLEN + 1];
  char   childpath[MAXPATHLEN + 1];
  pid_t  killpids;
  FILE  *fd;
  DIR   *dir;

  struct dirent *pdirent;
  static char id[] = "cpuset_delete";

  struct stat statbuf;

  /* accept a full path or jobid */

  if (cpusetname[0] == '/')
    strcpy(path, cpusetname);
  else
#ifdef SGI4700
    sprintf(path, "%s/%s/%s", TTORQUECPUSET_PATH, mom_host, cpusetname);
#else
    sprintf(path, "%s/%s", TTORQUECPUSET_PATH, cpusetname);
#endif  /* end SGI4700 */

  if ((dir = opendir(path)) == NULL)
    {
    /* cpuset does not exist... no one cares! */

    return(-1);
    }

  while ((pdirent = readdir(dir)) != NULL)
    {
    /* Skip parent and current directory. */

    if (!strcmp(pdirent->d_name, ".") || !strcmp(pdirent->d_name, ".."))
      continue;

    /* Prepend directory name to file name for lstat. */
    snprintf(childpath,sizeof(childpath),"%s/%s",path,pdirent->d_name);

    /* Skip file on error. */

    if (!(lstat(childpath, &statbuf) >= 0))
      continue;

    /* If a directory is found try to get cpuset info about it. */

    if (statbuf.st_mode&S_IFDIR)
      {
      if (cpuset_delete(childpath) == 0)
        {
        sprintf(log_buffer, "Unused cpuset '%s' deleted.",
                childpath);

        log_event(PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_SERVER,
          id,
          log_buffer);
        }
      else
        {
        sprintf(log_buffer, "Could not delete unused cpuset %s.",
                childpath);

        log_err(-1, id, log_buffer);
        }
      }
    else if (!strcmp(pdirent->d_name, "tasks"))
      {
      /* FIXME: need a more careful mechanism here... including a possibly useless sleep */

      if ((fd = fopen(childpath, "r")) != NULL)
        {
        while (fscanf(fd, "%d", &killpids) == 1)
          kill(killpids, SIGKILL);

        fclose(fd);
        }

      sleep(1);
      }

    /* FIXME: only need when testing with a fake /dev/cpuset */
    /* does this mean it should be removed for production? */

    if (!(statbuf.st_mode & S_IFDIR))
      unlink(childpath);
    } /* END while((pdirent = readdir(dir)) != NULL) */

  closedir(dir);

  if (rmdir(path) != 0)
    {
    /* FAILURE */

    return(-1);
    }

  /* SUCCESS */

  return(0);
  }  /* END cpuset_delete() */



/**
 * removes cpusets for jobs that no longer exist
 *
 * @see initialize_root_cpuset() - parent
 */
void remove_defunct_cpusets()

  {
  DIR *dir;
  struct dirent *pdirent;

  struct stat    statbuf;

  char *id = "remove_defunct_cpusets";
  char  path[MAXPATHLEN];

  /* Find all the job cpusets. */

  strcpy(path, TTORQUECPUSET_PATH);
#ifdef SGI4700
  strcat(path, "/");
  strcat(path, mom_host);
#endif  /* end SGI4700 */
  if ((dir = opendir(path)) == NULL)
    {
    sprintf(log_buffer, "opendir(%s) failed.\n",path);

    log_err(-1, id, log_buffer);
    }

  while ((pdirent = readdir(dir)) != NULL)
    {
    /* Skip parent and current directory. */
    if (!strcmp(pdirent->d_name, ".") || !strcmp(pdirent->d_name, "..")) 
      continue;

    /* Prepend directory name to file name for lstat. */
    strcpy(path, TTORQUECPUSET_PATH);
#ifdef SGI4700
    strcat(path, "/");
    strcat(path, mom_host);
#endif  /* end SGI4700 */

    if (path[strlen(path)-1] != '/') 
      strcat(path, "/");

    strcat(path, pdirent->d_name);

    /* Skip file on error. */
    if (!(lstat(path, &statbuf) >= 0)) continue;

    /* If a directory is found try to get cpuset info about it. */
    if (statbuf.st_mode&S_IFDIR)
      {
      /* If the job isn't found, delete its cpuset. */
      if (find_job(pdirent->d_name) == NULL)
        {
        if (cpuset_delete(pdirent->d_name) == 0)
          {
          sprintf(log_buffer, "Unused cpuset '%s' deleted.", path);
          log_event(PBSEVENT_SYSTEM,
            PBS_EVENTCLASS_SERVER,
            id,
            log_buffer);
          }
        else
          {
          sprintf(log_buffer, "Could not delete unused cpuset %s.", path);
          log_err(-1, id, log_buffer);
          }
        }
      }
    } /* END while ((pdirent = readdir(dir)) != NULL) */

  closedir(dir);
  } /* END remove_defunct_cpusets() */




/*
 * Create the mom cpuset for Torque if it doesn't already exist.
 * clear out any job cpusets for jobs that no longer exist.
 *
 * @see remove_defunct_cpusets() - child
 */

#ifdef SGI4700
void
initialize_mom_cpuset(void)

  {
  static char    id[] = "initialize_mom_cpuset";

  char           path[MAXPATHLEN + 1];

  struct stat    statbuf;

  FILE           *fp;

  sprintf(log_buffer, "Init TORQUE MOM cpuset %s/%s.",
          TTORQUECPUSET_PATH,
          mom_host);

  log_event(PBSEVENT_SYSTEM,
    PBS_EVENTCLASS_SERVER,
    id,
    log_buffer);

  /* make sure cpusets are available */

  sprintf(path, "%s/cpus",
          TTORQUECPUSET_PATH);

  if (lstat(path, &statbuf) != 0)
    {
    sprintf(log_buffer, "cannot locate %s - cpusets not configured/enabled on host\n",
            path);

    log_err(-1, id, log_buffer);

    /* FAILURE */

    return;
    }

  sprintf(path, "%s/%s",
          TTORQUECPUSET_PATH,
          mom_host);

  if (lstat(path, &statbuf) != 0)
    {
    sprintf(log_buffer, "TORQUE cpuset %s does not exist, creating it now.\n",
            path);

    log_event(PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      id,
      log_buffer);

    mkdir(path, 0755);

    /* load in only cpuset for this MOM */
    /* use cpus from mom.layout file */

    sprintf(log_buffer, "TORQUE cpuset %s loaded with value '%s'\n",
            path,
            cpus_str);

    log_event(PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      id,
      log_buffer);

    /* create new TORQUE MOM set */

    sprintf(path, "%s/%s/cpus",
            TTORQUECPUSET_PATH,
            mom_host);

    fp = fopen(path, "w");

    if (fp != NULL)
      {
      /* write all TORQUE cpus into TORQUE MOM cpuset */

      sprintf(log_buffer, "adding cpus %s to %s",
              cpus_str,
              path);

      log_event(PBSEVENT_SYSTEM,
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);

      if (fwrite(cpus_str, sizeof(char), strlen(cpus_str), fp) < strlen(cpus_str))
        {
        /* FAILURE */
        sprintf(log_buffer, "failed to write cpus cpuset to %s\n",
                path);
        log_err(-1, id, log_buffer);
        return;
        }

      fclose(fp);
      }

    /* use mems from mom.layout file */

    sprintf(path, "%s/mems",
            TTORQUECPUSET_PATH);

    fp = fopen(path, "r");

    if (fp != NULL)
      {
      fclose(fp);

      sprintf(path, "%s/%s/mems",
              TTORQUECPUSET_PATH,
              mom_host);

      fp = fopen(path, "w");

      if (fp != NULL)
        {
        /* write all TORQUE mems into TORQUE MOM cpuset */

        sprintf(log_buffer, "adding mems %s to %s",
                mem_str,
                path);

        log_event(PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_SERVER,
          id,
          log_buffer);

        if (fwrite(mem_str, sizeof(char), strlen(mem_str), fp) < strlen(mem_str))
          {
          /* FAILURE */
          sprintf(log_buffer, "failed to write mems cpuset to %s\n",
                  path);
          log_err(-1, id, log_buffer);
          return;
          }

        fclose(fp);
        }
      }  /* END if (fp != NULL) */
    }    /* END if (lstat(path,&statbuf) != 0) */
  else
    {
    /* The cpuset already exists, delete any cpusets for jobs that no longer exist. */

    remove_defunct_cpusets();
    }

  return;
  }  /* END initialize_mom_cpuset() */
#endif  /* end SGI4700 */




/*
 * Create the root cpuset for Torque if it doesn't already exist.
 * clear out any job cpusets for jobs that no longer exist.
 *
 * @see remove_defunct_cpusets() - child
 */

void
initialize_root_cpuset(void)

  {
  static char    id[] = "initialize_root_cpuset";

  char           path[MAXPATHLEN + 1];

  struct stat    statbuf;

  char           cpuset_buf[MAXPATHLEN];
  FILE           *fp;

  sprintf(log_buffer, "Init TORQUE cpuset %s.",
          TTORQUECPUSET_PATH);

  log_event(PBSEVENT_SYSTEM,
    PBS_EVENTCLASS_SERVER,
    id,
    log_buffer);

  /* make sure cpusets are available */

  sprintf(path, "%s/cpus",
          TROOTCPUSET_PATH);

  if (lstat(path, &statbuf) != 0)
    {
    sprintf(log_buffer, "cannot locate %s - cpusets not configured/enabled on host\n",
            path);

    log_err(-1, id, log_buffer);

    /* FAILURE */

    return;
    }

  sprintf(path, "%s",
          TTORQUECPUSET_PATH);

  if (lstat(path, &statbuf) != 0)
    {
    sprintf(log_buffer, "TORQUE cpuset %s does not exist, creating it now.\n",
            path);

    log_event(PBSEVENT_SYSTEM,
      PBS_EVENTCLASS_SERVER,
      id,
      log_buffer);

    mkdir(path, 0755);

    /* load all cpus in root set */

    sprintf(path, "%s/cpus",
            TROOTCPUSET_PATH);

    fp = fopen(path, "r");

    if (fp != NULL)
      {
      char *cptr;
      char *dptr;

      char *ptr;

      int   maxindex;
      int   mindex;

      char  tmpBuf[MAXPATHLEN];

      /* FORMAT:  <CPU#>[<CPU#>][,<CPU#>[<CPU#>]]... */

      /* read cpus from root cpuset */

      if (fread(cpuset_buf, sizeof(char), sizeof(cpuset_buf), fp) != sizeof(cpuset_buf))
        {
        if (ferror(fp) != 0)
          {
          log_err(-1,id,
            "An error occurred while reading the root cpuset, attempting to continue.\n");
          }
        }

      /* Replace trailing newline with NULL */
      *(index(cpuset_buf, '\n')) = '\0';

      fclose(fp);

      sprintf(log_buffer, "root cpuset %s loaded with value '%s'\n",
              path,
              cpuset_buf);

      log_event(PBSEVENT_SYSTEM,
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);

      /* convert string to lookup table */

      strncpy(tmpBuf, cpuset_buf, sizeof(tmpBuf));

      /* extract last cpu index value */

      cptr = strchr(cpuset_buf, ',');
      dptr = strchr(cpuset_buf, '-');

      ptr = MAX(cptr, dptr);

      if (ptr == NULL)
        ptr = cpuset_buf;
      else
        ptr++;

      maxindex = strtol(ptr, NULL, 10);

      VPToCPUMap = (int *)calloc(1, sizeof(int) * maxindex);

      strncpy(tmpBuf, cpuset_buf, sizeof(tmpBuf));

      mindex = 0;

      ptr = strtok(tmpBuf, ",");

      /* Commented out as currently results in an infinite loop */
#if 0
      while (ptr != NULL)
        {
        ptr = strtok(ptr, "-");

        while (ptr != NULL)
          {
          /* What was meant to be here ? - csamuel@vpac.org */
          }
        }

#endif
      /* NOTE:  load 'boot' set */

      sprintf(path, "%s/boot/cpus",
              TROOTCPUSET_PATH);

      fp = fopen(path, "r");

      if (fp != NULL)
        {
        char bootbuf[MAXPATHLEN];

        /* what is format of data? */

        /* read cpus from boot cpuset */

        /* FIXME: need proper error checking and response */

        if (fread(bootbuf, sizeof(char), sizeof(bootbuf), fp) != sizeof(bootbuf))
          {
          if (ferror(fp) != 0)
            {
            log_err(-1,id,
              "An error occurred while reading the root cpuset, attempting to continue.\n");
            }
          }

        /* Replace trailing newline with NULL */
        *(index(bootbuf, '\n')) = '\0';

        fclose(fp);

        /* subtract bootset from rootset */

        /* NYI */
        }  /* END if (fp != NULL) */

      /* create new TORQUE set */

      sprintf(path, "%s/cpus",
              TTORQUECPUSET_PATH);

      fp = fopen(path, "w");

      if (fp != NULL)
        {
        /* write all root cpus into TORQUE cpuset */

        sprintf(log_buffer, "adding cpus %s to %s",
                cpuset_buf,
                path);

        log_event(PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_SERVER,
          id,
          log_buffer);

        fwrite(cpuset_buf, sizeof(char), strlen(cpuset_buf), fp);

        fclose(fp);
        }

      memset(cpuset_buf, '\0', sizeof(cpuset_buf));
      }  /* END if (fp != NULL) */

    /* add all mems to torqueset */

    sprintf(path, "%s/mems",
            TROOTCPUSET_PATH);

    fp = fopen(path, "r");

    if (fp != NULL)
      {
      /* what is format of data? */

      /* read all mems from root cpuset */

      if (fread(cpuset_buf, sizeof(char), sizeof(cpuset_buf), fp) != sizeof(cpuset_buf))
        {
        if (ferror(fp) != 0)
          {
          log_err(-1,id,
            "An error occurred while reading the root cpuset, attempting to continue.\n");
          }
        }

      fclose(fp);

      sprintf(path, "%s/mems",
              TTORQUECPUSET_PATH);

      fp = fopen(path, "w");

      if (fp != NULL)
        {
        /* write all root mems into TORQUE cpuset */

        sprintf(log_buffer, "adding mems %s to %s",
                cpuset_buf,
                path);

        log_event(PBSEVENT_SYSTEM,
          PBS_EVENTCLASS_SERVER,
          id,
          log_buffer);

        fwrite(cpuset_buf, sizeof(char), strlen(cpuset_buf), fp);

        fclose(fp);
        }

      memset(cpuset_buf, '\0', sizeof(cpuset_buf));
      }  /* END if (fp != NULL) */
    }    /* END if (lstat(path,&statbuf) != 0) */
#ifdef SGI4700
  initialize_mom_cpuset();
#else
  else
    {
    /* The cpuset already exists, delete any cpusets for jobs that no longer exist. */

    remove_defunct_cpusets();
    }
#endif  /* end SGI4700 */

  return;
  }  /* END initialize_root_cpuset() */




/**
 * get_cpu_string
 * @see add_cpus_to_jobset() - parent
 *
 * @param pjob - (I) the job whose cpu string we're building
 * @param CpuStr - (O) the cpu string
 * @return 1 if the cpu string is built, 0 otherwise
 */

int get_cpu_string(

  job  *pjob,   /* I */
  char *CpuStr) /* O */

  {
  vnodent *np = pjob->ji_vnods;
  int     j;
  char    tmpStr[MAXPATHLEN];

  if ((pjob == NULL) || 
      (CpuStr == NULL))
    return(FAILURE);

  CpuStr[0] = '\0';


  for (j = 0;j < pjob->ji_numvnod;++j, np++)
    {
    if (pjob->ji_nodeid == np->vn_host->hn_node)
      {
      if (CpuStr[0] != '\0')
        strcat(CpuStr, ",");

      sprintf(tmpStr, "%d", np->vn_index);

      strcat(CpuStr, tmpStr);

      }
    }

  return(SUCCESS);
  }




/**
 * get_cpuset_strings
 * @see add_cpus_to_jobset() - parent
 *
 * @param pjob - (I) the job whose cpu string we're building
 * @param CpuStr - (O) the cpu string
 * @param MemStr - (O) the mem string
 * @return 1 if the cpu string is built, 0 otherwise
 */

int get_cpuset_strings(

  job  *pjob,   /* I */
  char *CpuStr, /* O */
  char *MemStr) /* O */

  {
  vnodent *np = pjob->ji_vnods;
  int     j;
  int     lastmem = -1;
  int     ratio = num_cpus / num_mems;
  char    tmpStr[MAXPATHLEN];

  if ((pjob == NULL) || 
      (CpuStr == NULL) ||
      (MemStr == NULL))
    return(FAILURE);

  CpuStr[0] = '\0';
  MemStr[0] = '\0';

  for (j = 0;j < pjob->ji_numvnod;++j, np++)
    {
    if (pjob->ji_nodeid == np->vn_host->hn_node)
      {
      if (CpuStr[0] != '\0')
        strcat(CpuStr, ",");

      sprintf(tmpStr, "%d", np->vn_index+cpu_offset);

      strcat(CpuStr, tmpStr);

      if (lastmem != np->vn_index / ratio)
        {
        lastmem = np->vn_index / ratio;
        sprintf(tmpStr, "%d", lastmem);

        if (MemStr[0] != '\0')
          strcat(MemStr, ",");

        strcat(MemStr, tmpStr);
        }
      }
    }

  if (LOGLEVEL >= 7)
    {
    sprintf(log_buffer, "found cpus (%s) mems (%s) ratio = %d",
      CpuStr, MemStr, ratio);
    log_ext(-1, "get_cpuset_strings", log_buffer, LOG_DEBUG);
    }

  return(SUCCESS);
  }




/**
 * initializes the cpuset for the job
 * 
 * deletes any existing cpuset
 * creates the directory
 * copies relevant memory information
 *
 * @see create_jobset() - parent
 * @param path - (I) the path where the job's cpuset should be
 * @param pjob - (I) the job
 * @param savemask - (I) the settings to be restored
 * @param membuf- (O) the contents of the memory being moved
 */
int init_jobset(

  char  *path,     /* I */
  job   *pjob,     /* I */
  mode_t savemask, /* I */
  char  *membuf)   /* O */

  {
  char *id = "init_jobset";
  char  tmppath[MAXPATHLEN+1];
#ifndef SGI4700
  FILE *fd;
#endif  /* end SGI4700 */

  if ((path == NULL) ||
      (pjob == NULL) ||
      (membuf == NULL))
    return(FAILURE);

  membuf[0] = '\0';

  /* delete the current cpuset for the job if it exists */
  if (access(path, F_OK) == 0)
    {
    if (cpuset_delete(path) != 0)
      {
      sprintf(log_buffer, "Could not delete cpuset for job %s.\n", pjob->ji_qs.ji_jobid);
      log_err(-1, id, log_buffer);
      umask(savemask);
      return(FAILURE);
      }
    }
  /* don't "else return(FAILURE);" because the directory doesn't necessarily exist */

  /* create the directory and copy the relevant memory data */
#ifndef SGI4700
  snprintf(tmppath,sizeof(tmppath),"%s/%s/mems",TTORQUECPUSET_PATH,mom_host);
  if ((access(TTORQUECPUSET_PATH, F_OK) == 0) &&
      (access(tmppath, F_OK) == 0))
#else
  snprintf(tmppath,sizeof(tmppath),"%s/mems",TTORQUECPUSET_PATH);
  if (access(TTORQUECPUSET_PATH, F_OK) == 0)
#endif  /* end SGI4700 */
    {

    /* create the jobset */
    mkdir(path, 0755);

#ifndef SGI4700
    /* add all mems to jobset */
    fd = fopen(tmppath, "r");

    if (fd)
      {
      if (fread(membuf, sizeof(char), sizeof(membuf), fd))
        {
        if (ferror(fd) != 0)
          {
          log_err(-1,id,
            "An error occurred while reading cpuset's memory\n");

          return(FAILURE);
          }
        }

      fclose(fd);
      snprintf(tmppath,sizeof(tmppath),"%s/mems",path);
      fd = fopen(tmppath, "w");
      if (fd)
        {
        fwrite(membuf, sizeof(char), strlen(membuf), fd);
        fclose(fd);
        }
      return(SUCCESS);
      }
#else
    return(SUCCESS);
#endif  /* end SGI4700 */
    }

  return(FAILURE);
  }




/**
 * creates the vnodesets for the job
 *
 * creates and writes the files for each virtual node on the job
 *
 * @see create_jobset() - parent
 *
 * @param pjob (I) - the job whose vnodesets are being created
 * @param path (I) - path to the job's cpuset directory
 * @param membuf (I) - the memory information to be copied
 * @param savemask (I) - the settings to be restored
 */
int create_vnodesets(

  job    *pjob,     /* I */
  char   *path,     /* I */
  char   *membuf,   /* I */
  mode_t  savemask) /* I */

  {
  FILE    *fd;
  vnodent *np;
  int      j;
  int      rc = SUCCESS;

  char    *id = "create_vnodesets";
  char     cpusbuf[MAXPATHLEN+1];
  char     tasksbuf[MAXPATHLEN+1];
  char     tmppath[MAXPATHLEN+1];

  np = pjob->ji_vnods;
  cpusbuf[0] = '\0';

  for (j = 0;j < pjob->ji_numvnod;++j, np++)
    {
    if (pjob->ji_nodeid == np->vn_host->hn_node)
      {
      snprintf(tmppath,sizeof(tmppath),"%s/%d",path,np->vn_node);
      mkdir(tmppath, 0755);
      chmod(tmppath, 00755);
      sprintf(tasksbuf, "%d", np->vn_index);
      strcat(tmppath, "/cpus");
      sprintf(log_buffer, "TASKSET: %s cpus %s\n", tmppath, tasksbuf);
      log_event(PBSEVENT_SYSTEM, 
        PBS_EVENTCLASS_SERVER,
        id,
        log_buffer);
      fd = fopen(tmppath, "w");

      if (fd)
        {
        fwrite(tasksbuf, sizeof(char), strlen(tasksbuf), fd);
        fclose(fd);
        }
      else
        rc = FAILURE;

      memset(tasksbuf, '\0', sizeof(tasksbuf));

      /* add all mems to torqueset - membuf has info stored */
      sprintf(tmppath, "%s/%d/%s",path,np->vn_node,"mems");
      fd = fopen(tmppath, "w");

      if (fd)
        {
        sprintf(log_buffer, "adding %s to %s", tasksbuf, tmppath);
        log_event(PBSEVENT_SYSTEM, 
          PBS_EVENTCLASS_SERVER,
          id,
          log_buffer);

        fwrite(membuf, sizeof(char), strlen(tasksbuf), fd);
        fclose(fd);
        }
      else
        rc = FAILURE;

      }
    }

  umask(savemask);

  return(rc);
  }




/**
 * adds the cpus to the jobset
 *
 * @param pjob - the job associated with the jobset
 * @param path - the path to the jobset directory
 * @return SUCCESS if the files are correctly written, else FALSE
 */
int add_cpus_to_jobset(

  char *path,
  job  *pjob)

  {
  FILE *fd;
  char *id = "add_cpus_to_jobset";
  char  cpusbuf[MAXPATHLEN+1];
  char  tmppath[MAXPATHLEN+1];
#ifdef SGI4700
  char  memsbuf[MAXPATHLEN+1];
#endif  /* end SGI4700 */

  if ((pjob == NULL) ||
      (path == NULL))
    {
    return(FAILURE);
    }

  /* Make the string defining the CPUs to add into the jobset */
#ifdef SGI4700
  get_cpuset_strings(pjob,cpusbuf,memsbuf);
#else
  get_cpu_string(pjob,cpusbuf);
#endif  /* end SGI4700 */

  snprintf(tmppath,sizeof(tmppath),"%s/cpus",path);

  sprintf(log_buffer, "CPUSET: %s job %s path %s\n", cpusbuf,
          pjob->ji_qs.ji_jobid, tmppath);
  log_event(PBSEVENT_SYSTEM, 
    PBS_EVENTCLASS_SERVER,
    id,
    log_buffer);

  fd = fopen(tmppath, "w");
  if (fd)
    {
    if (LOGLEVEL >= 7)
      {
      sprintf(log_buffer, "adding cpus %s to %s", cpusbuf, tmppath);
      log_ext(-1, id, log_buffer, LOG_DEBUG);
      }
    fwrite(cpusbuf, sizeof(char), strlen(cpusbuf), fd);
    fclose(fd);
#ifdef SGI4700
    snprintf(tmppath,sizeof(tmppath),"%s/mems",path);
    fd = fopen(tmppath, "w");
    if (fd)
      {
      if (LOGLEVEL >= 7)
        {
        sprintf(log_buffer, "adding mems %s to %s", memsbuf, tmppath);
        log_ext(-1, id, log_buffer, LOG_DEBUG);
        }
      fwrite(memsbuf, sizeof(char), strlen(memsbuf), fd);
      fclose(fd);
      return(SUCCESS);
      }
#else
    return(SUCCESS);
#endif  /* end SGI4700 */
    }
    
  return(FAILURE);
  }





int create_jobset(

  job *pjob)  /* I */

  {
  char path[MAXPATHLEN+1];
  char membuf[MAXPATHLEN+1];

  mode_t savemask;

  savemask = (umask(0022));

#ifdef SGI4700
  snprintf(path,sizeof(path), "%s/%s/%s", TTORQUECPUSET_PATH, mom_host,
    pjob->ji_qs.ji_jobid);
#else
  snprintf(path,sizeof(path), "%s/%s", TTORQUECPUSET_PATH, pjob->ji_qs.ji_jobid);
#endif  /* end SGI4700 */

  if (init_jobset(path,pjob,savemask,membuf) == FAILURE)
    {
    return(FAILURE);
    }

  /* add the CPUs to the jobset */
  if (add_cpus_to_jobset(path,pjob) == FAILURE)
    {
    return(FAILURE);
    }

#ifndef SGI4700
  /* Create the vnodesets */
  if (create_vnodesets(pjob,path,membuf,savemask) == FAILURE)
    {
    return(FAILURE);
    }
#endif /* end SGI4700 */

  return(SUCCESS);
  }  /* END create_jobset() */


int move_to_jobset(

  pid_t pid,  /* I */
  job *pjob)  /* I */

  {
  char   *id = "move_to_jobset";

  char    pidbuf[MAXPATHLEN];
  char    taskspath[MAXPATHLEN];
  FILE   *fd;
  mode_t  savemask;

  savemask = (umask(0022));

  sprintf(pidbuf, "%d", pid);
#ifdef SGI4700
  sprintf(taskspath, "%s/%s/%s/tasks", TTORQUECPUSET_PATH, mom_host,
    pjob->ji_qs.ji_jobid);
#else
  sprintf(taskspath, "%s/%s/tasks", TTORQUECPUSET_PATH, pjob->ji_qs.ji_jobid);
#endif  /* end SGI4700 */
  sprintf(log_buffer, "CPUSET MOVE: %s  %s\n", taskspath, pidbuf);
  log_event(PBSEVENT_SYSTEM,
    PBS_EVENTCLASS_SERVER,
    id,
    log_buffer);

  fd = fopen(taskspath, "w");

  if (fd)
    {
    fwrite(pidbuf, sizeof(char), strlen(pidbuf), fd);
    fclose(fd);
    }
  /* ERROR HANDLING - job won't be bound correctly */

  memset(pidbuf, '\0', sizeof(pidbuf));

  umask(savemask);

  return(SUCCESS);
  }  /* END move_to_jobset() */


int move_to_taskset(

  pid_t pid, job *pjob, /* I */
  char * vnodeid)  /* I */

  {
  char   *id = "move_to_taskset";

  char    pidbuf[MAXPATHLEN];
  char    taskspath[MAXPATHLEN];
  FILE   *fd;
  mode_t  savemask;

  savemask = (umask(0022));

  sprintf(pidbuf, "%d", pid);
#ifdef SGI4700
  sprintf(taskspath, "%s/%s/%s/%s/tasks", TTORQUECPUSET_PATH, mom_host,
    pjob->ji_qs.ji_jobid,
    vnodeid);
#else
  sprintf(taskspath, "%s/%s/%s/tasks", TTORQUECPUSET_PATH, pjob->ji_qs.ji_jobid, vnodeid);
#endif  /* end SGI4700 */
  sprintf(log_buffer, "TASKSET MOVE: %s  %s\n", taskspath, pidbuf);
  log_event(PBSEVENT_SYSTEM,
    PBS_EVENTCLASS_SERVER,
    id,
    log_buffer);

  fd = fopen(taskspath, "w");

  if (fd)
    {
    fwrite(pidbuf, sizeof(char), strlen(pidbuf), fd);
    fclose(fd);
    }
  /* ERROR HANDLING - job won't be bound correctly */

  memset(pidbuf, '\0', sizeof(pidbuf));

  umask(savemask);

  return 0;
  }  /* END move_to_taskset() */


