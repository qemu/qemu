#include <asm/unistd.h>

static inline volatile void exit(int status)
{
  int __res;
  __asm__ volatile ("movl %%ecx,%%ebx\n"\
		    "int $0x80" \
		    :  "=a" (__res) : "0" (__NR_exit),"c" ((long)(status)));
}

static inline int write(int fd, const char * buf, int len)
{
  int status;
  __asm__ volatile ("pushl %%ebx\n"\
		    "movl %%esi,%%ebx\n"\
		    "int $0x80\n" \
		    "popl %%ebx\n"\
		    : "=a" (status) \
		    : "0" (__NR_write),"S" ((long)(fd)),"c" ((long)(buf)),"d" ((long)(len)));
}

void _start(void)
{
    write(1, "Hello World\n", 12);
    exit(0);
}
