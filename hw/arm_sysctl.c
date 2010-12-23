/*
 * Status and system control registers for ARM RealView/Versatile boards.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licenced under the GPL.
 */

#include "hw.h"
#include "qemu-timer.h"
#include "sysbus.h"
#include "primecell.h"
#include "sysemu.h"

#define LOCK_VALUE 0xa05f

typedef struct {
    SysBusDevice busdev;
    uint32_t sys_id;
    uint32_t leds;
    uint16_t lockval;
    uint32_t cfgdata1;
    uint32_t cfgdata2;
    uint32_t flags;
    uint32_t nvflags;
    uint32_t resetlevel;
    uint32_t proc_id;
} arm_sysctl_state;

static const VMStateDescription vmstate_arm_sysctl = {
    .name = "realview_sysctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(leds, arm_sysctl_state),
        VMSTATE_UINT16(lockval, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata1, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata2, arm_sysctl_state),
        VMSTATE_UINT32(flags, arm_sysctl_state),
        VMSTATE_UINT32(nvflags, arm_sysctl_state),
        VMSTATE_UINT32(resetlevel, arm_sysctl_state),
        VMSTATE_END_OF_LIST()
    }
};

static void arm_sysctl_reset(DeviceState *d)
{
    arm_sysctl_state *s = FROM_SYSBUS(arm_sysctl_state, sysbus_from_qdev(d));

    s->leds = 0;
    s->lockval = 0;
    s->cfgdata1 = 0;
    s->cfgdata2 = 0;
    s->flags = 0;
    s->resetlevel = 0;
}

static uint32_t arm_sysctl_read(void *opaque, target_phys_addr_t offset)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;

    switch (offset) {
    case 0x00: /* ID */
        return s->sys_id;
    case 0x04: /* SW */
        /* General purpose hardware switches.
           We don't have a useful way of exposing these to the user.  */
        return 0;
    case 0x08: /* LED */
        return s->leds;
    case 0x20: /* LOCK */
        return s->lockval;
    case 0x0c: /* OSC0 */
    case 0x10: /* OSC1 */
    case 0x14: /* OSC2 */
    case 0x18: /* OSC3 */
    case 0x1c: /* OSC4 */
    case 0x24: /* 100HZ */
        /* ??? Implement these.  */
        return 0;
    case 0x28: /* CFGDATA1 */
        return s->cfgdata1;
    case 0x2c: /* CFGDATA2 */
        return s->cfgdata2;
    case 0x30: /* FLAGS */
        return s->flags;
    case 0x38: /* NVFLAGS */
        return s->nvflags;
    case 0x40: /* RESETCTL */
        return s->resetlevel;
    case 0x44: /* PCICTL */
        return 1;
    case 0x48: /* MCI */
        return 0;
    case 0x4c: /* FLASH */
        return 0;
    case 0x50: /* CLCD */
        return 0x1000;
    case 0x54: /* CLCDSER */
        return 0;
    case 0x58: /* BOOTCS */
        return 0;
    case 0x5c: /* 24MHz */
        return muldiv64(qemu_get_clock(vm_clock), 24000000, get_ticks_per_sec());
    case 0x60: /* MISC */
        return 0;
    case 0x84: /* PROCID0 */
        return s->proc_id;
    case 0x88: /* PROCID1 */
        return 0xff000000;
    case 0x64: /* DMAPSR0 */
    case 0x68: /* DMAPSR1 */
    case 0x6c: /* DMAPSR2 */
    case 0x70: /* IOSEL */
    case 0x74: /* PLDCTL */
    case 0x80: /* BUSID */
    case 0x8c: /* OSCRESET0 */
    case 0x90: /* OSCRESET1 */
    case 0x94: /* OSCRESET2 */
    case 0x98: /* OSCRESET3 */
    case 0x9c: /* OSCRESET4 */
    case 0xc0: /* SYS_TEST_OSC0 */
    case 0xc4: /* SYS_TEST_OSC1 */
    case 0xc8: /* SYS_TEST_OSC2 */
    case 0xcc: /* SYS_TEST_OSC3 */
    case 0xd0: /* SYS_TEST_OSC4 */
        return 0;
    default:
        printf ("arm_sysctl_read: Bad register offset 0x%x\n", (int)offset);
        return 0;
    }
}

