/*
 * QEMU L2VIC Interrupt Controller
 *
 * Arm PrimeCell PL190 Vector Interrupt Controller was used as a reference.
 * Copyright(c) 2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "l2vic.h"
#include "trace.h"

#define L2VICA(s, n) (s[(n) >> 2])

#define TYPE_L2VIC "l2vic"
#define L2VIC(obj) OBJECT_CHECK(L2VICState, (obj), TYPE_L2VIC)

#define SLICE_MAX (L2VIC_INTERRUPT_MAX / 32)

typedef struct L2VICState {
    SysBusDevice parent_obj;

    QemuMutex active;
    MemoryRegion iomem;
    MemoryRegion fast_iomem;
    uint32_t level;
    /*
     * offset 0:vid group 0 etc, 10 bits in each group
     * are used:
     */
    uint32_t vid_group[4];
    uint32_t vid0;
    /* Clear Status of Active Edge interrupt, not used: */
    uint32_t int_clear[SLICE_MAX] QEMU_ALIGNED(16);
    /* Enable interrupt source */
    uint32_t int_enable[SLICE_MAX] QEMU_ALIGNED(16);
    /* Clear (set to 0) corresponding bit in int_enable */
    uint32_t int_enable_clear;
    /* Set (to 1) corresponding bit in int_enable */
    uint32_t int_enable_set;
    /* Present for debugging, not used */
    uint32_t int_pending[SLICE_MAX] QEMU_ALIGNED(16);
    /* Generate an interrupt */
    uint32_t int_soft;
    /* Which enabled interrupt is active */
    uint32_t int_status[SLICE_MAX] QEMU_ALIGNED(16);
    /* Edge or Level interrupt */
    uint32_t int_type[SLICE_MAX] QEMU_ALIGNED(16);
    /* L2 interrupt group 0-3 0x600-0x7FF */
    uint32_t int_group_n0[SLICE_MAX] QEMU_ALIGNED(16);
    uint32_t int_group_n1[SLICE_MAX] QEMU_ALIGNED(16);
    uint32_t int_group_n2[SLICE_MAX] QEMU_ALIGNED(16);
    uint32_t int_group_n3[SLICE_MAX] QEMU_ALIGNED(16);
    qemu_irq irq[8];
} L2VICState;


/*
 * Find out if this irq is associated with a group other than
 * the default group
 */
static uint32_t *get_int_group(L2VICState *s, int irq)
{
    int n = irq & 0x1f;
    if (n < 8) {
        return s->int_group_n0;
    }
    if (n < 16) {
        return s->int_group_n1;
    }
    if (n < 24) {
        return s->int_group_n2;
    }
    return s->int_group_n3;
}

static int find_slice(int irq)
{
    return irq / 32;
}

static int get_vid(L2VICState *s, int irq)
{
    uint32_t *group = get_int_group(s, irq);
    uint32_t slice = group[find_slice(irq)];
    /* Mask with 0x7 to remove the GRP:EN bit */
    uint32_t val = slice >> ((irq & 0x7) * 4);
    if (val & 0x8) {
        return val & 0x7;
    } else {
        return 0;
    }
}

static inline bool vid_active(L2VICState *s)

{
    /* scan all 1024 bits in int_status arrary */
    const int size = sizeof(s->int_status) * CHAR_BIT;
    const int active_irq = find_first_bit((unsigned long *)s->int_status, size);
    return ((active_irq != size)) ? true : false;
}

static bool l2vic_update(L2VICState *s, int irq)
{
    if (vid_active(s)) {
        return true;
    }

    bool pending = test_bit(irq, (unsigned long *)s->int_pending);
    bool enable = test_bit(irq, (unsigned long *)s->int_enable);
    if (pending && enable) {
        int vid = get_vid(s, irq);
        set_bit(irq, (unsigned long *)s->int_status);
        clear_bit(irq, (unsigned long *)s->int_pending);
        clear_bit(irq, (unsigned long *)s->int_enable);
        /* ensure the irq line goes low after going high */
        s->vid0 = irq;
        s->vid_group[get_vid(s, irq)] = irq;

        /* already low: now call pulse */
        /*     pulse: calls qemu_upper() and then qemu_lower()) */
        qemu_irq_pulse(s->irq[vid + 2]);
        trace_l2vic_delivered(irq, vid);
        return true;
    }
    return false;
}

static void l2vic_update_all(L2VICState *s)
{
    for (int i = 0; i < L2VIC_INTERRUPT_MAX; i++) {
        if (l2vic_update(s, i) == true) {
            /* once vid is active, no-one else can set it until ciad */
            return;
        }
    }
}

static void l2vic_set_irq(void *opaque, int irq, int level)
{
    L2VICState *s = (L2VICState *)opaque;
    if (level) {
        qemu_mutex_lock(&s->active);
        set_bit(irq, (unsigned long *)s->int_pending);
        qemu_mutex_unlock(&s->active);
    }
    l2vic_update(s, irq);
}

