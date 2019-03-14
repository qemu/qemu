/* CB_SYS_time doesn't implement the Linux time syscall; the return
   value isn't written to the argument.  */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  time_t x = (time_t) -1;
  time_t t = time (&x);

  if (t == (time_t) -1 || t != x)
    abort ();
  printf ("pass\n");
  exit (0);
}
