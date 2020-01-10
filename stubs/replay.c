#include "qemu/osdep.h"
#include "sysemu/replay.h"

ReplayMode replay_mode;

int64_t replay_save_clock(unsigned int kind, int64_t clock, int64_t raw_icount)
{
    abort();
    return 0;
}

int64_t replay_read_clock(unsigned int kind)
{
    abort();
    return 0;
}

bool replay_checkpoint(ReplayCheckpoint checkpoint)
{
    return true;
}

bool replay_events_enabled(void)
{
    return false;
}

void replay_finish(void)
{
}

void replay_register_char_driver(Chardev *chr)
{
}

void replay_chr_be_write(Chardev *s, uint8_t *buf, int len)
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

void replay_block_event(QEMUBH *bh, uint64_t id)
{
}

uint64_t blkreplay_next_id(void)
{
    return 0;
}

void replay_mutex_lock(void)
{
}

void replay_mutex_unlock(void)
{
}

void replay_save_random(int ret, void *buf, size_t len)
{
}

int replay_read_random(void *buf, size_t len)
{
    return 0;
}
