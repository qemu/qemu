/*
This program sends out arbitrary packet read from standard input.
Based on a program by:
yuri volobuev'97
volobuev@t1.chem.umn.edu
*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>

#ifdef linux
#define	NEWSOCKET()	socket(AF_INET, SOCK_PACKET, htons(ETH_P_RARP))
#else
#define	NEWSOCKET()	socket(SOL_SOCKET, SOCK_RAW, ETHERTYPE_REVARP)
#endif

#define perror(msg) puts("Error in " msg "()")

char pkt[64*1024];

int read_upto(int fd, void* buf, size_t size) {
	size_t done = 0;
	while(1) {
		int sz = read(fd, buf, size);
		if(sz < 0)
			return sz;
		if(!sz)
			return done;
		done += sz;
		size -= sz;
		if(!size)
			return done;
		buf = ((char*)buf) + sz;
	}
}

int main(int argc, char** argv) {
	if (argc != 2) {
		puts("Usage: sendpkt <iface>");
		exit(1);
	}

	int sock = NEWSOCKET();
	if (sock < 0) {
		perror("socket");
		exit(1);
	}

	int sz = read_upto(0, pkt, sizeof(pkt));
	if(sz < 0) {
		perror("read");
		exit(1);
	}

	struct sockaddr sa;
	strncpy(sa.sa_data, argv[1], sizeof(sa.sa_data)); /* device to send it over */

	int err = sendto(sock, pkt, sz, 0, &sa, sizeof(sa));
	if (err < 0) {
		perror("sendto");
		exit(1);
	}
	exit(0);
}
