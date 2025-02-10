/*
 * Status and system control registers for ARM RealView/Versatile boards.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/timer.h"
#include "system/runstate.h"
#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/arm/primecell.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

#define LOCK_VALUE 0xa05f

#define TYPE_ARM_SYSCTL "realview_sysctl"
OBJECT_DECLARE_SIMPLE_TYPE(arm_sysctl_state, ARM_SYSCTL)

struct arm_sysctl_state {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
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
    uint32_t mb_clock[6];
    uint32_t *db_clock;
    uint32_t db_num_vsensors;
    uint32_t *db_voltage;
    uint32_t db_num_clocks;
    uint32_t *db_clock_reset;
};

static const VMStateDescription vmstate_arm_sysctl = {
    .name = "realview_sysctl",
    .version_id = 4,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
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
        VMSTATE_UINT32_ARRAY_V(mb_clock, arm_sysctl_state, 6, 4),
        VMSTATE_VARRAY_UINT32(db_clock, arm_sysctl_state, db_num_clocks,
                              4, vmstate_info_uint32, uint32_t),
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
    arm_sysctl_state *s = ARM_SYSCTL(d);
    int i;

    s->leds = 0;
    s->lockval = 0;
    s->cfgdata1 = 0;
    s->cfgdata2 = 0;
    s->flags = 0;
    s->resetlevel = 0;
    /* Motherboard oscillators (in Hz) */
    s->mb_clock[0] = 50000000; /* Static memory clock: 50MHz */
    s->mb_clock[1] = 23750000; /* motherboard CLCD clock: 23.75MHz */
    s->mb_clock[2] = 24000000; /* IO FPGA peripheral clock: 24MHz */
    s->mb_clock[3] = 24000000; /* IO FPGA reserved clock: 24MHz */
    s->mb_clock[4] = 24000000; /* System bus global clock: 24MHz */
    s->mb_clock[5] = 24000000; /* IO FPGA reserved clock: 24MHz */
    /* Daughterboard oscillators: reset from property values */
    for (i = 0; i < s->db_num_clocks; i++) {
        s->db_clock[i] = s->db_clock_reset[i];
    }
    if (board_id(s) == BOARD_ID_VEXPRESS) {
        /* On VExpress this register will RAZ/WI */
        s->sys_clcd = 0;
    } else {
        /* All others: CLCDID 0x1f, indicating VGA */
        s->sys_clcd = 0x1f00;
    }
}

static uint64_t arm_sysctl_read(void *opaque, hwaddr offset,
                                unsigned size)
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
        return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), 24000000,
                        NANOSECONDS_PER_SECOND);
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
        qemu_log_mask(LOG_GUEST_ERROR,
                      "arm_sysctl_read: Bad register offset 0x%x\n",
                      (int)offset);
        return 0;
    }
}

/* SYS_CFGCTRL functions */
#define SYS_CFG_OSC 1
#define SYS_CFG_VOLT 2
#define SYS_CFG_AMP 3
#define SYS_CFG_TEMP 4
#define SYS_CFG_RESET 5
#define SYS_CFG_SCC 6
#define SYS_CFG_MUXFPGA 7
#define SYS_CFG_SHUTDOWN 8
#define SYS_CFG_REBOOT 9
#define SYS_CFG_DVIMODE 11
#define SYS_CFG_POWER 12
#define SYS_CFG_ENERGY 13

/* SYS_CFGCTRL site field values */
#define SYS_CFG_SITE_MB 0
#define SYS_CFG_SITE_DB1 1
#define SYS_CFG_SITE_DB2 2

/**
 * vexpress_cfgctrl_read:
 * @s: arm_sysctl_state pointer
 * @dcc, @function, @site, @position, @device: split out values from
 * SYS_CFGCTRL register
 * @val: pointer to where to put the read data on success
 *
 * Handle a VExpress SYS_CFGCTRL register read. On success, return true and
 * write the read value to *val. On failure, return false (and val may
 * or may not be written to).
 */
