/*
 * RISC-V ACLINT (Advanced Core Local Interruptor)
 * URL: https://github.com/riscv/riscv-aclint
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017 SiFive, Inc.
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * This provides real-time clock, timer and interprocessor interrupts.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "target/riscv/cpu.h"
#include "hw/qdev-properties.h"
#include "hw/intc/riscv_aclint.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "migration/vmstate.h"

typedef struct riscv_aclint_mtimer_callback {
    RISCVAclintMTimerState *s;
    int num;
} riscv_aclint_mtimer_callback;

static uint64_t cpu_riscv_read_rtc_raw(uint32_t timebase_freq)
{
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
        timebase_freq, NANOSECONDS_PER_SECOND);
}

static uint64_t cpu_riscv_read_rtc(void *opaque)
{
    RISCVAclintMTimerState *mtimer = opaque;
    return cpu_riscv_read_rtc_raw(mtimer->timebase_freq) + mtimer->time_delta;
}

/*
 * Called when timecmp is written to update the QEMU timer or immediately
 * trigger timer interrupt if mtimecmp <= current timer value.
 */
static void riscv_aclint_mtimer_write_timecmp(RISCVAclintMTimerState *mtimer,
                                              RISCVCPU *cpu,
                                              int hartid,
                                              uint64_t value)
{
    uint32_t timebase_freq = mtimer->timebase_freq;
    uint64_t next;
    uint64_t diff;

    uint64_t rtc = cpu_riscv_read_rtc(mtimer);

    /* Compute the relative hartid w.r.t the socket */
    hartid = hartid - mtimer->hartid_base;

    mtimer->timecmp[hartid] = value;
    if (mtimer->timecmp[hartid] <= rtc) {
        /*
         * If we're setting an MTIMECMP value in the "past",
         * immediately raise the timer interrupt
         */
        qemu_irq_raise(mtimer->timer_irqs[hartid]);
        return;
    }

    /* otherwise, set up the future timer interrupt */
    qemu_irq_lower(mtimer->timer_irqs[hartid]);
    diff = mtimer->timecmp[hartid] - rtc;
    /* back to ns (note args switched in muldiv64) */
    uint64_t ns_diff = muldiv64(diff, NANOSECONDS_PER_SECOND, timebase_freq);

    /*
     * check if ns_diff overflowed and check if the addition would potentially
     * overflow
     */
    if ((NANOSECONDS_PER_SECOND > timebase_freq && ns_diff < diff) ||
        ns_diff > INT64_MAX) {
        next = INT64_MAX;
    } else {
        /*
         * as it is very unlikely qemu_clock_get_ns will return a value
         * greater than INT64_MAX, no additional check is needed for an
         * unsigned integer overflow.
         */
        next = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns_diff;
        /*
         * if ns_diff is INT64_MAX next may still be outside the range
         * of a signed integer.
         */
        next = MIN(next, INT64_MAX);
    }

    timer_mod(mtimer->timers[hartid], next);
}

/*
 * Callback used when the timer set using timer_mod expires.
 * Should raise the timer interrupt line
 */
static void riscv_aclint_mtimer_cb(void *opaque)
{
    riscv_aclint_mtimer_callback *state = opaque;

    qemu_irq_raise(state->s->timer_irqs[state->num]);
}

