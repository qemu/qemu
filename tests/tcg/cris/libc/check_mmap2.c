/*
#notarget: cris*-*-elf
*/

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main (int argc, char *argv[])
{
  int fd = open (argv[0], O_RDONLY);
  struct stat sb;
  int size;
  void *a;
  const char *str = "a string you'll only find in the program";

  if (fd == -1)
    {
      perror ("open");
      abort ();
    }

  if (fstat (fd, &sb) < 0)
    {
      perror ("fstat");
      abort ();
    }

  size = sb.st_size;

  /* We want to test mmapping a size that isn't exactly a page.  */
  if ((size & 8191) == 0)
    size--;

  a = mmap (NULL, size, PROT_READ, MAP_SHARED, fd, 0);

  if (memmem (a, size, str, strlen (str) + 1) == NULL)
    abort ();

  printf ("pass\n");
  exit (0);
}
