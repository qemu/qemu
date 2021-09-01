/*
 * ARMv7M CPU object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_H
#define HW_ARM_ARMV7M_H

#include "hw/sysbus.h"
#include "hw/intc/armv7m_nvic.h"
#include "hw/misc/armv7m_ras.h"
#include "target/arm/idau.h"
#include "qom/object.h"
#include "hw/clock.h"

#define TYPE_BITBAND "ARM-bitband-memory"
OBJECT_DECLARE_SIMPLE_TYPE(BitBandState, BITBAND)

struct BitBandState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    AddressSpace source_as;
    MemoryRegion iomem;
    uint32_t base;
    MemoryRegion *source_memory;
};

#define TYPE_ARMV7M "armv7m"
OBJECT_DECLARE_SIMPLE_TYPE(ARMv7MState, ARMV7M)

#define ARMV7M_NUM_BITBANDS 2

/* ARMv7M container object.
 * + Unnamed GPIO input lines: external IRQ lines for the NVIC
 * + Named GPIO output SYSRESETREQ: signalled for guest AIRCR.SYSRESETREQ.
 *   If this GPIO is not wired up then the NVIC will default to performing
 *   a qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET).
 * + Property "cpu-type": CPU type to instantiate
 * + Property "num-irq": number of external IRQ lines
 * + Property "memory": MemoryRegion defining the physical address space
 *   that CPU accesses see. (The NVIC, bitbanding and other CPU-internal
 *   devices will be automatically layered on top of this view.)
 * + Property "idau": IDAU interface (forwarded to CPU object)
 * + Property "init-svtor": secure VTOR reset value (forwarded to CPU object)
 * + Property "init-nsvtor": non-secure VTOR reset value (forwarded to CPU object)
 * + Property "vfp": enable VFP (forwarded to CPU object)
 * + Property "dsp": enable DSP (forwarded to CPU object)
 * + Property "enable-bitband": expose bitbanded IO
 * + Clock input "refclk" is the external reference clock for the systick timers
 * + Clock input "cpuclk" is the main CPU clock
 */
struct ARMv7MState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    NVICState nvic;
    BitBandState bitband[ARMV7M_NUM_BITBANDS];
    ARMCPU *cpu;
    ARMv7MRAS ras;
    SysTickState systick[M_REG_NUM_BANKS];

    /* MemoryRegion we pass to the CPU, with our devices layered on
     * top of the ones the board provides in board_memory.
     */
    MemoryRegion container;
    /*
     * MemoryRegion which passes the transaction to either the S or the
     * NS systick device depending on the transaction attributes
     */
    MemoryRegion systickmem;
    /*
     * MemoryRegion which enforces the S/NS handling of the systick
     * device NS alias region and passes the transaction to the
     * NS systick device if appropriate.
     */
    MemoryRegion systick_ns_mem;
    /* Ditto, for the sysregs region provided by the NVIC */
    MemoryRegion sysreg_ns_mem;
    /* MR providing default PPB behaviour */
    MemoryRegion defaultmem;

    Clock *refclk;
    Clock *cpuclk;

    /* Properties */
    char *cpu_type;
    /* MemoryRegion the board provides to us (with its devices, RAM, etc) */
    MemoryRegion *board_memory;
    Object *idau;
    uint32_t init_svtor;
    uint32_t init_nsvtor;
    bool enable_bitband;
    bool start_powered_off;
    bool vfp;
    bool dsp;
};

#endif
