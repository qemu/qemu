/*
 * Nitro Enclave Serial (vsock)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_NITRO_SERIAL_VSOCK_H
#define HW_CHAR_NITRO_SERIAL_VSOCK_H

#include "hw/nitro/nitro-vsock-bus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_NITRO_SERIAL_VSOCK "nitro-serial-vsock"
OBJECT_DECLARE_SIMPLE_TYPE(NitroSerialVsockState, NITRO_SERIAL_VSOCK)

struct NitroSerialVsockState {
    NitroVsockDevice parent_obj;

    CharFrontend output;    /* chardev to write console output to */
    CharFrontend vsock;     /* vsock chardev to enclave console */
};

#endif /* HW_CHAR_NITRO_SERIAL_VSOCK_H */
