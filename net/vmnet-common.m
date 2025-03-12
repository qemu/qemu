/*
 * vmnet-common.m - network client wrapper for Apple vmnet.framework
 *
 * Copyright(c) 2022 Vladislav Yaroshchuk <vladislav.yaroshchuk@jetbrains.com>
 * Copyright(c) 2021 Phillip Tennen <phillip@axleos.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qapi/qapi-types-net.h"
#include "vmnet_int.h"
#include "clients.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "system/runstate.h"
#include "net/eth.h"

#include <vmnet/vmnet.h>
#include <dispatch/dispatch.h>


static void vmnet_send_completed(NetClientState *nc, ssize_t len);


const char *vmnet_status_map_str(vmnet_return_t status)
{
    switch (status) {
    case VMNET_SUCCESS:
        return "success";
    case VMNET_FAILURE:
        return "general failure (possibly not enough privileges)";
    case VMNET_MEM_FAILURE:
        return "memory allocation failure";
    case VMNET_INVALID_ARGUMENT:
        return "invalid argument specified";
    case VMNET_SETUP_INCOMPLETE:
        return "interface setup is not complete";
    case VMNET_INVALID_ACCESS:
        return "invalid access, permission denied";
    case VMNET_PACKET_TOO_BIG:
        return "packet size is larger than MTU";
    case VMNET_BUFFER_EXHAUSTED:
        return "buffers exhausted in kernel";
    case VMNET_TOO_MANY_PACKETS:
        return "packet count exceeds limit";
    case VMNET_SHARING_SERVICE_BUSY:
        return "conflict, sharing service is in use";
    default:
        return "unknown vmnet error";
    }
}


/**
 * Write packets from QEMU to vmnet interface.
 *
 * vmnet.framework supports iov, but writing more than
 * one iov into vmnet interface fails with
 * 'VMNET_INVALID_ARGUMENT'. Collecting provided iovs into
 * one and passing it to vmnet works fine. That's the
 * reason why receive_iov() left unimplemented. But it still
 * works with good performance having .receive() only.
 */
ssize_t vmnet_receive_common(NetClientState *nc,
                             const uint8_t *buf,
                             size_t size)
{
    VmnetState *s = DO_UPCAST(VmnetState, nc, nc);
    struct vmpktdesc packet;
    struct iovec iov;
    int pkt_cnt;
    vmnet_return_t if_status;

    if (size > s->max_packet_size) {
        warn_report("vmnet: packet is too big, %zu > %" PRIu64,
            packet.vm_pkt_size,
            s->max_packet_size);
        return -1;
    }

    iov.iov_base = (char *) buf;
    iov.iov_len = size;

    packet.vm_pkt_iovcnt = 1;
    packet.vm_flags = 0;
    packet.vm_pkt_size = size;
    packet.vm_pkt_iov = &iov;
    pkt_cnt = 1;

    if_status = vmnet_write(s->vmnet_if, &packet, &pkt_cnt);
    if (if_status != VMNET_SUCCESS) {
        error_report("vmnet: write error: %s",
                     vmnet_status_map_str(if_status));
        return -1;
    }

    if (pkt_cnt) {
        return size;
    }
    return 0;
}


/**
 * Read packets from vmnet interface and write them
 * to temporary buffers in VmnetState.
 *
 * Returns read packets number (may be 0) on success,
 * -1 on error
 */
static int vmnet_read_packets(VmnetState *s)
{
    assert(s->packets_send_current_pos == s->packets_send_end_pos);

    struct vmpktdesc *packets = s->packets_buf;
    vmnet_return_t status;
    int i;

    /* Read as many packets as present */
    s->packets_send_current_pos = 0;
    s->packets_send_end_pos = VMNET_PACKETS_LIMIT;
    for (i = 0; i < s->packets_send_end_pos; ++i) {
        packets[i].vm_pkt_size = s->max_packet_size;
        packets[i].vm_pkt_iovcnt = 1;
        packets[i].vm_flags = 0;
    }

    status = vmnet_read(s->vmnet_if, packets, &s->packets_send_end_pos);
    if (status != VMNET_SUCCESS) {
        error_printf("vmnet: read failed: %s\n",
                     vmnet_status_map_str(status));
        s->packets_send_current_pos = 0;
        s->packets_send_end_pos = 0;
        return -1;
    }
    return s->packets_send_end_pos;
}


