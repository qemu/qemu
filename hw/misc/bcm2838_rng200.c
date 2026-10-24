/*
 * BCM2838 Random Number Generator emulation
 *
 * Copyright (C) 2022 Sergey Pushkarev <sergey.pushkarev@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qom/object_interfaces.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/bcm2838_rng200.h"
#include "hw/core/registerfields.h"
#include "hw/core/resettable.h"
#include "migration/vmstate.h"
#include "trace.h"

/* RNG200 registers */
REG32(RNG_CTRL,               0x00)
    FIELD(RNG_CTRL, RBG_ENABLE,   0 , 1)
    FIELD(RNG_CTRL, RSVD,         1 , 12)
    FIELD(RNG_CTRL, DIV,         13 , 8)

REG32(RNG_SOFT_RESET,                0x04)
REG32(RBG_SOFT_RESET,                0x08)
REG32(RNG_TOTAL_BIT_COUNT,           0x0C)
REG32(RNG_TOTAL_BIT_COUNT_THRESHOLD, 0x10)

REG32(RNG_INT_STATUS,                               0x18)
    FIELD(RNG_INT_STATUS, TOTAL_BITS_COUNT_IRQ,         0, 1)
    FIELD(RNG_INT_STATUS, RSVD0,                        1, 4)
    FIELD(RNG_INT_STATUS, NIST_FAIL_IRQ,                5, 1)
    FIELD(RNG_INT_STATUS, RSVD1,                        6, 11)
    FIELD(RNG_INT_STATUS, STARTUP_TRANSITIONS_MET_IRQ,  17, 1)
    FIELD(RNG_INT_STATUS, RSVD2,                        18, 13)
    FIELD(RNG_INT_STATUS, MASTER_FAIL_LOCKOUT_IRQ,      30, 1)

REG32(RNG_INT_ENABLE,                               0x1C)
    FIELD(RNG_INT_ENABLE, TOTAL_BITS_COUNT_IRQ,         0, 1)
    FIELD(RNG_INT_ENABLE, RSVD0,                        1, 4)
    FIELD(RNG_INT_ENABLE, NIST_FAIL_IRQ,                5, 1)
    FIELD(RNG_INT_ENABLE, RSVD1,                        6, 11)
    FIELD(RNG_INT_ENABLE, STARTUP_TRANSITIONS_MET_IRQ,  17, 1)
    FIELD(RNG_INT_ENABLE, RSVD2,                        18, 13)
    FIELD(RNG_INT_ENABLE, MASTER_FAIL_LOCKOUT_IRQ,      30, 1)

REG32(RNG_FIFO_DATA, 0x20)

REG32(RNG_FIFO_COUNT,              0x24)
    FIELD(RNG_FIFO_COUNT, COUNT,       0, 8)
    FIELD(RNG_FIFO_COUNT, THRESHOLD,   8, 8)


#define RNG_WARM_UP_PERIOD_ELAPSED           17

#define SOFT_RESET    1
#define IRQ_PENDING   1

#define BCM2838_RNG200_PTIMER_POLICY         (PTIMER_POLICY_CONTINUOUS_TRIGGER)

static const VMStateDescription vmstate_bcm2838_rng200 = {
    .name = "bcm2838_rng200",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rng_fifo_cap, BCM2838Rng200State),
        VMSTATE_UINT32_ARRAY(regs, BCM2838Rng200State, N_BCM2838_RNG200_REGS),

        VMSTATE_END_OF_LIST()
    }
};

static bool is_rbg_enabled(BCM2838Rng200State *s)
{
    return FIELD_EX32(s->regs[R_RNG_CTRL], RNG_CTRL, RBG_ENABLE);
}

static void bcm2838_rng200_update_irq(BCM2838Rng200State *s)
{
    qemu_set_irq(s->irq,
                !!(s->regs[R_RNG_INT_ENABLE] & s->regs[R_RNG_INT_STATUS]));
}

static void bcm2838_rng200_update_rbg_period(void *opaque, ClockEvent event)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;

    ptimer_transaction_begin(s->ptimer);
    ptimer_set_period_from_clock(s->ptimer, s->clock, s->rng_fifo_cap * 8);
    ptimer_transaction_commit(s->ptimer);
}

