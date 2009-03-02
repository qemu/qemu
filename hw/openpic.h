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

qemu_irq *openpic_init (PCIBus *bus, int *pmem_index, int nb_cpus,
                        qemu_irq **irqs, qemu_irq irq_out);
qemu_irq *mpic_init (target_phys_addr_t base, int nb_cpus,
                        qemu_irq **irqs, qemu_irq irq_out);
#endif /* __OPENPIC_H__ */
