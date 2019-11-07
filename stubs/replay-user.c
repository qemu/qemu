#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "sysemu/sysemu.h"

void replay_bh_schedule_oneshot_event(AioContext *ctx,
    QEMUBHFunc *cb, void *opaque)
{
    aio_bh_schedule_oneshot(ctx, cb, opaque);
}