static void bcm2838_rng200_update_fifo(void *opaque, const void *buf,
                                       size_t size)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;
    Fifo8 *fifo = &s->fifo;
    size_t num = MIN(size, fifo8_num_free(fifo));
    uint32_t num_bits = num * 8;
    uint32_t bit_count = 0;
    uint32_t bit_count_thld = 0;
    uint32_t fifo_thld = 0;

    bit_count = s->regs[R_RNG_TOTAL_BIT_COUNT];
    bit_count_thld = s->regs[R_RNG_TOTAL_BIT_COUNT_THRESHOLD];

    if (bit_count + num_bits < bit_count_thld) {
        s->regs[R_RNG_TOTAL_BIT_COUNT] += num_bits;

        fifo8_push_all(fifo, buf, num);

        fifo_thld = FIELD_EX32(s->regs[R_RNG_FIFO_COUNT],
                               RNG_FIFO_COUNT, THRESHOLD);

        if (fifo8_num_used(fifo) > fifo_thld) {
            s->regs[R_RNG_INT_STATUS] = FIELD_DP32(s->regs[R_RNG_INT_STATUS],
                                                    RNG_INT_STATUS,
                                                    TOTAL_BITS_COUNT_IRQ, 1);
        }
    }

    s->regs[R_RNG_FIFO_COUNT] = FIELD_DP32(s->regs[R_RNG_FIFO_COUNT],
                                           RNG_FIFO_COUNT,
                                           COUNT,
                                           fifo8_num_used(fifo) >> 2);
    bcm2838_rng200_update_irq(s);
    trace_bcm2838_rng200_update_fifo(num, fifo8_num_used(fifo));
}

static void bcm2838_rng200_fill_fifo(BCM2838Rng200State *s)
{
    rng_backend_request_entropy(s->rng, fifo8_num_free(&s->fifo),
                                bcm2838_rng200_update_fifo, s);
}

static void bcm2838_rng200_disable_rbg(void)
{
    trace_bcm2838_rng200_disable_rbg();
}

static void bcm2838_rng200_enable_rbg(BCM2838Rng200State *s)
{
    s->regs[R_RNG_TOTAL_BIT_COUNT] = RNG_WARM_UP_PERIOD_ELAPSED;

    bcm2838_rng200_fill_fifo(s);

    trace_bcm2838_rng200_enable_rbg();
}

static void bcm2838_rng200_rng_reset(BCM2838Rng200State *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[R_RNG_INT_STATUS] = FIELD_DP32(s->regs[R_RNG_INT_STATUS],
                                           RNG_INT_STATUS,
                                           STARTUP_TRANSITIONS_MET_IRQ,
                                           IRQ_PENDING);
    fifo8_reset(&s->fifo);

    trace_bcm2838_rng200_rng_soft_reset();
}

static void bcm2838_rng200_rbg_reset(BCM2838Rng200State *s)
{
    trace_bcm2838_rng200_rbg_soft_reset();
}

static uint32_t bcm2838_rng200_read_fifo_data(BCM2838Rng200State *s)
{
    Fifo8 *fifo = &s->fifo;
    uint32_t to_read = MIN(fifo8_num_used(fifo), 4);
    uint8_t byte_buf[4] = {};
    uint8_t *p = byte_buf;
    uint32_t ret = 0;
    uint32_t num;

    while (to_read) {
        num = fifo8_pop_buf(fifo, p, to_read);
        p += num;
        to_read -= num;
    }
    ret = ldl_le_p(byte_buf);

    s->regs[R_RNG_FIFO_COUNT] = FIELD_DP32(s->regs[R_RNG_FIFO_COUNT],
                                           RNG_FIFO_COUNT,
                                           COUNT,
                                           fifo8_num_used(fifo) >> 2);

    bcm2838_rng200_fill_fifo(s);

    return ret;
}

static void bcm2838_rng200_ctrl_write(BCM2838Rng200State *s, uint32_t value)
{
    bool currently_enabled = is_rbg_enabled(s);
    bool enable_requested = FIELD_EX32(value, RNG_CTRL, RBG_ENABLE);

    s->regs[R_RNG_CTRL] = value;

    if (!currently_enabled && enable_requested) {
        bcm2838_rng200_enable_rbg(s);
    } else if (currently_enabled && !enable_requested) {
        bcm2838_rng200_disable_rbg();
    }
}

static uint64_t bcm2838_rng200_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;
    uint32_t res = 0;

    switch (offset) {
    case A_RNG_CTRL:
        res = s->regs[R_RNG_CTRL];
        break;
    case A_RNG_SOFT_RESET:
    case A_RBG_SOFT_RESET:
        break;
    case A_RNG_INT_STATUS:
        res = s->regs[R_RNG_INT_STATUS];
        break;
    case A_RNG_INT_ENABLE:
        res = s->regs[R_RNG_INT_ENABLE];
        break;
    case A_RNG_FIFO_DATA:
        res = bcm2838_rng200_read_fifo_data(s);
        break;
    case A_RNG_FIFO_COUNT:
        res = s->regs[R_RNG_FIFO_COUNT];
        break;
    case A_RNG_TOTAL_BIT_COUNT:
        res = s->regs[R_RNG_TOTAL_BIT_COUNT];
        break;
    case A_RNG_TOTAL_BIT_COUNT_THRESHOLD:
        res = s->regs[R_RNG_TOTAL_BIT_COUNT_THRESHOLD];
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "bcm2838_rng200_read: Bad offset 0x%" HWADDR_PRIx "\n",
            offset
        );
        res = 0;
        break;
    }

    trace_bcm2838_rng200_read(offset, size, res);
    return res;
}

