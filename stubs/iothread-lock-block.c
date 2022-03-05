#include "qemu/osdep.h"
#include "qemu/main-loop.h"

bool qemu_in_main_thread(void)
{
    return qemu_get_current_aio_context() == qemu_get_aio_context();
}