static bool vexpress_cfgctrl_read(arm_sysctl_state *s, unsigned int dcc,
                                  unsigned int function, unsigned int site,
                                  unsigned int position, unsigned int device,
                                  uint32_t *val)
{
    /* We don't support anything other than DCC 0, board stack position 0
     * or sites other than motherboard/daughterboard:
     */
    if (dcc != 0 || position != 0 ||
        (site != SYS_CFG_SITE_MB && site != SYS_CFG_SITE_DB1)) {
        goto cfgctrl_unimp;
    }

    switch (function) {
    case SYS_CFG_VOLT:
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_vsensors) {
            *val = s->db_voltage[device];
            return true;
        }
        if (site == SYS_CFG_SITE_MB && device == 0) {
            /* There is only one motherboard voltage sensor:
             * VIO : 3.3V : bus voltage between mother and daughterboard
             */
            *val = 3300000;
            return true;
        }
        break;
    case SYS_CFG_OSC:
        if (site == SYS_CFG_SITE_MB && device < ARRAY_SIZE(s->mb_clock)) {
            /* motherboard clock */
            *val = s->mb_clock[device];
            return true;
        }
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_clocks) {
            /* daughterboard clock */
            *val = s->db_clock[device];
            return true;
        }
        break;
    default:
        break;
    }

cfgctrl_unimp:
    qemu_log_mask(LOG_UNIMP,
                  "arm_sysctl: Unimplemented SYS_CFGCTRL read of function "
                  "0x%x DCC 0x%x site 0x%x position 0x%x device 0x%x\n",
                  function, dcc, site, position, device);
    return false;
}

/**
 * vexpress_cfgctrl_write:
 * @s: arm_sysctl_state pointer
 * @dcc, @function, @site, @position, @device: split out values from
 * SYS_CFGCTRL register
 * @val: data to write
 *
 * Handle a VExpress SYS_CFGCTRL register write. On success, return true.
 * On failure, return false.
 */
static bool vexpress_cfgctrl_write(arm_sysctl_state *s, unsigned int dcc,
                                   unsigned int function, unsigned int site,
                                   unsigned int position, unsigned int device,
                                   uint32_t val)
{
    /* We don't support anything other than DCC 0, board stack position 0
     * or sites other than motherboard/daughterboard:
     */
    if (dcc != 0 || position != 0 ||
        (site != SYS_CFG_SITE_MB && site != SYS_CFG_SITE_DB1)) {
        goto cfgctrl_unimp;
    }

    switch (function) {
    case SYS_CFG_OSC:
        if (site == SYS_CFG_SITE_MB && device < ARRAY_SIZE(s->mb_clock)) {
            /* motherboard clock */
            s->mb_clock[device] = val;
            return true;
        }
        if (site == SYS_CFG_SITE_DB1 && device < s->db_num_clocks) {
            /* daughterboard clock */
            s->db_clock[device] = val;
            return true;
        }
        break;
    case SYS_CFG_MUXFPGA:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            /* Select whether video output comes from motherboard
             * or daughterboard: log and ignore as QEMU doesn't
             * support this.
             */
            qemu_log_mask(LOG_UNIMP, "arm_sysctl: selection of video output "
                          "not supported, ignoring\n");
            return true;
        }
        break;
    case SYS_CFG_SHUTDOWN:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            return true;
        }
        break;
    case SYS_CFG_REBOOT:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return true;
        }
        break;
    case SYS_CFG_DVIMODE:
        if (site == SYS_CFG_SITE_MB && device == 0) {
            /* Selecting DVI mode is meaningless for QEMU: we will
             * always display the output correctly according to the
             * pixel height/width programmed into the CLCD controller.
             */
            return true;
        }
    default:
        break;
    }

cfgctrl_unimp:
    qemu_log_mask(LOG_UNIMP,
                  "arm_sysctl: Unimplemented SYS_CFGCTRL write of function "
                  "0x%x DCC 0x%x site 0x%x position 0x%x device 0x%x\n",
                  function, dcc, site, position, device);
    return false;
}