/**
 * Write packets from temporary buffers in VmnetState
 * to QEMU.
 */
static void vmnet_write_packets_to_qemu(VmnetState *s)
{
    uint8_t *pkt;
    size_t pktsz;
    uint8_t min_pkt[ETH_ZLEN];
    size_t min_pktsz;
    ssize_t size;

    while (s->packets_send_current_pos < s->packets_send_end_pos) {
        pkt = s->iov_buf[s->packets_send_current_pos].iov_base;
        pktsz = s->packets_buf[s->packets_send_current_pos].vm_pkt_size;

        if (net_peer_needs_padding(&s->nc)) {
            min_pktsz = sizeof(min_pkt);

            if (eth_pad_short_frame(min_pkt, &min_pktsz, pkt, pktsz)) {
                pkt = min_pkt;
                pktsz = min_pktsz;
            }
        }

        size = qemu_send_packet_async(&s->nc, pkt, pktsz,
                                      vmnet_send_completed);

        if (size == 0) {
            /* QEMU is not ready to consume more packets -
             * stop and wait for completion callback call */
            return;
        }
        ++s->packets_send_current_pos;
    }
}


/**
 * Bottom half callback that transfers packets from vmnet interface
 * to QEMU.
 *
 * The process of transferring packets is three-staged:
 * 1. Handle vmnet event;
 * 2. Read packets from vmnet interface into temporary buffer;
 * 3. Write packets from temporary buffer to QEMU.
 *
 * QEMU may suspend this process on the last stage, returning 0 from
 * qemu_send_packet_async function. If this happens, we should
 * respectfully wait until it is ready to consume more packets,
 * write left ones in temporary buffer and only after this
 * continue reading more packets from vmnet interface.
 *
 * Packets to be transferred are stored into packets_buf,
 * in the window [packets_send_current_pos..packets_send_end_pos)
 * including current_pos, excluding end_pos.
 *
 * Thus, if QEMU is not ready, buffer is not read and
 * packets_send_current_pos < packets_send_end_pos.
 */
static void vmnet_send_bh(void *opaque)
{
    NetClientState *nc = (NetClientState *) opaque;
    VmnetState *s = DO_UPCAST(VmnetState, nc, nc);

    /*
     * Do nothing if QEMU is not ready - wait
     * for completion callback invocation
     */
    if (s->packets_send_current_pos < s->packets_send_end_pos) {
        return;
    }

    /* Read packets from vmnet interface */
    if (vmnet_read_packets(s) > 0) {
        /* Send them to QEMU */
        vmnet_write_packets_to_qemu(s);
    }
}


/**
 * Completion callback to be invoked by QEMU when it becomes
 * ready to consume more packets.
 */
static void vmnet_send_completed(NetClientState *nc, ssize_t len)
{
    VmnetState *s = DO_UPCAST(VmnetState, nc, nc);

    /* Callback is invoked eq queued packet is sent */
    ++s->packets_send_current_pos;

    /* Complete sending packets left in VmnetState buffers */
    vmnet_write_packets_to_qemu(s);

    /* And read new ones from vmnet if VmnetState buffer is ready */
    if (s->packets_send_current_pos < s->packets_send_end_pos) {
        qemu_bh_schedule(s->send_bh);
    }
}


static void vmnet_bufs_init(VmnetState *s)
{
    struct vmpktdesc *packets = s->packets_buf;
    struct iovec *iov = s->iov_buf;
    int i;

    for (i = 0; i < VMNET_PACKETS_LIMIT; ++i) {
        iov[i].iov_len = s->max_packet_size;
        iov[i].iov_base = g_malloc0(iov[i].iov_len);
        packets[i].vm_pkt_iov = iov + i;
    }
}

/**
 * Called on state change to un-register/re-register handlers
 */
static void vmnet_vm_state_change_cb(void *opaque, bool running, RunState state)
{
    VmnetState *s = opaque;

    if (running) {
        vmnet_interface_set_event_callback(
            s->vmnet_if,
            VMNET_INTERFACE_PACKETS_AVAILABLE,
            s->if_queue,
            ^(interface_event_t event_id, xpc_object_t event) {
                assert(event_id == VMNET_INTERFACE_PACKETS_AVAILABLE);
                /*
                 * This function is being called from a non qemu thread, so
                 * we only schedule a BH, and do the rest of the io completion
                 * handling from vmnet_send_bh() which runs in a qemu context.
                 */
                qemu_bh_schedule(s->send_bh);
            });
    } else {
        vmnet_interface_set_event_callback(
            s->vmnet_if,
            VMNET_INTERFACE_PACKETS_AVAILABLE,
            NULL,
            NULL);
    }
}

