#include "qemu/osdep.h"
#include "sysemu/replay.h"
#include "sysemu/sysemu.h"

ReplayMode replay_mode;

int64_t replay_save_clock(unsigned int kind, int64_t clock)
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
