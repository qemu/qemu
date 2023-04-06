/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch ipi interrupt support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "hw/loongarch/virt.h"
#include "migration/vmstate.h"
#include "target/loongarch/internals.h"
#include "trace.h"

static uint64_t loongarch_ipi_readl(void *opaque, hwaddr addr, unsigned size)
{
    IPICore *s = opaque;
    uint64_t ret = 0;
    int index = 0;

    addr &= 0xff;
    switch (addr) {
    case CORE_STATUS_OFF:
        ret = s->status;
        break;
    case CORE_EN_OFF:
        ret = s->en;
        break;
    case CORE_SET_OFF:
        ret = 0;
        break;
    case CORE_CLEAR_OFF:
        ret = 0;
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        ret = s->buf[index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid read: %x", (uint32_t)addr);
        break;
    }

    trace_loongarch_ipi_read(size, (uint64_t)addr, ret);
    return ret;
}

static void send_ipi_data(CPULoongArchState *env, uint64_t val, hwaddr addr)
{
    int i, mask = 0, data = 0;

    /*
     * bit 27-30 is mask for byte writing,
     * if the mask is 0, we need not to do anything.
     */
    if ((val >> 27) & 0xf) {
        data = address_space_ldl(&env->address_space_iocsr, addr,
                                 MEMTXATTRS_UNSPECIFIED, NULL);
        for (i = 0; i < 4; i++) {
            /* get mask for byte writing */
            if (val & (0x1 << (27 + i))) {
                mask |= 0xff << (i * 8);
            }
        }
    }

    data &= mask;
    data |= (val >> 32) & ~mask;
    address_space_stl(&env->address_space_iocsr, addr,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void ipi_send(uint64_t val)
{
    uint32_t cpuid;
    uint8_t vector;
    CPULoongArchState *env;
    CPUState *cs;
    LoongArchCPU *cpu;

    cpuid = extract32(val, 16, 10);
    if (cpuid >= LOONGARCH_MAX_CPUS) {
        trace_loongarch_ipi_unsupported_cpuid("IOCSR_IPI_SEND", cpuid);
        return;
    }

    /* IPI status vector */
    vector = extract8(val, 0, 5);

    cs = qemu_get_cpu(cpuid);
    cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    address_space_stl(&env->address_space_iocsr, 0x1008,
                      BIT(vector), MEMTXATTRS_UNSPECIFIED, NULL);
}

static void mail_send(uint64_t val)
{
    uint32_t cpuid;
    hwaddr addr;
    CPULoongArchState *env;
    CPUState *cs;
    LoongArchCPU *cpu;

    cpuid = extract32(val, 16, 10);
    if (cpuid >= LOONGARCH_MAX_CPUS) {
        trace_loongarch_ipi_unsupported_cpuid("IOCSR_MAIL_SEND", cpuid);
        return;
    }

    addr = 0x1020 + (val & 0x1c);
    cs = qemu_get_cpu(cpuid);
    cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    send_ipi_data(env, val, addr);
}

static void any_send(uint64_t val)
{
    uint32_t cpuid;
    hwaddr addr;
    CPULoongArchState *env;
    CPUState *cs;
    LoongArchCPU *cpu;

    cpuid = extract32(val, 16, 10);
    if (cpuid >= LOONGARCH_MAX_CPUS) {
        trace_loongarch_ipi_unsupported_cpuid("IOCSR_ANY_SEND", cpuid);
        return;
    }

    addr = val & 0xffff;
    cs = qemu_get_cpu(cpuid);
    cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    send_ipi_data(env, val, addr);
}

static void loongarch_ipi_writel(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IPICore *s = opaque;
    int index = 0;

    addr &= 0xff;
    trace_loongarch_ipi_write(size, (uint64_t)addr, val);
    switch (addr) {
    case CORE_STATUS_OFF:
        qemu_log_mask(LOG_GUEST_ERROR, "can not be written");
        break;
    case CORE_EN_OFF:
        s->en = val;
        break;
    case CORE_SET_OFF:
        s->status |= val;
        if (s->status != 0 && (s->status & s->en) != 0) {
            qemu_irq_raise(s->irq);
        }
        break;
    case CORE_CLEAR_OFF:
        s->status &= ~val;
        if (s->status == 0 && s->en != 0) {
            qemu_irq_lower(s->irq);
        }
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        s->buf[index] = val;
        break;
    case IOCSR_IPI_SEND:
        ipi_send(val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid write: %x", (uint32_t)addr);
        break;
    }
}

static const MemoryRegionOps loongarch_ipi_ops = {
    .read = loongarch_ipi_readl,
    .write = loongarch_ipi_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* mail send and any send only support writeq */
static void loongarch_ipi_writeq(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    addr &= 0xfff;
    switch (addr) {
    case MAIL_SEND_OFFSET:
        mail_send(val);
        break;
    case ANY_SEND_OFFSET:
        any_send(val);
        break;
    default:
       break;
    }
}

static const MemoryRegionOps loongarch_ipi64_ops = {
    .write = loongarch_ipi_writeq,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_ipi_init(Object *obj)
{
    LoongArchIPI *s = LOONGARCH_IPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->ipi_iocsr_mem, obj, &loongarch_ipi_ops,
                          &s->ipi_core, "loongarch_ipi_iocsr", 0x48);

    /* loongarch_ipi_iocsr performs re-entrant IO through ipi_send */
    s->ipi_iocsr_mem.disable_reentrancy_guard = true;

    sysbus_init_mmio(sbd, &s->ipi_iocsr_mem);

    memory_region_init_io(&s->ipi64_iocsr_mem, obj, &loongarch_ipi64_ops,
                          &s->ipi_core, "loongarch_ipi64_iocsr", 0x118);
    sysbus_init_mmio(sbd, &s->ipi64_iocsr_mem);
    qdev_init_gpio_out(DEVICE(obj), &s->ipi_core.irq, 1);
}

static const VMStateDescription vmstate_ipi_core = {
    .name = "ipi-single",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(status, IPICore),
        VMSTATE_UINT32(en, IPICore),
        VMSTATE_UINT32(set, IPICore),
        VMSTATE_UINT32(clear, IPICore),
        VMSTATE_UINT32_ARRAY(buf, IPICore, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_loongarch_ipi = {
    .name = TYPE_LOONGARCH_IPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(ipi_core, LoongArchIPI, 0, vmstate_ipi_core, IPICore),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_ipi;
}

static const TypeInfo loongarch_ipi_info = {
    .name          = TYPE_LOONGARCH_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchIPI),
    .instance_init = loongarch_ipi_init,
    .class_init    = loongarch_ipi_class_init,
};

static void loongarch_ipi_register_types(void)
{
    type_register_static(&loongarch_ipi_info);
}

type_init(loongarch_ipi_register_types)