/* CPU read MTIMER register */
static uint64_t riscv_aclint_mtimer_read(void *opaque, hwaddr addr,
    unsigned size)
{
    RISCVAclintMTimerState *mtimer = opaque;

    if (addr >= mtimer->timecmp_base &&
        addr < (mtimer->timecmp_base + (mtimer->num_harts << 3))) {
        size_t hartid = mtimer->hartid_base +
                        ((addr - mtimer->timecmp_base) >> 3);
        CPUState *cpu = cpu_by_arch_id(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "aclint-mtimer: invalid hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            /* timecmp_lo for RV32/RV64 or timecmp for RV64 */
            uint64_t timecmp = mtimer->timecmp[hartid];
            return (size == 4) ? (timecmp & 0xFFFFFFFF) : timecmp;
        } else if ((addr & 0x7) == 4) {
            /* timecmp_hi */
            uint64_t timecmp = mtimer->timecmp[hartid];
            return (timecmp >> 32) & 0xFFFFFFFF;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "aclint-mtimer: invalid read: %08x", (uint32_t)addr);
            return 0;
        }
    } else if (addr == mtimer->time_base) {
        /* time_lo for RV32/RV64 or timecmp for RV64 */
        uint64_t rtc = cpu_riscv_read_rtc(mtimer);
        return (size == 4) ? (rtc & 0xFFFFFFFF) : rtc;
    } else if (addr == mtimer->time_base + 4) {
        /* time_hi */
        return (cpu_riscv_read_rtc(mtimer) >> 32) & 0xFFFFFFFF;
    }

    qemu_log_mask(LOG_UNIMP,
                  "aclint-mtimer: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU write MTIMER register */
static void riscv_aclint_mtimer_write(void *opaque, hwaddr addr,
    uint64_t value, unsigned size)
{
    RISCVAclintMTimerState *mtimer = opaque;
    int i;

    if (addr >= mtimer->timecmp_base &&
        addr < (mtimer->timecmp_base + (mtimer->num_harts << 3))) {
        size_t hartid = mtimer->hartid_base +
                        ((addr - mtimer->timecmp_base) >> 3);
        CPUState *cpu = cpu_by_arch_id(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "aclint-mtimer: invalid hartid: %zu", hartid);
        } else if ((addr & 0x7) == 0) {
            if (size == 4) {
                /* timecmp_lo for RV32/RV64 */
                uint64_t timecmp_hi = mtimer->timecmp[hartid] >> 32;
                riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu), hartid,
                    timecmp_hi << 32 | (value & 0xFFFFFFFF));
            } else {
                /* timecmp for RV64 */
                riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu), hartid,
                                                  value);
            }
        } else if ((addr & 0x7) == 4) {
            if (size == 4) {
                /* timecmp_hi for RV32/RV64 */
                uint64_t timecmp_lo = mtimer->timecmp[hartid];
                riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu), hartid,
                    value << 32 | (timecmp_lo & 0xFFFFFFFF));
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "aclint-mtimer: invalid timecmp_hi write: %08x",
                              (uint32_t)addr);
            }
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "aclint-mtimer: invalid timecmp write: %08x",
                          (uint32_t)addr);
        }
        return;
    } else if (addr == mtimer->time_base || addr == mtimer->time_base + 4) {
        uint64_t rtc_r = cpu_riscv_read_rtc_raw(mtimer->timebase_freq);
        uint64_t rtc = cpu_riscv_read_rtc(mtimer);

        if (addr == mtimer->time_base) {
            if (size == 4) {
                /* time_lo for RV32/RV64 */
                mtimer->time_delta = ((rtc & ~0xFFFFFFFFULL) | value) - rtc_r;
            } else {
                /* time for RV64 */
                mtimer->time_delta = value - rtc_r;
            }
        } else {
            if (size == 4) {
                /* time_hi for RV32/RV64 */
                mtimer->time_delta = (value << 32 | (rtc & 0xFFFFFFFF)) - rtc_r;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "aclint-mtimer: invalid time_hi write: %08x",
                              (uint32_t)addr);
                return;
            }
        }

        /* Check if timer interrupt is triggered for each hart. */
        for (i = 0; i < mtimer->num_harts; i++) {
            CPUState *cpu = cpu_by_arch_id(mtimer->hartid_base + i);
            CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
            if (!env) {
                continue;
            }
            riscv_aclint_mtimer_write_timecmp(mtimer, RISCV_CPU(cpu),
                                              mtimer->hartid_base + i,
                                              mtimer->timecmp[i]);
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "aclint-mtimer: invalid write: %08x", (uint32_t)addr);
}

static const MemoryRegionOps riscv_aclint_mtimer_ops = {
    .read = riscv_aclint_mtimer_read,
    .write = riscv_aclint_mtimer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    }
};

