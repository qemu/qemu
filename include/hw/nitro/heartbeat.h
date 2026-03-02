/*
 * Nitro Heartbeat device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_NITRO_HEARTBEAT_H
#define HW_MISC_NITRO_HEARTBEAT_H

#include "hw/nitro/nitro-vsock-bus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_NITRO_HEARTBEAT "nitro-heartbeat"
OBJECT_DECLARE_SIMPLE_TYPE(NitroHeartbeatState, NITRO_HEARTBEAT)

struct NitroHeartbeatState {
    NitroVsockDevice parent_obj;

    CharFrontend vsock;     /* vsock server chardev for heartbeat */
    bool done;
};

#endif /* HW_MISC_NITRO_HEARTBEAT_H */
