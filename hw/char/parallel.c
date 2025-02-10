/*
 * QEMU Parallel PORT emulation
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 * Copyright (c) 2007 Marko Kohtala
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
#include "qapi/error.h"
#include "qemu/module.h"
#include "chardev/char-parallel.h"
#include "hw/acpi/acpi_aml_interface.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "hw/char/parallel-isa.h"
#include "hw/char/parallel.h"
#include "system/reset.h"
#include "system/system.h"
#include "trace.h"
#include "qom/object.h"

//#define DEBUG_PARALLEL

#ifdef DEBUG_PARALLEL
#define pdebug(fmt, ...) printf("pp: " fmt, ## __VA_ARGS__)
#else
#define pdebug(fmt, ...) ((void)0)
#endif

#define PARA_REG_DATA 0
#define PARA_REG_STS 1
#define PARA_REG_CTR 2
#define PARA_REG_EPP_ADDR 3
#define PARA_REG_EPP_DATA 4

/*
 * These are the definitions for the Printer Status Register
 */
#define PARA_STS_BUSY   0x80    /* Busy complement */
#define PARA_STS_ACK    0x40    /* Acknowledge */
#define PARA_STS_PAPER  0x20    /* Out of paper */
#define PARA_STS_ONLINE 0x10    /* Online */
#define PARA_STS_ERROR  0x08    /* Error complement */
#define PARA_STS_TMOUT  0x01    /* EPP timeout */

/*
 * These are the definitions for the Printer Control Register
 */
#define PARA_CTR_DIR    0x20    /* Direction (1=read, 0=write) */
#define PARA_CTR_INTEN  0x10    /* IRQ Enable */
#define PARA_CTR_SELECT 0x08    /* Select In complement */
#define PARA_CTR_INIT   0x04    /* Initialize Printer complement */
#define PARA_CTR_AUTOLF 0x02    /* Auto linefeed complement */
#define PARA_CTR_STROBE 0x01    /* Strobe complement */

#define PARA_CTR_SIGNAL (PARA_CTR_SELECT|PARA_CTR_INIT|PARA_CTR_AUTOLF|PARA_CTR_STROBE)

static void parallel_update_irq(ParallelState *s)
{
    if (s->irq_pending)
        qemu_irq_raise(s->irq);
    else
        qemu_irq_lower(s->irq);
}

static void
parallel_ioport_write_sw(void *opaque, uint32_t addr, uint32_t val)
{
    ParallelState *s = opaque;

    addr &= 7;
    trace_parallel_ioport_write("SW", addr, val);
    switch(addr) {
    case PARA_REG_DATA:
        s->dataw = val;
        parallel_update_irq(s);
        break;
    case PARA_REG_CTR:
        val |= 0xc0;
        if ((val & PARA_CTR_INIT) == 0 ) {
            s->status = PARA_STS_BUSY;
            s->status |= PARA_STS_ACK;
            s->status |= PARA_STS_ONLINE;
            s->status |= PARA_STS_ERROR;
        }
        else if (val & PARA_CTR_SELECT) {
            if (val & PARA_CTR_STROBE) {
                s->status &= ~PARA_STS_BUSY;
                if ((s->control & PARA_CTR_STROBE) == 0)
                    /* XXX this blocks entire thread. Rewrite to use
                     * qemu_chr_fe_write and background I/O callbacks */
                    qemu_chr_fe_write_all(&s->chr, &s->dataw, 1);
            } else {
                if (s->control & PARA_CTR_INTEN) {
                    s->irq_pending = 1;
                }
            }
        }
        parallel_update_irq(s);
        s->control = val;
        break;
    }
}

