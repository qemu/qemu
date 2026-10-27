/*
 * QEMU L2VIC Interrupt Controller
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_HEX_L2VIC_H
#define HW_INTC_HEX_L2VIC_H

#include "qom/object.h"

#define TYPE_HEX_L2VIC "hex-l2vic"
/*
 * L2VIC Interface for CPU/GlobalReg interaction
 */
#define TYPE_HEX_L2VIC_INTERFACE "hex-l2vic-if"

typedef struct HexL2VicInterface HexL2VicInterface;

typedef struct HexL2VicInterfaceClass {
    InterfaceClass parent_class;

    uint32_t (*read_vid)(HexL2VicInterface *l2vic, uint32_t group);

    /*
     * Write the VID: unpack the fields into per-group VIDs.  This does
     * not deliver or clear any interrupt; a pending interrupt stays
     * gated until ciad.
     */
    void (*update_vid)(HexL2VicInterface *l2vic, uint32_t group,
                        uint32_t value);

    /*
     * Clear interrupts using the CIAD instruction.
     * @mask the CPU interrupts that the CIAD acknowledged.  The interrupt is
     * only cleared if @mask has the CPU interrupt that its VID group is
     * delivered on.
     */
    void (*clear_interrupt)(HexL2VicInterface *l2vic, uint32_t mask);
} HexL2VicInterfaceClass;

DECLARE_OBJ_CHECKERS(HexL2VicInterface, HexL2VicInterfaceClass,
                     HEX_L2VIC_INTERFACE, TYPE_HEX_L2VIC_INTERFACE);

static inline uint32_t l2vic_read_vid(HexL2VicInterface *l2vic,
                                       uint32_t group)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    return k->read_vid(l2vic, group);
}

static inline void l2vic_update_vid(HexL2VicInterface *l2vic, uint32_t group,
                                     uint32_t value)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    k->update_vid(l2vic, group, value);
}

static inline void l2vic_clear_interrupt(HexL2VicInterface *l2vic,
                                         uint32_t mask)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_GET_CLASS(l2vic);
    k->clear_interrupt(l2vic, mask);
}

#endif /* HW_INTC_HEX_L2VIC_H */
