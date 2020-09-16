/*
 * ARM TrustZone master security controller emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/*
 * This is a model of the TrustZone master security controller (MSC).
 * It is documented in the ARM CoreLink SIE-200 System IP for Embedded TRM
 * (DDI 0571G):
 * https://developer.arm.com/products/architecture/m-profile/docs/ddi0571/g
 *
 * The MSC sits in front of a device which can be a bus master (such as
 * a DMA controller) and allows secure software to configure it to either
 * pass through or reject transactions made by that bus master.
 * Rejected transactions may be configured to either be aborted, or to
 * behave as RAZ/WI. An interrupt can be signalled for a rejected transaction.
 *
 * The MSC has no register interface -- it is configured purely by a
 * collection of input signals from other hardware in the system. Typically
 * they are either hardwired or exposed in an ad-hoc register interface by
 * the SoC that uses the MSC.
 *
 * We don't currently implement the irq_enable GPIO input, because on
 * the MPS2 FPGA images it is always tied high, which is awkward to
 * implement in QEMU.
 *
 * QEMU interface:
 * + Named GPIO input "cfg_nonsec": set to 1 if the bus master should be
 *   treated as nonsecure, or 0 for secure
 * + Named GPIO input "cfg_sec_resp": set to 1 if a rejected transaction should
 *   result in a transaction error, or 0 for the transaction to RAZ/WI
 * + Named GPIO input "irq_clear": set to 1 to clear a pending interrupt
 * + Named GPIO output "irq": set for a transaction-failed interrupt
 * + Property "downstream": MemoryRegion defining where bus master transactions
 *   are made if they are not blocked
 * + Property "idau": an object implementing IDAUInterface, which defines which
 *   addresses should be treated as secure and which as non-secure.
 *   This need not be the same IDAU as the one used by the CPU.
 * + sysbus MMIO region 0: MemoryRegion defining the upstream end of the MSC;
 *   this should be passed to the bus master device as the region it should
 *   make memory transactions to
 */

#ifndef TZ_MSC_H
#define TZ_MSC_H

#include "hw/sysbus.h"
#include "target/arm/idau.h"
#include "qom/object.h"

#define TYPE_TZ_MSC "tz-msc"
OBJECT_DECLARE_SIMPLE_TYPE(TZMSC, TZ_MSC)

struct TZMSC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* State: these just track the values of our input signals */
    bool cfg_nonsec;
    bool cfg_sec_resp;
    bool irq_clear;
    /* State: are we asserting irq ? */
    bool irq_status;

    qemu_irq irq;
    MemoryRegion *downstream;
    AddressSpace downstream_as;
    MemoryRegion upstream;
    IDAUInterface *idau;
};

#endif