static void parallel_ioport_write_hw(void *opaque, uint32_t addr, uint32_t val)
{
    ParallelState *s = opaque;
    uint8_t parm = val;
    int dir;

    /* Sometimes programs do several writes for timing purposes on old
       HW. Take care not to waste time on writes that do nothing. */

    s->last_read_offset = ~0U;

    addr &= 7;
    trace_parallel_ioport_write("HW", addr, val);
    switch(addr) {
    case PARA_REG_DATA:
        if (s->dataw == val)
            return;
        pdebug("wd%02x\n", val);
        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_DATA, &parm);
        s->dataw = val;
        break;
    case PARA_REG_STS:
        pdebug("ws%02x\n", val);
        if (val & PARA_STS_TMOUT)
            s->epp_timeout = 0;
        break;
    case PARA_REG_CTR:
        val |= 0xc0;
        if (s->control == val)
            return;
        pdebug("wc%02x\n", val);

        if ((val & PARA_CTR_DIR) != (s->control & PARA_CTR_DIR)) {
            if (val & PARA_CTR_DIR) {
                dir = 1;
            } else {
                dir = 0;
            }
            qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_DATA_DIR, &dir);
            parm &= ~PARA_CTR_DIR;
        }

        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_WRITE_CONTROL, &parm);
        s->control = val;
        break;
    case PARA_REG_EPP_ADDR:
        if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != PARA_CTR_INIT)
            /* Controls not correct for EPP address cycle, so do nothing */
            pdebug("wa%02x s\n", val);
        else {
            struct ParallelIOArg ioarg = { .buffer = &parm, .count = 1 };
            if (qemu_chr_fe_ioctl(&s->chr,
                                  CHR_IOCTL_PP_EPP_WRITE_ADDR, &ioarg)) {
                s->epp_timeout = 1;
                pdebug("wa%02x t\n", val);
            }
            else
                pdebug("wa%02x\n", val);
        }
        break;
    case PARA_REG_EPP_DATA:
        if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != PARA_CTR_INIT)
            /* Controls not correct for EPP data cycle, so do nothing */
            pdebug("we%02x s\n", val);
        else {
            struct ParallelIOArg ioarg = { .buffer = &parm, .count = 1 };
            if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_WRITE, &ioarg)) {
                s->epp_timeout = 1;
                pdebug("we%02x t\n", val);
            }
            else
                pdebug("we%02x\n", val);
        }
        break;
    }
}

static void
parallel_ioport_eppdata_write_hw2(void *opaque, uint32_t addr, uint32_t val)
{
    ParallelState *s = opaque;
    uint16_t eppdata = cpu_to_le16(val);
    int err;
    struct ParallelIOArg ioarg = {
        .buffer = &eppdata, .count = sizeof(eppdata)
    };

    trace_parallel_ioport_write("EPP", addr, val);
    if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != PARA_CTR_INIT) {
        /* Controls not correct for EPP data cycle, so do nothing */
        pdebug("we%04x s\n", val);
        return;
    }
    err = qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_WRITE, &ioarg);
    if (err) {
        s->epp_timeout = 1;
        pdebug("we%04x t\n", val);
    }
    else
        pdebug("we%04x\n", val);
}

static void
parallel_ioport_eppdata_write_hw4(void *opaque, uint32_t addr, uint32_t val)
{
    ParallelState *s = opaque;
    uint32_t eppdata = cpu_to_le32(val);
    int err;
    struct ParallelIOArg ioarg = {
        .buffer = &eppdata, .count = sizeof(eppdata)
    };

    trace_parallel_ioport_write("EPP", addr, val);
    if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != PARA_CTR_INIT) {
        /* Controls not correct for EPP data cycle, so do nothing */
        pdebug("we%08x s\n", val);
        return;
    }
    err = qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_WRITE, &ioarg);
    if (err) {
        s->epp_timeout = 1;
        pdebug("we%08x t\n", val);
    }
    else
        pdebug("we%08x\n", val);
}

static uint32_t parallel_ioport_read_sw(void *opaque, uint32_t addr)
{
    ParallelState *s = opaque;
    uint32_t ret = 0xff;

    addr &= 7;
    switch(addr) {
    case PARA_REG_DATA:
        if (s->control & PARA_CTR_DIR)
            ret = s->datar;
        else
            ret = s->dataw;
        break;
    case PARA_REG_STS:
        ret = s->status;
        s->irq_pending = 0;
        if ((s->status & PARA_STS_BUSY) == 0 && (s->control & PARA_CTR_STROBE) == 0) {
            /* XXX Fixme: wait 5 microseconds */
            if (s->status & PARA_STS_ACK)
                s->status &= ~PARA_STS_ACK;
            else {
                /* XXX Fixme: wait 5 microseconds */
                s->status |= PARA_STS_ACK;
                s->status |= PARA_STS_BUSY;
            }
        }
        parallel_update_irq(s);
        break;
    case PARA_REG_CTR:
        ret = s->control;
        break;
    }
    trace_parallel_ioport_read("SW", addr, ret);
    return ret;
}

