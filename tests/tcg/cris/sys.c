#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static inline int mystrlen(char *s) {
	int i = 0;
	while (s[i])
		i++;
	return i;
}

void pass(void) {
	char s[] = "passed.\n";
	write (1, s, sizeof (s) - 1);
	exit (0);
}

void _fail(char *reason) {
	char s[] = "\nfailed: ";
	int len = mystrlen(reason);
	write (1, s, sizeof (s) - 1);
	write (1, reason, len);
	write (1, "\n", 1);
//	exit (1);
}

void *memset (void *s, int c, size_t n) {
	char *p = s;
	int i;
	for (i = 0; i < n; i++)
		p[i] = c;
	return p;
}

void exit (int status) {
	register unsigned int callno asm ("r9") = 1; /* NR_exit */

	asm volatile ("break 13\n"
		      :
		      : "r" (callno)
		      : "memory" );
	while(1)
		;
}

ssize_t write (int fd, const void *buf, size_t count) {
	register unsigned int callno asm ("r9") = 4; /* NR_write */
	register unsigned int r10 asm ("r10") = fd;
	register const void *r11 asm ("r11") = buf;
	register size_t r12 asm ("r12") = count;
	register unsigned int r asm ("r10");

	asm volatile ("break 13\n"
	     : "=r" (r)
	     : "r" (callno), "0" (r10), "r" (r11), "r" (r12)
	     : "memory");

	return r;
}
