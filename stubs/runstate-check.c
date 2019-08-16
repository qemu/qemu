#include "qemu/osdep.h"

#include "sysemu/runstate.h"
bool runstate_check(RunState state)
{
    return state == RUN_STATE_PRELAUNCH;
}
