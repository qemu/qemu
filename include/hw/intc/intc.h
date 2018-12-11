#ifndef INTC_H
#define INTC_H

#include "qom/object.h"

#define TYPE_INTERRUPT_STATS_PROVIDER "intctrl"

#define INTERRUPT_STATS_PROVIDER_CLASS(klass) \
    OBJECT_CLASS_CHECK(InterruptStatsProviderClass, (klass), \
                       TYPE_INTERRUPT_STATS_PROVIDER)
#define INTERRUPT_STATS_PROVIDER_GET_CLASS(obj) \
    OBJECT_GET_CLASS(InterruptStatsProviderClass, (obj), \
                     TYPE_INTERRUPT_STATS_PROVIDER)
#define INTERRUPT_STATS_PROVIDER(obj) \
    INTERFACE_CHECK(InterruptStatsProvider, (obj), \
                    TYPE_INTERRUPT_STATS_PROVIDER)

typedef struct InterruptStatsProvider InterruptStatsProvider;

typedef struct InterruptStatsProviderClass {
    InterfaceClass parent;

    /* The returned pointer and statistics must remain valid until
     * the BQL is next dropped.
     */
    bool (*get_statistics)(InterruptStatsProvider *obj, uint64_t **irq_counts,
                           unsigned int *nb_irqs);
    void (*print_info)(InterruptStatsProvider *obj, Monitor *mon);
} InterruptStatsProviderClass;

#endif
