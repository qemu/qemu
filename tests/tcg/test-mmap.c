/*
 * Small test program to verify simulated mmap behaviour.
 *
 * When running qemu-linux-user with the -p flag, you may need to tell
 * this test program about the pagesize because getpagesize() will not reflect
 * the -p choice. Simply pass one argument beeing the pagesize.
 *
 * Copyright (c) 2007 AXIS Communications AB
 * Written by Edgar E. Iglesias.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/mman.h>

#define D(x)

#define fail_unless(x)                                         \
do                                                             \
{                                                              \
  if (!(x)) {                                                  \
    fprintf (stderr, "FAILED at %s:%d\n", __FILE__, __LINE__); \
    exit (EXIT_FAILURE);                                       \
  }                                                            \
} while (0);

unsigned char *dummybuf;
static unsigned int pagesize;
static unsigned int pagemask;
int test_fd;
size_t test_fsize;

void check_aligned_anonymous_unfixed_mmaps(void)
{
	void *p1;
	void *p2;
	void *p3;
	void *p4;
	void *p5;
	uintptr_t p;
	int i;

	fprintf (stderr, "%s", __func__);
	for (i = 0; i < 0x1fff; i++)
	{
		size_t len;

		len = pagesize + (pagesize * i & 7);
		p1 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		p2 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		p3 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		p4 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		p5 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		/* Make sure we get pages aligned with the pagesize. The
		   target expects this.  */
		fail_unless (p1 != MAP_FAILED);
		fail_unless (p2 != MAP_FAILED);
		fail_unless (p3 != MAP_FAILED);
		fail_unless (p4 != MAP_FAILED);
		fail_unless (p5 != MAP_FAILED);
		p = (uintptr_t) p1;
		D(printf ("p=%x\n", p));
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p2;
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p3;
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p4;
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p5;
		fail_unless ((p & pagemask) == 0);

		/* Make sure we can read from the entire area.  */
		memcpy (dummybuf, p1, pagesize);
		memcpy (dummybuf, p2, pagesize);
		memcpy (dummybuf, p3, pagesize);
		memcpy (dummybuf, p4, pagesize);
		memcpy (dummybuf, p5, pagesize);

		munmap (p1, len);
		munmap (p2, len);
		munmap (p3, len);
		munmap (p4, len);
		munmap (p5, len);
	}
	fprintf (stderr, " passed\n");
}

