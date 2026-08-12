/*
 * QEMU L2VIC Interrupt Controller
 *
 * Arm PrimeCell PL190 Vector Interrupt Controller was used as a reference.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/bitmap.h"
#include "qemu/bitops.h"
#include "hw/intc/hex-l2vic.h"
#include "trace.h"

#define L2VIC_VID_GRP_0 0x0 /* Read */
#define L2VIC_VID_GRP_1 0x4 /* Read */
#define L2VIC_VID_GRP_2 0x8 /* Read */
#define L2VIC_VID_GRP_3 0xC /* Read */
#define L2VIC_INT_ENABLEn 0x100 /* Read/Write */
#define L2VIC_INT_ENABLE_CLEARn 0x180 /* Write */
#define L2VIC_INT_ENABLE_SETn 0x200 /* Write */
#define L2VIC_INT_TYPEn 0x280 /* Read/Write */
#define L2VIC_INT_STATUSn 0x380 /* Read */
#define L2VIC_INT_CLEARn 0x400 /* Write */
#define L2VIC_SOFT_INTn 0x480 /* Write */
#define L2VIC_INT_PENDINGn 0x500 /* Read */
#define L2VIC_INT_GRPn_0 0x600 /* Read/Write */
#define L2VIC_INT_GRPn_1 0x680 /* Read/Write */
#define L2VIC_INT_GRPn_2 0x700 /* Read/Write */
#define L2VIC_INT_GRPn_3 0x780 /* Read/Write */

#define L2VIC_INTERRUPT_MAX 1024
/*
 * Note about l2vic groups:
 * Each interrupt to L2VIC can be configured to associate with one of
 * four groups.
 * Group 0 interrupts go to IRQ2 via VID 0 (SSR: 0xC2, the default)
 * Group 1 interrupts go to IRQ3 via VID 1 (SSR: 0xC3)
 * Group 2 interrupts go to IRQ4 via VID 2 (SSR: 0xC4)
 * Group 3 interrupts go to IRQ5 via VID 3 (SSR: 0xC5)
 */

static void bitmap32_write_word(uint32_t *bitmap, int word_offset, uint32_t val)
{
    bitmap[word_offset] = val;
}

static void bitmap32_clear_word(uint32_t *bitmap, int word_offset,
                                uint32_t mask)
{
    bitmap[word_offset] &= ~mask;
}

static void bitmap32_set_word(uint32_t *bitmap, int word_offset, uint32_t mask)
{
    bitmap[word_offset] |= mask;
}

static uint32_t bitmap32_read_word(uint32_t *bitmap, int word_offset)
{
    return bitmap[word_offset];
}

OBJECT_DECLARE_SIMPLE_TYPE(HexL2VICState, HEX_L2VIC)

#define SLICE_MAX (L2VIC_INTERRUPT_MAX / 32)
#define L2VIC_REG_RANGE_SIZE 0x80

typedef struct HexL2VICState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion fast_iomem;
    /*
     * vid_group[i] is readable at L2VIC_VID_GRP_i (offset i*4): the irq
     * last delivered through VID group i, 0-1023 so only 10 bits are used.
     */
    uint32_t vid_group[4];
    /*
     * Last irq delivered on any VID group; not specific to group 0.
     * Used by the ciad path to clear the most-recently-delivered
     * interrupt from int_status.
     */
    uint32_t vid;
    DECLARE_BITMAP32(int_enable, L2VIC_INTERRUPT_MAX);
    /* Asserted interrupts awaiting delivery once no VID is active */
    DECLARE_BITMAP32(int_pending, L2VIC_INTERRUPT_MAX);
    /* Which enabled interrupt is active */
    DECLARE_BITMAP32(int_status, L2VIC_INTERRUPT_MAX);
    /* Edge or Level interrupt */
    DECLARE_BITMAP32(int_type, L2VIC_INTERRUPT_MAX);
    DECLARE_BITMAP32(int_group_n[4], L2VIC_INTERRUPT_MAX);
    qemu_irq irq[8];
} HexL2VICState;

typedef enum {
    L2VIC_OP_WRITE,
    L2VIC_OP_CLEAR,
    L2VIC_OP_SET,
    L2VIC_OP_NONE,
} L2VicWriteOp;

typedef struct {
    hwaddr base;
    size_t state_offset;
    L2VicWriteOp write_op;
    bool write_only;
} L2VicRegRange;

