#ifndef _QEMU_VIRTIO_9P_MARSHAL_H
#define _QEMU_VIRTIO_9P_MARSHAL_H

#include "9p-marshal.h"


ssize_t v9fs_pack(struct iovec *in_sg, int in_num, size_t offset,
                  const void *src, size_t size);
ssize_t v9fs_unmarshal(struct iovec *out_sg, int out_num, size_t offset,
                       int bswap, const char *fmt, ...);
ssize_t v9fs_marshal(struct iovec *in_sg, int in_num, size_t offset,
                     int bswap, const char *fmt, ...);
#endif
