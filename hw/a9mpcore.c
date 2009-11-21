/*
 * Cortex-A9MPCore internal peripheral emulation.
 *
 * Copyright (c) 2009 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

/* 64 external IRQ lines.  */
#define GIC_NIRQ 96
#include "mpcore.c"

static SysBusDeviceInfo mpcore_priv_info = {
    .init = mpcore_priv_init,
    .qdev.name  = "a9mpcore_priv",
    .qdev.size  = sizeof(mpcore_priv_state),
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("num-cpu", mpcore_priv_state, num_cpu, 1),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void a9mpcore_register_devices(void)
{
    sysbus_register_withprop(&mpcore_priv_info);
}

device_init(a9mpcore_register_devices)