static Property riscv_aclint_mtimer_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVAclintMTimerState,
        hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVAclintMTimerState, num_harts, 1),
    DEFINE_PROP_UINT32("timecmp-base", RISCVAclintMTimerState,
        timecmp_base, RISCV_ACLINT_DEFAULT_MTIMECMP),
    DEFINE_PROP_UINT32("time-base", RISCVAclintMTimerState,
        time_base, RISCV_ACLINT_DEFAULT_MTIME),
    DEFINE_PROP_UINT32("aperture-size", RISCVAclintMTimerState,
        aperture_size, RISCV_ACLINT_DEFAULT_MTIMER_SIZE),
    DEFINE_PROP_UINT32("timebase-freq", RISCVAclintMTimerState,
        timebase_freq, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_mtimer_realize(DeviceState *dev, Error **errp)
{
    RISCVAclintMTimerState *s = RISCV_ACLINT_MTIMER(dev);
    int i;

    memory_region_init_io(&s->mmio, OBJECT(dev), &riscv_aclint_mtimer_ops,
                          s, TYPE_RISCV_ACLINT_MTIMER, s->aperture_size);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    s->timer_irqs = g_new(qemu_irq, s->num_harts);
    qdev_init_gpio_out(dev, s->timer_irqs, s->num_harts);

    s->timers = g_new0(QEMUTimer *, s->num_harts);
    s->timecmp = g_new0(uint64_t, s->num_harts);
    /* Claim timer interrupt bits */
    for (i = 0; i < s->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(cpu_by_arch_id(s->hartid_base + i));
        if (riscv_cpu_claim_interrupts(cpu, MIP_MTIP) < 0) {
            error_report("MTIP already claimed");
            exit(1);
        }
    }
}

static void riscv_aclint_mtimer_reset_enter(Object *obj, ResetType type)
{
    /*
     * According to RISC-V ACLINT spec:
     *   - On MTIMER device reset, the MTIME register is cleared to zero.
     *   - On MTIMER device reset, the MTIMECMP registers are in unknown state.
     */
    RISCVAclintMTimerState *mtimer = RISCV_ACLINT_MTIMER(obj);

    /*
     * Clear mtime register by writing to 0 it.
     * Pending mtime interrupts will also be cleared at the same time.
     */
    riscv_aclint_mtimer_write(mtimer, mtimer->time_base, 0, 8);
}

static const VMStateDescription vmstate_riscv_mtimer = {
    .name = "riscv_mtimer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
            VMSTATE_VARRAY_UINT32(timecmp, RISCVAclintMTimerState,
                                  num_harts, 0,
                                  vmstate_info_uint64, uint64_t),
            VMSTATE_END_OF_LIST()
        }
};

static void riscv_aclint_mtimer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_aclint_mtimer_realize;
    device_class_set_props(dc, riscv_aclint_mtimer_properties);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.enter = riscv_aclint_mtimer_reset_enter;
    dc->vmsd = &vmstate_riscv_mtimer;
}

static const TypeInfo riscv_aclint_mtimer_info = {
    .name          = TYPE_RISCV_ACLINT_MTIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVAclintMTimerState),
    .class_init    = riscv_aclint_mtimer_class_init,
};

/*
 * Create ACLINT MTIMER device.
 */
DeviceState *riscv_aclint_mtimer_create(hwaddr addr, hwaddr size,
    uint32_t hartid_base, uint32_t num_harts,
    uint32_t timecmp_base, uint32_t time_base, uint32_t timebase_freq,
    bool provide_rdtime)
{
    int i;
    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_MTIMER);
    RISCVAclintMTimerState *s = RISCV_ACLINT_MTIMER(dev);

    assert(num_harts <= RISCV_ACLINT_MAX_HARTS);
    assert(!(addr & 0x7));
    assert(!(timecmp_base & 0x7));
    assert(!(time_base & 0x7));

    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "timecmp-base", timecmp_base);
    qdev_prop_set_uint32(dev, "time-base", time_base);
    qdev_prop_set_uint32(dev, "aperture-size", size);
    qdev_prop_set_uint32(dev, "timebase-freq", timebase_freq);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = cpu_by_arch_id(hartid_base + i);
        RISCVCPU *rvcpu = RISCV_CPU(cpu);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        riscv_aclint_mtimer_callback *cb =
            g_new0(riscv_aclint_mtimer_callback, 1);

        if (!env) {
            g_free(cb);
            continue;
        }
        if (provide_rdtime) {
            riscv_cpu_set_rdtime_fn(env, cpu_riscv_read_rtc, dev);
        }

        cb->s = s;
        cb->num = i;
        s->timers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  &riscv_aclint_mtimer_cb, cb);
        s->timecmp[i] = 0;

        qdev_connect_gpio_out(dev, i,
                              qdev_get_gpio_in(DEVICE(rvcpu), IRQ_M_TIMER));
    }

    return dev;
}

/* CPU read [M|S]SWI register */
static uint64_t riscv_aclint_swi_read(void *opaque, hwaddr addr,
    unsigned size)
{
    RISCVAclintSwiState *swi = opaque;

    if (addr < (swi->num_harts << 2)) {
        size_t hartid = swi->hartid_base + (addr >> 2);
        CPUState *cpu = cpu_by_arch_id(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "aclint-swi: invalid hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            return (swi->sswi) ? 0 : ((env->mip & MIP_MSIP) > 0);
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "aclint-swi: invalid read: %08x", (uint32_t)addr);
    return 0;
}

/* CPU write [M|S]SWI register */
static void riscv_aclint_swi_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    RISCVAclintSwiState *swi = opaque;

    if (addr < (swi->num_harts << 2)) {
        size_t hartid = swi->hartid_base + (addr >> 2);
        CPUState *cpu = cpu_by_arch_id(hartid);
        CPURISCVState *env = cpu ? cpu->env_ptr : NULL;
        if (!env) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "aclint-swi: invalid hartid: %zu", hartid);
        } else if ((addr & 0x3) == 0) {
            if (value & 0x1) {
                qemu_irq_raise(swi->soft_irqs[hartid - swi->hartid_base]);
            } else {
                if (!swi->sswi) {
                    qemu_irq_lower(swi->soft_irqs[hartid - swi->hartid_base]);
                }
            }
            return;
        }
    }

    qemu_log_mask(LOG_UNIMP,
                  "aclint-swi: invalid write: %08x", (uint32_t)addr);
}

