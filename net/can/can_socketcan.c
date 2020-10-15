/*
 * CAN c support to connect to the Linux host SocketCAN interfaces
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "net/can_emu.h"
#include "net/can_host.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "qom/object.h"

#ifndef DEBUG_CAN
#define DEBUG_CAN 0
#endif /*DEBUG_CAN*/

#define TYPE_CAN_HOST_SOCKETCAN "can-host-socketcan"
OBJECT_DECLARE_SIMPLE_TYPE(CanHostSocketCAN, CAN_HOST_SOCKETCAN)

#define CAN_READ_BUF_LEN  5
struct CanHostSocketCAN {
    CanHostState       parent;
    char               *ifname;

    qemu_can_filter    *rfilter;
    int                rfilter_num;
    can_err_mask_t     err_mask;

    qemu_can_frame     buf[CAN_READ_BUF_LEN];
    int                bufcnt;
    int                bufptr;

    int                fd;
};

/* Check that QEMU and Linux kernel flags encoding and structure matches */
QEMU_BUILD_BUG_ON(QEMU_CAN_EFF_FLAG != CAN_EFF_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_RTR_FLAG != CAN_RTR_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_ERR_FLAG != CAN_ERR_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_INV_FILTER != CAN_INV_FILTER);
QEMU_BUILD_BUG_ON(offsetof(qemu_can_frame, data)
                  != offsetof(struct can_frame, data));

static void can_host_socketcan_display_msg(struct qemu_can_frame *msg)
{
    int i;
    FILE *logfile = qemu_log_lock();
    qemu_log("[cansocketcan]: %03X [%01d] %s %s",
             msg->can_id & QEMU_CAN_EFF_MASK,
             msg->can_dlc,
             msg->can_id & QEMU_CAN_EFF_FLAG ? "EFF" : "SFF",
             msg->can_id & QEMU_CAN_RTR_FLAG ? "RTR" : "DAT");

    for (i = 0; i < msg->can_dlc; i++) {
        qemu_log(" %02X", msg->data[i]);
    }
    qemu_log("\n");
    qemu_log_flush();
    qemu_log_unlock(logfile);
}

static void can_host_socketcan_read(void *opaque)
{
    CanHostSocketCAN *c = opaque;
    CanHostState *ch = CAN_HOST(c);

    /* CAN_READ_BUF_LEN for multiple messages syscall is possible for future */
    c->bufcnt = read(c->fd, c->buf, sizeof(qemu_can_frame));
    if (c->bufcnt < 0) {
        warn_report("CAN bus host read failed (%s)", strerror(errno));
        return;
    }

    if (!ch->bus_client.fd_mode) {
        c->buf[0].flags = 0;
    } else {
        if (c->bufcnt > CAN_MTU) {
            c->buf[0].flags |= QEMU_CAN_FRMF_TYPE_FD;
        }
    }

    can_bus_client_send(&ch->bus_client, c->buf, 1);

    if (DEBUG_CAN) {
        can_host_socketcan_display_msg(c->buf);
    }
}

static bool can_host_socketcan_can_receive(CanBusClientState *client)
{
    return true;
}

static ssize_t can_host_socketcan_receive(CanBusClientState *client,
                            const qemu_can_frame *frames, size_t frames_cnt)
{
    CanHostState *ch = container_of(client, CanHostState, bus_client);
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);

    size_t len;
    int res;

    if (c->fd < 0) {
        return -1;
    }
    if (frames->flags & QEMU_CAN_FRMF_TYPE_FD) {
        if (!ch->bus_client.fd_mode) {
            return 0;
        }
        len = CANFD_MTU;
    } else {
        len = CAN_MTU;

    }

    res = write(c->fd, frames, len);

    if (!res) {
        warn_report("[cansocketcan]: write message to host returns zero");
        return -1;
    }

    if (res != len) {
        if (res < 0) {
            warn_report("[cansocketcan]: write to host failed (%s)",
                        strerror(errno));
        } else {
            warn_report("[cansocketcan]: write to host truncated");
        }
        return -1;
    }

    return 1;
}

static void can_host_socketcan_disconnect(CanHostState *ch)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);

    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, NULL, NULL, c);
        close(c->fd);
        c->fd = -1;
    }

    g_free(c->rfilter);
    c->rfilter = NULL;
    c->rfilter_num = 0;
}

