/*
 * ARM TrustZone peripheral protection controller emulation
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

/* This is a model of the TrustZone peripheral protection controller (PPC).
 * It is documented in the ARM CoreLink SIE-200 System IP for Embedded TRM
 * (DDI 0571G):
 * https://developer.arm.com/products/architecture/m-profile/docs/ddi0571/g
 *
 * The PPC sits in front of peripherals and allows secure software to
 * configure it to either pass through or reject transactions.
 * Rejected transactions may be configured to either be aborted, or to
 * behave as RAZ/WI. An interrupt can be signalled for a rejected transaction.
 *
 * The PPC has no register interface -- it is configured purely by a
 * collection of input signals from other hardware in the system. Typically
 * they are either hardwired or exposed in an ad-hoc register interface by
 * the SoC that uses the PPC.
 *
 * This QEMU model can be used to model either the AHB5 or APB4 TZ PPC,
 * since the only difference between them is that the AHB version has a
 * "default" port which has no security checks applied. In QEMU the default
 * port can be emulated simply by wiring its downstream devices directly
 * into the parent address space, since the PPC does not need to intercept
 * transactions there.
 *
 * In the hardware, selection of which downstream port to use is done by
 * the user's decode logic asserting one of the hsel[] signals. In QEMU,
 * we provide 16 MMIO regions, one per port, and the user maps these into
 * the desired addresses to implement the address decode.
 *
 * QEMU interface:
 * + sysbus MMIO regions 0..15: MemoryRegions defining the upstream end
 *   of each of the 16 ports of the PPC. When a port is unused (i.e. no
 *   downstream MemoryRegion is connected to it) at the end of the 0..15
 *   range then no sysbus MMIO region is created for its upstream. When an
 *   unused port lies in the middle of the range with other used ports at
 *   higher port numbers, a dummy MMIO region is created to ensure that
 *   port N's upstream is always sysbus MMIO region N. Dummy regions should
 *   not be mapped, and will assert if any access is made to them.
 * + Property "port[0..15]": MemoryRegion defining the downstream device(s)
 *   for each of the 16 ports of the PPC
 * + Named GPIO inputs "cfg_nonsec[0..15]": set to 1 if the port should be
 *   accessible to NonSecure transactions
 * + Named GPIO inputs "cfg_ap[0..15]": set to 1 if the port should be
 *   accessible to non-privileged transactions
 * + Named GPIO input "cfg_sec_resp": set to 1 if a rejected transaction should
 *   result in a transaction error, or 0 for the transaction to RAZ/WI
 * + Named GPIO input "irq_enable": set to 1 to enable interrupts
 * + Named GPIO input "irq_clear": set to 1 to clear a pending interrupt
 * + Named GPIO output "irq": set for a transaction-failed interrupt
 * + Property "NONSEC_MASK": if a bit is set in this mask then accesses to
 *   the associated port do not have the TZ security check performed. (This
 *   corresponds to the hardware allowing this to be set as a Verilog
 *   parameter.)
 */

#ifndef TZ_PPC_H
#define TZ_PPC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_TZ_PPC "tz-ppc"
OBJECT_DECLARE_SIMPLE_TYPE(TZPPC, TZ_PPC)

#define TZ_NUM_PORTS 16


typedef struct TZPPCPort {
    TZPPC *ppc;
    MemoryRegion upstream;
    AddressSpace downstream_as;
    MemoryRegion *downstream;
} TZPPCPort;

struct TZPPC {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/

    /* State: these just track the values of our input signals */
    bool cfg_nonsec[TZ_NUM_PORTS];
    bool cfg_ap[TZ_NUM_PORTS];
    bool cfg_sec_resp;
    bool irq_enable;
    bool irq_clear;
    /* State: are we asserting irq ? */
    bool irq_status;

    qemu_irq irq;

    /* Properties */
    uint32_t nonsec_mask;

    TZPPCPort port[TZ_NUM_PORTS];
};

#endif
