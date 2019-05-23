#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "machine.h"
#include "migration/cpu.h"

const VMStateDescription vmstate_moxie_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(flags, CPUMoxieState),
        VMSTATE_UINT32_ARRAY(gregs, CPUMoxieState, 16),
        VMSTATE_UINT32_ARRAY(sregs, CPUMoxieState, 256),
        VMSTATE_UINT32(pc, CPUMoxieState),
        VMSTATE_UINT32(cc_a, CPUMoxieState),
        VMSTATE_UINT32(cc_b, CPUMoxieState),
        VMSTATE_END_OF_LIST()
    }
};
