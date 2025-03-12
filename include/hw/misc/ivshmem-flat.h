/*
 * Inter-VM Shared Memory Flat Device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (c) 2023 Linaro Ltd.
 * Authors:
 *   Gustavo Romero
 *
 */

#ifndef IVSHMEM_FLAT_H
#define IVSHMEM_FLAT_H

#include "qemu/queue.h"
#include "qemu/event_notifier.h"
#include "chardev/char-fe.h"
#include "system/memory.h"
#include "qom/object.h"
#include "hw/sysbus.h"

#define IVSHMEM_MAX_VECTOR_NUM 64

/*
 * QEMU interface:
 *  + QOM property "chardev" is the character device id of the ivshmem server
 *    socket
 *  + QOM property "shmem-size" sets the size of the RAM region shared between
 *    the device and the ivshmem server
 *  + sysbus MMIO region 0: device I/O mapped registers
 *  + sysbus MMIO region 1: shared memory with ivshmem server
 *  + sysbus IRQ 0: single output interrupt
 */

#define TYPE_IVSHMEM_FLAT "ivshmem-flat"
typedef struct IvshmemFTState IvshmemFTState;

DECLARE_INSTANCE_CHECKER(IvshmemFTState, IVSHMEM_FLAT, TYPE_IVSHMEM_FLAT)

/* Ivshmem registers. See ./docs/specs/ivshmem-spec.txt for details. */
enum ivshmem_registers {
    INTMASK = 0,
    INTSTATUS = 4,
    IVPOSITION = 8,
    DOORBELL = 12,
};

typedef struct VectorInfo {
    EventNotifier event_notifier;
    uint16_t id;
} VectorInfo;

typedef struct IvshmemPeer {
    QTAILQ_ENTRY(IvshmemPeer) next;
    VectorInfo vector[IVSHMEM_MAX_VECTOR_NUM];
    int vector_counter;
    uint16_t id;
} IvshmemPeer;

struct IvshmemFTState {
    SysBusDevice parent_obj;

    uint64_t msg_buf;
    int msg_buffered_bytes;

    QTAILQ_HEAD(, IvshmemPeer) peer;
    IvshmemPeer own;

    CharBackend server_chr;

    /* IRQ */
    qemu_irq irq;

    /* I/O registers */
    MemoryRegion iomem;
    uint32_t intmask;
    uint32_t intstatus;
    uint32_t ivposition;
    uint32_t doorbell;

    /* Shared memory */
    MemoryRegion shmem;
    int shmem_fd;
    uint32_t shmem_size;
};

#endif /* IVSHMEM_FLAT_H */
