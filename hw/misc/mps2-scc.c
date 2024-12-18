/*
 * ARM MPS2 SCC emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the SCC (Serial Communication Controller)
 * found in the FPGA images of MPS2 development boards.
 *
 * Documentation of it can be found in the MPS2 TRM:
 * https://developer.arm.com/documentation/100112/latest/
 * and also in the Application Notes documenting individual FPGA images.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitops.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/misc/mps2-scc.h"
#include "hw/misc/led.h"
#include "hw/qdev-properties.h"

REG32(CFG0, 0)
REG32(CFG1, 4)
REG32(CFG2, 8)
REG32(CFG3, 0xc)
REG32(CFG4, 0x10)
REG32(CFG5, 0x14)
REG32(CFG6, 0x18)
REG32(CFG7, 0x1c)
REG32(CFGDATA_RTN, 0xa0)
REG32(CFGDATA_OUT, 0xa4)
REG32(CFGCTRL, 0xa8)
    FIELD(CFGCTRL, DEVICE, 0, 12)
    FIELD(CFGCTRL, RES1, 12, 8)
    FIELD(CFGCTRL, FUNCTION, 20, 6)
    FIELD(CFGCTRL, RES2, 26, 4)
    FIELD(CFGCTRL, WRITE, 30, 1)
    FIELD(CFGCTRL, START, 31, 1)
REG32(CFGSTAT, 0xac)
    FIELD(CFGSTAT, DONE, 0, 1)
    FIELD(CFGSTAT, ERROR, 1, 1)
REG32(DLL, 0x100)
REG32(AID, 0xFF8)
REG32(ID, 0xFFC)

static int scc_partno(MPS2SCC *s)
{
    /* Return the partno field of the SCC_ID (0x524, 0x511, etc) */
    return extract32(s->id, 4, 8);
}

/* Is CFG_REG2 present? */
static bool have_cfg2(MPS2SCC *s)
{
    return scc_partno(s) == 0x524 || scc_partno(s) == 0x547 ||
        scc_partno(s) == 0x536;
}

/* Is CFG_REG3 present? */
static bool have_cfg3(MPS2SCC *s)
{
    return scc_partno(s) != 0x524 && scc_partno(s) != 0x547 &&
        scc_partno(s) != 0x536;
}

/* Is CFG_REG5 present? */
static bool have_cfg5(MPS2SCC *s)
{
    return scc_partno(s) == 0x524 || scc_partno(s) == 0x547 ||
        scc_partno(s) == 0x536;
}

/* Is CFG_REG6 present? */
static bool have_cfg6(MPS2SCC *s)
{
    return scc_partno(s) == 0x524 || scc_partno(s) == 0x536;
}

/* Is CFG_REG7 present? */
static bool have_cfg7(MPS2SCC *s)
{
    return scc_partno(s) == 0x536;
}

/* Does CFG_REG0 drive the 'remap' GPIO output? */
static bool cfg0_is_remap(MPS2SCC *s)
{
    return scc_partno(s) != 0x536;
}

/* Is CFG_REG1 driving a set of LEDs? */
static bool cfg1_is_leds(MPS2SCC *s)
{
    return scc_partno(s) != 0x536;
}

/* Handle a write via the SYS_CFG channel to the specified function/device.
 * Return false on error (reported to guest via SYS_CFGCTRL ERROR bit).
 */
static bool scc_cfg_write(MPS2SCC *s, unsigned function,
                          unsigned device, uint32_t value)
{
    trace_mps2_scc_cfg_write(function, device, value);

    if (function != 1 || device >= s->num_oscclk) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC config write: bad function %d device %d\n",
                      function, device);
        return false;
    }

    s->oscclk[device] = value;
    return true;
}

/* Handle a read via the SYS_CFG channel to the specified function/device.
 * Return false on error (reported to guest via SYS_CFGCTRL ERROR bit),
 * or set *value on success.
 */
static bool scc_cfg_read(MPS2SCC *s, unsigned function,
                         unsigned device, uint32_t *value)
{
    if (function != 1 || device >= s->num_oscclk) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC config read: bad function %d device %d\n",
                      function, device);
        return false;
    }

    *value = s->oscclk[device];

    trace_mps2_scc_cfg_read(function, device, *value);
    return true;
}

