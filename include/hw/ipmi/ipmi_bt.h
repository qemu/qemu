/*
 * QEMU IPMI BT emulation
 *
 * Copyright (c) 2015 Corey Minyard, MontaVista Software, LLC
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

#ifndef HW_IPMI_BT_H
#define HW_IPMI_BT_H

#include "hw/ipmi/ipmi.h"

typedef struct IPMIBT {
    IPMIBmc *bmc;

    bool do_wake;

    bool obf_irq_set;
    bool atn_irq_set;
    bool irqs_enabled;

    uint8_t outmsg[MAX_IPMI_MSG_SIZE];
    uint32_t outpos;
    uint32_t outlen;

    uint8_t inmsg[MAX_IPMI_MSG_SIZE];
    uint32_t inlen;

    uint8_t control_reg;
    uint8_t mask_reg;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;
    uint8_t waiting_seq;

    uint32_t io_base;
    unsigned long io_length;
    MemoryRegion io;
    unsigned long size_mask;

    void (*raise_irq)(struct IPMIBT *ib);
    void (*lower_irq)(struct IPMIBT *ib);
    void *opaque;

    bool use_irq;
} IPMIBT;

void ipmi_bt_get_fwinfo(IPMIBT *ik, IPMIFwInfo *info);
void ipmi_bt_class_init(IPMIInterfaceClass *iic);
extern const VMStateDescription vmstate_IPMIBT;
int ipmi_bt_vmstate_post_load(void *opaque, int version);

#endif /* HW_IPMI_BT_H */
