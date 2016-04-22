#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qemu/main-loop.h"

void qemu_set_fd_handler(int fd,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque)
{
    abort();
}

void aio_set_fd_handler(AioContext *ctx,
                        int fd,
                        bool is_external,
                        IOHandler *io_read,
                        IOHandler *io_write,
                        void *opaque)
{
    abort();
}
