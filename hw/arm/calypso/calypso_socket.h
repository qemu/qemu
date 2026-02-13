/*
 * calypso_socket.h — Calypso Socket device for communication with transceiver
 *
 * Exposes a UNIX domain socket for communication with transceiver/transceiver
 * firmware in QEMU.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_CALYPSO_SOCKET_H
#define HW_ARM_CALYPSO_SOCKET_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define TYPE_CALYPSO_SOCKET "calypso-socket"

/* Device state */
typedef struct CalypsoSocketState {
    /* Socket handling */
    int socket_fd;
    struct sockaddr_un socket_addr;
    bool socket_running;
    
    /* Mutex and condition variable for thread synchronization */
    QemuMutex socket_mutex;
    QemuCond socket_cond;
    QemuThread socket_thread;
    
    /* Buffer for incoming data */
    uint8_t rx_buffer[1024];
    uint32_t rx_len;
    
    /* Connection information */
    char socket_path[256];
    bool connect_transceiver;
    struct sockaddr_un transceiver_addr;
    
    /* Device status */
    uint32_t status;
    
    /* Memory region */
    MemoryRegion mmio;
} CalypsoSocketState;

/* Register offsets */
#define CALYPSO_SOCKET_CTRL   0x00
#define CALYPSO_SOCKET_STATUS 0x04
#define CALYPSO_SOCKET_DATA   0x08

/* Control bits */
#define CALYPSO_SOCKET_CTRL_START  (1 << 0)
#define CALYPSO_SOCKET_CTRL_STOP   (1 << 1)
#define CALYPSO_SOCKET_CTRL_RESET  (1 << 2)

/* Status bits */
#define CALYPSO_SOCKET_STATUS_READY (1 << 0)
#define CALYPSO_SOCKET_STATUS_ERROR (1 << 1)
#define CALYPSO_SOCKET_STATUS_TX    (1 << 2)

/* Device class */
typedef struct CalypsoSocketDevice {
    CalypsoSocketState s;
} CalypsoSocketDevice;

#define CALYPSO_SOCKET(obj) \
    OBJECT_CHECK(CalypsoSocketState, (obj), TYPE_CALYPSO_SOCKET)

/* Device realization function */
static void calypso_socket_realize(DeviceState *dev, Error **errp);

/* Reset function */
void calypso_socket_reset(DeviceState *dev);

/* Thread function */
void *calypso_socket_thread_func(void *opaque);

#endif /* HW_ARM_CALYPSO_SOCKET_H */
