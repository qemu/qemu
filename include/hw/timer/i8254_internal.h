/*
 * QEMU 8253/8254 - internal interfaces
 *
 * Copyright (c) 2011 Jan Kiszka, Siemens AG
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

#ifndef QEMU_I8254_INTERNAL_H
#define QEMU_I8254_INTERNAL_H

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"

typedef struct PITChannelState {
    int count; /* can be 65536 */
    uint16_t latched_count;
    uint8_t count_latched;
    uint8_t status_latched;
    uint8_t status;
    uint8_t read_state;
    uint8_t write_state;
    uint8_t write_latch;
    uint8_t rw_mode;
    uint8_t mode;
    uint8_t bcd; /* not supported */
    uint8_t gate; /* timer start */
    int64_t count_load_time;
    /* irq handling */
    int64_t next_transition_time;
    QEMUTimer *irq_timer;
    qemu_irq irq;
    uint32_t irq_disabled;
} PITChannelState;

typedef struct PITCommonState {
    ISADevice dev;
    MemoryRegion ioports;
    uint32_t iobase;
    PITChannelState channels[3];
} PITCommonState;

#define TYPE_PIT_COMMON "pit-common"
#define PIT_COMMON(obj) \
     OBJECT_CHECK(PITCommonState, (obj), TYPE_PIT_COMMON)
#define PIT_COMMON_CLASS(klass) \
     OBJECT_CLASS_CHECK(PITCommonClass, (klass), TYPE_PIT_COMMON)
#define PIT_COMMON_GET_CLASS(obj) \
     OBJECT_GET_CLASS(PITCommonClass, (obj), TYPE_PIT_COMMON)

typedef struct PITCommonClass {
    ISADeviceClass parent_class;

    int (*init)(PITCommonState *s);
    void (*set_channel_gate)(PITCommonState *s, PITChannelState *sc, int val);
    void (*get_channel_info)(PITCommonState *s, PITChannelState *sc,
                             PITChannelInfo *info);
    void (*pre_save)(PITCommonState *s);
    void (*post_load)(PITCommonState *s);
} PITCommonClass;

int pit_get_out(PITChannelState *s, int64_t current_time);
int64_t pit_get_next_transition_time(PITChannelState *s, int64_t current_time);
void pit_get_channel_info_common(PITCommonState *s, PITChannelState *sc,
                                 PITChannelInfo *info);
void pit_reset_common(PITCommonState *s);

#endif /* !QEMU_I8254_INTERNAL_H */
