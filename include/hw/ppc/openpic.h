#ifndef OPENPIC_H
#define OPENPIC_H

#include "hw/sysbus.h"
#include "hw/core/cpu.h"
#include "qom/object.h"

#define MAX_CPU     32
#define MAX_MSI     8
#define VID         0x03 /* MPIC version ID */

/* OpenPIC have 5 outputs per CPU connected and one IRQ out single output */
enum {
    OPENPIC_OUTPUT_INT = 0, /* IRQ                       */
    OPENPIC_OUTPUT_CINT,    /* critical IRQ              */
    OPENPIC_OUTPUT_MCK,     /* Machine check event       */
    OPENPIC_OUTPUT_DEBUG,   /* Inconditional debug event */
    OPENPIC_OUTPUT_RESET,   /* Core reset event          */
    OPENPIC_OUTPUT_NB,
};

typedef struct IrqLines { qemu_irq irq[OPENPIC_OUTPUT_NB]; } IrqLines;

#define OPENPIC_MODEL_FSL_MPIC_20 1
#define OPENPIC_MODEL_FSL_MPIC_42 2
#define OPENPIC_MODEL_KEYLARGO    3

#define OPENPIC_MAX_SRC     256
#define OPENPIC_MAX_TMR     4
#define OPENPIC_MAX_IPI     4
#define OPENPIC_MAX_IRQ     (OPENPIC_MAX_SRC + OPENPIC_MAX_IPI + \
                             OPENPIC_MAX_TMR)

/* KeyLargo */
#define KEYLARGO_MAX_CPU  4
#define KEYLARGO_MAX_EXT  64
#define KEYLARGO_MAX_IPI  4
#define KEYLARGO_MAX_IRQ  (64 + KEYLARGO_MAX_IPI)
#define KEYLARGO_MAX_TMR  0
#define KEYLARGO_IPI_IRQ  (KEYLARGO_MAX_EXT) /* First IPI IRQ */
/* Timers don't exist but this makes the code happy... */
#define KEYLARGO_TMR_IRQ  (KEYLARGO_IPI_IRQ + KEYLARGO_MAX_IPI)

typedef struct FslMpicInfo {
    int max_ext;
} FslMpicInfo;

typedef enum IRQType {
    IRQ_TYPE_NORMAL = 0,
    IRQ_TYPE_FSLINT,        /* FSL internal interrupt -- level only */
    IRQ_TYPE_FSLSPECIAL,    /* FSL timer/IPI interrupt, edge, no polarity */
} IRQType;

/*
 * Round up to the nearest 64 IRQs so that the queue length
 * won't change when moving between 32 and 64 bit hosts.
 */
#define IRQQUEUE_SIZE_BITS ROUND_UP(OPENPIC_MAX_IRQ, 64)

typedef struct IRQQueue {
    unsigned long *queue;
    int32_t queue_size; /* Only used for VMSTATE_BITMAP */
    int next;
    int priority;
} IRQQueue;

typedef struct IRQSource {
    uint32_t ivpr;  /* IRQ vector/priority register */
    uint32_t idr;   /* IRQ destination register */
    uint32_t destmask; /* bitmap of CPU destinations */
    int last_cpu;
    int output;     /* IRQ level, e.g. OPENPIC_OUTPUT_INT */
    int pending;    /* TRUE if IRQ is pending */
    IRQType type;
    bool level:1;   /* level-triggered */
    bool nomask:1;  /* critical interrupts ignore mask on some FSL MPICs */
} IRQSource;

#define IVPR_MASK_SHIFT       31
#define IVPR_MASK_MASK        (1U << IVPR_MASK_SHIFT)
#define IVPR_ACTIVITY_SHIFT   30
#define IVPR_ACTIVITY_MASK    (1U << IVPR_ACTIVITY_SHIFT)
#define IVPR_MODE_SHIFT       29
#define IVPR_MODE_MASK        (1U << IVPR_MODE_SHIFT)
#define IVPR_POLARITY_SHIFT   23
#define IVPR_POLARITY_MASK    (1U << IVPR_POLARITY_SHIFT)
#define IVPR_SENSE_SHIFT      22
#define IVPR_SENSE_MASK       (1U << IVPR_SENSE_SHIFT)

#define IVPR_PRIORITY_MASK     (0xFU << 16)
#define IVPR_PRIORITY(_ivprr_) ((int)(((_ivprr_) & IVPR_PRIORITY_MASK) >> 16))
#define IVPR_VECTOR(opp, _ivprr_) ((_ivprr_) & (opp)->vector_mask)

/* IDR[EP/CI] are only for FSL MPIC prior to v4.0 */
#define IDR_EP      0x80000000  /* external pin */
#define IDR_CI      0x40000000  /* critical interrupt */

typedef struct OpenPICTimer {
    uint32_t tccr;  /* Global timer current count register */
    uint32_t tbcr;  /* Global timer base count register */
    int                   n_IRQ;
    bool                  qemu_timer_active; /* Is the qemu_timer is running? */
    struct QEMUTimer     *qemu_timer;
    struct OpenPICState  *opp;          /* Device timer is part of. */
    /*
     * The QEMU_CLOCK_VIRTUAL time (in ns) corresponding to the last
     * current_count written or read, only defined if qemu_timer_active.
     */
    uint64_t              origin_time;
} OpenPICTimer;

typedef struct OpenPICMSI {
    uint32_t msir;   /* Shared Message Signaled Interrupt Register */
} OpenPICMSI;

typedef struct IRQDest {
    int32_t ctpr; /* CPU current task priority */
    IRQQueue raised;
    IRQQueue servicing;
    qemu_irq *irqs;

    /* Count of IRQ sources asserting on non-INT outputs */
    uint32_t outputs_active[OPENPIC_OUTPUT_NB];
} IRQDest;

#define TYPE_OPENPIC "openpic"
OBJECT_DECLARE_SIMPLE_TYPE(OpenPICState, OPENPIC)

struct OpenPICState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion mem;

    /* Behavior control */
    FslMpicInfo *fsl;
    uint32_t model;
    uint32_t flags;
    uint32_t nb_irqs;
    uint32_t vid;
    uint32_t vir; /* Vendor identification register */
    uint32_t vector_mask;
    uint32_t tfrr_reset;
    uint32_t ivpr_reset;
    uint32_t idr_reset;
    uint32_t brr1;
    uint32_t mpic_mode_mask;

    /* Sub-regions */
    MemoryRegion sub_io_mem[6];

    /* Global registers */
    uint32_t frr; /* Feature reporting register */
    uint32_t gcr; /* Global configuration register  */
    uint32_t pir; /* Processor initialization register */
    uint32_t spve; /* Spurious vector register */
    uint32_t tfrr; /* Timer frequency reporting register */
    /* Source registers */
    IRQSource src[OPENPIC_MAX_IRQ];
    /* Local registers per output pin */
    IRQDest dst[MAX_CPU];
    uint32_t nb_cpus;
    /* Timer registers */
    OpenPICTimer timers[OPENPIC_MAX_TMR];
    uint32_t max_tmr;

    /* Shared MSI registers */
    OpenPICMSI msi[MAX_MSI];
    uint32_t max_irq;
    uint32_t irq_ipi0;
    uint32_t irq_tim0;
    uint32_t irq_msi;
};

#endif /* OPENPIC_H */
