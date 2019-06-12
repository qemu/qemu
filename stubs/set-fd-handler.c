#include "qemu/osdep.h"
#include "qemu/main-loop.h"

void qemu_set_fd_handler(int fd,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    abort();
}