static CanBusClientInfo can_host_socketcan_bus_client_info = {
    .can_receive = can_host_socketcan_can_receive,
    .receive = can_host_socketcan_receive,
};

static void can_host_socketcan_connect(CanHostState *ch, Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);
    int s; /* can raw socket */
    int mtu;
    int enable_canfd = 1;
    struct sockaddr_can addr;
    struct ifreq ifr;

    if (!c->ifname) {
        error_setg(errp, "'if' property not set");
        return;
    }

    /* open socket */
    s = qemu_socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        error_setg_errno(errp, errno, "failed to create CAN_RAW socket");
        return;
    }

    addr.can_family = AF_CAN;
    memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, c->ifname);
    /* check if the frame fits into the CAN netdevice */
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        error_setg_errno(errp, errno,
                         "SocketCAN host interface %s not available",
                         c->ifname);
        goto fail;
    }
    addr.can_ifindex = ifr.ifr_ifindex;

    if (ioctl(s, SIOCGIFMTU, &ifr) < 0) {
        error_setg_errno(errp, errno,
                         "SocketCAN host interface %s SIOCGIFMTU failed",
                         c->ifname);
        goto fail;
    }
    mtu = ifr.ifr_mtu;

    if (mtu >= CANFD_MTU) {
        /* interface is ok - try to switch the socket into CAN FD mode */
        if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                        &enable_canfd, sizeof(enable_canfd))) {
            warn_report("SocketCAN host interface %s enabling CAN FD failed",
                        c->ifname);
        } else {
            c->parent.bus_client.fd_mode = true;
        }
    }

    c->err_mask = 0xffffffff; /* Receive error frame. */
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &c->err_mask, sizeof(c->err_mask));

    c->rfilter_num = 1;
    c->rfilter = g_new(struct qemu_can_filter, c->rfilter_num);

    /* Receive all data frame. If |= CAN_INV_FILTER no data. */
    c->rfilter[0].can_id = 0;
    c->rfilter[0].can_mask = 0;
    c->rfilter[0].can_mask &= ~CAN_ERR_FLAG;

    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, c->rfilter,
               c->rfilter_num * sizeof(struct qemu_can_filter));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "failed to bind to host interface %s",
                         c->ifname);
        goto fail;
    }

    c->fd = s;
    ch->bus_client.info = &can_host_socketcan_bus_client_info;
    qemu_set_fd_handler(c->fd, can_host_socketcan_read, NULL, c);
    return;

fail:
    close(s);
    g_free(c->rfilter);
    c->rfilter = NULL;
    c->rfilter_num = 0;
}

static char *can_host_socketcan_get_if(Object *obj, Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);

    return g_strdup(c->ifname);
}

static void can_host_socketcan_set_if(Object *obj, const char *value,
                                      Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);
    struct ifreq ifr;

    if (strlen(value) >= sizeof(ifr.ifr_name)) {
        error_setg(errp, "CAN interface name longer than %zd characters",
                   sizeof(ifr.ifr_name) - 1);
        return;
    }

    if (c->fd != -1) {
        error_setg(errp, "CAN interface already connected");
        return;
    }

    g_free(c->ifname);
    c->ifname = g_strdup(value);
}

static void can_host_socketcan_instance_init(Object *obj)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);

    c->fd = -1;
}

static void can_host_socketcan_class_init(ObjectClass *klass,
                                          void *class_data G_GNUC_UNUSED)
{
    CanHostClass *chc = CAN_HOST_CLASS(klass);

    object_class_property_add_str(klass, "if",
                                  can_host_socketcan_get_if,
                                  can_host_socketcan_set_if);
    chc->connect = can_host_socketcan_connect;
    chc->disconnect = can_host_socketcan_disconnect;
}

static const TypeInfo can_host_socketcan_info = {
    .parent = TYPE_CAN_HOST,
    .name = TYPE_CAN_HOST_SOCKETCAN,
    .instance_size = sizeof(CanHostSocketCAN),
    .instance_init = can_host_socketcan_instance_init,
    .class_init = can_host_socketcan_class_init,
};

static void can_host_register_types(void)
{
    type_register_static(&can_host_socketcan_info);
}

type_init(can_host_register_types);
