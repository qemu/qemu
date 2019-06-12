#ifndef APM_H
#define APM_H

#include "hw/hw.h"
#include "exec/memory.h"

#define APM_CNT_IOPORT  0xb2
#define ACPI_PORT_SMI_CMD APM_CNT_IOPORT

typedef void (*apm_ctrl_changed_t)(uint32_t val, void *arg);

typedef struct APMState {
    uint8_t apmc;
    uint8_t apms;

    apm_ctrl_changed_t callback;
    void *arg;
    MemoryRegion io;
} APMState;

void apm_init(PCIDevice *dev, APMState *s, apm_ctrl_changed_t callback,
              void *arg);

extern const VMStateDescription vmstate_apm;

#endif /* APM_H */