static void arm_sysctl_write(void *opaque, target_phys_addr_t offset,
                          uint32_t val)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;

    switch (offset) {
    case 0x08: /* LED */
        s->leds = val;
    case 0x0c: /* OSC0 */
    case 0x10: /* OSC1 */
    case 0x14: /* OSC2 */
    case 0x18: /* OSC3 */
    case 0x1c: /* OSC4 */
        /* ??? */
        break;
    case 0x20: /* LOCK */
        if (val == LOCK_VALUE)
            s->lockval = val;
        else
            s->lockval = val & 0x7fff;
        break;
    case 0x28: /* CFGDATA1 */
        /* ??? Need to implement this.  */
        s->cfgdata1 = val;
        break;
    case 0x2c: /* CFGDATA2 */
        /* ??? Need to implement this.  */
        s->cfgdata2 = val;
        break;
    case 0x30: /* FLAGSSET */
        s->flags |= val;
        break;
    case 0x34: /* FLAGSCLR */
        s->flags &= ~val;
        break;
    case 0x38: /* NVFLAGSSET */
        s->nvflags |= val;
        break;
    case 0x3c: /* NVFLAGSCLR */
        s->nvflags &= ~val;
        break;
    case 0x40: /* RESETCTL */
        if (s->lockval == LOCK_VALUE) {
            s->resetlevel = val;
            if (val & 0x100)
                qemu_system_reset_request ();
        }
        break;
    case 0x44: /* PCICTL */
        /* nothing to do.  */
        break;
    case 0x4c: /* FLASH */
    case 0x50: /* CLCD */
    case 0x54: /* CLCDSER */
    case 0x64: /* DMAPSR0 */
    case 0x68: /* DMAPSR1 */
    case 0x6c: /* DMAPSR2 */
    case 0x70: /* IOSEL */
    case 0x74: /* PLDCTL */
    case 0x80: /* BUSID */
    case 0x84: /* PROCID0 */
    case 0x88: /* PROCID1 */
    case 0x8c: /* OSCRESET0 */
    case 0x90: /* OSCRESET1 */
    case 0x94: /* OSCRESET2 */
    case 0x98: /* OSCRESET3 */
    case 0x9c: /* OSCRESET4 */
        break;
    default:
        printf ("arm_sysctl_write: Bad register offset 0x%x\n", (int)offset);
        return;
    }
}

static CPUReadMemoryFunc * const arm_sysctl_readfn[] = {
   arm_sysctl_read,
   arm_sysctl_read,
   arm_sysctl_read
};

static CPUWriteMemoryFunc * const arm_sysctl_writefn[] = {
   arm_sysctl_write,
   arm_sysctl_write,
   arm_sysctl_write
};

static int arm_sysctl_init1(SysBusDevice *dev)
{
    arm_sysctl_state *s = FROM_SYSBUS(arm_sysctl_state, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(arm_sysctl_readfn,
                                       arm_sysctl_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    /* ??? Save/restore.  */
    return 0;
}

/* Legacy helper function.  */
void arm_sysctl_init(uint32_t base, uint32_t sys_id, uint32_t proc_id)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "realview_sysctl");
    qdev_prop_set_uint32(dev, "sys_id", sys_id);
    qdev_init_nofail(dev);
    qdev_prop_set_uint32(dev, "proc_id", proc_id);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
}

static SysBusDeviceInfo arm_sysctl_info = {
    .init = arm_sysctl_init1,
    .qdev.name  = "realview_sysctl",
    .qdev.size  = sizeof(arm_sysctl_state),
    .qdev.vmsd = &vmstate_arm_sysctl,
    .qdev.reset = arm_sysctl_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("sys_id", arm_sysctl_state, sys_id, 0),
        DEFINE_PROP_UINT32("proc_id", arm_sysctl_state, proc_id, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void arm_sysctl_register_devices(void)
{
    sysbus_register_withprop(&arm_sysctl_info);
}

device_init(arm_sysctl_register_devices)