static uint32_t parallel_ioport_read_hw(void *opaque, uint32_t addr)
{
    ParallelState *s = opaque;
    uint8_t ret = 0xff;
    addr &= 7;
    switch(addr) {
    case PARA_REG_DATA:
        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_READ_DATA, &ret);
        if (s->last_read_offset != addr || s->datar != ret)
            pdebug("rd%02x\n", ret);
        s->datar = ret;
        break;
    case PARA_REG_STS:
        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_READ_STATUS, &ret);
        ret &= ~PARA_STS_TMOUT;
        if (s->epp_timeout)
            ret |= PARA_STS_TMOUT;
        if (s->last_read_offset != addr || s->status != ret)
            pdebug("rs%02x\n", ret);
        s->status = ret;
        break;
    case PARA_REG_CTR:
        /* s->control has some bits fixed to 1. It is zero only when
           it has not been yet written to.  */
        if (s->control == 0) {
            qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_READ_CONTROL, &ret);
            if (s->last_read_offset != addr)
                pdebug("rc%02x\n", ret);
            s->control = ret;
        }
        else {
            ret = s->control;
            if (s->last_read_offset != addr)
                pdebug("rc%02x\n", ret);
        }
        break;
    case PARA_REG_EPP_ADDR:
        if ((s->control & (PARA_CTR_DIR | PARA_CTR_SIGNAL)) !=
            (PARA_CTR_DIR | PARA_CTR_INIT))
            /* Controls not correct for EPP addr cycle, so do nothing */
            pdebug("ra%02x s\n", ret);
        else {
            struct ParallelIOArg ioarg = { .buffer = &ret, .count = 1 };
            if (qemu_chr_fe_ioctl(&s->chr,
                                  CHR_IOCTL_PP_EPP_READ_ADDR, &ioarg)) {
                s->epp_timeout = 1;
                pdebug("ra%02x t\n", ret);
            }
            else
                pdebug("ra%02x\n", ret);
        }
        break;
    case PARA_REG_EPP_DATA:
        if ((s->control & (PARA_CTR_DIR | PARA_CTR_SIGNAL)) !=
            (PARA_CTR_DIR | PARA_CTR_INIT))
            /* Controls not correct for EPP data cycle, so do nothing */
            pdebug("re%02x s\n", ret);
        else {
            struct ParallelIOArg ioarg = { .buffer = &ret, .count = 1 };
            if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_READ, &ioarg)) {
                s->epp_timeout = 1;
                pdebug("re%02x t\n", ret);
            }
            else
                pdebug("re%02x\n", ret);
        }
        break;
    }
    trace_parallel_ioport_read("HW", addr, ret);
    s->last_read_offset = addr;
    return ret;
}

static uint32_t
parallel_ioport_eppdata_read_hw2(void *opaque, uint32_t addr)
{
    ParallelState *s = opaque;
    uint32_t ret;
    uint16_t eppdata = ~0;
    int err;
    struct ParallelIOArg ioarg = {
        .buffer = &eppdata, .count = sizeof(eppdata)
    };
    if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != (PARA_CTR_DIR|PARA_CTR_INIT)) {
        /* Controls not correct for EPP data cycle, so do nothing */
        pdebug("re%04x s\n", eppdata);
        return eppdata;
    }
    err = qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_READ, &ioarg);
    ret = le16_to_cpu(eppdata);

    if (err) {
        s->epp_timeout = 1;
        pdebug("re%04x t\n", ret);
    }
    else
        pdebug("re%04x\n", ret);
    trace_parallel_ioport_read("EPP", addr, ret);
    return ret;
}

