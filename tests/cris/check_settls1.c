#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>

#ifndef SYS_set_thread_area
#define SYS_set_thread_area 243
#endif

int main (void)
{
    unsigned long tp;
    int ret;

    ret = syscall (SYS_set_thread_area, 0xf0);
    if (ret != -1 || errno != EINVAL) {
        perror ("Invalid thread area accepted:");
        abort();
    }

    ret = syscall (SYS_set_thread_area, 0xeddeed00);
    if (ret != 0) {
        perror ("Valid thread area not accepted: ");
        abort ();
    }

    asm ("move $pid,%0" : "=r" (tp));
    tp &= ~0xff;

    if (tp != 0xeddeed00) {
        perror ("tls2");
        abort ();
    }

    printf ("pass\n");
    return EXIT_SUCCESS;
}
