/*
 * QEMU PReP System I/O emulation
 *
 * Copyright (c) 2017 Hervé Poussineau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "exec/address-spaces.h"
#include "qom/object.h"
#include "qemu/error-report.h" /* for error_report() */
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "cpu.h"
#include "trace.h"

#define TYPE_PREP_SYSTEMIO "prep-systemio"
OBJECT_DECLARE_SIMPLE_TYPE(PrepSystemIoState, PREP_SYSTEMIO)

/* Bit as defined in PowerPC Reference Platform v1.1, sect. 6.1.5, p. 132 */
#define PREP_BIT(n) (1 << (7 - (n)))

struct PrepSystemIoState {
    ISADevice parent_obj;
    MemoryRegion ppc_parity_mem;

    qemu_irq non_contiguous_io_map_irq;
    uint8_t sreset; /* 0x0092 */
    uint8_t equipment; /* 0x080c */
    uint8_t system_control; /* 0x081c */
    uint8_t iomap_type; /* 0x0850 */
    uint8_t ibm_planar_id; /* 0x0852 */
    qemu_irq softreset_irq;
    PortioList portio;
};

/* PORT 0092 -- Special Port 92 (Read/Write) */

enum {
    PORT0092_SOFTRESET  = PREP_BIT(7),
    PORT0092_LE_MODE    = PREP_BIT(6),
};

static void prep_port0092_write(void *opaque, uint32_t addr, uint32_t val)
{
    PrepSystemIoState *s = opaque;

    trace_prep_systemio_write(addr, val);

    s->sreset = val & PORT0092_SOFTRESET;
    qemu_set_irq(s->softreset_irq, s->sreset);

    if ((val & PORT0092_LE_MODE) != 0) {
        /* XXX Not supported yet */
        error_report("little-endian mode not supported");
        vm_stop(RUN_STATE_PAUSED);
    } else {
        /* Nothing to do */
    }
}

static uint32_t prep_port0092_read(void *opaque, uint32_t addr)
{
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_read(addr, s->sreset);
    return s->sreset;
}

/* PORT 0808 -- Hardfile Light Register (Write Only) */

enum {
    PORT0808_HARDFILE_LIGHT_ON  = PREP_BIT(7),
};

static void prep_port0808_write(void *opaque, uint32_t addr, uint32_t val)
{
    trace_prep_systemio_write(addr, val);
}

/* PORT 0810 -- Password Protect 1 Register (Write Only) */

/* reset by port 0x4D in the SIO */
static void prep_port0810_write(void *opaque, uint32_t addr, uint32_t val)
{
    trace_prep_systemio_write(addr, val);
}

/* PORT 0812 -- Password Protect 2 Register (Write Only) */

/* reset by port 0x4D in the SIO */
static void prep_port0812_write(void *opaque, uint32_t addr, uint32_t val)
{
    trace_prep_systemio_write(addr, val);
}

/* PORT 0814 -- L2 Invalidate Register (Write Only) */

static void prep_port0814_write(void *opaque, uint32_t addr, uint32_t val)
{
    trace_prep_systemio_write(addr, val);
}

/* PORT 0818 -- Reserved for Keylock (Read Only) */

enum {
    PORT0818_KEYLOCK_SIGNAL_HIGH    = PREP_BIT(7),
};

static uint32_t prep_port0818_read(void *opaque, uint32_t addr)
{
    uint32_t val = 0;
    trace_prep_systemio_read(addr, val);
    return val;
}

/* PORT 080C -- Equipment */

enum {
    PORT080C_SCSIFUSE               = PREP_BIT(1),
    PORT080C_L2_COPYBACK            = PREP_BIT(4),
    PORT080C_L2_256                 = PREP_BIT(5),
    PORT080C_UPGRADE_CPU            = PREP_BIT(6),
    PORT080C_L2                     = PREP_BIT(7),
};

static uint32_t prep_port080c_read(void *opaque, uint32_t addr)
{
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_read(addr, s->equipment);
    return s->equipment;
}

/* PORT 081C -- System Control Register (Read/Write) */

enum {
    PORT081C_FLOPPY_MOTOR_INHIBIT   = PREP_BIT(3),
    PORT081C_MASK_TEA               = PREP_BIT(2),
    PORT081C_L2_UPDATE_INHIBIT      = PREP_BIT(1),
    PORT081C_L2_CACHEMISS_INHIBIT   = PREP_BIT(0),
};

static void prep_port081c_write(void *opaque, uint32_t addr, uint32_t val)
{
    static const uint8_t mask = PORT081C_FLOPPY_MOTOR_INHIBIT |
                                PORT081C_MASK_TEA |
                                PORT081C_L2_UPDATE_INHIBIT |
                                PORT081C_L2_CACHEMISS_INHIBIT;
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_write(addr, val);
    s->system_control = val & mask;
}

