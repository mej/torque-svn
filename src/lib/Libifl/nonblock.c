/*
 * Defns of nonblocking read,write.
 * Headers redefine read/write to name these instead, before inclusion
 * of stdio.h, so system declaration is used.
 */
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>



ssize_t write_nonblocking_socket(

  int     fd,
  void   *buf,
  ssize_t count)

  {
  ssize_t i;
  time_t  start, now;

  /* NOTE:  under some circumstances, a blocking fd will be passed */

  /* Set a timer to prevent an infinite loop here. */
  time(&now);
  start = now;

  for (;;)
    {
    i = write(fd, buf, count);

    if (i >= 0)
      {
      /* successfully wrote 'i' bytes */

      return(i);
      }

    if (errno != EAGAIN)
      {
      /* write failed */

      return(i);
      }


    time(&now);
    if ((now - start) > 30)
      {
      /* timed out */

      return(i);
      }
    }    /* END for () */

  /*NOTREACHED*/

  return(0);
  }  /* END write_nonblocking_socket() */



ssize_t read_nonblocking_socket(

  int     fd,
  void   *buf,
  ssize_t count)

  {
  ssize_t i;
  time_t  start, now;

  /* NOTE:  under some circumstances, a blocking fd will be passed */

  /* Set a timer to prevent an infinite loop here. */

  start = -1;

  for (;;)
    {
    i = read(fd, buf, count);

    if (i >= 0)
      {
      return(i);
      }

    if (errno != EAGAIN)
      {
      return(i);
      }

    time(&now);

    if (start == -1)
      {
      start = now;
      }
    else if ((now - start) > 30)
      {
      return(i);
      }
    }    /* END for () */

  /*NOTREACHED*/

  return(0);
  }  /* END read_nonblocking_socket() */





/*
 * Call the real read, for things that want to block.
 */

ssize_t read_blocking_socket(

  int      fd,
  void    *buf,
  ssize_t  count)

  {
  return(read(fd, buf, count));
  }