static void l2vic_write(void *opaque, hwaddr offset, uint64_t val,
                        unsigned size)
{
    L2VICState *s = (L2VICState *)opaque;
    qemu_mutex_lock(&s->active);
    trace_l2vic_reg_write((unsigned)offset, (uint32_t)val);

    if (offset == L2VIC_VID_0) {
        if ((int)val != L2VIC_CIAD_INSTRUCTION) {
            s->vid0 = val;
        } else {
            /* ciad issued: clear int_status */
            clear_bit(s->vid0, (unsigned long *)s->int_status);
        }
    } else if (offset >= L2VIC_INT_ENABLEn &&
               offset < (L2VIC_INT_ENABLE_CLEARn)) {
        L2VICA(s->int_enable, offset - L2VIC_INT_ENABLEn) = val;
    } else if (offset >= L2VIC_INT_ENABLE_CLEARn &&
               offset < L2VIC_INT_ENABLE_SETn) {
        L2VICA(s->int_enable, offset - L2VIC_INT_ENABLE_CLEARn) &= ~val;
    } else if (offset >= L2VIC_INT_ENABLE_SETn && offset < L2VIC_INT_TYPEn) {
        L2VICA(s->int_enable, offset - L2VIC_INT_ENABLE_SETn) |= val;
    } else if (offset >= L2VIC_INT_TYPEn && offset < L2VIC_INT_TYPEn + 0x80) {
        L2VICA(s->int_type, offset - L2VIC_INT_TYPEn) = val;
    } else if (offset >= L2VIC_INT_STATUSn && offset < L2VIC_INT_CLEARn) {
        L2VICA(s->int_status, offset - L2VIC_INT_STATUSn) = val;
    } else if (offset >= L2VIC_INT_CLEARn && offset < L2VIC_SOFT_INTn) {
        L2VICA(s->int_clear, offset - L2VIC_INT_CLEARn) = val;
    } else if (offset >= L2VIC_INT_PENDINGn &&
               offset < L2VIC_INT_PENDINGn + 0x80) {
        L2VICA(s->int_pending, offset - L2VIC_INT_PENDINGn) = val;
    } else if (offset >= L2VIC_SOFT_INTn && offset < L2VIC_INT_PENDINGn) {
        L2VICA(s->int_enable, offset - L2VIC_SOFT_INTn) |= val;
        /*
         *  Need to reverse engineer the actual irq number.
         */
        int irq = find_first_bit((unsigned long *)&val,
                                 sizeof(s->int_enable[0]) * CHAR_BIT);
        hwaddr byteoffset = offset - L2VIC_SOFT_INTn;
        g_assert(irq != sizeof(s->int_enable[0]) * CHAR_BIT);
        irq += byteoffset * 8;

        /* The soft-int interface only works with edge-triggered interrupts */
        if (test_bit(irq, (unsigned long *)s->int_type)) {
            qemu_mutex_unlock(&s->active);
            l2vic_set_irq(opaque, irq, 1);
            qemu_mutex_lock(&s->active);
        }
    } else if (offset >= L2VIC_INT_GRPn_0 && offset < L2VIC_INT_GRPn_1) {
        L2VICA(s->int_group_n0, offset - L2VIC_INT_GRPn_0) = val;
    } else if (offset >= L2VIC_INT_GRPn_1 && offset < L2VIC_INT_GRPn_2) {
        L2VICA(s->int_group_n1, offset - L2VIC_INT_GRPn_1) = val;
    } else if (offset >= L2VIC_INT_GRPn_2 && offset < L2VIC_INT_GRPn_3) {
        L2VICA(s->int_group_n2, offset - L2VIC_INT_GRPn_2) = val;
    } else if (offset >= L2VIC_INT_GRPn_3 && offset < L2VIC_INT_GRPn_3 + 0x80) {
        L2VICA(s->int_group_n3, offset - L2VIC_INT_GRPn_3) = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: offset %x unimplemented\n", __func__,
                      (int)offset);
    }
    l2vic_update_all(s);
    qemu_mutex_unlock(&s->active);
    return;
}

