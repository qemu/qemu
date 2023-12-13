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
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "hw/loongarch/virt.h"
#include "migration/vmstate.h"
#include "target/loongarch/internals.h"
#include "trace.h"

static MemTxResult loongarch_ipi_readl(void *opaque, hwaddr addr,
                                       uint64_t *data,
                                       unsigned size, MemTxAttrs attrs)
{
    IPICore *s;
    LoongArchIPI *ipi = opaque;
    uint64_t ret = 0;
    int index = 0;

    s = &ipi->cpu[attrs.requester_id];
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
    *data = ret;
    return MEMTX_OK;
}

static void send_ipi_data(CPULoongArchState *env, uint64_t val, hwaddr addr,
                          MemTxAttrs attrs)
{
    int i, mask = 0, data = 0;

    /*
     * bit 27-30 is mask for byte writing,
     * if the mask is 0, we need not to do anything.
     */
    if ((val >> 27) & 0xf) {
        data = address_space_ldl(env->address_space_iocsr, addr,
                                 attrs, NULL);
        for (i = 0; i < 4; i++) {
            /* get mask for byte writing */
            if (val & (0x1 << (27 + i))) {
                mask |= 0xff << (i * 8);
            }
        }
    }

    data &= mask;
    data |= (val >> 32) & ~mask;
    address_space_stl(env->address_space_iocsr, addr,
                      data, attrs, NULL);
}

static int archid_cmp(const void *a, const void *b)
{
   CPUArchId *archid_a = (CPUArchId *)a;
   CPUArchId *archid_b = (CPUArchId *)b;

   return archid_a->arch_id - archid_b->arch_id;
}

static CPUArchId *find_cpu_by_archid(MachineState *ms, uint32_t id)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
        ms->possible_cpus->len, sizeof(*ms->possible_cpus->cpus),
        archid_cmp);

    return found_cpu;
}

static CPUState *ipi_getcpu(int arch_id)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    CPUArchId *archid;

    archid = find_cpu_by_archid(machine, arch_id);
    if (archid) {
        return CPU(archid->cpu);
    }

    return NULL;
}

static MemTxResult mail_send(uint64_t val, MemTxAttrs attrs)
{
    uint32_t cpuid;
    hwaddr addr;
    CPUState *cs;

    cpuid = extract32(val, 16, 10);
    if (cpuid >= LOONGARCH_MAX_CPUS) {
        trace_loongarch_ipi_unsupported_cpuid("IOCSR_MAIL_SEND", cpuid);
        return MEMTX_DECODE_ERROR;
    }

    cs = ipi_getcpu(cpuid);
    if (cs == NULL) {
        return MEMTX_DECODE_ERROR;
    }

    /* override requester_id */
    addr = SMP_IPI_MAILBOX + CORE_BUF_20 + (val & 0x1c);
    attrs.requester_id = cs->cpu_index;
    send_ipi_data(&LOONGARCH_CPU(cs)->env, val, addr, attrs);
    return MEMTX_OK;
}

static MemTxResult any_send(uint64_t val, MemTxAttrs attrs)
{
    uint32_t cpuid;
    hwaddr addr;
    CPUState *cs;

    cpuid = extract32(val, 16, 10);
    if (cpuid >= LOONGARCH_MAX_CPUS) {
        trace_loongarch_ipi_unsupported_cpuid("IOCSR_ANY_SEND", cpuid);
        return MEMTX_DECODE_ERROR;
    }

    cs = ipi_getcpu(cpuid);
    if (cs == NULL) {
        return MEMTX_DECODE_ERROR;
    }

    /* override requester_id */
    addr = val & 0xffff;
    attrs.requester_id = cs->cpu_index;
    send_ipi_data(&LOONGARCH_CPU(cs)->env, val, addr, attrs);
    return MEMTX_OK;
}

