/*
#notarget: cris*-*-elf
*/

#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>

int main (int argc, char *argv[])
{
  volatile unsigned char *a;

  /* Check that we can map a non-multiple of a page and still get a full page.  */
  a = mmap (NULL, 0x4c, PROT_READ | PROT_WRITE | PROT_EXEC,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (a == NULL || a == (unsigned char *) -1)
    abort ();

  a[0] = 0xbe;
  a[8191] = 0xef;
  memset ((char *) a + 1, 0, 8190);

  if (a[0] != 0xbe || a[8191] != 0xef)
    abort ();

  printf ("pass\n");
  exit (0);
}
