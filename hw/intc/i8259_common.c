/*
 * QEMU 8259 - common bits of emulated and KVM kernel model
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2011      Jan Kiszka, Siemens AG
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
#include "hw/intc/i8259.h"
#include "hw/isa/i8259_internal.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"


void i8259_common_reset(I8259CommonState *cs)
{
    cs->last_irr = 0;
    cs->irr &= cs->elcr;
    cs->imr = 0;
    cs->isr = 0;
    cs->priority_add = 0;
    cs->irq_base = 0;
    cs->read_reg_select = 0;
    cs->poll = 0;
    cs->special_mask = 0;
    cs->init_state = 0;
    cs->auto_eoi = 0;
    cs->rotate_on_auto_eoi = 0;
    cs->special_fully_nested_mode = 0;
    cs->init4 = 0;
    cs->single_mode = 0;
    /* Note: ELCR and LTIM are not reset */
}

static int i8259_common_dispatch_pre_save(void *opaque)
{
    I8259CommonState *cs = opaque;
    I8259CommonClass *info = I8259_COMMON_GET_CLASS(cs);

    if (info->pre_save) {
        info->pre_save(cs);
    }

    return 0;
}

static int i8259_common_dispatch_post_load(void *opaque, int version_id)
{
    I8259CommonState *cs = opaque;
    I8259CommonClass *info = I8259_COMMON_GET_CLASS(cs);

    if (info->post_load) {
        info->post_load(cs);
    }
    return 0;
}

static void i8259_common_realize(DeviceState *dev, Error **errp)
{
    I8259CommonState *cs = I8259_COMMON(dev);
    ISADevice *isa = ISA_DEVICE(dev);

    isa_register_ioport(isa, &cs->base_io, cs->iobase);
    if (cs->elcr_addr != -1) {
        isa_register_ioport(isa, &cs->elcr_io, cs->elcr_addr);
    }

    qdev_set_legacy_instance_id(dev, cs->iobase, 1);
}

void i8259_stat_update_irq(uint64_t *irq_counts, int *irq_levels,
                           int irq, int level)
{
    if (level != irq_levels[irq]) {
        irq_levels[irq] = level;
        if (level == 1) {
            irq_counts[irq]++;
        }
    }
}

void i8259_pic_print_info(InterruptStatsProvider *obj, GString *buf)
{
    /* No information for i8259-based PICs */
    return;
}

static bool i8259_common_get_statistics(InterruptStatsProvider *obj,
                                        uint64_t **irq_counts,
                                        unsigned int *nb_irqs)
{
    /* No statistics for individual i8259s */
    *nb_irqs = 0;

    return true;
}

static void i8259_common_print_info(InterruptStatsProvider *obj, GString *buf)
{
    I8259CommonState *cs = I8259_COMMON(obj);

    i8259_common_dispatch_pre_save(cs);
    g_string_append_printf(buf,
                           "pic%d: irr=0x%02x imr=0x%02x isr=0x%02x hprio=%d "
                           "irq_base=0x%02x rr_sel=%d elcr=0x%02x fnm=%d\n",
                           cs->master ? 0 : 1, cs->irr, cs->imr, cs->isr,
                           cs->priority_add,
                           cs->irq_base, cs->read_reg_select, cs->elcr,
                           cs->special_fully_nested_mode);
}

static bool ltim_state_needed(void *opaque)
{
    I8259CommonState *cs = I8259_COMMON(opaque);

    return !!cs->ltim;
}

static const VMStateDescription vmstate_pic_ltim = {
    .name = "i8259/ltim",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ltim_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(ltim, I8259CommonState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_i8259_common = {
    .name = "i8259",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = i8259_common_dispatch_pre_save,
    .post_load = i8259_common_dispatch_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(last_irr, I8259CommonState),
        VMSTATE_UINT8(irr, I8259CommonState),
        VMSTATE_UINT8(imr, I8259CommonState),
        VMSTATE_UINT8(isr, I8259CommonState),
        VMSTATE_UINT8(priority_add, I8259CommonState),
        VMSTATE_UINT8(irq_base, I8259CommonState),
        VMSTATE_UINT8(read_reg_select, I8259CommonState),
        VMSTATE_UINT8(poll, I8259CommonState),
        VMSTATE_UINT8(special_mask, I8259CommonState),
        VMSTATE_UINT8(init_state, I8259CommonState),
        VMSTATE_UINT8(auto_eoi, I8259CommonState),
        VMSTATE_UINT8(rotate_on_auto_eoi, I8259CommonState),
        VMSTATE_UINT8(special_fully_nested_mode, I8259CommonState),
        VMSTATE_UINT8(init4, I8259CommonState),
        VMSTATE_UINT8(single_mode, I8259CommonState),
        VMSTATE_UINT8(elcr, I8259CommonState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_pic_ltim,
        NULL
    }
};

static const Property i8259_common_properties[] = {
    DEFINE_PROP_UINT32("iobase", I8259CommonState, iobase,  -1),
    DEFINE_PROP_UINT32("elcr_addr", I8259CommonState, elcr_addr,  -1),
    DEFINE_PROP_UINT8("elcr_mask", I8259CommonState, elcr_mask,  -1),
    DEFINE_PROP_BIT("master", I8259CommonState, master,  0, false),
};

static void i8259_common_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    InterruptStatsProviderClass *ic = INTERRUPT_STATS_PROVIDER_CLASS(klass);

    dc->vmsd = &vmstate_i8259_common;
    device_class_set_props(dc, i8259_common_properties);
    dc->realize = i8259_common_realize;
    /*
     * Reason: unlike ordinary ISA devices, the PICs need additional
     * wiring: its IRQ input lines are set up by board code, and the
     * wiring of the slave to the master is hard-coded in device model
     * code.
     */
    dc->user_creatable = false;
    ic->get_statistics = i8259_common_get_statistics;
    ic->print_info = i8259_common_print_info;
}

static const TypeInfo i8259_common_type_infos[] = {
    {
        .name = TYPE_I8259_COMMON,
        .parent = TYPE_ISA_DEVICE,
        .instance_size = sizeof(I8259CommonState),
        .class_size = sizeof(I8259CommonClass),
        .class_init = i8259_common_class_init,
        .abstract = true,
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_INTERRUPT_STATS_PROVIDER },
            { }
        },
    },
};

DEFINE_TYPES(i8259_common_type_infos)
