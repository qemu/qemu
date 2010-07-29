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
    unsigned long tp, old_tp;
    int ret;

    asm volatile ("move $pid,%0" : "=r" (old_tp));
    old_tp &= ~0xff;

    ret = syscall (SYS_set_thread_area, 0xf0);
    if (ret != -1 || errno != EINVAL) {
        syscall (SYS_set_thread_area, old_tp);
        perror ("Invalid thread area accepted:");
        abort();
    }

    ret = syscall (SYS_set_thread_area, 0xeddeed00);
    if (ret != 0) {
        perror ("Valid thread area not accepted: ");
        abort ();
    }

    asm volatile ("move $pid,%0" : "=r" (tp));
    tp &= ~0xff;
    syscall (SYS_set_thread_area, old_tp);

    if (tp != 0xeddeed00) {
	* (volatile int *) 0 = 0;
        perror ("tls2");
        abort ();
    }

    printf ("pass\n");
    return EXIT_SUCCESS;
}