static uint32_t
parallel_ioport_eppdata_read_hw4(void *opaque, uint32_t addr)
{
    ParallelState *s = opaque;
    uint32_t ret;
    uint32_t eppdata = ~0U;
    int err;
    struct ParallelIOArg ioarg = {
        .buffer = &eppdata, .count = sizeof(eppdata)
    };
    if ((s->control & (PARA_CTR_DIR|PARA_CTR_SIGNAL)) != (PARA_CTR_DIR|PARA_CTR_INIT)) {
        /* Controls not correct for EPP data cycle, so do nothing */
        pdebug("re%08x s\n", eppdata);
        return eppdata;
    }
    err = qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_EPP_READ, &ioarg);
    ret = le32_to_cpu(eppdata);

    if (err) {
        s->epp_timeout = 1;
        pdebug("re%08x t\n", ret);
    }
    else
        pdebug("re%08x\n", ret);
    trace_parallel_ioport_read("EPP", addr, ret);
    return ret;
}

static void parallel_ioport_ecp_write(void *opaque, uint32_t addr, uint32_t val)
{
    trace_parallel_ioport_write("ECP", addr & 7, val);
    pdebug("wecp%d=%02x\n", addr & 7, val);
}

static uint32_t parallel_ioport_ecp_read(void *opaque, uint32_t addr)
{
    uint8_t ret = 0xff;

    trace_parallel_ioport_read("ECP", addr & 7, ret);
    pdebug("recp%d:%02x\n", addr & 7, ret);
    return ret;
}

static void parallel_reset(void *opaque)
{
    ParallelState *s = opaque;

    s->datar = ~0;
    s->dataw = ~0;
    s->status = PARA_STS_BUSY;
    s->status |= PARA_STS_ACK;
    s->status |= PARA_STS_ONLINE;
    s->status |= PARA_STS_ERROR;
    s->status |= PARA_STS_TMOUT;
    s->control = PARA_CTR_SELECT;
    s->control |= PARA_CTR_INIT;
    s->control |= 0xc0;
    s->irq_pending = 0;
    s->hw_driver = 0;
    s->epp_timeout = 0;
    s->last_read_offset = ~0U;
}

static const int isa_parallel_io[MAX_PARALLEL_PORTS] = { 0x378, 0x278, 0x3bc };

static const MemoryRegionPortio isa_parallel_portio_hw_list[] = {
    { 0, 8, 1,
      .read = parallel_ioport_read_hw,
      .write = parallel_ioport_write_hw },
    { 4, 1, 2,
      .read = parallel_ioport_eppdata_read_hw2,
      .write = parallel_ioport_eppdata_write_hw2 },
    { 4, 1, 4,
      .read = parallel_ioport_eppdata_read_hw4,
      .write = parallel_ioport_eppdata_write_hw4 },
    { 0x400, 8, 1,
      .read = parallel_ioport_ecp_read,
      .write = parallel_ioport_ecp_write },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionPortio isa_parallel_portio_sw_list[] = {
    { 0, 8, 1,
      .read = parallel_ioport_read_sw,
      .write = parallel_ioport_write_sw },
    PORTIO_END_OF_LIST(),
};


static const VMStateDescription vmstate_parallel_isa = {
    .name = "parallel_isa",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(state.dataw, ISAParallelState),
        VMSTATE_UINT8(state.datar, ISAParallelState),
        VMSTATE_UINT8(state.status, ISAParallelState),
        VMSTATE_UINT8(state.control, ISAParallelState),
        VMSTATE_INT32(state.irq_pending, ISAParallelState),
        VMSTATE_INT32(state.epp_timeout, ISAParallelState),
        VMSTATE_END_OF_LIST()
    }
};

static int parallel_can_receive(void *opaque)
{
     return 1;
}

