/*
 * QEMU IPMI KCS emulation
 *
 * Copyright (c) 2015,2017 Corey Minyard, MontaVista Software, LLC
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

#ifndef HW_IPMI_KCS_H
#define HW_IPMI_KCS_H

#include "hw/ipmi/ipmi.h"

typedef struct IPMIKCS {
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
    bool write_end;

    uint8_t status_reg;
    uint8_t data_out_reg;

    int16_t data_in_reg; /* -1 means not written */
    int16_t cmd_reg;

    /*
     * This is a response number that we send with the command to make
     * sure that the response matches the command.
     */
    uint8_t waiting_rsp;

    uint32_t io_base;
    unsigned long io_length;
    MemoryRegion io;
    unsigned long size_mask;

    void (*raise_irq)(struct IPMIKCS *ik);
    void (*lower_irq)(struct IPMIKCS *ik);
    void *opaque;

    bool use_irq;
} IPMIKCS;

void ipmi_kcs_get_fwinfo(IPMIKCS *ik, IPMIFwInfo *info);
void ipmi_kcs_class_init(IPMIInterfaceClass *iic);
extern const VMStateDescription vmstate_IPMIKCS;
int ipmi_kcs_vmstate_post_load(void *opaque, int version);

#endif /* HW_IPMI_KCS_H */