static uint64_t mps2_scc_read(void *opaque, hwaddr offset, unsigned size)
{
    MPS2SCC *s = MPS2_SCC(opaque);
    uint64_t r;

    switch (offset) {
    case A_CFG0:
        r = s->cfg0;
        break;
    case A_CFG1:
        r = s->cfg1;
        break;
    case A_CFG2:
        if (!have_cfg2(s)) {
            goto bad_offset;
        }
        r = s->cfg2;
        break;
    case A_CFG3:
        if (!have_cfg3(s)) {
            goto bad_offset;
        }
        /*
         * These are user-settable DIP switches on the board. We don't
         * model that, so just return zeroes.
         *
         * TODO: for AN536 this is MCC_MSB_ADDR "additional MCC addressing
         * bits". These change which part of the DDR4 the motherboard
         * configuration controller can see in its memory map (see the
         * appnote section 2.4). QEMU doesn't model the MCC at all, so these
         * bits are not interesting to us; read-as-zero is as good as anything
         * else.
         */
        r = 0;
        break;
    case A_CFG4:
        r = s->cfg4;
        break;
    case A_CFG5:
        if (!have_cfg5(s)) {
            goto bad_offset;
        }
        r = s->cfg5;
        break;
    case A_CFG6:
        if (!have_cfg6(s)) {
            goto bad_offset;
        }
        r = s->cfg6;
        break;
    case A_CFG7:
        if (!have_cfg7(s)) {
            goto bad_offset;
        }
        r = s->cfg7;
        break;
    case A_CFGDATA_RTN:
        r = s->cfgdata_rtn;
        break;
    case A_CFGDATA_OUT:
        r = s->cfgdata_out;
        break;
    case A_CFGCTRL:
        r = s->cfgctrl;
        break;
    case A_CFGSTAT:
        r = s->cfgstat;
        break;
    case A_DLL:
        r = s->dll;
        break;
    case A_AID:
        r = s->aid;
        break;
    case A_ID:
        r = s->id;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }

    trace_mps2_scc_read(offset, r, size);
    return r;
}