static void bcm2838_rng200_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    BCM2838Rng200State *s = (BCM2838Rng200State *)opaque;

    trace_bcm2838_rng200_write(offset, value, size);

    switch (offset) {
    case A_RNG_CTRL:
        bcm2838_rng200_ctrl_write(s, value);
        break;
    case A_RNG_SOFT_RESET:
        if (value & SOFT_RESET) {
            bcm2838_rng200_rng_reset(s);
        }
        break;
    case A_RBG_SOFT_RESET:
        if (value & SOFT_RESET) {
            bcm2838_rng200_rbg_reset(s);
        }
        break;
    case A_RNG_INT_STATUS:
        s->regs[R_RNG_INT_STATUS] &= ~value;
        bcm2838_rng200_update_irq(s);
        break;
    case A_RNG_INT_ENABLE:
        s->regs[R_RNG_INT_ENABLE] = value;
        bcm2838_rng200_update_irq(s);
        break;
    case A_RNG_FIFO_COUNT:
        s->regs[R_RNG_FIFO_COUNT] = value;
        break;
    case A_RNG_TOTAL_BIT_COUNT_THRESHOLD:
        s->regs[R_RNG_TOTAL_BIT_COUNT_THRESHOLD] = value;
        s->regs[R_RNG_TOTAL_BIT_COUNT] = value + 1;
        break;
    default:
        qemu_log_mask(
            LOG_GUEST_ERROR,
            "bcm2838_rng200_write: Bad offset 0x%" HWADDR_PRIx "\n",
            offset
        );
        break;
    }
}

static const MemoryRegionOps bcm2838_rng200_ops = {
    .read = bcm2838_rng200_read,
    .write = bcm2838_rng200_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
    },
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4
    },
};

static void bcm2838_rng200_realize(DeviceState *dev, Error **errp)
{
    BCM2838Rng200State *s = BCM2838_RNG200(dev);

    if (s->rng == NULL) {
        Object *default_backend = object_new(TYPE_RNG_BUILTIN);

        if (!user_creatable_complete(USER_CREATABLE(default_backend),
                                     errp)) {
            object_unref(default_backend);
            error_setg(errp, "Failed to create user creatable RNG backend");
            return;
        }

        object_property_add_child(OBJECT(dev), "default-backend",
                                  default_backend);
        object_unref(default_backend);

        object_property_set_link(OBJECT(dev), "rng", default_backend,
                                 errp);
    }

    fifo8_create(&s->fifo, s->rng_fifo_cap);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static void bcm2838_rng200_init(Object *obj)
{
    BCM2838Rng200State *s = BCM2838_RNG200(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->rng_fifo_cap = 128;

    s->clock = qdev_init_clock_in(DEVICE(s), "rbg-clock",
                                  bcm2838_rng200_update_rbg_period, s,
                                  ClockPreUpdate);
    if (s->clock == NULL) {
        error_setg(&error_fatal, "Failed to init RBG clock");
        return;
    }

    memory_region_init_io(&s->iomem, obj, &bcm2838_rng200_ops, s,
                          TYPE_BCM2838_RNG200, 0x28);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void bcm2838_rng200_reset(Object *obj, ResetType type)
{
    BCM2838Rng200State *s = BCM2838_RNG200(obj);

    bcm2838_rng200_rbg_reset(s);
    bcm2838_rng200_rng_reset(s);
}

static const Property bcm2838_rng200_properties[] = {
    DEFINE_PROP_LINK("rng", BCM2838Rng200State, rng,
                     TYPE_RNG_BACKEND, RngBackend *),
};

static void bcm2838_rng200_class_init(ObjectClass *klass, const void *data)
{
    static ResettablePhases unused_parent_phases;
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = bcm2838_rng200_realize;
    resettable_class_set_parent_phases(rc, NULL, bcm2838_rng200_reset, NULL,
                                       &unused_parent_phases);
    dc->vmsd = &vmstate_bcm2838_rng200;

    device_class_set_props(dc, bcm2838_rng200_properties);
}

static const TypeInfo bcm2838_rng200_info = {
    .name          = TYPE_BCM2838_RNG200,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2838Rng200State),
    .class_init    = bcm2838_rng200_class_init,
    .instance_init = bcm2838_rng200_init,
};

static void bcm2838_rng200_register_types(void)
{
    type_register_static(&bcm2838_rng200_info);
}

type_init(bcm2838_rng200_register_types)
