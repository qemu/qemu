#include <stdio.h>
#include <stdlib.h>

/* Basic sanity check that syscalls to implement malloc (brk, mmap2,
   munmap) are trivially functional.  */

int main ()
{
  void *p1, *p2, *p3, *p4, *p5, *p6;

  if ((p1 = malloc (8100)) == NULL
      || (p2 = malloc (16300)) == NULL
      || (p3 = malloc (4000)) == NULL
      || (p4 = malloc (500)) == NULL
      || (p5 = malloc (1023*1024)) == NULL
      || (p6 = malloc (8191*1024)) == NULL)
  {
    printf ("fail\n");
    exit (1);
  }

  free (p1);
  free (p2);
  free (p3);
  free (p4);
  free (p5);
  free (p6);

  p1 = malloc (64000);
  if (p1 == NULL)
  {
    printf ("fail\n");
    exit (1);
  }
  free (p1);

  printf ("pass\n");
  exit (0);
}
