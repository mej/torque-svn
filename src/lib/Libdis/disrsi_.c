#include "license_pbs.h" /* See here for the software license */
#include <pbs_config.h>   /* the master config generated by configure */

#include <assert.h>
#include <stddef.h>

#include "dis.h"
#include "lib_dis.h"
#include "rpp.h"
#include "tcp.h"




int disrsi_(

  struct tcp_chan *chan,
  int             *negate,
  unsigned        *value,
  unsigned         count)

  {
  int       c;
  unsigned  locval;
  unsigned  ndigs;
  char     *cp;
  char      scratch[DIS_BUFSIZ];

  if (negate == NULL)
    return DIS_INVALID;
  if (value == NULL)
    return DIS_INVALID;
  if (count == 0)
    return DIS_INVALID;

  memset(scratch, 0, sizeof(scratch));

  if (dis_umaxd == 0)
    disiui_();
  
  if (count > DIS_BUFSIZ)
    return DIS_INVALID;

  switch (c = tcp_getc(chan))
    {

    case '-':
    case '+':

      *negate = c == '-';

      if (tcp_gets(chan, scratch, count) != (int)count)
        {
        return(DIS_EOD);
        }

      if (count > dis_umaxd)
        goto overflow;
      if (count == dis_umaxd)
        {
        if (memcmp(scratch, dis_umax, dis_umaxd) > 0)
          goto overflow;
        }

      cp = scratch;

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
        if (tcp_gets(chan, scratch + 1, count - 1) != (int)count - 1)
          {
          return(DIS_EOD);
          }

        cp = scratch;

        if (count >= dis_umaxd)
          {
          if (count > dis_umaxd)
            break;

          *cp = c;

          if (memcmp(scratch, dis_umax, dis_umaxd) > 0)
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

      return(disrsi_(chan, negate, value, ndigs));
      break;

    case -1:
      return(DIS_EOD);
      break;

    case -2:
      return(DIS_EOF);
      break;

    default:
      return(DIS_NONDIGIT);
      break;
    }

  *negate = FALSE;

overflow:

  *value = UINT_MAX;

  return(DIS_OVERFLOW);
  }  /* END disrsi_() */




