#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "block/aio.h"

bool replay_events_enabled(void)
{
    return false;
}

int64_t replay_save_clock(ReplayClockKind kind,
                          int64_t clock, int64_t raw_icount)
{
    abort();
    return 0;
}

int64_t replay_read_clock(ReplayClockKind kind, int64_t raw_icount)
{
    abort();
    return 0;
}

uint64_t replay_get_current_icount(void)
{
    return 0;
}

void replay_bh_schedule_event(QEMUBH *bh)
{
    qemu_bh_schedule(bh);
}

void replay_bh_schedule_oneshot_event(AioContext *ctx,
     QEMUBHFunc *cb, void *opaque)
{
    aio_bh_schedule_oneshot(ctx, cb, opaque);
}

bool replay_checkpoint(ReplayCheckpoint checkpoint)
{
    return true;
}

void replay_mutex_lock(void)
{
}

void replay_mutex_unlock(void)
{
}

void replay_register_char_driver(struct Chardev *chr)
{
}

void replay_chr_be_write(struct Chardev *s, const uint8_t *buf, int len)
{
    abort();
}

void replay_char_write_event_save(int res, int offset)
{
    abort();
}

void replay_char_write_event_load(int *res, int *offset)
{
    abort();
}

int replay_char_read_all_load(uint8_t *buf)
{
    abort();
}

void replay_char_read_all_save_error(int res)
{
    abort();
}

void replay_char_read_all_save_buf(uint8_t *buf, int offset)
{
    abort();
}
