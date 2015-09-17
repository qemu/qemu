#include "sysemu/replay.h"
#include <stdlib.h>

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
