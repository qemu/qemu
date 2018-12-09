/*
 * QEMU PowerPC sPAPR XIVE interrupt controller model
 *
 * Copyright (c) 2017-2018, IBM Corporation.
 *
 * This code is licensed under the GPL version 2 or later. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "target/ppc/cpu.h"
#include "sysemu/cpus.h"
#include "monitor/monitor.h"
#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_xive.h"
#include "hw/ppc/xive.h"
#include "hw/ppc/xive_regs.h"

/*
 * XIVE Virtualization Controller BAR and Thread Managment BAR that we
 * use for the ESB pages and the TIMA pages
 */
#define SPAPR_XIVE_VC_BASE   0x0006010000000000ull
#define SPAPR_XIVE_TM_BASE   0x0006030203180000ull

/*
 * The allocation of VP blocks is a complex operation in OPAL and the
 * VP identifiers have a relation with the number of HW chips, the
 * size of the VP blocks, VP grouping, etc. The QEMU sPAPR XIVE
 * controller model does not have the same constraints and can use a
 * simple mapping scheme of the CPU vcpu_id
 *
 * These identifiers are never returned to the OS.
 */

#define SPAPR_XIVE_NVT_BASE 0x400

/*
 * sPAPR NVT and END indexing helpers
 */
static uint32_t spapr_xive_nvt_to_target(uint8_t nvt_blk, uint32_t nvt_idx)
{
    return nvt_idx - SPAPR_XIVE_NVT_BASE;
}

/*
 * On sPAPR machines, use a simplified output for the XIVE END
 * structure dumping only the information related to the OS EQ.
 */
static void spapr_xive_end_pic_print_info(sPAPRXive *xive, XiveEND *end,
                                          Monitor *mon)
{
    uint32_t qindex = xive_get_field32(END_W1_PAGE_OFF, end->w1);
    uint32_t qgen = xive_get_field32(END_W1_GENERATION, end->w1);
    uint32_t qsize = xive_get_field32(END_W0_QSIZE, end->w0);
    uint32_t qentries = 1 << (qsize + 10);
    uint32_t nvt = xive_get_field32(END_W6_NVT_INDEX, end->w6);
    uint8_t priority = xive_get_field32(END_W7_F0_PRIORITY, end->w7);

    monitor_printf(mon, "%3d/%d % 6d/%5d ^%d",
                   spapr_xive_nvt_to_target(0, nvt),
                   priority, qindex, qentries, qgen);

    xive_end_queue_pic_print_info(end, 6, mon);
    monitor_printf(mon, "]");
}

void spapr_xive_pic_print_info(sPAPRXive *xive, Monitor *mon)
{
    XiveSource *xsrc = &xive->source;
    int i;

    monitor_printf(mon, "  LSIN         PQ    EISN     CPU/PRIO EQ\n");

    for (i = 0; i < xive->nr_irqs; i++) {
        uint8_t pq = xive_source_esb_get(xsrc, i);
        XiveEAS *eas = &xive->eat[i];

        if (!xive_eas_is_valid(eas)) {
            continue;
        }

        monitor_printf(mon, "  %08x %s %c%c%c %s %08x ", i,
                       xive_source_irq_is_lsi(xsrc, i) ? "LSI" : "MSI",
                       pq & XIVE_ESB_VAL_P ? 'P' : '-',
                       pq & XIVE_ESB_VAL_Q ? 'Q' : '-',
                       xsrc->status[i] & XIVE_STATUS_ASSERTED ? 'A' : ' ',
                       xive_eas_is_masked(eas) ? "M" : " ",
                       (int) xive_get_field64(EAS_END_DATA, eas->w));

        if (!xive_eas_is_masked(eas)) {
            uint32_t end_idx = xive_get_field64(EAS_END_INDEX, eas->w);
            XiveEND *end;

            assert(end_idx < xive->nr_ends);
            end = &xive->endt[end_idx];

            if (xive_end_is_valid(end)) {
                spapr_xive_end_pic_print_info(xive, end, mon);
            }
        }
        monitor_printf(mon, "\n");
    }
}

static void spapr_xive_map_mmio(sPAPRXive *xive)
{
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 0, xive->vc_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 1, xive->end_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(xive), 2, xive->tm_base);
}

static void spapr_xive_end_reset(XiveEND *end)
{
    memset(end, 0, sizeof(*end));

    /* switch off the escalation and notification ESBs */
    end->w1 = cpu_to_be32(END_W1_ESe_Q | END_W1_ESn_Q);
}

