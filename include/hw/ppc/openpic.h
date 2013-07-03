#if !defined(__OPENPIC_H__)
#define __OPENPIC_H__

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

#endif /* __OPENPIC_H__ */
