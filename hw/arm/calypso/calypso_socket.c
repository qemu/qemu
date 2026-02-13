/*
 * calypso_socket.c — Calypso Socket device for communication with transceiver
 *
 * Exposes a UNIX domain socket for communication with transceiver/transceiver
 * firmware in QEMU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "qapi/error.h"
#include "hw/arm/calypso/calypso_socket.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include "qemu/cutils.h"

void calypso_socket_reset(DeviceState *dev)
{
    CalypsoSocketState *s = CALYPSO_SOCKET(dev);

    s->socket_fd = -1;
    s->socket_running = false;
    s->status = 0;
    s->rx_len = 0;
}


void *calypso_socket_thread_func(void *opaque)
{
    CalypsoSocketState *s = (CalypsoSocketState *)opaque;
    
    while (s->socket_running) {
        fd_set read_fds;
        int max_fd;
        
        FD_ZERO(&read_fds);
        FD_SET(s->socket_fd, &read_fds);
        max_fd = s->socket_fd + 1;
        
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        
        int ret = select(max_fd, &read_fds, NULL, NULL, &tv);
        
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            s->status = CALYPSO_SOCKET_STATUS_ERROR;
            break;
        }
        
        if (ret > 0 && FD_ISSET(s->socket_fd, &read_fds)) {
            uint8_t buffer[1024];
            int bytes_received = recv(s->socket_fd, buffer, sizeof(buffer), 0);
            
            if (bytes_received > 0) {
                if (s->rx_len + bytes_received < sizeof(s->rx_buffer)) {
                    memcpy(&s->rx_buffer[s->rx_len], buffer, bytes_received);
                    s->rx_len += bytes_received;
                    s->status |= CALYPSO_SOCKET_STATUS_READY;
                } else {
                    s->status = CALYPSO_SOCKET_STATUS_ERROR;
                }
            } else if (bytes_received == 0) {
                s->status = CALYPSO_SOCKET_STATUS_ERROR;
                close(s->socket_fd);
                s->socket_fd = -1;
            }
        }
    }
    
    s->status &= ~CALYPSO_SOCKET_STATUS_READY;
    return NULL;
}

static uint64_t calypso_socket_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    CalypsoSocketState *s = (CalypsoSocketState *)opaque;
    
    switch (offset) {
    case CALYPSO_SOCKET_STATUS:
        return s->status;
    
    case CALYPSO_SOCKET_DATA:
        if (s->rx_len > 0) {
            uint8_t data = s->rx_buffer[0];
            s->rx_len--;
            memmove(&s->rx_buffer[0], &s->rx_buffer[1], s->rx_len);
            return data;
        }
        return 0;
    
    default:
        return 0;
    }
}

static void calypso_socket_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    CalypsoSocketState *s = (CalypsoSocketState *)opaque;
    
    switch (offset) {
    case CALYPSO_SOCKET_CTRL:
        if (value & CALYPSO_SOCKET_CTRL_START) {
            s->socket_running = true;
            qemu_thread_create(&s->socket_thread, "calypso-socket",
                              calypso_socket_thread_func, s,
                              QEMU_THREAD_JOINABLE);
        }
        if (value & CALYPSO_SOCKET_CTRL_STOP) {
            s->socket_running = false;
            if (s->socket_fd >= 0) {
                close(s->socket_fd);
            }
        }
        if (value & CALYPSO_SOCKET_CTRL_RESET) {
            s->socket_running = false;
            s->socket_fd = -1;
            s->status = 0;
            s->rx_len = 0;
        }
        break;
    
    default:
        break;
    }
}

static const MemoryRegionOps calypso_socket_ops = {
    .read = calypso_socket_read,
    .write = calypso_socket_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};


void calypso_socket_realize(DeviceState *dev, Error **errp)
{
    CalypsoSocketState *s = CALYPSO_SOCKET(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    if (s->socket_path[0] == '\0') {
        error_setg(errp, "socket-path property not set");
        return;
    }

    /* Create UNIX socket */
    s->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s->socket_fd < 0) {
        error_setg(errp, "Failed to create UNIX socket: %s", strerror(errno));
        return;
    }

    /* Set up socket address */
    memset(&s->socket_addr, 0, sizeof(s->socket_addr));
    s->socket_addr.sun_family = AF_UNIX;
    pstrcpy(s->socket_addr.sun_path,
            sizeof(s->socket_addr.sun_path),
            s->socket_path);

    /* Bind to socket path */
    if (bind(s->socket_fd, (struct sockaddr *)&s->socket_addr,
             sizeof(s->socket_addr)) < 0) {
        error_setg(errp, "Failed to bind to socket: %s", strerror(errno));
        close(s->socket_fd);
        s->socket_fd = -1;
        return;
    }

    /* Listen on socket */
    if (listen(s->socket_fd, 1) < 0) {
        error_setg(errp, "Failed to listen on socket: %s", strerror(errno));
        close(s->socket_fd);
        s->socket_fd = -1;
        return;
    }

    /* Create thread for handling socket operations */
    s->socket_running = true;
    qemu_thread_create(&s->socket_thread, "calypso-socket",
                       calypso_socket_thread_func, s,
                       QEMU_THREAD_JOINABLE);

    /* Set up MMIO region */
    memory_region_init_io(&s->mmio, OBJECT(dev), &calypso_socket_ops, s,
                          TYPE_CALYPSO_SOCKET, 0x10);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void calypso_socket_instance_init(Object *obj)
{
    CalypsoSocketState *s = CALYPSO_SOCKET(obj);
    
    s->socket_fd = -1;
    s->socket_running = false;
    s->status = 0;
    s->rx_len = 0;
}

static void calypso_socket_finalize(Object *obj)
{
    CalypsoSocketState *s = CALYPSO_SOCKET(obj);
    
    s->socket_running = false;
    
    if (s->socket_fd >= 0) {
        close(s->socket_fd);
        s->socket_fd = -1;
    }
}

static void calypso_socket_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    
    dc->realize = calypso_socket_realize;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo calypso_socket_info = {
    .name = TYPE_CALYPSO_SOCKET,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoSocketState),
    .instance_init = calypso_socket_instance_init,
    .instance_finalize = calypso_socket_finalize,
    .class_init = calypso_socket_class_init,
};

static void calypso_socket_register_types(void)
{
    type_register_static(&calypso_socket_info);
}

type_init(calypso_socket_register_types);