static void arm_sysctl_write(void *opaque, hwaddr offset,
                             uint64_t val, unsigned size)
{
    arm_sysctl_state *s = (arm_sysctl_state *)opaque;

    switch (offset) {
    case 0x08: /* LED */
        s->leds = val;
        break;
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
        switch (board_id(s)) {
        case BOARD_ID_PB926:
            if (s->lockval == LOCK_VALUE) {
                s->resetlevel = val;
                if (val & 0x100) {
                    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                }
            }
            break;
        case BOARD_ID_PBX:
        case BOARD_ID_PBA8:
            if (s->lockval == LOCK_VALUE) {
                s->resetlevel = val;
                if (val & 0x04) {
                    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
                }
            }
            break;
        case BOARD_ID_VEXPRESS:
        case BOARD_ID_EB:
        default:
            /* reserved: RAZ/WI */
            break;
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
        break;
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
        /* Undefined bits [19:18] are RAZ/WI, and writing to
         * the start bit just triggers the action; it always reads
         * as zero.
         */
        s->sys_cfgctrl = val & ~((3 << 18) | (1 << 31));
        if (extract64(val, 31, 1)) {
            /* Start bit set -- actually do something */
            unsigned int dcc = extract32(s->sys_cfgctrl, 26, 4);
            unsigned int function = extract32(s->sys_cfgctrl, 20, 6);
            unsigned int site = extract32(s->sys_cfgctrl, 16, 2);
            unsigned int position = extract32(s->sys_cfgctrl, 12, 4);
            unsigned int device = extract32(s->sys_cfgctrl, 0, 12);
            s->sys_cfgstat = 1;            /* complete */
            if (s->sys_cfgctrl & (1 << 30)) {
                if (!vexpress_cfgctrl_write(s, dcc, function, site, position,
                                            device, s->sys_cfgdata)) {
                    s->sys_cfgstat |= 2;        /* error */
                }
            } else {
                uint32_t data;
                if (!vexpress_cfgctrl_read(s, dcc, function, site, position,
                                           device, &data)) {
                    s->sys_cfgstat |= 2;        /* error */
                } else {
                    s->sys_cfgdata = data;
                }
            }
        }
        s->sys_cfgctrl &= ~(1 << 31);
        return;
    case 0xa8: /* SYS_CFGSTAT */
        if (board_id(s) != BOARD_ID_VEXPRESS) {
            goto bad_reg;
        }
        s->sys_cfgstat = val & 3;
        return;
    default:
    bad_reg:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "arm_sysctl_write: Bad register offset 0x%x\n",
                      (int)offset);
        return;
    }
}

static const MemoryRegionOps arm_sysctl_ops = {
    .read = arm_sysctl_read,
    .write = arm_sysctl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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

static void arm_sysctl_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    arm_sysctl_state *s = ARM_SYSCTL(obj);

    memory_region_init_io(&s->iomem, OBJECT(dev), &arm_sysctl_ops, s,
                          "arm-sysctl", 0x1000);
    sysbus_init_mmio(sd, &s->iomem);
    qdev_init_gpio_in(dev, arm_sysctl_gpio_set, 2);
    qdev_init_gpio_out(dev, &s->pl110_mux_ctrl, 1);
}

static void arm_sysctl_realize(DeviceState *d, Error **errp)
{
    arm_sysctl_state *s = ARM_SYSCTL(d);

    s->db_clock = g_new0(uint32_t, s->db_num_clocks);
}

static void arm_sysctl_finalize(Object *obj)
{
    arm_sysctl_state *s = ARM_SYSCTL(obj);

    g_free(s->db_voltage);
    g_free(s->db_clock);
    g_free(s->db_clock_reset);
}

static const Property arm_sysctl_properties[] = {
    DEFINE_PROP_UINT32("sys_id", arm_sysctl_state, sys_id, 0),
    DEFINE_PROP_UINT32("proc_id", arm_sysctl_state, proc_id, 0),
    /* Daughterboard power supply voltages (as reported via SYS_CFG) */
    DEFINE_PROP_ARRAY("db-voltage", arm_sysctl_state, db_num_vsensors,
                      db_voltage, qdev_prop_uint32, uint32_t),
    /* Daughterboard clock reset values (as reported via SYS_CFG) */
    DEFINE_PROP_ARRAY("db-clock", arm_sysctl_state, db_num_clocks,
                      db_clock_reset, qdev_prop_uint32, uint32_t),
};

static void arm_sysctl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = arm_sysctl_realize;
    device_class_set_legacy_reset(dc, arm_sysctl_reset);
    dc->vmsd = &vmstate_arm_sysctl;
    device_class_set_props(dc, arm_sysctl_properties);
}

static const TypeInfo arm_sysctl_info = {
    .name          = TYPE_ARM_SYSCTL,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(arm_sysctl_state),
    .instance_init = arm_sysctl_init,
    .instance_finalize = arm_sysctl_finalize,
    .class_init    = arm_sysctl_class_init,
};

static void arm_sysctl_register_types(void)
{
    type_register_static(&arm_sysctl_info);
}

type_init(arm_sysctl_register_types)