static const L2VicRegRange l2vic_reg_ranges[] = {
    { L2VIC_INT_ENABLEn, offsetof(HexL2VICState, int_enable),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_ENABLE_CLEARn, offsetof(HexL2VICState, int_enable),
      L2VIC_OP_CLEAR, true },
    { L2VIC_INT_ENABLE_SETn, offsetof(HexL2VICState, int_enable),
      L2VIC_OP_SET, true },
    { L2VIC_INT_TYPEn, offsetof(HexL2VICState, int_type),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_STATUSn, offsetof(HexL2VICState, int_status),
      L2VIC_OP_NONE, false },
    { L2VIC_INT_CLEARn, offsetof(HexL2VICState, int_status),
      L2VIC_OP_CLEAR, true },
    { L2VIC_SOFT_INTn, offsetof(HexL2VICState, int_pending),
      L2VIC_OP_NONE, true },
    { L2VIC_INT_PENDINGn, offsetof(HexL2VICState, int_pending),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_GRPn_0, offsetof(HexL2VICState, int_group_n[0]),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_GRPn_1, offsetof(HexL2VICState, int_group_n[1]),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_GRPn_2, offsetof(HexL2VICState, int_group_n[2]),
      L2VIC_OP_WRITE, false },
    { L2VIC_INT_GRPn_3, offsetof(HexL2VICState, int_group_n[3]),
      L2VIC_OP_WRITE, false },
};

static uint32_t *l2vic_state_bitmap(HexL2VICState *s, size_t state_offset)
{
    return (uint32_t *)((char *)s + state_offset);
}

static bool l2vic_reg_read_range(HexL2VICState *s, hwaddr offset,
                                 uint64_t *value)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(l2vic_reg_ranges); i++) {
        const L2VicRegRange *r = &l2vic_reg_ranges[i];

        if (offset >= r->base &&
            offset < r->base + L2VIC_REG_RANGE_SIZE) {
            if (r->write_only) {
                *value = 0;
            } else {
                uint32_t *bitmap = l2vic_state_bitmap(s, r->state_offset);
                *value = bitmap32_read_word(bitmap,
                                            (offset - r->base) >> 2);
            }
            return true;
        }
    }
    return false;
}

static bool l2vic_reg_write_range(HexL2VICState *s, hwaddr offset,
                                  uint32_t val)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(l2vic_reg_ranges); i++) {
        const L2VicRegRange *r = &l2vic_reg_ranges[i];

        if (offset >= r->base &&
            offset < r->base + L2VIC_REG_RANGE_SIZE) {
            uint32_t *bitmap = l2vic_state_bitmap(s, r->state_offset);
            int word = (offset - r->base) >> 2;

            switch (r->write_op) {
            case L2VIC_OP_WRITE:
                bitmap32_write_word(bitmap, word, val);
                break;
            case L2VIC_OP_CLEAR:
                bitmap32_clear_word(bitmap, word, val);
                break;
            case L2VIC_OP_SET:
                bitmap32_set_word(bitmap, word, val);
                break;
            case L2VIC_OP_NONE:
                /* Read-only or handled elsewhere; ignore the write. */
                break;
            default:
                g_assert_not_reached();
            }
            return true;
        }
    }
    return false;
}

/*
 * The four INT_GRPn_* register arrays are interleaved across irqs in
 * blocks of 8: irq 0-7 live in group_n[0], irq 8-15 in group_n[1], irq
 * 16-23 in group_n[2], irq 24-31 in group_n[3], irq 32-39 back in
 * group_n[0], and so on.
 */
static uint32_t *get_int_group(HexL2VICState *s, int irq)
{
    return s->int_group_n[extract32(irq, 3, 2)];
}

static int find_slice(int irq)
{
    return irq / 32;
}

static int get_vid(HexL2VICState *s, int irq)
{
    uint32_t *group = get_int_group(s, irq);
    uint32_t slice = group[find_slice(irq)];
    uint32_t vid;
    /*
     * Each irq occupies a 4-bit field: bit 3 is the group-enable bit,
     * bits 0-2 select the VID group.  Shift down to this irq's field.
     */
    uint32_t val = slice >> ((irq & 0x7) * 4);

    if (!(val & 0x8)) {
        return 0;
    }
    vid = val & 0x7;
    if (vid >= ARRAY_SIZE(s->vid_group)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "L2VIC: irq %d requests invalid vid group %u\n",
                      irq, vid);
        return 0;
    }
    return vid;
}

static inline bool vid_active(HexL2VICState *s)
{
    const uint32_t size = L2VIC_INTERRUPT_MAX;
    const uint32_t active_irq = find_first_bit32(s->int_status, size);
    return active_irq != size;
}

