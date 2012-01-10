#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#define MAGIC (0xdeadbeef)

int s = 0;
void sighandler(int sig)
{
	s = MAGIC;
}

int main(int argc, char **argv)
{
	int p;

	p = getpid();
	signal(SIGALRM, sighandler);
	kill(p, SIGALRM);
	if (s != MAGIC)
		return EXIT_FAILURE;

	printf ("passed\n");
	return EXIT_SUCCESS;
}