static uint64_t l2vic_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t value;
    L2VICState *s = (L2VICState *)opaque;
    qemu_mutex_lock(&s->active);

    if (offset == L2VIC_VID_GRP_0) {
        value = s->vid_group[0];
    } else if (offset == L2VIC_VID_GRP_1) {
        value = s->vid_group[1];
    } else if (offset == L2VIC_VID_GRP_2) {
        value = s->vid_group[2];
    } else if (offset == L2VIC_VID_GRP_3) {
        value = s->vid_group[3];
    } else if (offset == L2VIC_VID_0) {
        value = s->vid0;
    } else if (offset >= L2VIC_INT_ENABLEn &&
               offset < L2VIC_INT_ENABLE_CLEARn) {
        value = L2VICA(s->int_enable, offset - L2VIC_INT_ENABLEn);
    } else if (offset >= L2VIC_INT_ENABLE_CLEARn &&
               offset < L2VIC_INT_ENABLE_SETn) {
        value = 0;
    } else if (offset >= L2VIC_INT_ENABLE_SETn && offset < L2VIC_INT_TYPEn) {
        value = 0;
    } else if (offset >= L2VIC_INT_TYPEn && offset < L2VIC_INT_TYPEn + 0x80) {
        value = L2VICA(s->int_type, offset - L2VIC_INT_TYPEn);
    } else if (offset >= L2VIC_INT_STATUSn && offset < L2VIC_INT_CLEARn) {
        value = L2VICA(s->int_status, offset - L2VIC_INT_STATUSn);
    } else if (offset >= L2VIC_INT_CLEARn && offset < L2VIC_SOFT_INTn) {
        value = L2VICA(s->int_clear, offset - L2VIC_INT_CLEARn);
    } else if (offset >= L2VIC_SOFT_INTn && offset < L2VIC_INT_PENDINGn) {
        value = 0;
    } else if (offset >= L2VIC_INT_PENDINGn &&
               offset < L2VIC_INT_PENDINGn + 0x80) {
        value = L2VICA(s->int_pending, offset - L2VIC_INT_PENDINGn);
    } else if (offset >= L2VIC_INT_GRPn_0 && offset < L2VIC_INT_GRPn_1) {
        value = L2VICA(s->int_group_n0, offset - L2VIC_INT_GRPn_0);
    } else if (offset >= L2VIC_INT_GRPn_1 && offset < L2VIC_INT_GRPn_2) {
        value = L2VICA(s->int_group_n1, offset - L2VIC_INT_GRPn_1);
    } else if (offset >= L2VIC_INT_GRPn_2 && offset < L2VIC_INT_GRPn_3) {
        value = L2VICA(s->int_group_n2, offset - L2VIC_INT_GRPn_2);
    } else if (offset >= L2VIC_INT_GRPn_3 && offset < L2VIC_INT_GRPn_3 + 0x80) {
        value = L2VICA(s->int_group_n3, offset - L2VIC_INT_GRPn_3);
    } else {
        value = 0;
        qemu_log_mask(LOG_GUEST_ERROR, "L2VIC: %s: offset 0x%x\n", __func__,
                      (int)offset);
    }

    trace_l2vic_reg_read((unsigned)offset, value);
    qemu_mutex_unlock(&s->active);

    return value;
}

static const MemoryRegionOps l2vic_ops = {
    .read = l2vic_read,
    .write = l2vic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
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
        }
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write cmd %d\n", __func__,
                      cmd);
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write offset 0x%08x\n",
                  __func__, (unsigned int)offset);
}

static const MemoryRegionOps fastl2vic_ops = {
    .write = fastl2vic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .valid.unaligned = false,
};

static void l2vic_reset(DeviceState *d)
{
    L2VICState *s = L2VIC(d);
    memset(s->int_clear, 0, sizeof(s->int_clear));
    memset(s->int_enable, 0, sizeof(s->int_enable));
    memset(s->int_pending, 0, sizeof(s->int_pending));
    memset(s->int_status, 0, sizeof(s->int_status));
    memset(s->int_type, 0, sizeof(s->int_type));
    memset(s->int_group_n0, 0, sizeof(s->int_group_n0));
    memset(s->int_group_n1, 0, sizeof(s->int_group_n1));
    memset(s->int_group_n2, 0, sizeof(s->int_group_n2));
    memset(s->int_group_n3, 0, sizeof(s->int_group_n3));
    s->int_soft = 0;
    s->vid0 = 0;

    l2vic_update_all(s);
}


static void reset_irq_handler(void *opaque, int irq, int level)
{
    L2VICState *s = (L2VICState *)opaque;
    DeviceState *dev = DEVICE(opaque);
    if (level) {
        l2vic_reset(dev);
    }
    l2vic_update_all(s);
}

static void l2vic_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    L2VICState *s = L2VIC(obj);
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
    qemu_mutex_init(&s->active); /* TODO: Remove this is an experiment */
}

static const VMStateDescription vmstate_l2vic = {
    .name = "l2vic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields =
        (VMStateField[]){
            VMSTATE_UINT32(level, L2VICState),
            VMSTATE_UINT32_ARRAY(vid_group, L2VICState, 4),
            VMSTATE_UINT32(vid0, L2VICState),
            VMSTATE_UINT32_ARRAY(int_enable, L2VICState, SLICE_MAX),
            VMSTATE_UINT32(int_enable_clear, L2VICState),
            VMSTATE_UINT32(int_enable_set, L2VICState),
            VMSTATE_UINT32_ARRAY(int_type, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_status, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_clear, L2VICState, SLICE_MAX),
            VMSTATE_UINT32(int_soft, L2VICState),
            VMSTATE_UINT32_ARRAY(int_pending, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n0, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n1, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n2, L2VICState, SLICE_MAX),
            VMSTATE_UINT32_ARRAY(int_group_n3, L2VICState, SLICE_MAX),
            VMSTATE_END_OF_LIST() }
};

static void l2vic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, l2vic_reset);
    dc->vmsd = &vmstate_l2vic;
}

static const TypeInfo l2vic_info = {
    .name = TYPE_L2VIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(L2VICState),
    .instance_init = l2vic_init,
    .class_init = l2vic_class_init,
};

static void l2vic_register_types(void)
{
    type_register_static(&l2vic_info);
}

type_init(l2vic_register_types)