static uint32_t prep_port081c_read(void *opaque, uint32_t addr)
{
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_read(addr, s->system_control);
    return s->system_control;
}

/* System Board Identification */

static uint32_t prep_port0852_read(void *opaque, uint32_t addr)
{
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_read(addr, s->ibm_planar_id);
    return s->ibm_planar_id;
}

/* PORT 0850 -- I/O Map Type Register (Read/Write) */

enum {
    PORT0850_IOMAP_NONCONTIGUOUS    = PREP_BIT(7),
};

static uint32_t prep_port0850_read(void *opaque, uint32_t addr)
{
    PrepSystemIoState *s = opaque;
    trace_prep_systemio_read(addr, s->iomap_type);
    return s->iomap_type;
}

static void prep_port0850_write(void *opaque, uint32_t addr, uint32_t val)
{
    PrepSystemIoState *s = opaque;

    trace_prep_systemio_write(addr, val);
    qemu_set_irq(s->non_contiguous_io_map_irq,
                 val & PORT0850_IOMAP_NONCONTIGUOUS);
    s->iomap_type = val & PORT0850_IOMAP_NONCONTIGUOUS;
}

static const MemoryRegionPortio ppc_io800_port_list[] = {
    { 0x092, 1, 1, .read = prep_port0092_read,
                   .write = prep_port0092_write, },
    { 0x808, 1, 1, .write = prep_port0808_write, },
    { 0x80c, 1, 1, .read = prep_port080c_read, },
    { 0x810, 1, 1, .write = prep_port0810_write, },
    { 0x812, 1, 1, .write = prep_port0812_write, },
    { 0x814, 1, 1, .write = prep_port0814_write, },
    { 0x818, 1, 1, .read = prep_port0818_read },
    { 0x81c, 1, 1, .read = prep_port081c_read,
                   .write = prep_port081c_write, },
    { 0x850, 1, 1, .read = prep_port0850_read,
                   .write = prep_port0850_write, },
    { 0x852, 1, 1, .read = prep_port0852_read, },
    PORTIO_END_OF_LIST()
};

static uint64_t ppc_parity_error_readl(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    uint32_t val = 0;
    trace_prep_systemio_read((unsigned int)addr, val);
    return val;
}

static void ppc_parity_error_writel(void *opaque, hwaddr addr,
                                    uint64_t data, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid access\n", __func__);
}

static const MemoryRegionOps ppc_parity_error_ops = {
    .read = ppc_parity_error_readl,
    .write = ppc_parity_error_writel,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void prep_systemio_realize(DeviceState *dev, Error **errp)
{
    ISADevice *isa = ISA_DEVICE(dev);
    PrepSystemIoState *s = PREP_SYSTEMIO(dev);
    PowerPCCPU *cpu;

    qdev_init_gpio_out(dev, &s->non_contiguous_io_map_irq, 1);
    s->iomap_type = PORT0850_IOMAP_NONCONTIGUOUS;
    qemu_set_irq(s->non_contiguous_io_map_irq,
                 s->iomap_type & PORT0850_IOMAP_NONCONTIGUOUS);
    cpu = POWERPC_CPU(first_cpu);
    s->softreset_irq = qdev_get_gpio_in(DEVICE(cpu), PPC6xx_INPUT_HRESET);

    isa_register_portio_list(isa, &s->portio, 0x0, ppc_io800_port_list, s,
                             "systemio800");

    memory_region_init_io(&s->ppc_parity_mem, OBJECT(dev),
                          &ppc_parity_error_ops, s, "ppc-parity", 0x4);
    memory_region_add_subregion(get_system_memory(), 0xbfffeff0,
                                &s->ppc_parity_mem);
}

static const VMStateDescription vmstate_prep_systemio = {
    .name = "prep_systemio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(sreset, PrepSystemIoState),
        VMSTATE_UINT8(system_control, PrepSystemIoState),
        VMSTATE_UINT8(iomap_type, PrepSystemIoState),
        VMSTATE_END_OF_LIST()
    },
};

static Property prep_systemio_properties[] = {
    DEFINE_PROP_UINT8("ibm-planar-id", PrepSystemIoState, ibm_planar_id, 0),
    DEFINE_PROP_UINT8("equipment", PrepSystemIoState, equipment, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void prep_systemio_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = prep_systemio_realize;
    dc->vmsd = &vmstate_prep_systemio;
    device_class_set_props(dc, prep_systemio_properties);
}

static const TypeInfo prep_systemio800_info = {
    .name          = TYPE_PREP_SYSTEMIO,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(PrepSystemIoState),
    .class_init    = prep_systemio_class_initfn,
};

static void prep_systemio_register_types(void)
{
    type_register_static(&prep_systemio800_info);
}

type_init(prep_systemio_register_types)
