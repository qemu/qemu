/*
 * Communication channel between QEMU and remote device process
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef MPQEMU_LINK_H
#define MPQEMU_LINK_H

#include "qom/object.h"
#include "qemu/thread.h"
#include "io/channel.h"
#include "exec/hwaddr.h"
#include "io/channel-socket.h"
#include "hw/remote/proxy.h"

#define REMOTE_MAX_FDS 8

#define MPQEMU_MSG_HDR_SIZE offsetof(MPQemuMsg, data.u64)

/**
 * MPQemuCmd:
 *
 * MPQemuCmd enum type to specify the command to be executed on the remote
 * device.
 *
 * This uses a private protocol between QEMU and the remote process. vfio-user
 * protocol would supersede this in the future.
 *
 */
typedef enum {
    MPQEMU_CMD_SYNC_SYSMEM,
    MPQEMU_CMD_RET,
    MPQEMU_CMD_PCI_CFGWRITE,
    MPQEMU_CMD_PCI_CFGREAD,
    MPQEMU_CMD_BAR_WRITE,
    MPQEMU_CMD_BAR_READ,
    MPQEMU_CMD_SET_IRQFD,
    MPQEMU_CMD_DEVICE_RESET,
    MPQEMU_CMD_MAX,
} MPQemuCmd;

typedef struct {
    hwaddr gpas[REMOTE_MAX_FDS];
    uint64_t sizes[REMOTE_MAX_FDS];
    off_t offsets[REMOTE_MAX_FDS];
} SyncSysmemMsg;

typedef struct {
    uint32_t addr;
    uint32_t val;
    int len;
} PciConfDataMsg;

typedef struct {
    hwaddr addr;
    uint64_t val;
    unsigned size;
    bool memory;
} BarAccessMsg;

/**
 * MPQemuMsg:
 * @cmd: The remote command
 * @size: Size of the data to be shared
 * @data: Structured data
 * @fds: File descriptors to be shared with remote device
 *
 * MPQemuMsg Format of the message sent to the remote device from QEMU.
 *
 */

typedef struct {
    int cmd;
    size_t size;

    union {
        uint64_t u64;
        PciConfDataMsg pci_conf_data;
        SyncSysmemMsg sync_sysmem;
        BarAccessMsg bar_access;
    } data;

    int fds[REMOTE_MAX_FDS];
    int num_fds;
} MPQemuMsg;

bool mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc, Error **errp);
bool mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc, Error **errp);

uint64_t mpqemu_msg_send_and_await_reply(MPQemuMsg *msg, PCIProxyDev *pdev,
                                         Error **errp);
bool mpqemu_msg_valid(MPQemuMsg *msg);

#endif
