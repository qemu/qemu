/*
 * Copyright © 2020, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL-v2, version 2 or later.
 *
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "hw/remote/machine.h"
#include "io/channel.h"
#include "hw/remote/mpqemu-link.h"
#include "qapi/error.h"
#include "sysemu/runstate.h"
#include "hw/pci/pci.h"
#include "exec/memattrs.h"
#include "hw/remote/memory.h"
#include "hw/remote/iohub.h"
#include "sysemu/reset.h"

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg, Error **errp);
static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg, Error **errp);
static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);
static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp);
static void process_device_reset_msg(QIOChannel *ioc, PCIDevice *dev,
                                     Error **errp);

void coroutine_fn mpqemu_remote_msg_loop_co(void *data)
{
    g_autofree RemoteCommDev *com = (RemoteCommDev *)data;
    PCIDevice *pci_dev = NULL;
    Error *local_err = NULL;

    assert(com->ioc);

    pci_dev = com->dev;
    for (; !local_err;) {
        MPQemuMsg msg = {0};

        if (!mpqemu_msg_recv(&msg, com->ioc, &local_err)) {
            break;
        }

        if (!mpqemu_msg_valid(&msg)) {
            error_setg(&local_err, "Received invalid message from proxy"
                                   "in remote process pid="FMT_pid"",
                                   getpid());
            break;
        }

        switch (msg.cmd) {
        case MPQEMU_CMD_PCI_CFGWRITE:
            process_config_write(com->ioc, pci_dev, &msg, &local_err);
            break;
        case MPQEMU_CMD_PCI_CFGREAD:
            process_config_read(com->ioc, pci_dev, &msg, &local_err);
            break;
        case MPQEMU_CMD_BAR_WRITE:
            process_bar_write(com->ioc, &msg, &local_err);
            break;
        case MPQEMU_CMD_BAR_READ:
            process_bar_read(com->ioc, &msg, &local_err);
            break;
        case MPQEMU_CMD_SYNC_SYSMEM:
            remote_sysmem_reconfig(&msg, &local_err);
            break;
        case MPQEMU_CMD_SET_IRQFD:
            process_set_irqfd_msg(pci_dev, &msg);
            break;
        case MPQEMU_CMD_DEVICE_RESET:
            process_device_reset_msg(com->ioc, pci_dev, &local_err);
            break;
        default:
            error_setg(&local_err,
                       "Unknown command (%d) received for device %s"
                       " (pid="FMT_pid")",
                       msg.cmd, DEVICE(pci_dev)->id, getpid());
        }
    }

    if (local_err) {
        error_report_err(local_err);
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_ERROR);
    } else {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void process_config_write(QIOChannel *ioc, PCIDevice *dev,
                                 MPQemuMsg *msg, Error **errp)
{
    ERRP_GUARD();
    PciConfDataMsg *conf = (PciConfDataMsg *)&msg->data.pci_conf_data;
    MPQemuMsg ret = { 0 };

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_setg(errp, "Bad address for PCI config write, pid "FMT_pid".",
                   getpid());
        ret.data.u64 = UINT64_MAX;
    } else {
        pci_default_write_config(dev, conf->addr, conf->val, conf->len);
    }

    ret.cmd = MPQEMU_CMD_RET;
    ret.size = sizeof(ret.data.u64);

    if (!mpqemu_msg_send(&ret, ioc, NULL)) {
        error_prepend(errp, "Error returning code to proxy, pid "FMT_pid": ",
                      getpid());
    }
}

static void process_config_read(QIOChannel *ioc, PCIDevice *dev,
                                MPQemuMsg *msg, Error **errp)
{
    ERRP_GUARD();
    PciConfDataMsg *conf = (PciConfDataMsg *)&msg->data.pci_conf_data;
    MPQemuMsg ret = { 0 };

    if ((conf->addr + sizeof(conf->val)) > pci_config_size(dev)) {
        error_setg(errp, "Bad address for PCI config read, pid "FMT_pid".",
                   getpid());
        ret.data.u64 = UINT64_MAX;
    } else {
        ret.data.u64 = pci_default_read_config(dev, conf->addr, conf->len);
    }

    ret.cmd = MPQEMU_CMD_RET;
    ret.size = sizeof(ret.data.u64);

    if (!mpqemu_msg_send(&ret, ioc, NULL)) {
        error_prepend(errp, "Error returning code to proxy, pid "FMT_pid": ",
                      getpid());
    }
}

static void process_bar_write(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    ERRP_GUARD();
    BarAccessMsg *bar_access = &msg->data.bar_access;
    AddressSpace *as =
        bar_access->memory ? &address_space_memory : &address_space_io;
    MPQemuMsg ret = { 0 };
    MemTxResult res;
    uint64_t val;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        ret.data.u64 = UINT64_MAX;
        goto fail;
    }

    val = cpu_to_le64(bar_access->val);

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&val, bar_access->size, true);

    if (res != MEMTX_OK) {
        error_setg(errp, "Bad address %"PRIx64" for mem write, pid "FMT_pid".",
                   bar_access->addr, getpid());
        ret.data.u64 = -1;
    }

fail:
    ret.cmd = MPQEMU_CMD_RET;
    ret.size = sizeof(ret.data.u64);

    if (!mpqemu_msg_send(&ret, ioc, NULL)) {
        error_prepend(errp, "Error returning code to proxy, pid "FMT_pid": ",
                      getpid());
    }
}

static void process_bar_read(QIOChannel *ioc, MPQemuMsg *msg, Error **errp)
{
    ERRP_GUARD();
    BarAccessMsg *bar_access = &msg->data.bar_access;
    MPQemuMsg ret = { 0 };
    AddressSpace *as;
    MemTxResult res;
    uint64_t val = 0;

    as = bar_access->memory ? &address_space_memory : &address_space_io;

    if (!is_power_of_2(bar_access->size) ||
       (bar_access->size > sizeof(uint64_t))) {
        val = UINT64_MAX;
        goto fail;
    }

    res = address_space_rw(as, bar_access->addr, MEMTXATTRS_UNSPECIFIED,
                           (void *)&val, bar_access->size, false);

    if (res != MEMTX_OK) {
        error_setg(errp, "Bad address %"PRIx64" for mem read, pid "FMT_pid".",
                   bar_access->addr, getpid());
        val = UINT64_MAX;
    }

fail:
    ret.cmd = MPQEMU_CMD_RET;
    ret.data.u64 = le64_to_cpu(val);
    ret.size = sizeof(ret.data.u64);

    if (!mpqemu_msg_send(&ret, ioc, NULL)) {
        error_prepend(errp, "Error returning code to proxy, pid "FMT_pid": ",
                      getpid());
    }
}

static void process_device_reset_msg(QIOChannel *ioc, PCIDevice *dev,
                                     Error **errp)
{
    DeviceClass *dc = DEVICE_GET_CLASS(dev);
    DeviceState *s = DEVICE(dev);
    MPQemuMsg ret = { 0 };

    if (dc->reset) {
        dc->reset(s);
    }

    ret.cmd = MPQEMU_CMD_RET;

    mpqemu_msg_send(&ret, ioc, errp);
}
