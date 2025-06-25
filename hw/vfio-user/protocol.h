#ifndef VFIO_USER_PROTOCOL_H
#define VFIO_USER_PROTOCOL_H

/*
 * vfio protocol over a UNIX socket.
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * Each message has a standard header that describes the command
 * being sent, which is almost always a VFIO ioctl().
 *
 * The header may be followed by command-specific data, such as the
 * region and offset info for read and write commands.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

typedef struct {
    uint16_t id;
    uint16_t command;
    uint32_t size;
    uint32_t flags;
    uint32_t error_reply;
} VFIOUserHdr;

/* VFIOUserHdr commands */
enum vfio_user_command {
    VFIO_USER_VERSION                   = 1,
    VFIO_USER_DMA_MAP                   = 2,
    VFIO_USER_DMA_UNMAP                 = 3,
    VFIO_USER_DEVICE_GET_INFO           = 4,
    VFIO_USER_DEVICE_GET_REGION_INFO    = 5,
    VFIO_USER_DEVICE_GET_REGION_IO_FDS  = 6,
    VFIO_USER_DEVICE_GET_IRQ_INFO       = 7,
    VFIO_USER_DEVICE_SET_IRQS           = 8,
    VFIO_USER_REGION_READ               = 9,
    VFIO_USER_REGION_WRITE              = 10,
    VFIO_USER_DMA_READ                  = 11,
    VFIO_USER_DMA_WRITE                 = 12,
    VFIO_USER_DEVICE_RESET              = 13,
    VFIO_USER_DIRTY_PAGES               = 14,
    VFIO_USER_MAX,
};

/* VFIOUserHdr flags */
#define VFIO_USER_REQUEST       0x0
#define VFIO_USER_REPLY         0x1
#define VFIO_USER_TYPE          0xF

#define VFIO_USER_NO_REPLY      0x10
#define VFIO_USER_ERROR         0x20

#endif /* VFIO_USER_PROTOCOL_H */