static void parallel_isa_realizefn(DeviceState *dev, Error **errp)
{
    static int index;
    ISADevice *isadev = ISA_DEVICE(dev);
    ISAParallelState *isa = ISA_PARALLEL(dev);
    ParallelState *s = &isa->state;
    int base;
    uint8_t dummy;

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(errp, "Can't create parallel device, empty char device");
        return;
    }

    if (isa->index == -1) {
        isa->index = index;
    }
    if (isa->index >= MAX_PARALLEL_PORTS) {
        error_setg(errp, "Max. supported number of parallel ports is %d.",
                   MAX_PARALLEL_PORTS);
        return;
    }
    if (isa->iobase == -1) {
        isa->iobase = isa_parallel_io[isa->index];
    }
    index++;

    base = isa->iobase;
    s->irq = isa_get_irq(isadev, isa->isairq);
    qemu_register_reset(parallel_reset, s);

    qemu_chr_fe_set_handlers(&s->chr, parallel_can_receive, NULL,
                             NULL, NULL, s, NULL, true);
    if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_PP_READ_STATUS, &dummy) == 0) {
        s->hw_driver = 1;
        s->status = dummy;
    }

    isa_register_portio_list(isadev, &isa->portio_list, base,
                             (s->hw_driver
                              ? &isa_parallel_portio_hw_list[0]
                              : &isa_parallel_portio_sw_list[0]),
                             s, "parallel");
}

static void parallel_isa_build_aml(AcpiDevAmlIf *adev, Aml *scope)
{
    ISAParallelState *isa = ISA_PARALLEL(adev);
    Aml *dev;
    Aml *crs;

    crs = aml_resource_template();
    aml_append(crs, aml_io(AML_DECODE16, isa->iobase, isa->iobase, 0x08, 0x08));
    aml_append(crs, aml_irq_no_flags(isa->isairq));

    dev = aml_device("LPT%d", isa->index + 1);
    aml_append(dev, aml_name_decl("_HID", aml_eisaid("PNP0400")));
    aml_append(dev, aml_name_decl("_UID", aml_int(isa->index + 1)));
    aml_append(dev, aml_name_decl("_STA", aml_int(0xf)));
    aml_append(dev, aml_name_decl("_CRS", crs));

    aml_append(scope, dev);
}

/* Memory mapped interface */
static uint64_t parallel_mm_readfn(void *opaque, hwaddr addr, unsigned size)
{
    ParallelState *s = opaque;

    return parallel_ioport_read_sw(s, addr >> s->it_shift) &
        MAKE_64BIT_MASK(0, size * 8);
}

static void parallel_mm_writefn(void *opaque, hwaddr addr,
                                uint64_t value, unsigned size)
{
    ParallelState *s = opaque;

    parallel_ioport_write_sw(s, addr >> s->it_shift,
                             value & MAKE_64BIT_MASK(0, size * 8));
}

static const MemoryRegionOps parallel_mm_ops = {
    .read = parallel_mm_readfn,
    .write = parallel_mm_writefn,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* If fd is zero, it means that the parallel device uses the console */
bool parallel_mm_init(MemoryRegion *address_space,
                      hwaddr base, int it_shift, qemu_irq irq,
                      Chardev *chr)
{
    ParallelState *s;

    s = g_new0(ParallelState, 1);
    s->irq = irq;
    qemu_chr_fe_init(&s->chr, chr, &error_abort);
    s->it_shift = it_shift;
    qemu_register_reset(parallel_reset, s);

    memory_region_init_io(&s->iomem, NULL, &parallel_mm_ops, s,
                          "parallel", 8 << it_shift);
    memory_region_add_subregion(address_space, base, &s->iomem);
    return true;
}

static const Property parallel_isa_properties[] = {
    DEFINE_PROP_UINT32("index", ISAParallelState, index,   -1),
    DEFINE_PROP_UINT32("iobase", ISAParallelState, iobase,  -1),
    DEFINE_PROP_UINT32("irq",   ISAParallelState, isairq,  7),
    DEFINE_PROP_CHR("chardev",  ISAParallelState, state.chr),
};

static void parallel_isa_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AcpiDevAmlIfClass *adevc = ACPI_DEV_AML_IF_CLASS(klass);

    dc->realize = parallel_isa_realizefn;
    dc->vmsd = &vmstate_parallel_isa;
    adevc->build_dev_aml = parallel_isa_build_aml;
    device_class_set_props(dc, parallel_isa_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo parallel_isa_info = {
    .name          = TYPE_ISA_PARALLEL,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(ISAParallelState),
    .class_init    = parallel_isa_class_initfn,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_ACPI_DEV_AML_IF },
        { },
    },
};

static void parallel_register_types(void)
{
    type_register_static(&parallel_isa_info);
}

type_init(parallel_register_types)
