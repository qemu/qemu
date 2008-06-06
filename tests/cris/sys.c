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
	asm volatile ("moveq 1, $r9\n" /* NR_exit.  */
		      "break 13\n");
	while(1)
		;
}

ssize_t write (int fd, const void *buf, size_t count) {
	int r;
	asm ("move.d %0, $r10\n"
	     "move.d %1, $r11\n"
	     "move.d %2, $r12\n"
	     "moveq 4, $r9\n" /* NR_write.  */
	     "break 13\n" : : "r" (fd), "r" (buf), "r" (count) : "memory");
	asm ("move.d $r10, %0\n" : "=r" (r));
	return r;
}
