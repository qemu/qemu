#include "qemu/osdep.h"
#include "qemu/main-loop.h"

bool bql_locked(void)
{
    return false;
}

void bql_lock_impl(const char *file, int line)
{
}

void bql_unlock(void)
{
}
