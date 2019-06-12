#include "qemu/osdep.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "migration/cpu.h"

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
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

const VMStateDescription vmstate_lm32_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(env, LM32CPU, 1, vmstate_env, CPULM32State),
        VMSTATE_END_OF_LIST()
    }
};