void check_large_anonymous_unfixed_mmap(void)
{
	void *p1;
	uintptr_t p;
	size_t len;

	fprintf (stderr, "%s", __func__);

	len = 0x02000000;
	p1 = mmap(NULL, len, PROT_READ, 
		  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	/* Make sure we get pages aligned with the pagesize. The
	   target expects this.  */
	fail_unless (p1 != MAP_FAILED);
	p = (uintptr_t) p1;
	fail_unless ((p & pagemask) == 0);
	
	/* Make sure we can read from the entire area.  */
	memcpy (dummybuf, p1, pagesize);
	munmap (p1, len);
	fprintf (stderr, " passed\n");
}

void check_aligned_anonymous_unfixed_colliding_mmaps(void)
{
	char *p1;
	char *p2;
	char *p3;
	uintptr_t p;
	int i;

	fprintf (stderr, "%s", __func__);
	for (i = 0; i < 0x2fff; i++)
	{
		int nlen;
		p1 = mmap(NULL, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		fail_unless (p1 != MAP_FAILED);
		p = (uintptr_t) p1;
		fail_unless ((p & pagemask) == 0);
		memcpy (dummybuf, p1, pagesize);

		p2 = mmap(NULL, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		fail_unless (p2 != MAP_FAILED);
		p = (uintptr_t) p2;
		fail_unless ((p & pagemask) == 0);
		memcpy (dummybuf, p2, pagesize);


		munmap (p1, pagesize);
		nlen = pagesize * 8;
		p3 = mmap(NULL, nlen, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		fail_unless (p3 != MAP_FAILED);

		/* Check if the mmaped areas collide.  */
		if (p3 < p2 
		    && (p3 + nlen) > p2)
			fail_unless (0);

		memcpy (dummybuf, p3, pagesize);

		/* Make sure we get pages aligned with the pagesize. The
		   target expects this.  */
		p = (uintptr_t) p3;
		fail_unless ((p & pagemask) == 0);
		munmap (p2, pagesize);
		munmap (p3, nlen);
	}
	fprintf (stderr, " passed\n");
}

void check_aligned_anonymous_fixed_mmaps(void)
{
	char *addr;
	void *p1;
	uintptr_t p;
	int i;

	/* Find a suitable address to start with.  */
	addr = mmap(NULL, pagesize * 40, PROT_READ | PROT_WRITE, 
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	fprintf (stderr, "%s addr=%p", __func__, addr);
	fail_unless (addr != MAP_FAILED);

	for (i = 0; i < 40; i++)
	{
		/* Create submaps within our unfixed map.  */
		p1 = mmap(addr, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			  -1, 0);
		/* Make sure we get pages aligned with the pagesize. 
		   The target expects this.  */
		p = (uintptr_t) p1;
		fail_unless (p1 == addr);
		fail_unless ((p & pagemask) == 0);		
		memcpy (dummybuf, p1, pagesize);
		munmap (p1, pagesize);
		addr += pagesize;
	}
	fprintf (stderr, " passed\n");
}

void check_aligned_anonymous_fixed_mmaps_collide_with_host(void)
{
	char *addr;
	void *p1;
	uintptr_t p;
	int i;

	/* Find a suitable address to start with.  Right were the x86 hosts
	 stack is.  */
	addr = ((void *)0x80000000);
	fprintf (stderr, "%s addr=%p", __func__, addr);
	fprintf (stderr, "FIXME: QEMU fails to track pages used by the host.");

	for (i = 0; i < 20; i++)
	{
		/* Create submaps within our unfixed map.  */
		p1 = mmap(addr, pagesize, PROT_READ | PROT_WRITE, 
			  MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			  -1, 0);
		/* Make sure we get pages aligned with the pagesize. 
		   The target expects this.  */
		p = (uintptr_t) p1;
		fail_unless (p1 == addr);
		fail_unless ((p & pagemask) == 0);		
		memcpy (p1, dummybuf, pagesize);
		munmap (p1, pagesize);
		addr += pagesize;
	}
	fprintf (stderr, " passed\n");
}

void check_file_unfixed_mmaps(void)
{
	unsigned int *p1, *p2, *p3;
	uintptr_t p;
	int i;

	fprintf (stderr, "%s", __func__);
	for (i = 0; i < 0x10; i++)
	{
		size_t len;

		len = pagesize;
		p1 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE, 
			  test_fd, 0);
		p2 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE, 
			  test_fd, pagesize);
		p3 = mmap(NULL, len, PROT_READ, 
			  MAP_PRIVATE, 
			  test_fd, pagesize * 2);

		fail_unless (p1 != MAP_FAILED);
		fail_unless (p2 != MAP_FAILED);
		fail_unless (p3 != MAP_FAILED);

		/* Make sure we get pages aligned with the pagesize. The
		   target expects this.  */
		p = (uintptr_t) p1;
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p2;
		fail_unless ((p & pagemask) == 0);
		p = (uintptr_t) p3;
		fail_unless ((p & pagemask) == 0);

		/* Verify that the file maps was made correctly.  */
		D(printf ("p1=%d p2=%d p3=%d\n", *p1, *p2, *p3));
		fail_unless (*p1 == 0);
		fail_unless (*p2 == (pagesize / sizeof *p2));
		fail_unless (*p3 == ((pagesize * 2) / sizeof *p3));

		memcpy (dummybuf, p1, pagesize);
		memcpy (dummybuf, p2, pagesize);
		memcpy (dummybuf, p3, pagesize);
		munmap (p1, len);
		munmap (p2, len);
		munmap (p3, len);
	}
	fprintf (stderr, " passed\n");
}

void check_file_unfixed_eof_mmaps(void)
{
	char *cp;
	unsigned int *p1;
	uintptr_t p;
	int i;

	fprintf (stderr, "%s", __func__);
	for (i = 0; i < 0x10; i++)
	{
		p1 = mmap(NULL, pagesize, PROT_READ, 
			  MAP_PRIVATE, 
			  test_fd, 
			  (test_fsize - sizeof *p1) & ~pagemask);

		fail_unless (p1 != MAP_FAILED);

		/* Make sure we get pages aligned with the pagesize. The
		   target expects this.  */
		p = (uintptr_t) p1;
		fail_unless ((p & pagemask) == 0);
		/* Verify that the file maps was made correctly.  */
		fail_unless (p1[(test_fsize & pagemask) / sizeof *p1 - 1]
			     == ((test_fsize - sizeof *p1) / sizeof *p1));

		/* Verify that the end of page is accessible and zeroed.  */
		cp = (void *) p1;
		fail_unless (cp[pagesize - 4] == 0);
		munmap (p1, pagesize);
	}
	fprintf (stderr, " passed\n");
}

void check_file_fixed_eof_mmaps(void)
{
	char *addr;
	char *cp;
	unsigned int *p1;
	uintptr_t p;
	int i;

	/* Find a suitable address to start with.  */
	addr = mmap(NULL, pagesize * 44, PROT_READ, 
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);

	fprintf (stderr, "%s addr=%p", __func__, (void *)addr);
	fail_unless (addr != MAP_FAILED);

	for (i = 0; i < 0x10; i++)
	{
		/* Create submaps within our unfixed map.  */
		p1 = mmap(addr, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_FIXED, 
			  test_fd, 
			  (test_fsize - sizeof *p1) & ~pagemask);

		fail_unless (p1 != MAP_FAILED);

		/* Make sure we get pages aligned with the pagesize. The
		   target expects this.  */
		p = (uintptr_t) p1;
		fail_unless ((p & pagemask) == 0);

		/* Verify that the file maps was made correctly.  */
		fail_unless (p1[(test_fsize & pagemask) / sizeof *p1 - 1]
			     == ((test_fsize - sizeof *p1) / sizeof *p1));

		/* Verify that the end of page is accessible and zeroed.  */
		cp = (void *)p1;
		fail_unless (cp[pagesize - 4] == 0);
		munmap (p1, pagesize);
		addr += pagesize;
	}
	fprintf (stderr, " passed\n");
}

void check_file_fixed_mmaps(void)
{
	unsigned char *addr;
	unsigned int *p1, *p2, *p3, *p4;
	int i;

	/* Find a suitable address to start with.  */
	addr = mmap(NULL, pagesize * 40 * 4, PROT_READ, 
		    MAP_PRIVATE | MAP_ANONYMOUS,
		    -1, 0);
	fprintf (stderr, "%s addr=%p", __func__, (void *)addr);
	fail_unless (addr != MAP_FAILED);

	for (i = 0; i < 40; i++)
	{
		p1 = mmap(addr, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_FIXED,
			  test_fd, 0);
		p2 = mmap(addr + pagesize, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_FIXED,
			  test_fd, pagesize);
		p3 = mmap(addr + pagesize * 2, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_FIXED,
			  test_fd, pagesize * 2);
		p4 = mmap(addr + pagesize * 3, pagesize, PROT_READ, 
			  MAP_PRIVATE | MAP_FIXED,
			  test_fd, pagesize * 3);

		/* Make sure we get pages aligned with the pagesize. 
		   The target expects this.  */
		fail_unless (p1 == (void *)addr);
		fail_unless (p2 == (void *)addr + pagesize);
		fail_unless (p3 == (void *)addr + pagesize * 2);
		fail_unless (p4 == (void *)addr + pagesize * 3);

		/* Verify that the file maps was made correctly.  */
		fail_unless (*p1 == 0);
		fail_unless (*p2 == (pagesize / sizeof *p2));
		fail_unless (*p3 == ((pagesize * 2) / sizeof *p3));
		fail_unless (*p4 == ((pagesize * 3) / sizeof *p4));

		memcpy (dummybuf, p1, pagesize);
		memcpy (dummybuf, p2, pagesize);
		memcpy (dummybuf, p3, pagesize);
		memcpy (dummybuf, p4, pagesize);

		munmap (p1, pagesize);
		munmap (p2, pagesize);
		munmap (p3, pagesize);
		munmap (p4, pagesize);
		addr += pagesize * 4;
	}
	fprintf (stderr, " passed\n");
}

int main(int argc, char **argv)
{
	char tempname[] = "/tmp/.cmmapXXXXXX";
	unsigned int i;

	/* Trust the first argument, otherwise probe the system for our
	   pagesize.  */
	if (argc > 1)
		pagesize = strtoul(argv[1], NULL, 0);
	else
		pagesize = sysconf(_SC_PAGESIZE);

	/* Assume pagesize is a power of two.  */
	pagemask = pagesize - 1;
	dummybuf = malloc (pagesize);
	printf ("pagesize=%u pagemask=%x\n", pagesize, pagemask);

	test_fd = mkstemp(tempname);
	unlink(tempname);

	/* Fill the file with int's counting from zero and up.  */
	for (i = 0; i < (pagesize * 4) / sizeof i; i++)
		write (test_fd, &i, sizeof i);
	/* Append a few extra writes to make the file end at non 
	   page boundary.  */
	write (test_fd, &i, sizeof i); i++;
	write (test_fd, &i, sizeof i); i++;
	write (test_fd, &i, sizeof i); i++;

	test_fsize = lseek(test_fd, 0, SEEK_CUR);

	/* Run the tests.  */
	check_aligned_anonymous_unfixed_mmaps();
	check_aligned_anonymous_unfixed_colliding_mmaps();
	check_aligned_anonymous_fixed_mmaps();
	check_file_unfixed_mmaps();
	check_file_fixed_mmaps();
	check_file_fixed_eof_mmaps();
	check_file_unfixed_eof_mmaps();

	/* Fails at the moment.  */
	/* check_aligned_anonymous_fixed_mmaps_collide_with_host(); */

	return EXIT_SUCCESS;
}
