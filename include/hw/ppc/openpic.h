#if !defined(__OPENPIC_H__)
#define __OPENPIC_H__

#include "qemu-common.h"
#include "hw/qdev.h"

/* OpenPIC have 5 outputs per CPU connected and one IRQ out single output */
enum {
    OPENPIC_OUTPUT_INT = 0, /* IRQ                       */
    OPENPIC_OUTPUT_CINT,    /* critical IRQ              */
    OPENPIC_OUTPUT_MCK,     /* Machine check event       */
    OPENPIC_OUTPUT_DEBUG,   /* Inconditional debug event */
    OPENPIC_OUTPUT_RESET,   /* Core reset event          */
    OPENPIC_OUTPUT_NB,
};

#define OPENPIC_MODEL_RAVEN       0
#define OPENPIC_MODEL_FSL_MPIC_20 1
#define OPENPIC_MODEL_FSL_MPIC_42 2

#define OPENPIC_MAX_SRC     256
#define OPENPIC_MAX_TMR     4
#define OPENPIC_MAX_IPI     4
#define OPENPIC_MAX_IRQ     (OPENPIC_MAX_SRC + OPENPIC_MAX_IPI + \
                             OPENPIC_MAX_TMR)

DeviceState *kvm_openpic_create(BusState *bus, int model);

#endif /* __OPENPIC_H__ */
