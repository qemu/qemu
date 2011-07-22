/*
 * Status and system control registers for ARM RealView/Versatile boards.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw.h"
#include "qemu-timer.h"
#include "sysbus.h"
#include "primecell.h"
#include "sysemu.h"

#define LOCK_VALUE 0xa05f

typedef struct {
    SysBusDevice busdev;
    qemu_irq pl110_mux_ctrl;

    uint32_t sys_id;
    uint32_t leds;
    uint16_t lockval;
    uint32_t cfgdata1;
    uint32_t cfgdata2;
    uint32_t flags;
    uint32_t nvflags;
    uint32_t resetlevel;
    uint32_t proc_id;
    uint32_t sys_mci;
    uint32_t sys_cfgdata;
    uint32_t sys_cfgctrl;
    uint32_t sys_cfgstat;
    uint32_t sys_clcd;
} arm_sysctl_state;

static const VMStateDescription vmstate_arm_sysctl = {
    .name = "realview_sysctl",
    .version_id = 3,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(leds, arm_sysctl_state),
        VMSTATE_UINT16(lockval, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata1, arm_sysctl_state),
        VMSTATE_UINT32(cfgdata2, arm_sysctl_state),
        VMSTATE_UINT32(flags, arm_sysctl_state),
        VMSTATE_UINT32(nvflags, arm_sysctl_state),
        VMSTATE_UINT32(resetlevel, arm_sysctl_state),
        VMSTATE_UINT32_V(sys_mci, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgdata, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgctrl, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_cfgstat, arm_sysctl_state, 2),
        VMSTATE_UINT32_V(sys_clcd, arm_sysctl_state, 3),
        VMSTATE_END_OF_LIST()
    }
};

/* The PB926 actually uses a different format for
 * its SYS_ID register. Fortunately the bits which are
 * board type on later boards are distinct.
 */
#define BOARD_ID_PB926 0x100
#define BOARD_ID_EB 0x140
#define BOARD_ID_PBA8 0x178
#define BOARD_ID_PBX 0x182
#define BOARD_ID_VEXPRESS 0x190

static int board_id(arm_sysctl_state *s)
{
    /* Extract the board ID field from the SYS_ID register value */
    return (s->sys_id >> 16) & 0xfff;
}

static void arm_sysctl_reset(DeviceState *d)
{
    arm_sysctl_state *s = FROM_SYSBUS(arm_sysctl_state, sysbus_from_qdev(d));

    s->leds = 0;
    s->lockval = 0;
    s->cfgdata1 = 0;
    s->cfgdata2 = 0;
    s->flags = 0;
    s->resetlevel = 0;
    if (board_id(s) == BOARD_ID_VEXPRESS) {
        /* On VExpress this register will RAZ/WI */
        s->sys_clcd = 0;
    } else {
        /* All others: CLCDID 0x1f, indicating VGA */
        s->sys_clcd = 0x1f00;
    }
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
        if (board_id(s) == BOARD_ID_VEXPRESS) {
            /* reserved: RAZ/WI */
            return 0;
        }
        return s->resetlevel;
    case 0x44: /* PCICTL */
        return 1;
    case 0x48: /* MCI */
        return s->sys_mci;
    case 0x4c: /* FLASH */
        return 0;
    case 0x50: /* CLCD */
        return s->sys_clcd;
    case 0x54: /* CLCDSER */
        return 0;
    case 0x58: /* BOOTCS */
        return 0;
    case 0x5c: /* 24MHz */
        return muldiv64(qemu_get_clock_ns(vm_clock), 24000000, get_ticks_per_sec());
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
    case 0xa0: /* SYS_CFGDATA */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgdata;
    case 0xa4: /* SYS_CFGCTRL */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgctrl;
    case 0xa8: /* SYS_CFGSTAT */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        return s->sys_cfgstat;
    default:
    bad_reg:
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
        if (board_id(s) == BOARD_ID_VEXPRESS) {
            /* reserved: RAZ/WI */
            break;
        }
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
        break;
    case 0x50: /* CLCD */
        switch (board_id(s)) {
        case BOARD_ID_PB926:
            /* On 926 bits 13:8 are R/O, bits 1:0 control
             * the mux that defines how to interpret the PL110
             * graphics format, and other bits are r/w but we
             * don't implement them to do anything.
             */
            s->sys_clcd &= 0x3f00;
            s->sys_clcd |= val & ~0x3f00;
            qemu_set_irq(s->pl110_mux_ctrl, val & 3);
            break;
        case BOARD_ID_EB:
            /* The EB is the same except that there is no mux since
             * the EB has a PL111.
             */
            s->sys_clcd &= 0x3f00;
            s->sys_clcd |= val & ~0x3f00;
            break;
        case BOARD_ID_PBA8:
        case BOARD_ID_PBX:
            /* On PBA8 and PBX bit 7 is r/w and all other bits
             * are either r/o or RAZ/WI.
             */
            s->sys_clcd &= (1 << 7);
            s->sys_clcd |= val & ~(1 << 7);
            break;
        case BOARD_ID_VEXPRESS:
        default:
            /* On VExpress this register is unimplemented and will RAZ/WI */
            break;
        }
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
    case 0xa0: /* SYS_CFGDATA */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgdata = val;
        return;
    case 0xa4: /* SYS_CFGCTRL */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgctrl = val & ~(3 << 18);
        s->sys_cfgstat = 1;            /* complete */
        switch (s->sys_cfgctrl) {
        case 0xc0800000:            /* SYS_CFG_SHUTDOWN to motherboard */
            qemu_system_shutdown_request();
            break;
        case 0xc0900000:            /* SYS_CFG_REBOOT to motherboard */
            qemu_system_reset_request();
            break;
        default:
            s->sys_cfgstat |= 2;        /* error */
        }
        return;
    case 0xa8: /* SYS_CFGSTAT */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgstat = val & 3;
        return;
    default:
    bad_reg:
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

static void arm_sysctl_gpio_set(void *opaque, int line, int level)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;
    switch (line) {
    case ARM_SYSCTL_GPIO_MMC_WPROT:
    {
        /* For PB926 and EB write-protect is bit 2 of SYS_MCI;
         * for all later boards it is bit 1.
         */
        int bit = 2;
        if ((board_id(s) == BOARD_ID_PB926) || (board_id(s) == BOARD_ID_EB)) {
            bit = 4;
        }
        s->sys_mci &= ~bit;
        if (level) {
            s->sys_mci |= bit;
        }
        break;
    }
    case ARM_SYSCTL_GPIO_MMC_CARDIN:
        s->sys_mci &= ~1;
        if (level) {
            s->sys_mci |= 1;
        }
        break;
    }
}

static int arm_sysctl_init1(SysBusDevice *dev)
{
    arm_sysctl_state *s = FROM_SYSBUS(arm_sysctl_state, dev);
    int iomemtype;

    iomemtype = cpu_register_io_memory(arm_sysctl_readfn,
                                       arm_sysctl_writefn, s,
                                       DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, 0x1000, iomemtype);
    qdev_init_gpio_in(&s->busdev.qdev, arm_sysctl_gpio_set, 2);
    qdev_init_gpio_out(&s->busdev.qdev, &s->pl110_mux_ctrl, 1);
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
