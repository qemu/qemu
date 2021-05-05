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
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"
#include "hppa_sys.h"
#include "hw/net/lasi_82596.h"
#include "hw/char/parallel.h"
#include "hw/char/serial.h"
#include "hw/input/lasips2.h"
#include "migration/vmstate.h"
#include "qom/object.h"

#define TYPE_LASI_CHIP "lasi-chip"

#define LASI_IRR        0x00    /* RO */
#define LASI_IMR        0x04
#define LASI_IPR        0x08
#define LASI_ICR        0x0c
#define LASI_IAR        0x10

#define LASI_PCR        0x0C000 /* LASI Power Control register */
#define LASI_ERRLOG     0x0C004 /* LASI Error Logging register */
#define LASI_VER        0x0C008 /* LASI Version Control register */
#define LASI_IORESET    0x0C00C /* LASI I/O Reset register */
#define LASI_AMR        0x0C010 /* LASI Arbitration Mask register */
#define LASI_IO_CONF    0x7FFFE /* LASI primary configuration register */
#define LASI_IO_CONF2   0x7FFFF /* LASI secondary configuration register */

#define LASI_BIT(x)     (1ul << (x))
#define LASI_IRQ_BITS   (LASI_BIT(5) | LASI_BIT(7) | LASI_BIT(8) | LASI_BIT(9) \
            | LASI_BIT(13) | LASI_BIT(14) | LASI_BIT(16) | LASI_BIT(17) \
            | LASI_BIT(18) | LASI_BIT(19) | LASI_BIT(20) | LASI_BIT(21) \
            | LASI_BIT(26))

#define ICR_BUS_ERROR_BIT  LASI_BIT(8)  /* bit 8 in ICR */
#define ICR_TOC_BIT        LASI_BIT(1)  /* bit 1 in ICR */

OBJECT_DECLARE_SIMPLE_TYPE(LasiState, LASI_CHIP)

struct LasiState {
    PCIHostState parent_obj;

    uint32_t irr;
    uint32_t imr;
    uint32_t ipr;
    uint32_t icr;
    uint32_t iar;

    uint32_t errlog;
    uint32_t amr;
    uint32_t rtc;
    time_t rtc_ref;

    MemoryRegion this_mem;
};

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

    case (LASI_LAN_HPA - LASI_HPA):
    case (LASI_LPT_HPA - LASI_HPA):
    case (LASI_UART_HPA - LASI_HPA):
    case (LASI_RTC_HPA - LASI_HPA):

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

    case (LASI_LAN_HPA - LASI_HPA):
    case (LASI_LPT_HPA - LASI_HPA):
    case (LASI_UART_HPA - LASI_HPA):
        val = 0;
        break;
    case (LASI_RTC_HPA - LASI_HPA):
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
        if (((val & LASI_IRQ_BITS) != val) && (val != 0xffffffff))
            qemu_log_mask(LOG_GUEST_ERROR,
                "LASI: tried to set invalid %lx IMR value.\n",
                (unsigned long) val);
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

    case (LASI_LAN_HPA - LASI_HPA):
        /* XXX: reset LAN card */
        break;
    case (LASI_LPT_HPA - LASI_HPA):
        /* XXX: reset parallel port */
        break;
    case (LASI_UART_HPA - LASI_HPA):
        /* XXX: reset serial port */
        break;
    case (LASI_RTC_HPA - LASI_HPA):
        s->rtc_ref = val - time(NULL);
        break;

    case LASI_PCR:
        if (val == 0x02) /* immediately power off */
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
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
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(irr, LasiState),
        VMSTATE_UINT32(imr, LasiState),
        VMSTATE_UINT32(ipr, LasiState),
        VMSTATE_UINT32(icr, LasiState),
        VMSTATE_UINT32(iar, LasiState),
        VMSTATE_UINT32(errlog, LasiState),
        VMSTATE_UINT32(amr, LasiState),
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

static int lasi_get_irq(unsigned long hpa)
{
    switch (hpa) {
    case LASI_HPA:
        return 14;
    case LASI_UART_HPA:
        return 5;
    case LASI_LPT_HPA:
        return 7;
    case LASI_LAN_HPA:
        return 8;
    case LASI_SCSI_HPA:
        return 9;
    case LASI_AUDIO_HPA:
        return 13;
    case LASI_PS2KBD_HPA:
    case LASI_PS2MOU_HPA:
        return 26;
    default:
        g_assert_not_reached();
    }
}

DeviceState *lasi_init(MemoryRegion *address_space)
{
    DeviceState *dev;
    LasiState *s;

    dev = qdev_new(TYPE_LASI_CHIP);
    s = LASI_CHIP(dev);
    s->iar = CPU_HPA + 3;

    /* Lasi access from main memory.  */
    memory_region_init_io(&s->this_mem, OBJECT(s), &lasi_chip_ops,
                          s, "lasi", 0x100000);
    memory_region_add_subregion(address_space, LASI_HPA, &s->this_mem);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    /* LAN */
    if (enable_lasi_lan()) {
        qemu_irq lan_irq = qemu_allocate_irq(lasi_set_irq, s,
                lasi_get_irq(LASI_LAN_HPA));
        lasi_82596_init(address_space, LASI_LAN_HPA, lan_irq);
    }

    /* Parallel port */
    qemu_irq lpt_irq = qemu_allocate_irq(lasi_set_irq, s,
            lasi_get_irq(LASI_LPT_HPA));
    parallel_mm_init(address_space, LASI_LPT_HPA + 0x800, 0,
                     lpt_irq, parallel_hds[0]);

    /* Real time clock (RTC), it's only one 32-bit counter @9000 */

    s->rtc = time(NULL);
    s->rtc_ref = 0;

    if (serial_hd(1)) {
        /* Serial port */
        qemu_irq serial_irq = qemu_allocate_irq(lasi_set_irq, s,
                lasi_get_irq(LASI_UART_HPA));
        serial_mm_init(address_space, LASI_UART_HPA + 0x800, 0,
                serial_irq, 8000000 / 16,
                serial_hd(0), DEVICE_NATIVE_ENDIAN);
    }

    /* PS/2 Keyboard/Mouse */
    qemu_irq ps2kbd_irq = qemu_allocate_irq(lasi_set_irq, s,
            lasi_get_irq(LASI_PS2KBD_HPA));
    lasips2_init(address_space, LASI_PS2KBD_HPA,  ps2kbd_irq);

    return dev;
}

static void lasi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_lasi;
}

static const TypeInfo lasi_pcihost_info = {
    .name          = TYPE_LASI_CHIP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LasiState),
    .class_init    = lasi_class_init,
};

static void lasi_register_types(void)
{
    type_register_static(&lasi_pcihost_info);
}

type_init(lasi_register_types)
