/*
 * Virtio 9p Proxy callback
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 * M. Mohan Kumar <mohan@in.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */
#ifndef _QEMU_VIRTIO_9P_PROXY_H
#define _QEMU_VIRTIO_9P_PROXY_H

#define PROXY_MAX_IO_SZ (64 * 1024)

/*
 * proxy iovec only support one element and
 * marsha/unmarshal doesn't do little endian conversion.
 */
#define proxy_unmarshal(in_sg, offset, fmt, args...) \
    v9fs_unmarshal(in_sg, 1, offset, 0, fmt, ##args)
#define proxy_marshal(out_sg, offset, fmt, args...) \
    v9fs_marshal(out_sg, 1, offset, 0, fmt, ##args)

typedef struct {
    uint32_t type;
    uint32_t size;
} ProxyHeader;

#define PROXY_HDR_SZ (sizeof(ProxyHeader))

#endif
