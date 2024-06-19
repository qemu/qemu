/*
 *  IOAPIC emulation logic - common bits of emulated and KVM kernel model
 *
 *  Copyright (c) 2004-2005 Fabrice Bellard
 *  Copyright (c) 2009      Xiantao Zhang, Intel
 *  Copyright (c) 2011      Jan Kiszka, Siemens AG
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/intc/intc.h"
#include "hw/intc/ioapic.h"
#include "hw/intc/ioapic_internal.h"
#include "hw/sysbus.h"

/* ioapic_no count start from 0 to MAX_IOAPICS,
 * remove as static variable from ioapic_common_init.
 * now as a global variable, let child to increase the counter
 * then we can drop the 'instance_no' argument
 * and convert to our QOM's realize function
 */
int ioapic_no;

void ioapic_stat_update_irq(IOAPICCommonState *s, int irq, int level)
{
    if (level != s->irq_level[irq]) {
        s->irq_level[irq] = level;
        if (level == 1) {
            s->irq_count[irq]++;
        }
    }
}

static bool ioapic_get_statistics(InterruptStatsProvider *obj,
                                  uint64_t **irq_counts,
                                  unsigned int *nb_irqs)
{
    IOAPICCommonState *s = IOAPIC_COMMON(obj);

    *irq_counts = s->irq_count;
    *nb_irqs = IOAPIC_NUM_PINS;

    return true;
}

static void ioapic_irr_dump(GString *buf, const char *name, uint32_t bitmap)
{
    int i;

    g_string_append_printf(buf, "%-10s ", name);
    if (bitmap == 0) {
        g_string_append_printf(buf, "(none)\n");
        return;
    }
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        if (bitmap & (1 << i)) {
            g_string_append_printf(buf, "%-2u ", i);
        }
    }
    g_string_append_c(buf, '\n');
}

static void ioapic_print_redtbl(GString *buf, IOAPICCommonState *s)
{
    static const char *delm_str[] = {
        "fixed", "lowest", "SMI", "...", "NMI", "INIT", "...", "extINT"};
    uint32_t remote_irr = 0;
    int i;

    g_string_append_printf(buf, "ioapic0: ver=0x%x id=0x%02x sel=0x%02x",
                           s->version, s->id, s->ioregsel);
    if (s->ioregsel) {
        g_string_append_printf(buf, " (redir[%u])\n",
                               (s->ioregsel - IOAPIC_REG_REDTBL_BASE) >> 1);
    } else {
        g_string_append_c(buf, '\n');
    }
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        uint64_t entry = s->ioredtbl[i];
        uint32_t delm = (uint32_t)((entry & IOAPIC_LVT_DELIV_MODE) >>
                                   IOAPIC_LVT_DELIV_MODE_SHIFT);
        g_string_append_printf(buf, "  pin %-2u 0x%016"PRIx64" dest=%"PRIx64
                               " vec=%-3"PRIu64" %s %-5s %-6s %-6s %s\n",
                               i, entry,
                               (entry >> IOAPIC_LVT_DEST_SHIFT) &
                                    (entry & IOAPIC_LVT_DEST_MODE ? 0xff : 0xf),
                               entry & IOAPIC_VECTOR_MASK,
                               entry & IOAPIC_LVT_POLARITY
                                    ? "active-lo" : "active-hi",
                               entry & IOAPIC_LVT_TRIGGER_MODE
                                    ? "level" : "edge",
                               entry & IOAPIC_LVT_MASKED ? "masked" : "",
                               delm_str[delm],
                               entry & IOAPIC_LVT_DEST_MODE
                                    ? "logical" : "physical");

        remote_irr |= entry & IOAPIC_LVT_TRIGGER_MODE ?
                        (entry & IOAPIC_LVT_REMOTE_IRR ? (1 << i) : 0) : 0;
    }
    ioapic_irr_dump(buf, "  IRR", s->irr);
    ioapic_irr_dump(buf, "  Remote IRR", remote_irr);
}

void ioapic_reset_common(DeviceState *dev)
{
    IOAPICCommonState *s = IOAPIC_COMMON(dev);
    int i;

    s->id = 0;
    s->ioregsel = 0;
    s->irr = 0;
    for (i = 0; i < IOAPIC_NUM_PINS; i++) {
        s->ioredtbl[i] = 1 << IOAPIC_LVT_MASKED_SHIFT;
    }
}

static int ioapic_dispatch_pre_save(void *opaque)
{
    IOAPICCommonState *s = IOAPIC_COMMON(opaque);
    IOAPICCommonClass *info = IOAPIC_COMMON_GET_CLASS(s);

    if (info->pre_save) {
        info->pre_save(s);
    }

    return 0;
}

static int ioapic_dispatch_post_load(void *opaque, int version_id)
{
    IOAPICCommonState *s = IOAPIC_COMMON(opaque);
    IOAPICCommonClass *info = IOAPIC_COMMON_GET_CLASS(s);

    if (info->post_load) {
        info->post_load(s);
    }
    return 0;
}

static void ioapic_common_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    IOAPICCommonState *s = IOAPIC_COMMON(dev);
    IOAPICCommonClass *info;

    if (ioapic_no >= MAX_IOAPICS) {
        error_setg(errp, "Only %d ioapics allowed", MAX_IOAPICS);
        return;
    }

    info = IOAPIC_COMMON_GET_CLASS(s);
    info->realize(dev, errp);
    if (*errp) {
        return;
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->io_memory);
    ioapic_no++;
}

static void ioapic_print_info(InterruptStatsProvider *obj, GString *buf)
{
    IOAPICCommonState *s = IOAPIC_COMMON(obj);

    ioapic_dispatch_pre_save(s);
    ioapic_print_redtbl(buf, s);
}

static const VMStateDescription vmstate_ioapic_common = {
    .name = "ioapic",
    .version_id = 3,
    .minimum_version_id = 1,
    .pre_save = ioapic_dispatch_pre_save,
    .post_load = ioapic_dispatch_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(id, IOAPICCommonState),
        VMSTATE_UINT8(ioregsel, IOAPICCommonState),
        VMSTATE_UNUSED_V(2, 8), /* to account for qemu-kvm's v2 format */
        VMSTATE_UINT32_V(irr, IOAPICCommonState, 2),
        VMSTATE_UINT64_ARRAY(ioredtbl, IOAPICCommonState, IOAPIC_NUM_PINS),
        VMSTATE_END_OF_LIST()
    }
};

static void ioapic_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(klass);

    dc->realize = ioapic_common_realize;
    dc->vmsd = &vmstate_ioapic_common;
    ic->print_info = ioapic_print_info;
    ic->get_statistics = ioapic_get_statistics;
}

static const TypeInfo ioapic_common_type = {
    .name = TYPE_IOAPIC_COMMON,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IOAPICCommonState),
    .class_size = sizeof(IOAPICCommonClass),
    .class_init = ioapic_common_class_init,
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_INTERRUPT_STATS_PROVIDER },
        { }
    },
};

static void ioapic_common_register_types(void)
{
    type_register_static(&ioapic_common_type);
}

type_init(ioapic_common_register_types)