static bool l2vic_update(HexL2VICState *s, int irq)
{
    bool pending;
    bool enable;

    if (vid_active(s)) {
        return true;
    }

    pending = test_bit32(irq, s->int_pending);
    enable = test_bit32(irq, s->int_enable);
    if (pending && enable) {
        int vid = get_vid(s, irq);
        set_bit32(irq, s->int_status);
        clear_bit32(irq, s->int_pending);
        /*
         * Only auto-disable for edge-triggered interrupts (type=1).
         * Level-triggered interrupts (type=0, the default) keep their
         * enable bit set across deliveries -- the firmware enables once
         * and expects the interrupt to remain enabled.
         */
        if (test_bit32(irq, s->int_type)) {
            clear_bit32(irq, s->int_enable);
        }
        s->vid = irq;
        s->vid_group[vid] = irq;

        qemu_irq_pulse(s->irq[vid + 2]);
        trace_hex_l2vic_delivered(irq, vid);
        return true;
    }
    return false;
}

static void l2vic_update_all(HexL2VICState *s)
{
    for (int i = 0; i < L2VIC_INTERRUPT_MAX; i++) {
        if (l2vic_update(s, i)) {
            /* once vid is active, no-one else can set it until ciad */
            return;
        }
    }
}

static void l2vic_set_irq(void *opaque, int irq, int level)
{
    HexL2VICState *s = (HexL2VICState *)opaque;

    if (level) {
        set_bit32(irq, s->int_pending);
    }
    l2vic_update(s, irq);
}

static void l2vic_write(void *opaque, hwaddr offset, uint64_t val,
                        unsigned size)
{
    HexL2VICState *s = (HexL2VICState *)opaque;

    trace_hex_l2vic_reg_write((unsigned)offset, (uint32_t)val);

    if (!l2vic_reg_write_range(s, offset, val)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: offset 0x%" HWADDR_PRIx " unimplemented\n",
                      __func__, offset);
    }

    /* SOFT_INT also sets pending for edge-triggered interrupts */
    if (offset >= L2VIC_SOFT_INTn &&
        offset < L2VIC_SOFT_INTn + L2VIC_REG_RANGE_SIZE && val) {
        int base_irq = ((offset - L2VIC_SOFT_INTn) >> 2) * 32;
        uint32_t bits = val;
        int bit;

        while ((bit = ctz32(bits)) < 32) {
            int irq = base_irq + bit;

            if (test_bit32(irq, s->int_type)) {
                set_bit32(irq, s->int_pending);
            }
            bits &= ~(1u << bit);
        }
    }

    l2vic_update_all(s);
}

static uint64_t l2vic_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value;
    HexL2VICState *s = (HexL2VICState *)opaque;

    if (offset <= L2VIC_VID_GRP_3) {
        value = s->vid_group[offset >> 2];
    } else if (!l2vic_reg_read_range(s, offset, &value)) {
        value = 0;
        qemu_log_mask(LOG_GUEST_ERROR,
                      "L2VIC: %s: offset 0x%" HWADDR_PRIx "\n", __func__,
                      offset);
    }

    trace_hex_l2vic_reg_read((unsigned)offset, (uint32_t)value);
    return value;
}

static const MemoryRegionOps l2vic_ops = {
    .read = l2vic_read,
    .write = l2vic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

#define FASTL2VIC_ENABLE 0x0
#define FASTL2VIC_DISABLE 0x1
#define FASTL2VIC_INT 0x2

static void fastl2vic_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    if (offset == 0) {
        uint32_t cmd = (val >> 16) & 0x3;
        uint32_t irq = val & 0x3ff;
        uint32_t slice = (irq / 32) * 4;
        val = 1 << (irq % 32);

        if (cmd == FASTL2VIC_ENABLE) {
            l2vic_write(opaque, L2VIC_INT_ENABLE_SETn + slice, val, size);
        } else if (cmd == FASTL2VIC_DISABLE) {
            l2vic_write(opaque, L2VIC_INT_ENABLE_CLEARn + slice, val, size);
        } else if (cmd == FASTL2VIC_INT) {
            l2vic_write(opaque, L2VIC_SOFT_INTn + slice, val, size);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: invalid write cmd %" PRId32 "\n",
                          __func__, cmd);
        }
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write offset 0x%08" HWADDR_PRIx
            "\n", __func__, offset);
}

