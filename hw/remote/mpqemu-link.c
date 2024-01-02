/*
 * Communication channel between QEMU and remote device process
 *
 * Copyright © 2018, 2021 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"

#include "qemu/module.h"
#include "hw/remote/mpqemu-link.h"
#include "qapi/error.h"
#include "qemu/iov.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "io/channel.h"
#include "sysemu/iothread.h"
#include "trace.h"

/*
 * Send message over the ioc QIOChannel.
 * This function is safe to call from:
 * - main loop in co-routine context. Will block the main loop if not in
 *   co-routine context;
 * - vCPU thread with no co-routine context and if the channel is not part
 *   of the main loop handling;
 * - IOThread within co-routine context, outside of co-routine context
 *   will block IOThread;
 * Returns true if no errors were encountered, false otherwise.
 */
bool mpqemu_msg_send(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    bool drop_bql = bql_locked();
    bool iothread = qemu_in_iothread();
    struct iovec send[2] = {};
    int *fds = NULL;
    size_t nfds = 0;
    bool ret = false;

    send[0].iov_base = msg;
    send[0].iov_len = MPQEMU_MSG_HDR_SIZE;

    send[1].iov_base = (void *)&msg->data;
    send[1].iov_len = msg->size;

    if (msg->num_fds) {
        nfds = msg->num_fds;
        fds = msg->fds;
    }

    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    assert(qemu_in_coroutine() || !iothread);

    /*
     * Skip unlocking/locking BQL when the IOThread is running
     * in co-routine context. Co-routine context is asserted above
     * for IOThread case.
     * Also skip lock handling while in a co-routine in the main context.
     */
    if (drop_bql && !iothread && !qemu_in_coroutine()) {
        bql_unlock();
    }

    if (!qio_channel_writev_full_all(ioc, send, G_N_ELEMENTS(send),
                                    fds, nfds, 0, errp)) {
        ret = true;
    } else {
        trace_mpqemu_send_io_error(msg->cmd, msg->size, nfds);
    }

    if (drop_bql && !iothread && !qemu_in_coroutine()) {
        /* See above comment why skip locking here. */
        bql_lock();
    }

    return ret;
}

/*
 * Read message from the ioc QIOChannel.
 * This function is safe to call from:
 * - From main loop in co-routine context. Will block the main loop if not in
 *   co-routine context;
 * - From vCPU thread with no co-routine context and if the channel is not part
 *   of the main loop handling;
 * - From IOThread within co-routine context, outside of co-routine context
 *   will block IOThread;
 */
static ssize_t mpqemu_read(QIOChannel *ioc, void *buf, size_t len, int **fds,
                           size_t *nfds, Error **errp)
{
    struct iovec iov = { .iov_base = buf, .iov_len = len };
    bool drop_bql = bql_locked();
    bool iothread = qemu_in_iothread();
    int ret = -1;

    /*
     * Dont use in IOThread out of co-routine context as
     * it will block IOThread.
     */
    assert(qemu_in_coroutine() || !iothread);

    if (drop_bql && !iothread && !qemu_in_coroutine()) {
        bql_unlock();
    }

    ret = qio_channel_readv_full_all_eof(ioc, &iov, 1, fds, nfds, errp);

    if (drop_bql && !iothread && !qemu_in_coroutine()) {
        bql_lock();
    }

    return (ret <= 0) ? ret : iov.iov_len;
}

bool mpqemu_msg_recv(MPQemuMsg *msg, QIOChannel *ioc, Error **errp)
{
    ERRP_GUARD();
    g_autofree int *fds = NULL;
    size_t nfds = 0;
    ssize_t len;
    bool ret = false;

    len = mpqemu_read(ioc, msg, MPQEMU_MSG_HDR_SIZE, &fds, &nfds, errp);
    if (len <= 0) {
        goto fail;
    } else if (len != MPQEMU_MSG_HDR_SIZE) {
        error_setg(errp, "Message header corrupted");
        goto fail;
    }

    if (msg->size > sizeof(msg->data)) {
        error_setg(errp, "Invalid size for message");
        goto fail;
    }

    if (!msg->size) {
        goto copy_fds;
    }

    len = mpqemu_read(ioc, &msg->data, msg->size, NULL, NULL, errp);
    if (len <= 0) {
        goto fail;
    }
    if (len != msg->size) {
        error_setg(errp, "Unable to read full message");
        goto fail;
    }

copy_fds:
    msg->num_fds = nfds;
    if (nfds > G_N_ELEMENTS(msg->fds)) {
        error_setg(errp,
                   "Overflow error: received %zu fds, more than max of %d fds",
                   nfds, REMOTE_MAX_FDS);
        goto fail;
    }
    if (nfds) {
        memcpy(msg->fds, fds, nfds * sizeof(int));
    }

    ret = true;

fail:
    if (*errp) {
        trace_mpqemu_recv_io_error(msg->cmd, msg->size, nfds);
    }
    while (*errp && nfds) {
        close(fds[nfds - 1]);
        nfds--;
    }

    return ret;
}

/*
 * Send msg and wait for a reply with command code RET_MSG.
 * Returns the message received of size u64 or UINT64_MAX
 * on error.
 * Called from VCPU thread in non-coroutine context.
 * Used by the Proxy object to communicate to remote processes.
 */
uint64_t mpqemu_msg_send_and_await_reply(MPQemuMsg *msg, PCIProxyDev *pdev,
                                         Error **errp)
{
    MPQemuMsg msg_reply = {0};
    uint64_t ret = UINT64_MAX;

    assert(!qemu_in_coroutine());

    QEMU_LOCK_GUARD(&pdev->io_mutex);
    if (!mpqemu_msg_send(msg, pdev->ioc, errp)) {
        return ret;
    }

    if (!mpqemu_msg_recv(&msg_reply, pdev->ioc, errp)) {
        return ret;
    }

    if (!mpqemu_msg_valid(&msg_reply) || msg_reply.cmd != MPQEMU_CMD_RET) {
        error_setg(errp, "ERROR: Invalid reply received for command %d",
                         msg->cmd);
        return ret;
    }

    return msg_reply.data.u64;
}

bool mpqemu_msg_valid(MPQemuMsg *msg)
{
    if (msg->cmd >= MPQEMU_CMD_MAX || msg->cmd < 0) {
        return false;
    }

    /* Verify FDs. */
    if (msg->num_fds >= REMOTE_MAX_FDS) {
        return false;
    }

    if (msg->num_fds > 0) {
        for (int i = 0; i < msg->num_fds; i++) {
            if (fcntl(msg->fds[i], F_GETFL) == -1) {
                return false;
            }
        }
    }

     /* Verify message specific fields. */
    switch (msg->cmd) {
    case MPQEMU_CMD_SYNC_SYSMEM:
        if (msg->num_fds == 0 || msg->size != sizeof(SyncSysmemMsg)) {
            return false;
        }
        break;
    case MPQEMU_CMD_PCI_CFGWRITE:
    case MPQEMU_CMD_PCI_CFGREAD:
        if (msg->size != sizeof(PciConfDataMsg)) {
            return false;
        }
        break;
    case MPQEMU_CMD_BAR_WRITE:
    case MPQEMU_CMD_BAR_READ:
        if ((msg->size != sizeof(BarAccessMsg)) || (msg->num_fds != 0)) {
            return false;
        }
        break;
    case MPQEMU_CMD_SET_IRQFD:
        if (msg->size || (msg->num_fds != 2)) {
            return false;
        }
        break;
    default:
        break;
    }

    return true;
}
