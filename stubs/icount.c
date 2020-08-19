#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/cpu-timers.h"

/* icount - Instruction Counter API */

int use_icount;

void cpu_update_icount(CPUState *cpu)
{
    abort();
}
void configure_icount(QemuOpts *opts, Error **errp)
{
    /* signal error */
    error_setg(errp, "cannot configure icount, TCG support not available");
}
int64_t cpu_get_icount_raw(void)
{
    abort();
    return 0;
}
int64_t cpu_get_icount(void)
{
    abort();
    return 0;
}
int64_t cpu_icount_to_ns(int64_t icount)
{
    abort();
    return 0;
}
int64_t qemu_icount_round(int64_t count)
{
    abort();
    return 0;
}
void qemu_start_warp_timer(void)
{
    abort();
}
void qemu_account_warp_timer(void)
{
    abort();
}
