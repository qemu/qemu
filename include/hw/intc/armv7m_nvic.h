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
#include "qom/object.h"

#define TYPE_NVIC "armv7m_nvic"
OBJECT_DECLARE_SIMPLE_TYPE(NVICState, NVIC)

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

struct NVICState {
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
    int vectpending_prio; /* group prio of the exception in vectpending */

    MemoryRegion sysregmem;

    uint32_t num_irq;
    qemu_irq excpout;
    qemu_irq sysresetreq;
};

/* Interface between CPU and Interrupt controller.  */
/**
 * armv7m_nvic_set_pending: mark the specified exception as pending
 * @s: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Marks the specified exception as pending. Note that we will assert()
 * if @secure is true and @irq does not specify one of the fixed set
 * of architecturally banked exceptions.
 */
void armv7m_nvic_set_pending(NVICState *s, int irq, bool secure);
/**
 * armv7m_nvic_set_pending_derived: mark this derived exception as pending
 * @s: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Similar to armv7m_nvic_set_pending(), but specifically for derived
 * exceptions (exceptions generated in the course of trying to take
 * a different exception).
 */
void armv7m_nvic_set_pending_derived(NVICState *s, int irq, bool secure);
/**
 * armv7m_nvic_set_pending_lazyfp: mark this lazy FP exception as pending
 * @s: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Similar to armv7m_nvic_set_pending(), but specifically for exceptions
 * generated in the course of lazy stacking of FP registers.
 */
void armv7m_nvic_set_pending_lazyfp(NVICState *s, int irq, bool secure);
/**
 * armv7m_nvic_get_pending_irq_info: return highest priority pending
 *    exception, and whether it targets Secure state
 * @s: the NVIC
 * @pirq: set to pending exception number
 * @ptargets_secure: set to whether pending exception targets Secure
 *
 * This function writes the number of the highest priority pending
 * exception (the one which would be made active by
 * armv7m_nvic_acknowledge_irq()) to @pirq, and sets @ptargets_secure
 * to true if the current highest priority pending exception should
 * be taken to Secure state, false for NS.
 */
void armv7m_nvic_get_pending_irq_info(NVICState *s, int *pirq,
                                      bool *ptargets_secure);
/**
 * armv7m_nvic_acknowledge_irq: make highest priority pending exception active
 * @s: the NVIC
 *
 * Move the current highest priority pending exception from the pending
 * state to the active state, and update v7m.exception to indicate that
 * it is the exception currently being handled.
 */
void armv7m_nvic_acknowledge_irq(NVICState *s);
/**
 * armv7m_nvic_complete_irq: complete specified interrupt or exception
 * @s: the NVIC
 * @irq: the exception number to complete
 * @secure: true if this exception was secure
 *
 * Returns: -1 if the irq was not active
 *           1 if completing this irq brought us back to base (no active irqs)
 *           0 if there is still an irq active after this one was completed
 * (Ignoring -1, this is the same as the RETTOBASE value before completion.)
 */
int armv7m_nvic_complete_irq(NVICState *s, int irq, bool secure);
/**
 * armv7m_nvic_get_ready_status(void *opaque, int irq, bool secure)
 * @s: the NVIC
 * @irq: the exception number to mark pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Return whether an exception is "ready", i.e. whether the exception is
 * enabled and is configured at a priority which would allow it to
 * interrupt the current execution priority. This controls whether the
 * RDY bit for it in the FPCCR is set.
 */
bool armv7m_nvic_get_ready_status(NVICState *s, int irq, bool secure);
/**
 * armv7m_nvic_raw_execution_priority: return the raw execution priority
 * @s: the NVIC
 *
 * Returns: the raw execution priority as defined by the v8M architecture.
 * This is the execution priority minus the effects of AIRCR.PRIS,
 * and minus any PRIMASK/FAULTMASK/BASEPRI priority boosting.
 * (v8M ARM ARM I_PKLD.)
 */
int armv7m_nvic_raw_execution_priority(NVICState *s);
/**
 * armv7m_nvic_neg_prio_requested: return true if the requested execution
 * priority is negative for the specified security state.
 * @s: the NVIC
 * @secure: the security state to test
 * This corresponds to the pseudocode IsReqExecPriNeg().
 */
#ifndef CONFIG_USER_ONLY
bool armv7m_nvic_neg_prio_requested(NVICState *s, bool secure);
#else
static inline bool armv7m_nvic_neg_prio_requested(NVICState *s, bool secure)
{
    return false;
}
#endif
#ifndef CONFIG_USER_ONLY
bool armv7m_nvic_can_take_pending_exception(NVICState *s);
#else
static inline bool armv7m_nvic_can_take_pending_exception(NVICState *s)
{
    return true;
}
#endif

#endif
