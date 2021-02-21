/*
 * tpm_tis_isa.c - QEMU's TPM TIS ISA Device
 *
 * Copyright (C) 2006,2010-2013 IBM Corporation
 *
 * Authors:
 *  Stefan Berger <stefanb@us.ibm.com>
 *  David Safford <safford@us.ibm.com>
 *
 * Xen 4 support: Andrease Niederl <andreas.niederl@iaik.tugraz.at>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Implementation of the TIS interface according to specs found at
 * http://www.trustedcomputinggroup.org. This implementation currently
 * supports version 1.3, 21 March 2013
 * In the developers menu choose the PC Client section then find the TIS
 * specification.
 *
 * TPM TIS for TPM 2 implementation following TCG PC Client Platform
 * TPM Profile (PTP) Specification, Familiy 2.0, Revision 00.43
 */

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/acpi/tpm.h"
#include "tpm_prop.h"
#include "tpm_tis.h"
#include "qom/object.h"

struct TPMStateISA {
    /*< private >*/
    ISADevice parent_obj;

    /*< public >*/
    TPMState state; /* not a QOM object */
};

OBJECT_DECLARE_SIMPLE_TYPE(TPMStateISA, TPM_TIS_ISA)

static int tpm_tis_pre_save_isa(void *opaque)
{
    TPMStateISA *isadev = opaque;

    return tpm_tis_pre_save(&isadev->state);
}

static const VMStateDescription vmstate_tpm_tis_isa = {
    .name = "tpm-tis",
    .version_id = 0,
    .pre_save  = tpm_tis_pre_save_isa,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(state.buffer, TPMStateISA),
        VMSTATE_UINT16(state.rw_offset, TPMStateISA),
        VMSTATE_UINT8(state.active_locty, TPMStateISA),
        VMSTATE_UINT8(state.aborting_locty, TPMStateISA),
        VMSTATE_UINT8(state.next_locty, TPMStateISA),

        VMSTATE_STRUCT_ARRAY(state.loc, TPMStateISA, TPM_TIS_NUM_LOCALITIES, 0,
                             vmstate_locty, TPMLocality),

        VMSTATE_END_OF_LIST()
    }
};

static void tpm_tis_isa_request_completed(TPMIf *ti, int ret)
{
    TPMStateISA *isadev = TPM_TIS_ISA(ti);
    TPMState *s = &isadev->state;

    tpm_tis_request_completed(s, ret);
}

static enum TPMVersion tpm_tis_isa_get_tpm_version(TPMIf *ti)
{
    TPMStateISA *isadev = TPM_TIS_ISA(ti);
    TPMState *s = &isadev->state;

    return tpm_tis_get_tpm_version(s);
}

static void tpm_tis_isa_reset(DeviceState *dev)
{
    TPMStateISA *isadev = TPM_TIS_ISA(dev);
    TPMState *s = &isadev->state;

    return tpm_tis_reset(s);
}

static Property tpm_tis_isa_properties[] = {
    DEFINE_PROP_UINT32("irq", TPMStateISA, state.irq_num, TPM_TIS_IRQ),
    DEFINE_PROP_TPMBE("tpmdev", TPMStateISA, state.be_driver),
    DEFINE_PROP_BOOL("ppi", TPMStateISA, state.ppi_enabled, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void tpm_tis_isa_initfn(Object *obj)
{
    TPMStateISA *isadev = TPM_TIS_ISA(obj);
    TPMState *s = &isadev->state;

    memory_region_init_io(&s->mmio, obj, &tpm_tis_memory_ops,
                          s, "tpm-tis-mmio",
                          TPM_TIS_NUM_LOCALITIES << TPM_TIS_LOCALITY_SHIFT);
}

static void tpm_tis_isa_realizefn(DeviceState *dev, Error **errp)
{
    TPMStateISA *isadev = TPM_TIS_ISA(dev);
    TPMState *s = &isadev->state;

    if (!tpm_find()) {
        error_setg(errp, "at most one TPM device is permitted");
        return;
    }

    if (!s->be_driver) {
        error_setg(errp, "'tpmdev' property is required");
        return;
    }
    if (s->irq_num > 15) {
        error_setg(errp, "IRQ %d is outside valid range of 0 to 15",
                   s->irq_num);
        return;
    }

    isa_init_irq(ISA_DEVICE(dev), &s->irq, s->irq_num);

    memory_region_add_subregion(isa_address_space(ISA_DEVICE(dev)),
                                TPM_TIS_ADDR_BASE, &s->mmio);

    if (s->ppi_enabled) {
        tpm_ppi_init(&s->ppi, isa_address_space(ISA_DEVICE(dev)),
                     TPM_PPI_ADDR_BASE, OBJECT(dev));
    }
}

static void tpm_tis_isa_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    TPMIfClass *tc = TPM_IF_CLASS(klass);

    device_class_set_props(dc, tpm_tis_isa_properties);
    dc->vmsd  = &vmstate_tpm_tis_isa;
    tc->model = TPM_MODEL_TPM_TIS;
    dc->realize = tpm_tis_isa_realizefn;
    dc->reset = tpm_tis_isa_reset;
    tc->request_completed = tpm_tis_isa_request_completed;
    tc->get_version = tpm_tis_isa_get_tpm_version;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo tpm_tis_isa_info = {
    .name = TYPE_TPM_TIS_ISA,
    .parent = TYPE_ISA_DEVICE,
    .instance_size = sizeof(TPMStateISA),
    .instance_init = tpm_tis_isa_initfn,
    .class_init  = tpm_tis_isa_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_TPM_IF },
        { }
    }
};

static void tpm_tis_isa_register(void)
{
    type_register_static(&tpm_tis_isa_info);
}

type_init(tpm_tis_isa_register)