static MemTxResult loongarch_ipi_writel(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned size, MemTxAttrs attrs)
{
    LoongArchIPI *ipi = opaque;
    IPICore *s;
    int index = 0;
    uint32_t cpuid;
    uint8_t vector;
    CPUState *cs;

    s = &ipi->cpu[attrs.requester_id];
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
        cpuid = extract32(val, 16, 10);
        if (cpuid >= LOONGARCH_MAX_CPUS) {
            trace_loongarch_ipi_unsupported_cpuid("IOCSR_IPI_SEND", cpuid);
            return MEMTX_DECODE_ERROR;
        }

        /* IPI status vector */
        vector = extract8(val, 0, 5);
        cs = ipi_getcpu(cpuid);
        if (cs == NULL) {
            return MEMTX_DECODE_ERROR;
        }

        /* override requester_id */
        attrs.requester_id = cs->cpu_index;
        loongarch_ipi_writel(ipi, CORE_SET_OFF, BIT(vector), 4, attrs);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid write: %x", (uint32_t)addr);
        break;
    }

    return MEMTX_OK;
}

static const MemoryRegionOps loongarch_ipi_ops = {
    .read_with_attrs = loongarch_ipi_readl,
    .write_with_attrs = loongarch_ipi_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* mail send and any send only support writeq */
static MemTxResult loongarch_ipi_writeq(void *opaque, hwaddr addr, uint64_t val,
                                        unsigned size, MemTxAttrs attrs)
{
    MemTxResult ret = MEMTX_OK;

    addr &= 0xfff;
    switch (addr) {
    case MAIL_SEND_OFFSET:
        ret = mail_send(val, attrs);
        break;
    case ANY_SEND_OFFSET:
        ret = any_send(val, attrs);
        break;
    default:
       break;
    }

    return ret;
}

static const MemoryRegionOps loongarch_ipi64_ops = {
    .write_with_attrs = loongarch_ipi_writeq,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_ipi_realize(DeviceState *dev, Error **errp)
{
    LoongArchIPI *s = LOONGARCH_IPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (s->num_cpu == 0) {
        error_setg(errp, "num-cpu must be at least 1");
        return;
    }

    memory_region_init_io(&s->ipi_iocsr_mem, OBJECT(dev), &loongarch_ipi_ops,
                          s, "loongarch_ipi_iocsr", 0x48);

    /* loongarch_ipi_iocsr performs re-entrant IO through ipi_send */
    s->ipi_iocsr_mem.disable_reentrancy_guard = true;

    sysbus_init_mmio(sbd, &s->ipi_iocsr_mem);

    memory_region_init_io(&s->ipi64_iocsr_mem, OBJECT(dev),
                          &loongarch_ipi64_ops,
                          s, "loongarch_ipi64_iocsr", 0x118);
    sysbus_init_mmio(sbd, &s->ipi64_iocsr_mem);

    s->cpu = g_new0(IPICore, s->num_cpu);
    if (s->cpu == NULL) {
        error_setg(errp, "Memory allocation for ExtIOICore faile");
        return;
    }

    for (i = 0; i < s->num_cpu; i++) {
        qdev_init_gpio_out(dev, &s->cpu[i].irq, 1);
    }
}

static const VMStateDescription vmstate_ipi_core = {
    .name = "ipi-single",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(status, IPICore),
        VMSTATE_UINT32(en, IPICore),
        VMSTATE_UINT32(set, IPICore),
        VMSTATE_UINT32(clear, IPICore),
        VMSTATE_UINT32_ARRAY(buf, IPICore, IPI_MBX_NUM * 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_loongarch_ipi = {
    .name = TYPE_LOONGARCH_IPI,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(cpu, LoongArchIPI, num_cpu,
                         vmstate_ipi_core, IPICore),
        VMSTATE_END_OF_LIST()
    }
};

static Property ipi_properties[] = {
    DEFINE_PROP_UINT32("num-cpu", LoongArchIPI, num_cpu, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = loongarch_ipi_realize;
    device_class_set_props(dc, ipi_properties);
    dc->vmsd = &vmstate_loongarch_ipi;
}

static void loongarch_ipi_finalize(Object *obj)
{
    LoongArchIPI *s = LOONGARCH_IPI(obj);

    g_free(s->cpu);
}

static const TypeInfo loongarch_ipi_info = {
    .name          = TYPE_LOONGARCH_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchIPI),
    .class_init    = loongarch_ipi_class_init,
    .instance_finalize = loongarch_ipi_finalize,
};

static void loongarch_ipi_register_types(void)
{
    type_register_static(&loongarch_ipi_info);
}

type_init(loongarch_ipi_register_types)
