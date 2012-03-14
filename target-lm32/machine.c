#include "hw/hw.h"
#include "hw/boards.h"

static const VMStateDescription vmstate_cpu = {
    .name = "cpu",
    .version_id = CPU_SAVE_VERSION,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, CPULM32State, 32),
        VMSTATE_UINT32(pc, CPULM32State),
        VMSTATE_UINT32(ie, CPULM32State),
        VMSTATE_UINT32(icc, CPULM32State),
        VMSTATE_UINT32(dcc, CPULM32State),
        VMSTATE_UINT32(cc, CPULM32State),
        VMSTATE_UINT32(eba, CPULM32State),
        VMSTATE_UINT32(dc, CPULM32State),
        VMSTATE_UINT32(deba, CPULM32State),
        VMSTATE_UINT32_ARRAY(bp, CPULM32State, 4),
        VMSTATE_UINT32_ARRAY(wp, CPULM32State, 4),
        VMSTATE_END_OF_LIST()
    }
};

void cpu_save(QEMUFile *f, void *opaque)
{
    vmstate_save_state(f, &vmstate_cpu, opaque);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return vmstate_load_state(f, &vmstate_cpu, opaque, version_id);
}
