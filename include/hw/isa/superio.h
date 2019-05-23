/*
 * Generic ISA Super I/O
 *
 * Copyright (c) 2018 Philippe Mathieu-Daudé
 *
 * This code is licensed under the GNU GPLv2 and later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ISA_SUPERIO_H
#define HW_ISA_SUPERIO_H

#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"

#define TYPE_ISA_SUPERIO "isa-superio"
#define ISA_SUPERIO(obj) \
    OBJECT_CHECK(ISASuperIODevice, (obj), TYPE_ISA_SUPERIO)
#define ISA_SUPERIO_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ISASuperIOClass, (obj), TYPE_ISA_SUPERIO)
#define ISA_SUPERIO_CLASS(klass) \
    OBJECT_CLASS_CHECK(ISASuperIOClass, (klass), TYPE_ISA_SUPERIO)

#define SUPERIO_MAX_SERIAL_PORTS 4

typedef struct ISASuperIODevice {
    /*< private >*/
    ISADevice parent_obj;
    /*< public >*/

    ISADevice *parallel[MAX_PARALLEL_PORTS];
    ISADevice *serial[SUPERIO_MAX_SERIAL_PORTS];
    ISADevice *floppy;
    ISADevice *kbc;
    ISADevice *ide;
} ISASuperIODevice;

typedef struct ISASuperIOFuncs {
    size_t count;
    bool (*is_enabled)(ISASuperIODevice *sio, uint8_t index);
    uint16_t (*get_iobase)(ISASuperIODevice *sio, uint8_t index);
    unsigned int (*get_irq)(ISASuperIODevice *sio, uint8_t index);
    unsigned int (*get_dma)(ISASuperIODevice *sio, uint8_t index);
} ISASuperIOFuncs;

typedef struct ISASuperIOClass {
    /*< private >*/
    ISADeviceClass parent_class;
    /*< public >*/
    DeviceRealize parent_realize;

    ISASuperIOFuncs parallel;
    ISASuperIOFuncs serial;
    ISASuperIOFuncs floppy;
    ISASuperIOFuncs ide;
} ISASuperIOClass;

#define TYPE_FDC37M81X_SUPERIO  "fdc37m81x-superio"
#define TYPE_SMC37C669_SUPERIO  "smc37c669-superio"

#endif /* HW_ISA_SUPERIO_H */
