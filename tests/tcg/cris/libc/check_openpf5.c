/* Check that TRT happens when error on too many opened files.
#notarget: cris*-*-elf
#sim: --sysroot=@exedir@
*/
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int main (int argc, char *argv[])
{
  int i;
  int filemax;

#ifdef OPEN_MAX
  filemax = OPEN_MAX;
#else
  filemax = sysconf (_SC_OPEN_MAX);
#endif

  char *fn = malloc (strlen (argv[0]) + 2);
  if (fn == NULL)
    abort ();
  strcpy (fn, "/");
  strcat (fn, argv[0]);

  for (i = 0; i < filemax + 1; i++)
    {
      if (open (fn, O_RDONLY) < 0)
	{
	  /* Shouldn't happen too early.  */
	  if (i < filemax - 3 - 1)
	    {
	      fprintf (stderr, "i: %d\n", i);
	      abort ();
	    }
	  if (errno != EMFILE)
	    {
	      perror ("open");
	      abort ();
	    }
	  goto ok;
	}
    }
  abort ();

ok:
  printf ("pass\n");
  exit (0);
}
