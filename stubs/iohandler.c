#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"

AioContext *iohandler_get_aio_context(void)
{
    abort();
}
