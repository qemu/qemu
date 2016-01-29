#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

bool runstate_check(RunState state)
{
    return state == RUN_STATE_PRELAUNCH;
}