static uint64_t fastl2vic_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static const MemoryRegionOps fastl2vic_ops = {
    .read = fastl2vic_read,
    .write = fastl2vic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static uint32_t l2vic_interface_read_vid_impl(HexL2VicInterface *iface,
                                               uint32_t group)
{
    HexL2VICState *s = HEX_L2VIC(iface);
    uint32_t result = 0;

    if (group == 0) {
        /* VID register combines vid_group[0] (VID0) and vid_group[1] (VID1) */
        result = deposit32(result, 0, 16, s->vid_group[0]);
        result = deposit32(result, 16, 16, s->vid_group[1]);
    } else if (group == 1) {
        /* VID1 register combines vid_group[2] (VID2) and vid_group[3] (VID3) */
        result = deposit32(result, 0, 16, s->vid_group[2]);
        result = deposit32(result, 16, 16, s->vid_group[3]);
    }
    return result;
}

static void l2vic_interface_update_vid_impl(HexL2VicInterface *iface,
                                            uint32_t group, uint32_t value)
{
    HexL2VICState *s = HEX_L2VIC(iface);

    if (group == 0) {
        s->vid_group[0] = extract32(value, 0, 16);
        s->vid_group[1] = extract32(value, 16, 16);
    } else if (group == 1) {
        s->vid_group[2] = extract32(value, 0, 16);
        s->vid_group[3] = extract32(value, 16, 16);
    }

    l2vic_update_all(s);
}

static void l2vic_interface_clear_interrupt_impl(HexL2VicInterface *iface)
{
    HexL2VICState *s = HEX_L2VIC(iface);

    if (s->vid < L2VIC_INTERRUPT_MAX) {
        clear_bit32(s->vid, s->int_status);
    }
    l2vic_update_all(s);
}

static void l2vic_reset_hold(Object *obj, ResetType type G_GNUC_UNUSED)
{
    HexL2VICState *s = HEX_L2VIC(obj);

    memset(s->int_enable, 0, sizeof(s->int_enable));
    memset(s->int_pending, 0, sizeof(s->int_pending));
    memset(s->int_status, 0, sizeof(s->int_status));
    memset(s->int_type, 0, sizeof(s->int_type));
    memset(s->int_group_n, 0, sizeof(s->int_group_n));
    memset(s->vid_group, 0, sizeof(s->vid_group));
    s->vid = 0;

    l2vic_update_all(s);
}

static void reset_irq_handler(void *opaque, int irq, int level)
{
    Object *obj = OBJECT(opaque);

    if (level) {
        l2vic_reset_hold(obj, RESET_TYPE_COLD);
    }
}

static void l2vic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    HexL2VICState *s = HEX_L2VIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    int i;

    memory_region_init_io(&s->iomem, obj, &l2vic_ops, s, "l2vic", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    memory_region_init_io(&s->fast_iomem, obj, &fastl2vic_ops, s, "fast",
                          0x10000);
    sysbus_init_mmio(sbd, &s->fast_iomem);

    qdev_init_gpio_in(dev, l2vic_set_irq, L2VIC_INTERRUPT_MAX);
    qdev_init_gpio_in_named(dev, reset_irq_handler, "reset", 1);
    for (i = 0; i < 8; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
}

static const VMStateDescription vmstate_l2vic = {
    .name = "l2vic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (VMStateField[]){
            VMSTATE_UINT32_ARRAY(vid_group, HexL2VICState, 4),
            VMSTATE_UINT32(vid, HexL2VICState),
            VMSTATE_UINT32_ARRAY(int_enable, HexL2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_type, HexL2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_status, HexL2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_pending, HexL2VICState, SLICE_MAX),
            VMSTATE_UINT32_2DARRAY(int_group_n, HexL2VICState, 4, SLICE_MAX),
            VMSTATE_END_OF_LIST() }
};

static void l2vic_interface_class_init(ObjectClass *klass, const void *data)
{
    HexL2VicInterfaceClass *k = HEX_L2VIC_INTERFACE_CLASS(klass);

    k->read_vid = l2vic_interface_read_vid_impl;
    k->update_vid = l2vic_interface_update_vid_impl;
    k->clear_interrupt = l2vic_interface_clear_interrupt_impl;
}

static void l2vic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->vmsd = &vmstate_l2vic;
    rc->phases.hold = l2vic_reset_hold;
}

static const TypeInfo l2vic_interface_info = {
    .name = TYPE_HEX_L2VIC_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(HexL2VicInterfaceClass),
    .class_init = l2vic_interface_class_init,
};

static const TypeInfo l2vic_info = {
    .name = TYPE_HEX_L2VIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(HexL2VICState),
    .instance_init = l2vic_init,
    .class_init = l2vic_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_HEX_L2VIC_INTERFACE },
        { }
    },
};

static const TypeInfo l2vic_types[] = {
    l2vic_interface_info,
    l2vic_info,
};

DEFINE_TYPES(l2vic_types)
