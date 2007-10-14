/*
#notarget: cris*-*-elf
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int main (void)
{
  struct stat buf;

  if (lstat (".", &buf) != 0
      || !S_ISDIR (buf.st_mode))
    abort ();
  printf ("pass\n");
  exit (0);
}
