/* prints hex string argv in binary to stdout */

#include <stdio.h>
#include <ctype.h>

int char2hex(char c) {
	c = tolower((unsigned int)c);
	if (isdigit((unsigned int)c)) {
		return c-'0';
	} else if (c >= 'a' && c <= 'f') {
		return c-'a'+10;
	} else {
		return -1;
	}
}

void spew_bytes(const char *s) {
	char buf[256];
	int n1,n2;
	char *p;
again:
	p = buf;
	while(p < (buf+sizeof(buf))) {
		n1 = char2hex(*s++);
		if(n1 < 0) break;
		n2 = char2hex(*s++);
		if(n2 < 0) break;
		*p++ = (n1<<4) + n2;
	}
	if(p!=buf) write(1, buf, p-buf);
	if( (n1|n2)>=0 ) goto again;
}

int main(int argc,char** argv) {
	while(--argc > 0)
		spew_bytes(*++argv);
	return 0;
}
