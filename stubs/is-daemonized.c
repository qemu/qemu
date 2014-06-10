#include "qemu-common.h"

/* Win32 has its own inline stub */
#ifndef _WIN32
bool is_daemonized(void)
{
    return false;
}
#endif
