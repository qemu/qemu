/*
 * USB UHCI controller emulation
 *
 * Copyright (c) 2005 Fabrice Bellard
 *
 * Copyright (c) 2008 Max Krasnyansky
 *     Magor rewrite of the UHCI data structures parser and frame processor
 *     Support for fully async operation and multiple outstanding transactions
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
#ifndef HW_USB_HCD_UHCI_H
#define HW_USB_HCD_UHCI_H

#include "exec/memory.h"
#include "qemu/timer.h"
#include "hw/pci/pci.h"
#include "hw/usb.h"

typedef struct UHCIQueue UHCIQueue;

#define NB_PORTS 2

typedef struct UHCIPort {
    USBPort port;
    uint16_t ctrl;
} UHCIPort;

typedef struct UHCIState {
    PCIDevice dev;
    MemoryRegion io_bar;
    USBBus bus; /* Note unused when we're a companion controller */
    uint16_t cmd; /* cmd register */
    uint16_t status;
    uint16_t intr; /* interrupt enable register */
    uint16_t frnum; /* frame number */
    uint32_t fl_base_addr; /* frame list base address */
    uint8_t sof_timing;
    uint8_t status2; /* bit 0 and 1 are used to generate UHCI_STS_USBINT */
    int64_t expire_time;
    QEMUTimer *frame_timer;
    QEMUBH *bh;
    uint32_t frame_bytes;
    uint32_t frame_bandwidth;
    bool completions_only;
    UHCIPort ports[NB_PORTS];
    qemu_irq irq;
    /* Interrupts that should be raised at the end of the current frame.  */
    uint32_t pending_int_mask;

    /* Active packets */
    QTAILQ_HEAD(, UHCIQueue) queues;
    uint8_t num_ports_vmstate;

    /* Properties */
    char *masterbus;
    uint32_t firstport;
    uint32_t maxframes;
} UHCIState;

#define TYPE_UHCI "pci-uhci-usb"
DECLARE_INSTANCE_CHECKER(UHCIState, UHCI, TYPE_UHCI)

typedef struct UHCIInfo {
    const char *name;
    uint16_t   vendor_id;
    uint16_t   device_id;
    uint8_t    revision;
    uint8_t    irq_pin;
    void       (*realize)(PCIDevice *dev, Error **errp);
    bool       unplug;
    bool       notuser; /* disallow user_creatable */
} UHCIInfo;

void uhci_data_class_init(ObjectClass *klass, void *data);
void usb_uhci_common_realize(PCIDevice *dev, Error **errp);

#endif
