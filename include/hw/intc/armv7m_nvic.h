/*
 * ARMv7M NVIC object
 *
 * Copyright (c) 2017 Linaro Ltd
 * Written by Peter Maydell <peter.maydell@linaro.org>
 *
 * This code is licensed under the GPL version 2 or later.
 */

#ifndef HW_ARM_ARMV7M_NVIC_H
#define HW_ARM_ARMV7M_NVIC_H

#include "target/arm/cpu.h"
#include "hw/sysbus.h"
#include "hw/timer/armv7m_systick.h"

#define TYPE_NVIC "armv7m_nvic"

#define NVIC(obj) \
    OBJECT_CHECK(NVICState, (obj), TYPE_NVIC)

/* Highest permitted number of exceptions (architectural limit) */
#define NVIC_MAX_VECTORS 512
/* Number of internal exceptions */
#define NVIC_INTERNAL_VECTORS 16

typedef struct VecInfo {
    /* Exception priorities can range from -3 to 255; only the unmodifiable
     * priority values for RESET, NMI and HardFault can be negative.
     */
    int16_t prio;
    uint8_t enabled;
    uint8_t pending;
    uint8_t active;
    uint8_t level; /* exceptions <=15 never set level */
} VecInfo;

typedef struct NVICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    ARMCPU *cpu;

    VecInfo vectors[NVIC_MAX_VECTORS];
    /* If the v8M security extension is implemented, some of the internal
     * exceptions are banked between security states (ie there exists both
     * a Secure and a NonSecure version of the exception and its state):
     *  HardFault, MemManage, UsageFault, SVCall, PendSV, SysTick (R_PJHV)
     * The rest (including all the external exceptions) are not banked, though
     * they may be configurable to target either Secure or NonSecure state.
     * We store the secure exception state in sec_vectors[] for the banked
     * exceptions, and otherwise use only vectors[] (including for exceptions
     * like SecureFault that unconditionally target Secure state).
     * Entries in sec_vectors[] for non-banked exception numbers are unused.
     */
    VecInfo sec_vectors[NVIC_INTERNAL_VECTORS];
    /* The PRIGROUP field in AIRCR is banked */
    uint32_t prigroup[M_REG_NUM_BANKS];
    uint8_t num_prio_bits;

    /* v8M NVIC_ITNS state (stored as a bool per bit) */
    bool itns[NVIC_MAX_VECTORS];

    /* The following fields are all cached state that can be recalculated
     * from the vectors[] and sec_vectors[] arrays and the prigroup field:
     *  - vectpending
     *  - vectpending_is_secure
     *  - exception_prio
     *  - vectpending_prio
     */
    unsigned int vectpending; /* highest prio pending enabled exception */
    /* true if vectpending is a banked secure exception, ie it is in
     * sec_vectors[] rather than vectors[]
     */
    bool vectpending_is_s_banked;
    int exception_prio; /* group prio of the highest prio active exception */
    int vectpending_prio; /* group prio of the exeception in vectpending */

    MemoryRegion sysregmem;
    MemoryRegion sysreg_ns_mem;
    MemoryRegion systickmem;
    MemoryRegion systick_ns_mem;
    MemoryRegion container;

    uint32_t num_irq;
    qemu_irq excpout;
    qemu_irq sysresetreq;

    SysTickState systick[M_REG_NUM_BANKS];
} NVICState;

#endif
