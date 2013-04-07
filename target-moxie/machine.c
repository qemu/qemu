#include "hw/hw.h"
#include "hw/boards.h"

const VMStateDescription vmstate_moxie_cpu = {
    .name = "cpu",
    .version_id = CPU_SAVE_VERSION,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32(flags, CPUMoxieState),
        VMSTATE_UINT32_ARRAY(gregs, CPUMoxieState, 16),
        VMSTATE_UINT32_ARRAY(sregs, CPUMoxieState, 256),
        VMSTATE_UINT32(pc, CPUMoxieState),
        VMSTATE_UINT32(cc_a, CPUMoxieState),
        VMSTATE_UINT32(cc_b, CPUMoxieState),
        VMSTATE_END_OF_LIST()
    }
};

void cpu_save(QEMUFile *f, void *opaque)
{
    vmstate_save_state(f, &vmstate_moxie_cpu, opaque);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    return vmstate_load_state(f, &vmstate_moxie_cpu, opaque, version_id);
}
