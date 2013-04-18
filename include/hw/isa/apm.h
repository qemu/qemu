#ifndef APM_H
#define APM_H

#include <stdint.h>
#include "qemu-common.h"
#include "hw/hw.h"
#include "exec/memory.h"

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