static const MemoryRegionOps riscv_aclint_swi_ops = {
    .read = riscv_aclint_swi_read,
    .write = riscv_aclint_swi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static Property riscv_aclint_swi_properties[] = {
    DEFINE_PROP_UINT32("hartid-base", RISCVAclintSwiState, hartid_base, 0),
    DEFINE_PROP_UINT32("num-harts", RISCVAclintSwiState, num_harts, 1),
    DEFINE_PROP_UINT32("sswi", RISCVAclintSwiState, sswi, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void riscv_aclint_swi_realize(DeviceState *dev, Error **errp)
{
    RISCVAclintSwiState *swi = RISCV_ACLINT_SWI(dev);
    int i;

    memory_region_init_io(&swi->mmio, OBJECT(dev), &riscv_aclint_swi_ops, swi,
                          TYPE_RISCV_ACLINT_SWI, RISCV_ACLINT_SWI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &swi->mmio);

    swi->soft_irqs = g_new(qemu_irq, swi->num_harts);
    qdev_init_gpio_out(dev, swi->soft_irqs, swi->num_harts);

    /* Claim software interrupt bits */
    for (i = 0; i < swi->num_harts; i++) {
        RISCVCPU *cpu = RISCV_CPU(qemu_get_cpu(swi->hartid_base + i));
        /* We don't claim mip.SSIP because it is writable by software */
        if (riscv_cpu_claim_interrupts(cpu, swi->sswi ? 0 : MIP_MSIP) < 0) {
            error_report("MSIP already claimed");
            exit(1);
        }
    }
}

static void riscv_aclint_swi_reset_enter(Object *obj, ResetType type)
{
    /*
     * According to RISC-V ACLINT spec:
     *   - On MSWI device reset, each MSIP register is cleared to zero.
     *
     * p.s. SSWI device reset does nothing since SETSIP register always reads 0.
     */
    RISCVAclintSwiState *swi = RISCV_ACLINT_SWI(obj);
    int i;

    if (!swi->sswi) {
        for (i = 0; i < swi->num_harts; i++) {
            /* Clear MSIP registers by lowering software interrupts. */
            qemu_irq_lower(swi->soft_irqs[i]);
        }
    }
}

static void riscv_aclint_swi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = riscv_aclint_swi_realize;
    device_class_set_props(dc, riscv_aclint_swi_properties);
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    rc->phases.enter = riscv_aclint_swi_reset_enter;
}

static const TypeInfo riscv_aclint_swi_info = {
    .name          = TYPE_RISCV_ACLINT_SWI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RISCVAclintSwiState),
    .class_init    = riscv_aclint_swi_class_init,
};

/*
 * Create ACLINT [M|S]SWI device.
 */
DeviceState *riscv_aclint_swi_create(hwaddr addr, uint32_t hartid_base,
    uint32_t num_harts, bool sswi)
{
    int i;
    DeviceState *dev = qdev_new(TYPE_RISCV_ACLINT_SWI);

    assert(num_harts <= RISCV_ACLINT_MAX_HARTS);
    assert(!(addr & 0x3));

    qdev_prop_set_uint32(dev, "hartid-base", hartid_base);
    qdev_prop_set_uint32(dev, "num-harts", num_harts);
    qdev_prop_set_uint32(dev, "sswi", sswi ? true : false);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    for (i = 0; i < num_harts; i++) {
        CPUState *cpu = cpu_by_arch_id(hartid_base + i);
        RISCVCPU *rvcpu = RISCV_CPU(cpu);

        qdev_connect_gpio_out(dev, i,
                              qdev_get_gpio_in(DEVICE(rvcpu),
                                  (sswi) ? IRQ_S_SOFT : IRQ_M_SOFT));
    }

    return dev;
}

static void riscv_aclint_register_types(void)
{
    type_register_static(&riscv_aclint_mtimer_info);
    type_register_static(&riscv_aclint_swi_info);
}

type_init(riscv_aclint_register_types)
