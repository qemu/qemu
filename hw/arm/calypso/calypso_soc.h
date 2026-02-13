#ifndef HW_ARM_CALYPSO_SOC_H
#define HW_ARM_CALYPSO_SOC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_CALYPSO_SOC "calypso-soc"

/* Déclare CALYPSO_SOC(obj) + le type QOM */
OBJECT_DECLARE_SIMPLE_TYPE(CalypsoSoCState, CALYPSO_SOC)

typedef struct CalypsoSoCState {
    SysBusDevice parent_obj;

    /* MMIO principal du SoC */
    MemoryRegion mmio;

    /* Propriété exposée (ex: /tmp/calypso-socket) */
    char *socket_path;
} CalypsoSoCState;

#endif /* HW_ARM_CALYPSO_SOC_H */
