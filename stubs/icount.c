#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/cpu-timers.h"

/* icount - Instruction Counter API */

int use_icount;

void icount_update(CPUState *cpu)
{
    abort();
}
void icount_configure(QemuOpts *opts, Error **errp)
{
    /* signal error */
    error_setg(errp, "cannot configure icount, TCG support not available");
}
int64_t icount_get_raw(void)
{
    abort();
    return 0;
}
int64_t icount_get(void)
{
    abort();
    return 0;
}
int64_t icount_to_ns(int64_t icount)
{
    abort();
    return 0;
}
int64_t icount_round(int64_t count)
{
    abort();
    return 0;
}
void icount_start_warp_timer(void)
{
    abort();
}
void icount_account_warp_timer(void)
{
    abort();
}

void icount_notify_exit(void)
{
}
