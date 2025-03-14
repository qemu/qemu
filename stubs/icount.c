#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/icount.h"

/* icount - Instruction Counter API */

ICountMode use_icount = ICOUNT_DISABLED;

bool icount_configure(QemuOpts *opts, Error **errp)
{
    /* signal error */
    error_setg(errp, "cannot configure icount, TCG support not available");

    return false;
}
int64_t icount_get_raw(void)
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
    abort();
}