static void spapr_xive_reset(void *dev)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    int i;

    /*
     * The XiveSource has its own reset handler, which mask off all
     * IRQs (!P|Q)
     */

    /* Mask all valid EASs in the IRQ number space. */
    for (i = 0; i < xive->nr_irqs; i++) {
        XiveEAS *eas = &xive->eat[i];
        if (xive_eas_is_valid(eas)) {
            eas->w = cpu_to_be64(EAS_VALID | EAS_MASKED);
        } else {
            eas->w = 0;
        }
    }

    /* Clear all ENDs */
    for (i = 0; i < xive->nr_ends; i++) {
        spapr_xive_end_reset(&xive->endt[i]);
    }
}

static void spapr_xive_instance_init(Object *obj)
{
    sPAPRXive *xive = SPAPR_XIVE(obj);

    object_initialize(&xive->source, sizeof(xive->source), TYPE_XIVE_SOURCE);
    object_property_add_child(obj, "source", OBJECT(&xive->source), NULL);

    object_initialize(&xive->end_source, sizeof(xive->end_source),
                      TYPE_XIVE_END_SOURCE);
    object_property_add_child(obj, "end_source", OBJECT(&xive->end_source),
                              NULL);
}

static void spapr_xive_realize(DeviceState *dev, Error **errp)
{
    sPAPRXive *xive = SPAPR_XIVE(dev);
    XiveSource *xsrc = &xive->source;
    XiveENDSource *end_xsrc = &xive->end_source;
    Error *local_err = NULL;

    if (!xive->nr_irqs) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    if (!xive->nr_ends) {
        error_setg(errp, "Number of interrupt needs to be greater 0");
        return;
    }

    /*
     * Initialize the internal sources, for IPIs and virtual devices.
     */
    object_property_set_int(OBJECT(xsrc), xive->nr_irqs, "nr-irqs",
                            &error_fatal);
    object_property_add_const_link(OBJECT(xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * Initialize the END ESB source
     */
    object_property_set_int(OBJECT(end_xsrc), xive->nr_irqs, "nr-ends",
                            &error_fatal);
    object_property_add_const_link(OBJECT(end_xsrc), "xive", OBJECT(xive),
                                   &error_fatal);
    object_property_set_bool(OBJECT(end_xsrc), true, "realized", &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Set the mapping address of the END ESB pages after the source ESBs */
    xive->end_base = xive->vc_base + (1ull << xsrc->esb_shift) * xsrc->nr_irqs;

    /*
     * Allocate the routing tables
     */
    xive->eat = g_new0(XiveEAS, xive->nr_irqs);
    xive->endt = g_new0(XiveEND, xive->nr_ends);

    /* TIMA initialization */
    memory_region_init_io(&xive->tm_mmio, OBJECT(xive), &xive_tm_ops, xive,
                          "xive.tima", 4ull << TM_SHIFT);

    /* Define all XIVE MMIO regions on SysBus */
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xsrc->esb_mmio);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &end_xsrc->esb_mmio);
    sysbus_init_mmio(SYS_BUS_DEVICE(xive), &xive->tm_mmio);

    /* Map all regions */
    spapr_xive_map_mmio(xive);

    qemu_register_reset(spapr_xive_reset, dev);
}

static int spapr_xive_get_eas(XiveRouter *xrtr, uint8_t eas_blk,
                              uint32_t eas_idx, XiveEAS *eas)
{
    sPAPRXive *xive = SPAPR_XIVE(xrtr);

    if (eas_idx >= xive->nr_irqs) {
        return -1;
    }

    *eas = xive->eat[eas_idx];
    return 0;
}

static int spapr_xive_get_end(XiveRouter *xrtr,
                              uint8_t end_blk, uint32_t end_idx, XiveEND *end)
{
    sPAPRXive *xive = SPAPR_XIVE(xrtr);

    if (end_idx >= xive->nr_ends) {
        return -1;
    }

    memcpy(end, &xive->endt[end_idx], sizeof(XiveEND));
    return 0;
}

static int spapr_xive_write_end(XiveRouter *xrtr, uint8_t end_blk,
                                uint32_t end_idx, XiveEND *end,
                                uint8_t word_number)
{
    sPAPRXive *xive = SPAPR_XIVE(xrtr);

    if (end_idx >= xive->nr_ends) {
        return -1;
    }

    memcpy(&xive->endt[end_idx], end, sizeof(XiveEND));
    return 0;
}

static int spapr_xive_get_nvt(XiveRouter *xrtr,
                              uint8_t nvt_blk, uint32_t nvt_idx, XiveNVT *nvt)
{
    uint32_t vcpu_id = spapr_xive_nvt_to_target(nvt_blk, nvt_idx);
    PowerPCCPU *cpu = spapr_find_cpu(vcpu_id);

    if (!cpu) {
        /* TODO: should we assert() if we can find a NVT ? */
        return -1;
    }

    /*
     * sPAPR does not maintain a NVT table. Return that the NVT is
     * valid if we have found a matching CPU
     */
    nvt->w0 = cpu_to_be32(NVT_W0_VALID);
    return 0;
}

static int spapr_xive_write_nvt(XiveRouter *xrtr, uint8_t nvt_blk,
                                uint32_t nvt_idx, XiveNVT *nvt,
                                uint8_t word_number)
{
    /*
     * We don't need to write back to the NVTs because the sPAPR
     * machine should never hit a non-scheduled NVT. It should never
     * get called.
     */
    g_assert_not_reached();
}

static const VMStateDescription vmstate_spapr_xive_end = {
    .name = TYPE_SPAPR_XIVE "/end",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT32(w0, XiveEND),
        VMSTATE_UINT32(w1, XiveEND),
        VMSTATE_UINT32(w2, XiveEND),
        VMSTATE_UINT32(w3, XiveEND),
        VMSTATE_UINT32(w4, XiveEND),
        VMSTATE_UINT32(w5, XiveEND),
        VMSTATE_UINT32(w6, XiveEND),
        VMSTATE_UINT32(w7, XiveEND),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_xive_eas = {
    .name = TYPE_SPAPR_XIVE "/eas",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT64(w, XiveEAS),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_spapr_xive = {
    .name = TYPE_SPAPR_XIVE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_EQUAL(nr_irqs, sPAPRXive, NULL),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(eat, sPAPRXive, nr_irqs,
                                     vmstate_spapr_xive_eas, XiveEAS),
        VMSTATE_STRUCT_VARRAY_POINTER_UINT32(endt, sPAPRXive, nr_ends,
                                             vmstate_spapr_xive_end, XiveEND),
        VMSTATE_END_OF_LIST()
    },
};

static Property spapr_xive_properties[] = {
    DEFINE_PROP_UINT32("nr-irqs", sPAPRXive, nr_irqs, 0),
    DEFINE_PROP_UINT32("nr-ends", sPAPRXive, nr_ends, 0),
    DEFINE_PROP_UINT64("vc-base", sPAPRXive, vc_base, SPAPR_XIVE_VC_BASE),
    DEFINE_PROP_UINT64("tm-base", sPAPRXive, tm_base, SPAPR_XIVE_TM_BASE),
    DEFINE_PROP_END_OF_LIST(),
};

static void spapr_xive_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    XiveRouterClass *xrc = XIVE_ROUTER_CLASS(klass);

    dc->desc    = "sPAPR XIVE Interrupt Controller";
    dc->props   = spapr_xive_properties;
    dc->realize = spapr_xive_realize;
    dc->vmsd    = &vmstate_spapr_xive;

    xrc->get_eas = spapr_xive_get_eas;
    xrc->get_end = spapr_xive_get_end;
    xrc->write_end = spapr_xive_write_end;
    xrc->get_nvt = spapr_xive_get_nvt;
    xrc->write_nvt = spapr_xive_write_nvt;
}

static const TypeInfo spapr_xive_info = {
    .name = TYPE_SPAPR_XIVE,
    .parent = TYPE_XIVE_ROUTER,
    .instance_init = spapr_xive_instance_init,
    .instance_size = sizeof(sPAPRXive),
    .class_init = spapr_xive_class_init,
};

static void spapr_xive_register_types(void)
{
    type_register_static(&spapr_xive_info);
}

type_init(spapr_xive_register_types)

bool spapr_xive_irq_claim(sPAPRXive *xive, uint32_t lisn, bool lsi)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return false;
    }

    xive->eat[lisn].w |= cpu_to_be64(EAS_VALID);
    xive_source_irq_set(xsrc, lisn, lsi);
    return true;
}

bool spapr_xive_irq_free(sPAPRXive *xive, uint32_t lisn)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return false;
    }

    xive->eat[lisn].w &= cpu_to_be64(~EAS_VALID);
    xive_source_irq_set(xsrc, lisn, false);
    return true;
}

qemu_irq spapr_xive_qirq(sPAPRXive *xive, uint32_t lisn)
{
    XiveSource *xsrc = &xive->source;

    if (lisn >= xive->nr_irqs) {
        return NULL;
    }

    /* The sPAPR machine/device should have claimed the IRQ before */
    assert(xive_eas_is_valid(&xive->eat[lisn]));

    return xive_source_qirq(xsrc, lisn);
}
