/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Authors: Stafford Horne <shorne@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "qom/object.h"

#define TYPE_OR1K_OMPIC "or1k-ompic"
OBJECT_DECLARE_SIMPLE_TYPE(OR1KOMPICState, OR1K_OMPIC)

#define OMPIC_CTRL_IRQ_ACK  (1 << 31)
#define OMPIC_CTRL_IRQ_GEN  (1 << 30)
#define OMPIC_CTRL_DST(cpu) (((cpu) >> 16) & 0x3fff)

#define OMPIC_REG(addr)     (((addr) >> 2) & 0x1)
#define OMPIC_SRC_CPU(addr) (((addr) >> 3) & 0x4f)
#define OMPIC_DST_CPU(addr) (((addr) >> 3) & 0x4f)

#define OMPIC_STATUS_IRQ_PENDING (1 << 30)
#define OMPIC_STATUS_SRC(cpu)    (((cpu) & 0x3fff) << 16)
#define OMPIC_STATUS_DATA(data)  ((data) & 0xffff)

#define OMPIC_CONTROL 0
#define OMPIC_STATUS  1

#define OMPIC_MAX_CPUS 4 /* Real max is much higher, but dont waste memory */
#define OMPIC_ADDRSPACE_SZ (OMPIC_MAX_CPUS * 2 * 4) /* 2 32-bit regs per cpu */

typedef struct OR1KOMPICCPUState OR1KOMPICCPUState;

struct OR1KOMPICCPUState {
    qemu_irq irq;
    uint32_t status;
    uint32_t control;
};

struct OR1KOMPICState {
    SysBusDevice parent_obj;
    MemoryRegion mr;

    OR1KOMPICCPUState cpus[OMPIC_MAX_CPUS];

    uint32_t num_cpus;
};

static uint64_t ompic_read(void *opaque, hwaddr addr, unsigned size)
{
    OR1KOMPICState *s = opaque;
    int src_cpu = OMPIC_SRC_CPU(addr);

    /* We can only write to control control, write control + update status */
    if (OMPIC_REG(addr) == OMPIC_CONTROL) {
        return s->cpus[src_cpu].control;
    } else {
        return s->cpus[src_cpu].status;
   }

}

static void ompic_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    OR1KOMPICState *s = opaque;
    /* We can only write to control control, write control + update status */
    if (OMPIC_REG(addr) == OMPIC_CONTROL) {
        int src_cpu = OMPIC_SRC_CPU(addr);

        s->cpus[src_cpu].control = data;

        if (data & OMPIC_CTRL_IRQ_GEN) {
            int dst_cpu = OMPIC_CTRL_DST(data);

            s->cpus[dst_cpu].status = OMPIC_STATUS_IRQ_PENDING |
                OMPIC_STATUS_SRC(src_cpu) |
                OMPIC_STATUS_DATA(data);

            qemu_irq_raise(s->cpus[dst_cpu].irq);
        }
        if (data & OMPIC_CTRL_IRQ_ACK) {
            s->cpus[src_cpu].status &= ~OMPIC_STATUS_IRQ_PENDING;
            qemu_irq_lower(s->cpus[src_cpu].irq);
        }
    }
}

static const MemoryRegionOps ompic_ops = {
    .read = ompic_read,
    .write = ompic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 8,
    },
};

static void or1k_ompic_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    OR1KOMPICState *s = OR1K_OMPIC(obj);

    memory_region_init_io(&s->mr, OBJECT(s), &ompic_ops, s,
                          "or1k-ompic", OMPIC_ADDRSPACE_SZ);
    sysbus_init_mmio(sbd, &s->mr);
}

static void or1k_ompic_realize(DeviceState *dev, Error **errp)
{
    OR1KOMPICState *s = OR1K_OMPIC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    if (s->num_cpus > OMPIC_MAX_CPUS) {
        error_setg(errp, "Exceeded maximum CPUs %d", s->num_cpus);
        return;
    }
    /* Init IRQ sources for all CPUs */
    for (i = 0; i < s->num_cpus; i++) {
        sysbus_init_irq(sbd, &s->cpus[i].irq);
    }
}

static const Property or1k_ompic_properties[] = {
    DEFINE_PROP_UINT32("num-cpus", OR1KOMPICState, num_cpus, 1),
};

static const VMStateDescription vmstate_or1k_ompic_cpu = {
    .name = "or1k_ompic_cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
         VMSTATE_UINT32(status, OR1KOMPICCPUState),
         VMSTATE_UINT32(control, OR1KOMPICCPUState),
         VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_or1k_ompic = {
    .name = TYPE_OR1K_OMPIC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
         VMSTATE_STRUCT_ARRAY(cpus, OR1KOMPICState, OMPIC_MAX_CPUS, 1,
             vmstate_or1k_ompic_cpu, OR1KOMPICCPUState),
         VMSTATE_UINT32(num_cpus, OR1KOMPICState),
         VMSTATE_END_OF_LIST()
    }
};

static void or1k_ompic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, or1k_ompic_properties);
    dc->realize = or1k_ompic_realize;
    dc->vmsd = &vmstate_or1k_ompic;
}

static const TypeInfo or1k_ompic_info = {
    .name          = TYPE_OR1K_OMPIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OR1KOMPICState),
    .instance_init = or1k_ompic_init,
    .class_init    = or1k_ompic_class_init,
};

static void or1k_ompic_register_types(void)
{
    type_register_static(&or1k_ompic_info);
}

type_init(or1k_ompic_register_types)
