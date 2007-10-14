/* Basic time functionality test: check that milliseconds are
   incremented for each syscall (does not work on host).  */
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

void err (const char *s)
{
  perror (s);
  abort ();
}

int
main (void)
{
  struct timeval t_m = {0, 0};
  struct timezone t_z = {0, 0};
  struct timeval t_m1 = {0, 0};
  int i;

  if (gettimeofday (&t_m, &t_z) != 0)
    err ("gettimeofday");

  for (i = 1; i < 10000; i++)
    if (gettimeofday (&t_m1, NULL) != 0)
      err ("gettimeofday 1");
    else
      if (t_m1.tv_sec * 1000000 + t_m1.tv_usec
	  != (t_m.tv_sec * 1000000 + t_m.tv_usec + i * 1000))
	{
	  fprintf (stderr, "t0 (%ld, %ld), i %d, t1 (%ld, %ld)\n",
		   t_m.tv_sec, t_m.tv_usec, i, t_m1.tv_sec, t_m1.tv_usec);
	  abort ();
	}

  if (time (NULL) != t_m1.tv_sec)
    {
      fprintf (stderr, "time != gettod\n");
      abort ();
    }

  printf ("pass\n");
  exit (0);
}
