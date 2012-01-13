/* Basic file operations (rename, unlink); once without sysroot.  We
   also test that the simulator has chdir:ed to PREFIX, when defined.  */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PREFIX
#define PREFIX
#endif

void err (const char *s)
{
  perror (s);
  abort ();
}

int main (int argc, char *argv[])
{
  FILE *f;
  struct stat buf;

  unlink (PREFIX "testfoo2.tmp");

  f = fopen ("testfoo1.tmp", "w");
  if (f == NULL)
    err ("open");
  fclose (f);

  if (rename (PREFIX "testfoo1.tmp", PREFIX "testfoo2.tmp") != 0)
    err ("rename");

  if (stat (PREFIX "testfoo2.tmp", &buf) != 0
      || !S_ISREG (buf.st_mode))
    err ("stat 1");

  if (stat ("testfoo2.tmp", &buf) != 0
      || !S_ISREG (buf.st_mode))
    err ("stat 2");

  if (unlink (PREFIX "testfoo2.tmp") != 0)
    err ("unlink");

  printf ("pass\n");
  return 0;
}