static void mps2_scc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    MPS2SCC *s = MPS2_SCC(opaque);

    trace_mps2_scc_write(offset, value, size);

    switch (offset) {
    case A_CFG0:
        /*
         * On some boards bit 0 controls board-specific remapping;
         * we always reflect bit 0 in the 'remap' GPIO output line,
         * and let the board wire it up or not as it chooses.
         * TODO on some boards bit 1 is CPU_WAIT.
         *
         * TODO: on the AN536 this register controls reset and halt
         * for both CPUs. For the moment we don't implement this, so the
         * register just reads as written.
         */
        s->cfg0 = value;
        if (cfg0_is_remap(s)) {
            qemu_set_irq(s->remap, s->cfg0 & 1);
        }
        break;
    case A_CFG1:
        s->cfg1 = value;
        /*
         * On most boards this register drives LEDs.
         *
         * TODO: for AN536 this controls whether flash and ATCM are
         * enabled or disabled on reset. QEMU doesn't model this, and
         * always wires up RAM in the ATCM area and ROM in the flash area.
         */
        if (cfg1_is_leds(s)) {
            for (size_t i = 0; i < ARRAY_SIZE(s->led); i++) {
                led_set_state(s->led[i], extract32(value, i, 1));
            }
        }
        break;
    case A_CFG2:
        if (!have_cfg2(s)) {
            goto bad_offset;
        }
        /* AN524, AN536: QSPI Select signal */
        s->cfg2 = value;
        break;
    case A_CFG5:
        if (!have_cfg5(s)) {
            goto bad_offset;
        }
        /* AN524, AN536: ACLK frequency in Hz */
        s->cfg5 = value;
        break;
    case A_CFG6:
        if (!have_cfg6(s)) {
            goto bad_offset;
        }
        /* AN524: Clock divider for BRAM */
        /* AN536: Core 0 vector table base address */
        s->cfg6 = value;
        break;
    case A_CFG7:
        if (!have_cfg7(s)) {
            goto bad_offset;
        }
        /* AN536: Core 1 vector table base address */
        s->cfg6 = value;
        break;
    case A_CFGDATA_OUT:
        s->cfgdata_out = value;
        break;
    case A_CFGCTRL:
        /* Writing to CFGCTRL clears SYS_CFGSTAT */
        s->cfgstat = 0;
        s->cfgctrl = value & ~(R_CFGCTRL_RES1_MASK |
                               R_CFGCTRL_RES2_MASK |
                               R_CFGCTRL_START_MASK);

        if (value & R_CFGCTRL_START_MASK) {
            /* Start bit set -- do a read or write (instantaneously) */
            int device = extract32(s->cfgctrl, R_CFGCTRL_DEVICE_SHIFT,
                                   R_CFGCTRL_DEVICE_LENGTH);
            int function = extract32(s->cfgctrl, R_CFGCTRL_FUNCTION_SHIFT,
                                     R_CFGCTRL_FUNCTION_LENGTH);

            s->cfgstat = R_CFGSTAT_DONE_MASK;
            if (s->cfgctrl & R_CFGCTRL_WRITE_MASK) {
                if (!scc_cfg_write(s, function, device, s->cfgdata_out)) {
                    s->cfgstat |= R_CFGSTAT_ERROR_MASK;
                }
            } else {
                uint32_t result;
                if (!scc_cfg_read(s, function, device, &result)) {
                    s->cfgstat |= R_CFGSTAT_ERROR_MASK;
                } else {
                    s->cfgdata_rtn = result;
                }
            }
        }
        break;
    case A_DLL:
        /* DLL stands for Digital Locked Loop.
         * Bits [31:24] (DLL_LOCK_MASK) are writable, and indicate a
         * mask of which of the DLL_LOCKED bits [16:23] should be ORed
         * together to determine the ALL_UNMASKED_DLLS_LOCKED bit [0].
         * For QEMU, our DLLs are always locked, so we can leave bit 0
         * as 1 always and don't need to recalculate it.
         */
        s->dll = deposit32(s->dll, 24, 8, extract32(value, 24, 8));
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps mps2_scc_ops = {
    .read = mps2_scc_read,
    .write = mps2_scc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mps2_scc_reset(DeviceState *dev)
{
    MPS2SCC *s = MPS2_SCC(dev);
    int i;

    trace_mps2_scc_reset();
    s->cfg0 = s->cfg0_reset;
    s->cfg1 = 0;
    s->cfg2 = 0;
    s->cfg5 = 0;
    s->cfg6 = 0;
    s->cfgdata_rtn = 0;
    s->cfgdata_out = 0;
    s->cfgctrl = 0x100000;
    s->cfgstat = 0;
    s->dll = 0xffff0001;
    for (i = 0; i < s->num_oscclk; i++) {
        s->oscclk[i] = s->oscclk_reset[i];
    }
    for (i = 0; i < ARRAY_SIZE(s->led); i++) {
        device_cold_reset(DEVICE(s->led[i]));
    }
}

static void mps2_scc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MPS2SCC *s = MPS2_SCC(obj);

    memory_region_init_io(&s->iomem, obj, &mps2_scc_ops, s, "mps2-scc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    qdev_init_gpio_out_named(DEVICE(obj), &s->remap, "remap", 1);
}

static void mps2_scc_realize(DeviceState *dev, Error **errp)
{
    MPS2SCC *s = MPS2_SCC(dev);

    for (size_t i = 0; i < ARRAY_SIZE(s->led); i++) {
        char *name = g_strdup_printf("SCC LED%zu", i);
        s->led[i] = led_create_simple(OBJECT(dev), GPIO_POLARITY_ACTIVE_HIGH,
                                      LED_COLOR_GREEN, name);
        g_free(name);
    }

    s->oscclk = g_new0(uint32_t, s->num_oscclk);
}

static void mps2_scc_finalize(Object *obj)
{
    MPS2SCC *s = MPS2_SCC(obj);

    g_free(s->oscclk_reset);
}

static bool cfg7_needed(void *opaque)
{
    MPS2SCC *s = opaque;

    return have_cfg7(s);
}

static const VMStateDescription vmstate_cfg7 = {
    .name = "mps2-scc/cfg7",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cfg7_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg7, MPS2SCC),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription mps2_scc_vmstate = {
    .name = "mps2-scc",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg0, MPS2SCC),
        VMSTATE_UINT32(cfg1, MPS2SCC),
        VMSTATE_UINT32(cfg2, MPS2SCC),
        /* cfg3, cfg4 are read-only so need not be migrated */
        VMSTATE_UINT32(cfg5, MPS2SCC),
        VMSTATE_UINT32(cfg6, MPS2SCC),
        VMSTATE_UINT32(cfgdata_rtn, MPS2SCC),
        VMSTATE_UINT32(cfgdata_out, MPS2SCC),
        VMSTATE_UINT32(cfgctrl, MPS2SCC),
        VMSTATE_UINT32(cfgstat, MPS2SCC),
        VMSTATE_UINT32(dll, MPS2SCC),
        VMSTATE_VARRAY_UINT32(oscclk, MPS2SCC, num_oscclk,
                              0, vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_cfg7,
        NULL
    }
};

static const Property mps2_scc_properties[] = {
    /* Values for various read-only ID registers (which are specific
     * to the board model or FPGA image)
     */
    DEFINE_PROP_UINT32("scc-cfg4", MPS2SCC, cfg4, 0),
    DEFINE_PROP_UINT32("scc-aid", MPS2SCC, aid, 0),
    DEFINE_PROP_UINT32("scc-id", MPS2SCC, id, 0),
    /* Reset value for CFG0 register */
    DEFINE_PROP_UINT32("scc-cfg0", MPS2SCC, cfg0_reset, 0),
    /*
     * These are the initial settings for the source clocks on the board.
     * In hardware they can be configured via a config file read by the
     * motherboard configuration controller to suit the FPGA image.
     */
    DEFINE_PROP_ARRAY("oscclk", MPS2SCC, num_oscclk, oscclk_reset,
                      qdev_prop_uint32, uint32_t),
};

static void mps2_scc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mps2_scc_realize;
    dc->vmsd = &mps2_scc_vmstate;
    device_class_set_legacy_reset(dc, mps2_scc_reset);
    device_class_set_props(dc, mps2_scc_properties);
}

static const TypeInfo mps2_scc_info = {
    .name = TYPE_MPS2_SCC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPS2SCC),
    .instance_init = mps2_scc_init,
    .instance_finalize = mps2_scc_finalize,
    .class_init = mps2_scc_class_init,
};

static void mps2_scc_register_types(void)
{
    type_register_static(&mps2_scc_info);
}

type_init(mps2_scc_register_types);
