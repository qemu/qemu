/*
 * HP-PARISC Lasi chipset emulation.
 *
 * (C) 2019 by Helge Deller <deller@gmx.de>
 *
 * This work is licensed under the GNU GPL license version 2 or later.
 *
 * Documentation available at:
 * https://parisc.wiki.kernel.org/images-parisc/7/79/Lasi_ers.pdf
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "trace.h"
#include "hw/irq.h"
#include "system/system.h"
#include "system/runstate.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "hw/misc/lasi.h"


static bool lasi_chip_mem_valid(void *opaque, hwaddr addr,
                                unsigned size, bool is_write,
                                MemTxAttrs attrs)
{
    bool ret = false;

    switch (addr) {
    case LASI_IRR:
    case LASI_IMR:
    case LASI_IPR:
    case LASI_ICR:
    case LASI_IAR:

    case LASI_LPT:
    case LASI_AUDIO:
    case LASI_AUDIO + 4:
    case LASI_UART:
    case LASI_LAN:
    case LASI_LAN + 12: /* LASI LAN MAC */
    case LASI_RTC:
    case LASI_FDC:

    case LASI_PCR ... LASI_AMR:
        ret = true;
    }

    trace_lasi_chip_mem_valid(addr, ret);
    return ret;
}

static MemTxResult lasi_chip_read_with_attrs(void *opaque, hwaddr addr,
                                             uint64_t *data, unsigned size,
                                             MemTxAttrs attrs)
{
    LasiState *s = opaque;
    MemTxResult ret = MEMTX_OK;
    uint32_t val;

    switch (addr) {
    case LASI_IRR:
        val = s->irr;
        break;
    case LASI_IMR:
        val = s->imr;
        break;
    case LASI_IPR:
        val = s->ipr;
        /* Any read to IPR clears the register.  */
        s->ipr = 0;
        break;
    case LASI_ICR:
        val = s->icr & ICR_BUS_ERROR_BIT; /* bus_error */
        break;
    case LASI_IAR:
        val = s->iar;
        break;

    case LASI_LPT:
    case LASI_UART:
    case LASI_LAN:
    case LASI_LAN + 12:
    case LASI_FDC:
        val = 0;
        break;
    case LASI_RTC:
        val = time(NULL);
        val += s->rtc_ref;
        break;

    case LASI_PCR:
    case LASI_VER:      /* only version 0 existed. */
    case LASI_IORESET:
        val = 0;
        break;
    case LASI_ERRLOG:
        val = s->errlog;
        break;
    case LASI_AMR:
        val = s->amr;
        break;

    default:
        /* Controlled by lasi_chip_mem_valid above. */
        g_assert_not_reached();
    }

    trace_lasi_chip_read(addr, val);

    *data = val;
    return ret;
}

static MemTxResult lasi_chip_write_with_attrs(void *opaque, hwaddr addr,
                                              uint64_t val, unsigned size,
                                              MemTxAttrs attrs)
{
    LasiState *s = opaque;

    trace_lasi_chip_write(addr, val);

    switch (addr) {
    case LASI_IRR:
        /* read-only.  */
        break;
    case LASI_IMR:
        s->imr = val;
        if (((val & LASI_IRQ_BITS) != val) && (val != 0xffffffff)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                "LASI: tried to set invalid %lx IMR value.\n",
                (unsigned long) val);
        }
        break;
    case LASI_IPR:
        /* Any write to IPR clears the register. */
        s->ipr = 0;
        break;
    case LASI_ICR:
        s->icr = val;
        /* if (val & ICR_TOC_BIT) issue_toc(); */
        break;
    case LASI_IAR:
        s->iar = val;
        break;

    case LASI_LPT:
        /* XXX: reset parallel port */
        break;
    case LASI_AUDIO:
    case LASI_AUDIO + 4:
        /* XXX: reset audio port */
        break;
    case LASI_UART:
        /* XXX: reset serial port */
        break;
    case LASI_LAN:
        /* XXX: reset LAN card */
        break;
    case LASI_FDC:
        /* XXX: reset Floppy controller */
        break;
    case LASI_RTC:
        s->rtc_ref = val - time(NULL);
        break;

    case LASI_PCR:
        if (val == 0x02) { /* immediately power off */
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        break;
    case LASI_ERRLOG:
        s->errlog = val;
        break;
    case LASI_VER:
        /* read-only.  */
        break;
    case LASI_IORESET:
        break;  /* XXX: TODO: Reset various devices. */
    case LASI_AMR:
        s->amr = val;
        break;

    default:
        /* Controlled by lasi_chip_mem_valid above. */
        g_assert_not_reached();
    }
    return MEMTX_OK;
}

static const MemoryRegionOps lasi_chip_ops = {
    .read_with_attrs = lasi_chip_read_with_attrs,
    .write_with_attrs = lasi_chip_write_with_attrs,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .accepts = lasi_chip_mem_valid,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const VMStateDescription vmstate_lasi = {
    .name = "Lasi",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(irr, LasiState),
        VMSTATE_UINT32(imr, LasiState),
        VMSTATE_UINT32(ipr, LasiState),
        VMSTATE_UINT32(icr, LasiState),
        VMSTATE_UINT32(iar, LasiState),
        VMSTATE_UINT32(errlog, LasiState),
        VMSTATE_UINT32(amr, LasiState),
        VMSTATE_UINT32_V(rtc_ref, LasiState, 2),
        VMSTATE_END_OF_LIST()
    }
};


static void lasi_set_irq(void *opaque, int irq, int level)
{
    LasiState *s = opaque;
    uint32_t bit = 1u << irq;

    if (level) {
        s->ipr |= bit;
        if (bit & s->imr) {
            uint32_t iar = s->iar;
            s->irr |= bit;
            if ((s->icr & ICR_BUS_ERROR_BIT) == 0) {
                stl_be_phys(&address_space_memory, iar & -32, iar & 31);
            }
        }
    }
}

static void lasi_reset(DeviceState *dev)
{
    LasiState *s = LASI_CHIP(dev);

    s->iar = 0xFFFB0000 + 3; /* CPU_HPA + 3 */

    /* Real time clock (RTC), it's only one 32-bit counter @9000 */
    s->rtc_ref = 0;
}

static void lasi_init(Object *obj)
{
    LasiState *s = LASI_CHIP(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->this_mem, OBJECT(s), &lasi_chip_ops,
                          s, "lasi", 0x100000);

    sysbus_init_mmio(sbd, &s->this_mem);

    qdev_init_gpio_in(DEVICE(obj), lasi_set_irq, LASI_IRQS);
}

static void lasi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, lasi_reset);
    dc->vmsd = &vmstate_lasi;
}

static const TypeInfo lasi_pcihost_info = {
    .name          = TYPE_LASI_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = lasi_init,
    .instance_size = sizeof(LasiState),
    .class_init    = lasi_class_init,
};

static void lasi_register_types(void)
{
    type_register_static(&lasi_pcihost_info);
}

type_init(lasi_register_types)