int vmnet_if_create(NetClientState *nc,
                    xpc_object_t if_desc,
                    Error **errp)
{
    VmnetState *s = DO_UPCAST(VmnetState, nc, nc);
    dispatch_semaphore_t if_created_sem = dispatch_semaphore_create(0);
    __block vmnet_return_t if_status;

    s->if_queue = dispatch_queue_create(
        "org.qemu.vmnet.if_queue",
        DISPATCH_QUEUE_SERIAL
    );

    xpc_dictionary_set_bool(
        if_desc,
        vmnet_allocate_mac_address_key,
        false
    );

#ifdef DEBUG
    qemu_log("vmnet.start.interface_desc:\n");
    xpc_dictionary_apply(if_desc,
                         ^bool(const char *k, xpc_object_t v) {
                             char *desc = xpc_copy_description(v);
                             qemu_log("  %s=%s\n", k, desc);
                             free(desc);
                             return true;
                         });
#endif /* DEBUG */

    s->vmnet_if = vmnet_start_interface(
        if_desc,
        s->if_queue,
        ^(vmnet_return_t status, xpc_object_t interface_param) {
            if_status = status;
            if (status != VMNET_SUCCESS || !interface_param) {
                dispatch_semaphore_signal(if_created_sem);
                return;
            }

#ifdef DEBUG
            qemu_log("vmnet.start.interface_param:\n");
            xpc_dictionary_apply(interface_param,
                                 ^bool(const char *k, xpc_object_t v) {
                                     char *desc = xpc_copy_description(v);
                                     qemu_log("  %s=%s\n", k, desc);
                                     free(desc);
                                     return true;
                                 });
#endif /* DEBUG */

            s->mtu = xpc_dictionary_get_uint64(
                interface_param,
                vmnet_mtu_key);
            s->max_packet_size = xpc_dictionary_get_uint64(
                interface_param,
                vmnet_max_packet_size_key);

            dispatch_semaphore_signal(if_created_sem);
        });

    if (s->vmnet_if == NULL) {
        dispatch_release(s->if_queue);
        dispatch_release(if_created_sem);
        error_setg(errp,
                   "unable to create interface with requested params");
        return -1;
    }

    dispatch_semaphore_wait(if_created_sem, DISPATCH_TIME_FOREVER);
    dispatch_release(if_created_sem);

    if (if_status != VMNET_SUCCESS) {
        dispatch_release(s->if_queue);
        error_setg(errp,
                   "cannot create vmnet interface: %s",
                   vmnet_status_map_str(if_status));
        return -1;
    }

    s->send_bh = aio_bh_new(qemu_get_aio_context(), vmnet_send_bh, nc);
    vmnet_bufs_init(s);

    s->packets_send_current_pos = 0;
    s->packets_send_end_pos = 0;

    vmnet_vm_state_change_cb(s, 1, RUN_STATE_RUNNING);

    s->change = qemu_add_vm_change_state_handler(vmnet_vm_state_change_cb, s);

    return 0;
}


void vmnet_cleanup_common(NetClientState *nc)
{
    VmnetState *s = DO_UPCAST(VmnetState, nc, nc);
    dispatch_semaphore_t if_stopped_sem;

    if (s->vmnet_if == NULL) {
        return;
    }

    vmnet_vm_state_change_cb(s, 0, RUN_STATE_SHUTDOWN);
    qemu_del_vm_change_state_handler(s->change);
    if_stopped_sem = dispatch_semaphore_create(0);
    vmnet_stop_interface(
        s->vmnet_if,
        s->if_queue,
        ^(vmnet_return_t status) {
            assert(status == VMNET_SUCCESS);
            dispatch_semaphore_signal(if_stopped_sem);
        });
    dispatch_semaphore_wait(if_stopped_sem, DISPATCH_TIME_FOREVER);

    qemu_purge_queued_packets(nc);

    qemu_bh_delete(s->send_bh);
    dispatch_release(if_stopped_sem);
    dispatch_release(s->if_queue);

    for (int i = 0; i < VMNET_PACKETS_LIMIT; ++i) {
        g_free(s->iov_buf[i].iov_base);
    }
}
