/*
 * QEMU SPAPR Dynamic Reconfiguration Connector Implementation
 *
 * Copyright IBM Corp. 2014
 *
 * Authors:
 *  Michael Roth      <mdroth@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qmp/qnull.h"
#include "cpu.h"
#include "qemu/cutils.h"
#include "hw/ppc/spapr_drc.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "hw/ppc/spapr.h" /* for RTAS return codes */
#include "hw/pci-host/spapr.h" /* spapr_phb_remove_pci_device_cb callback */
#include "hw/ppc/spapr_nvdimm.h"
#include "sysemu/device_tree.h"
#include "sysemu/reset.h"
#include "trace.h"

#define DRC_CONTAINER_PATH "/dr-connector"
#define DRC_INDEX_TYPE_SHIFT 28
#define DRC_INDEX_ID_MASK ((1ULL << DRC_INDEX_TYPE_SHIFT) - 1)

SpaprDrcType spapr_drc_type(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    return 1 << drck->typeshift;
}

uint32_t spapr_drc_index(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    /* no set format for a drc index: it only needs to be globally
     * unique. this is how we encode the DRC type on bare-metal
     * however, so might as well do that here
     */
    return (drck->typeshift << DRC_INDEX_TYPE_SHIFT)
        | (drc->id & DRC_INDEX_ID_MASK);
}

static uint32_t drc_isolate_physical(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_PHYSICAL_POWERON:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_PHYSICAL_CONFIGURED:
        break; /* see below */
    case SPAPR_DRC_STATE_PHYSICAL_UNISOLATE:
        return RTAS_OUT_PARAM_ERROR; /* not allowed */
    default:
        g_assert_not_reached();
    }

    drc->state = SPAPR_DRC_STATE_PHYSICAL_POWERON;

    if (drc->unplug_requested) {
        uint32_t drc_index = spapr_drc_index(drc);
        trace_spapr_drc_set_isolation_state_finalizing(drc_index);
        spapr_drc_detach(drc);
    }

    return RTAS_OUT_SUCCESS;
}

static uint32_t drc_unisolate_physical(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_PHYSICAL_UNISOLATE:
    case SPAPR_DRC_STATE_PHYSICAL_CONFIGURED:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_PHYSICAL_POWERON:
        break; /* see below */
    default:
        g_assert_not_reached();
    }

    /* cannot unisolate a non-existent resource, and, or resources
     * which are in an 'UNUSABLE' allocation state. (PAPR 2.7,
     * 13.5.3.5)
     */
    if (!drc->dev) {
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }

    drc->state = SPAPR_DRC_STATE_PHYSICAL_UNISOLATE;
    drc->ccs_offset = drc->fdt_start_offset;
    drc->ccs_depth = 0;

    return RTAS_OUT_SUCCESS;
}

static uint32_t drc_isolate_logical(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_LOGICAL_AVAILABLE:
    case SPAPR_DRC_STATE_LOGICAL_UNUSABLE:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_LOGICAL_CONFIGURED:
        break; /* see below */
    case SPAPR_DRC_STATE_LOGICAL_UNISOLATE:
        return RTAS_OUT_PARAM_ERROR; /* not allowed */
    default:
        g_assert_not_reached();
    }

    /*
     * Fail any requests to ISOLATE the LMB DRC if this LMB doesn't
     * belong to a DIMM device that is marked for removal.
     *
     * Currently the guest userspace tool drmgr that drives the memory
     * hotplug/unplug will just try to remove a set of 'removable' LMBs
     * in response to a hot unplug request that is based on drc-count.
     * If the LMB being removed doesn't belong to a DIMM device that is
     * actually being unplugged, fail the isolation request here.
     */
    if (spapr_drc_type(drc) == SPAPR_DR_CONNECTOR_TYPE_LMB
        && !drc->unplug_requested) {
        return RTAS_OUT_HW_ERROR;
    }

    drc->state = SPAPR_DRC_STATE_LOGICAL_AVAILABLE;

    /* if we're awaiting release, but still in an unconfigured state,
     * it's likely the guest is still in the process of configuring
     * the device and is transitioning the devices to an ISOLATED
     * state as a part of that process. so we only complete the
     * removal when this transition happens for a device in a
     * configured state, as suggested by the state diagram from PAPR+
     * 2.7, 13.4
     */
    if (drc->unplug_requested) {
        uint32_t drc_index = spapr_drc_index(drc);
        trace_spapr_drc_set_isolation_state_finalizing(drc_index);
        spapr_drc_detach(drc);
    }
    return RTAS_OUT_SUCCESS;
}

static uint32_t drc_unisolate_logical(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_LOGICAL_UNISOLATE:
    case SPAPR_DRC_STATE_LOGICAL_CONFIGURED:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_LOGICAL_AVAILABLE:
        break; /* see below */
    case SPAPR_DRC_STATE_LOGICAL_UNUSABLE:
        return RTAS_OUT_NO_SUCH_INDICATOR; /* not allowed */
    default:
        g_assert_not_reached();
    }

    /* Move to AVAILABLE state should have ensured device was present */
    g_assert(drc->dev);

    drc->state = SPAPR_DRC_STATE_LOGICAL_UNISOLATE;
    drc->ccs_offset = drc->fdt_start_offset;
    drc->ccs_depth = 0;

    return RTAS_OUT_SUCCESS;
}

static uint32_t drc_set_usable(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_LOGICAL_AVAILABLE:
    case SPAPR_DRC_STATE_LOGICAL_UNISOLATE:
    case SPAPR_DRC_STATE_LOGICAL_CONFIGURED:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_LOGICAL_UNUSABLE:
        break; /* see below */
    default:
        g_assert_not_reached();
    }

    /* if there's no resource/device associated with the DRC, there's
     * no way for us to put it in an allocation state consistent with
     * being 'USABLE'. PAPR 2.7, 13.5.3.4 documents that this should
     * result in an RTAS return code of -3 / "no such indicator"
     */
    if (!drc->dev) {
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }
    if (drc->unplug_requested) {
        /* Don't allow the guest to move a device away from UNUSABLE
         * state when we want to unplug it */
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }

    drc->state = SPAPR_DRC_STATE_LOGICAL_AVAILABLE;

    return RTAS_OUT_SUCCESS;
}

static uint32_t drc_set_unusable(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_LOGICAL_UNUSABLE:
        return RTAS_OUT_SUCCESS; /* Nothing to do */
    case SPAPR_DRC_STATE_LOGICAL_AVAILABLE:
        break; /* see below */
    case SPAPR_DRC_STATE_LOGICAL_UNISOLATE:
    case SPAPR_DRC_STATE_LOGICAL_CONFIGURED:
        return RTAS_OUT_NO_SUCH_INDICATOR; /* not allowed */
    default:
        g_assert_not_reached();
    }

    drc->state = SPAPR_DRC_STATE_LOGICAL_UNUSABLE;
    if (drc->unplug_requested) {
        uint32_t drc_index = spapr_drc_index(drc);
        trace_spapr_drc_set_allocation_state_finalizing(drc_index);
        spapr_drc_detach(drc);
    }

    return RTAS_OUT_SUCCESS;
}

static char *spapr_drc_name(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    /* human-readable name for a DRC to encode into the DT
     * description. this is mainly only used within a guest in place
     * of the unique DRC index.
     *
     * in the case of VIO/PCI devices, it corresponds to a "location
     * code" that maps a logical device/function (DRC index) to a
     * physical (or virtual in the case of VIO) location in the system
     * by chaining together the "location label" for each
     * encapsulating component.
     *
     * since this is more to do with diagnosing physical hardware
     * issues than guest compatibility, we choose location codes/DRC
     * names that adhere to the documented format, but avoid encoding
     * the entire topology information into the label/code, instead
     * just using the location codes based on the labels for the
     * endpoints (VIO/PCI adaptor connectors), which is basically just
     * "C" followed by an integer ID.
     *
     * DRC names as documented by PAPR+ v2.7, 13.5.2.4
     * location codes as documented by PAPR+ v2.7, 12.3.1.5
     */
    return g_strdup_printf("%s%d", drck->drc_name_prefix, drc->id);
}

/*
 * dr-entity-sense sensor value
 * returned via get-sensor-state RTAS calls
 * as expected by state diagram in PAPR+ 2.7, 13.4
 * based on the current allocation/indicator/power states
 * for the DR connector.
 */
static SpaprDREntitySense physical_entity_sense(SpaprDrc *drc)
{
    /* this assumes all PCI devices are assigned to a 'live insertion'
     * power domain, where QEMU manages power state automatically as
     * opposed to the guest. present, non-PCI resources are unaffected
     * by power state.
     */
    return drc->dev ? SPAPR_DR_ENTITY_SENSE_PRESENT
        : SPAPR_DR_ENTITY_SENSE_EMPTY;
}

static SpaprDREntitySense logical_entity_sense(SpaprDrc *drc)
{
    switch (drc->state) {
    case SPAPR_DRC_STATE_LOGICAL_UNUSABLE:
        return SPAPR_DR_ENTITY_SENSE_UNUSABLE;
    case SPAPR_DRC_STATE_LOGICAL_AVAILABLE:
    case SPAPR_DRC_STATE_LOGICAL_UNISOLATE:
    case SPAPR_DRC_STATE_LOGICAL_CONFIGURED:
        g_assert(drc->dev);
        return SPAPR_DR_ENTITY_SENSE_PRESENT;
    default:
        g_assert_not_reached();
    }
}

static void prop_get_index(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(obj);
    uint32_t value = spapr_drc_index(drc);
    visit_type_uint32(v, name, &value, errp);
}

static void prop_get_fdt(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(obj);
    QNull *null = NULL;
    Error *err = NULL;
    int fdt_offset_next, fdt_offset, fdt_depth;
    void *fdt;

    if (!drc->fdt) {
        visit_type_null(v, NULL, &null, errp);
        qobject_unref(null);
        return;
    }

    fdt = drc->fdt;
    fdt_offset = drc->fdt_start_offset;
    fdt_depth = 0;

    do {
        const char *name = NULL;
        const struct fdt_property *prop = NULL;
        int prop_len = 0, name_len = 0;
        uint32_t tag;

        tag = fdt_next_tag(fdt, fdt_offset, &fdt_offset_next);
        switch (tag) {
        case FDT_BEGIN_NODE:
            fdt_depth++;
            name = fdt_get_name(fdt, fdt_offset, &name_len);
            visit_start_struct(v, name, NULL, 0, &err);
            if (err) {
                error_propagate(errp, err);
                return;
            }
            break;
        case FDT_END_NODE:
            /* shouldn't ever see an FDT_END_NODE before FDT_BEGIN_NODE */
            g_assert(fdt_depth > 0);
            visit_check_struct(v, &err);
            visit_end_struct(v, NULL);
            if (err) {
                error_propagate(errp, err);
                return;
            }
            fdt_depth--;
            break;
        case FDT_PROP: {
            int i;
            prop = fdt_get_property_by_offset(fdt, fdt_offset, &prop_len);
            name = fdt_string(fdt, fdt32_to_cpu(prop->nameoff));
            visit_start_list(v, name, NULL, 0, &err);
            if (err) {
                error_propagate(errp, err);
                return;
            }
            for (i = 0; i < prop_len; i++) {
                visit_type_uint8(v, NULL, (uint8_t *)&prop->data[i], &err);
                if (err) {
                    error_propagate(errp, err);
                    return;
                }
            }
            visit_check_list(v, &err);
            visit_end_list(v, NULL);
            if (err) {
                error_propagate(errp, err);
                return;
            }
            break;
        }
        default:
            error_report("device FDT in unexpected state: %d", tag);
            abort();
        }
        fdt_offset = fdt_offset_next;
    } while (fdt_depth != 0);
}

void spapr_drc_attach(SpaprDrc *drc, DeviceState *d, Error **errp)
{
    trace_spapr_drc_attach(spapr_drc_index(drc));

    if (drc->dev) {
        error_setg(errp, "an attached device is still awaiting release");
        return;
    }
    g_assert((drc->state == SPAPR_DRC_STATE_LOGICAL_UNUSABLE)
             || (drc->state == SPAPR_DRC_STATE_PHYSICAL_POWERON));

    drc->dev = d;

    object_property_add_link(OBJECT(drc), "device",
                             object_get_typename(OBJECT(drc->dev)),
                             (Object **)(&drc->dev),
                             NULL, 0, NULL);
}

static void spapr_drc_release(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    drck->release(drc->dev);

    drc->unplug_requested = false;
    g_free(drc->fdt);
    drc->fdt = NULL;
    drc->fdt_start_offset = 0;
    object_property_del(OBJECT(drc), "device", &error_abort);
    drc->dev = NULL;
}

void spapr_drc_detach(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    trace_spapr_drc_detach(spapr_drc_index(drc));

    g_assert(drc->dev);

    drc->unplug_requested = true;

    if (drc->state != drck->empty_state) {
        trace_spapr_drc_awaiting_quiesce(spapr_drc_index(drc));
        return;
    }

    spapr_drc_release(drc);
}

void spapr_drc_reset(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    trace_spapr_drc_reset(spapr_drc_index(drc));

    /* immediately upon reset we can safely assume DRCs whose devices
     * are pending removal can be safely removed.
     */
    if (drc->unplug_requested) {
        spapr_drc_release(drc);
    }

    if (drc->dev) {
        /* A device present at reset is ready to go, same as coldplugged */
        drc->state = drck->ready_state;
        /*
         * Ensure that we are able to send the FDT fragment again
         * via configure-connector call if the guest requests.
         */
        drc->ccs_offset = drc->fdt_start_offset;
        drc->ccs_depth = 0;
    } else {
        drc->state = drck->empty_state;
        drc->ccs_offset = -1;
        drc->ccs_depth = -1;
    }
}

static bool spapr_drc_unplug_requested_needed(void *opaque)
{
    return spapr_drc_unplug_requested(opaque);
}

static const VMStateDescription vmstate_spapr_drc_unplug_requested = {
    .name = "spapr_drc/unplug_requested",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_drc_unplug_requested_needed,
    .fields  = (VMStateField []) {
        VMSTATE_BOOL(unplug_requested, SpaprDrc),
        VMSTATE_END_OF_LIST()
    }
};

bool spapr_drc_transient(SpaprDrc *drc)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    /*
     * If no dev is plugged in there is no need to migrate the DRC state
     * nor to reset the DRC at CAS.
     */
    if (!drc->dev) {
        return false;
    }

    /*
     * We need to reset the DRC at CAS or to migrate the DRC state if it's
     * not equal to the expected long-term state, which is the same as the
     * coldplugged initial state, or if an unplug request is pending.
     */
    return drc->state != drck->ready_state ||
        spapr_drc_unplug_requested(drc);
}

static bool spapr_drc_needed(void *opaque)
{
    return spapr_drc_transient(opaque);
}

static const VMStateDescription vmstate_spapr_drc = {
    .name = "spapr_drc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_drc_needed,
    .fields  = (VMStateField []) {
        VMSTATE_UINT32(state, SpaprDrc),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * []) {
        &vmstate_spapr_drc_unplug_requested,
        NULL
    }
};

static void realize(DeviceState *d, Error **errp)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(d);
    Object *root_container;
    gchar *link_name;
    gchar *child_name;
    Error *err = NULL;

    trace_spapr_drc_realize(spapr_drc_index(drc));
    /* NOTE: we do this as part of realize/unrealize due to the fact
     * that the guest will communicate with the DRC via RTAS calls
     * referencing the global DRC index. By unlinking the DRC
     * from DRC_CONTAINER_PATH/<drc_index> we effectively make it
     * inaccessible by the guest, since lookups rely on this path
     * existing in the composition tree
     */
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);
    link_name = g_strdup_printf("%x", spapr_drc_index(drc));
    child_name = object_get_canonical_path_component(OBJECT(drc));
    trace_spapr_drc_realize_child(spapr_drc_index(drc), child_name);
    object_property_add_alias(root_container, link_name,
                              drc->owner, child_name, &err);
    g_free(child_name);
    g_free(link_name);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    vmstate_register(VMSTATE_IF(drc), spapr_drc_index(drc), &vmstate_spapr_drc,
                     drc);
    trace_spapr_drc_realize_complete(spapr_drc_index(drc));
}

static void unrealize(DeviceState *d, Error **errp)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(d);
    Object *root_container;
    gchar *name;

    trace_spapr_drc_unrealize(spapr_drc_index(drc));
    vmstate_unregister(VMSTATE_IF(drc), &vmstate_spapr_drc, drc);
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);
    name = g_strdup_printf("%x", spapr_drc_index(drc));
    object_property_del(root_container, name, errp);
    g_free(name);
}

SpaprDrc *spapr_dr_connector_new(Object *owner, const char *type,
                                         uint32_t id)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(object_new(type));
    char *prop_name;

    drc->id = id;
    drc->owner = owner;
    prop_name = g_strdup_printf("dr-connector[%"PRIu32"]",
                                spapr_drc_index(drc));
    object_property_add_child(owner, prop_name, OBJECT(drc), &error_abort);
    object_unref(OBJECT(drc));
    object_property_set_bool(OBJECT(drc), true, "realized", NULL);
    g_free(prop_name);

    return drc;
}

static void spapr_dr_connector_instance_init(Object *obj)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(obj);
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    object_property_add_uint32_ptr(obj, "id", &drc->id, OBJ_PROP_FLAG_READ,
                                   NULL);
    object_property_add(obj, "index", "uint32", prop_get_index,
                        NULL, NULL, NULL, NULL);
    object_property_add(obj, "fdt", "struct", prop_get_fdt,
                        NULL, NULL, NULL, NULL);
    drc->state = drck->empty_state;
}

static void spapr_dr_connector_class_init(ObjectClass *k, void *data)
{
    DeviceClass *dk = DEVICE_CLASS(k);

    dk->realize = realize;
    dk->unrealize = unrealize;
    /*
     * Reason: it crashes FIXME find and document the real reason
     */
    dk->user_creatable = false;
}

static bool drc_physical_needed(void *opaque)
{
    SpaprDrcPhysical *drcp = (SpaprDrcPhysical *)opaque;
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(drcp);

    if ((drc->dev && (drcp->dr_indicator == SPAPR_DR_INDICATOR_ACTIVE))
        || (!drc->dev && (drcp->dr_indicator == SPAPR_DR_INDICATOR_INACTIVE))) {
        return false;
    }
    return true;
}

static const VMStateDescription vmstate_spapr_drc_physical = {
    .name = "spapr_drc/physical",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = drc_physical_needed,
    .fields  = (VMStateField []) {
        VMSTATE_UINT32(dr_indicator, SpaprDrcPhysical),
        VMSTATE_END_OF_LIST()
    }
};

static void drc_physical_reset(void *opaque)
{
    SpaprDrc *drc = SPAPR_DR_CONNECTOR(opaque);
    SpaprDrcPhysical *drcp = SPAPR_DRC_PHYSICAL(drc);

    if (drc->dev) {
        drcp->dr_indicator = SPAPR_DR_INDICATOR_ACTIVE;
    } else {
        drcp->dr_indicator = SPAPR_DR_INDICATOR_INACTIVE;
    }
}

static void realize_physical(DeviceState *d, Error **errp)
{
    SpaprDrcPhysical *drcp = SPAPR_DRC_PHYSICAL(d);
    Error *local_err = NULL;

    realize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_register(VMSTATE_IF(drcp),
                     spapr_drc_index(SPAPR_DR_CONNECTOR(drcp)),
                     &vmstate_spapr_drc_physical, drcp);
    qemu_register_reset(drc_physical_reset, drcp);
}

static void unrealize_physical(DeviceState *d, Error **errp)
{
    SpaprDrcPhysical *drcp = SPAPR_DRC_PHYSICAL(d);
    Error *local_err = NULL;

    unrealize(d, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_unregister(VMSTATE_IF(drcp), &vmstate_spapr_drc_physical, drcp);
    qemu_unregister_reset(drc_physical_reset, drcp);
}

static void spapr_drc_physical_class_init(ObjectClass *k, void *data)
{
    DeviceClass *dk = DEVICE_CLASS(k);
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    dk->realize = realize_physical;
    dk->unrealize = unrealize_physical;
    drck->dr_entity_sense = physical_entity_sense;
    drck->isolate = drc_isolate_physical;
    drck->unisolate = drc_unisolate_physical;
    drck->ready_state = SPAPR_DRC_STATE_PHYSICAL_CONFIGURED;
    drck->empty_state = SPAPR_DRC_STATE_PHYSICAL_POWERON;
}

static void spapr_drc_logical_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->dr_entity_sense = logical_entity_sense;
    drck->isolate = drc_isolate_logical;
    drck->unisolate = drc_unisolate_logical;
    drck->ready_state = SPAPR_DRC_STATE_LOGICAL_CONFIGURED;
    drck->empty_state = SPAPR_DRC_STATE_LOGICAL_UNUSABLE;
}

static void spapr_drc_cpu_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_CPU;
    drck->typename = "CPU";
    drck->drc_name_prefix = "CPU ";
    drck->release = spapr_core_release;
    drck->dt_populate = spapr_core_dt_populate;
}

static void spapr_drc_pci_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_PCI;
    drck->typename = "28";
    drck->drc_name_prefix = "C";
    drck->release = spapr_phb_remove_pci_device_cb;
    drck->dt_populate = spapr_pci_dt_populate;
}

static void spapr_drc_lmb_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_LMB;
    drck->typename = "MEM";
    drck->drc_name_prefix = "LMB ";
    drck->release = spapr_lmb_release;
    drck->dt_populate = spapr_lmb_dt_populate;
}

static void spapr_drc_phb_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_PHB;
    drck->typename = "PHB";
    drck->drc_name_prefix = "PHB ";
    drck->release = spapr_phb_release;
    drck->dt_populate = spapr_phb_dt_populate;
}

static void spapr_drc_pmem_class_init(ObjectClass *k, void *data)
{
    SpaprDrcClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_PMEM;
    drck->typename = "PMEM";
    drck->drc_name_prefix = "PMEM ";
    drck->release = NULL;
    drck->dt_populate = spapr_pmem_dt_populate;
}

static const TypeInfo spapr_dr_connector_info = {
    .name          = TYPE_SPAPR_DR_CONNECTOR,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(SpaprDrc),
    .instance_init = spapr_dr_connector_instance_init,
    .class_size    = sizeof(SpaprDrcClass),
    .class_init    = spapr_dr_connector_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_physical_info = {
    .name          = TYPE_SPAPR_DRC_PHYSICAL,
    .parent        = TYPE_SPAPR_DR_CONNECTOR,
    .instance_size = sizeof(SpaprDrcPhysical),
    .class_init    = spapr_drc_physical_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_logical_info = {
    .name          = TYPE_SPAPR_DRC_LOGICAL,
    .parent        = TYPE_SPAPR_DR_CONNECTOR,
    .class_init    = spapr_drc_logical_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_cpu_info = {
    .name          = TYPE_SPAPR_DRC_CPU,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .class_init    = spapr_drc_cpu_class_init,
};

static const TypeInfo spapr_drc_pci_info = {
    .name          = TYPE_SPAPR_DRC_PCI,
    .parent        = TYPE_SPAPR_DRC_PHYSICAL,
    .class_init    = spapr_drc_pci_class_init,
};

static const TypeInfo spapr_drc_lmb_info = {
    .name          = TYPE_SPAPR_DRC_LMB,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .class_init    = spapr_drc_lmb_class_init,
};

static const TypeInfo spapr_drc_phb_info = {
    .name          = TYPE_SPAPR_DRC_PHB,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .instance_size = sizeof(SpaprDrc),
    .class_init    = spapr_drc_phb_class_init,
};

static const TypeInfo spapr_drc_pmem_info = {
    .name          = TYPE_SPAPR_DRC_PMEM,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .class_init    = spapr_drc_pmem_class_init,
};

/* helper functions for external users */

SpaprDrc *spapr_drc_by_index(uint32_t index)
{
    Object *obj;
    gchar *name;

    name = g_strdup_printf("%s/%x", DRC_CONTAINER_PATH, index);
    obj = object_resolve_path(name, NULL);
    g_free(name);

    return !obj ? NULL : SPAPR_DR_CONNECTOR(obj);
}

SpaprDrc *spapr_drc_by_id(const char *type, uint32_t id)
{
    SpaprDrcClass *drck
        = SPAPR_DR_CONNECTOR_CLASS(object_class_by_name(type));

    return spapr_drc_by_index(drck->typeshift << DRC_INDEX_TYPE_SHIFT
                              | (id & DRC_INDEX_ID_MASK));
}

/**
 * spapr_dt_drc
 *
 * @fdt: libfdt device tree
 * @path: path in the DT to generate properties
 * @owner: parent Object/DeviceState for which to generate DRC
 *         descriptions for
 * @drc_type_mask: mask of SpaprDrcType values corresponding
 *   to the types of DRCs to generate entries for
 *
 * generate OF properties to describe DRC topology/indices to guests
 *
 * as documented in PAPR+ v2.1, 13.5.2
 */
int spapr_dt_drc(void *fdt, int offset, Object *owner, uint32_t drc_type_mask)
{
    Object *root_container;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    uint32_t drc_count = 0;
    GArray *drc_indexes, *drc_power_domains;
    GString *drc_names, *drc_types;
    int ret;

    /* the first entry of each properties is a 32-bit integer encoding
     * the number of elements in the array. we won't know this until
     * we complete the iteration through all the matching DRCs, but
     * reserve the space now and set the offsets accordingly so we
     * can fill them in later.
     */
    drc_indexes = g_array_new(false, true, sizeof(uint32_t));
    drc_indexes = g_array_set_size(drc_indexes, 1);
    drc_power_domains = g_array_new(false, true, sizeof(uint32_t));
    drc_power_domains = g_array_set_size(drc_power_domains, 1);
    drc_names = g_string_set_size(g_string_new(NULL), sizeof(uint32_t));
    drc_types = g_string_set_size(g_string_new(NULL), sizeof(uint32_t));

    /* aliases for all DRConnector objects will be rooted in QOM
     * composition tree at DRC_CONTAINER_PATH
     */
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);

    object_property_iter_init(&iter, root_container);
    while ((prop = object_property_iter_next(&iter))) {
        Object *obj;
        SpaprDrc *drc;
        SpaprDrcClass *drck;
        char *drc_name = NULL;
        uint32_t drc_index, drc_power_domain;

        if (!strstart(prop->type, "link<", NULL)) {
            continue;
        }

        obj = object_property_get_link(root_container, prop->name, NULL);
        drc = SPAPR_DR_CONNECTOR(obj);
        drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

        if (owner && (drc->owner != owner)) {
            continue;
        }

        if ((spapr_drc_type(drc) & drc_type_mask) == 0) {
            continue;
        }

        drc_count++;

        /* ibm,drc-indexes */
        drc_index = cpu_to_be32(spapr_drc_index(drc));
        g_array_append_val(drc_indexes, drc_index);

        /* ibm,drc-power-domains */
        drc_power_domain = cpu_to_be32(-1);
        g_array_append_val(drc_power_domains, drc_power_domain);

        /* ibm,drc-names */
        drc_name = spapr_drc_name(drc);
        drc_names = g_string_append(drc_names, drc_name);
        drc_names = g_string_insert_len(drc_names, -1, "\0", 1);
        g_free(drc_name);

        /* ibm,drc-types */
        drc_types = g_string_append(drc_types, drck->typename);
        drc_types = g_string_insert_len(drc_types, -1, "\0", 1);
    }

    /* now write the drc count into the space we reserved at the
     * beginning of the arrays previously
     */
    *(uint32_t *)drc_indexes->data = cpu_to_be32(drc_count);
    *(uint32_t *)drc_power_domains->data = cpu_to_be32(drc_count);
    *(uint32_t *)drc_names->str = cpu_to_be32(drc_count);
    *(uint32_t *)drc_types->str = cpu_to_be32(drc_count);

    ret = fdt_setprop(fdt, offset, "ibm,drc-indexes",
                      drc_indexes->data,
                      drc_indexes->len * sizeof(uint32_t));
    if (ret) {
        error_report("Couldn't create ibm,drc-indexes property");
        goto out;
    }

    ret = fdt_setprop(fdt, offset, "ibm,drc-power-domains",
                      drc_power_domains->data,
                      drc_power_domains->len * sizeof(uint32_t));
    if (ret) {
        error_report("Couldn't finalize ibm,drc-power-domains property");
        goto out;
    }

    ret = fdt_setprop(fdt, offset, "ibm,drc-names",
                      drc_names->str, drc_names->len);
    if (ret) {
        error_report("Couldn't finalize ibm,drc-names property");
        goto out;
    }

    ret = fdt_setprop(fdt, offset, "ibm,drc-types",
                      drc_types->str, drc_types->len);
    if (ret) {
        error_report("Couldn't finalize ibm,drc-types property");
        goto out;
    }

out:
    g_array_free(drc_indexes, true);
    g_array_free(drc_power_domains, true);
    g_string_free(drc_names, true);
    g_string_free(drc_types, true);

    return ret;
}

/*
 * RTAS calls
 */

static uint32_t rtas_set_isolation_state(uint32_t idx, uint32_t state)
{
    SpaprDrc *drc = spapr_drc_by_index(idx);
    SpaprDrcClass *drck;

    if (!drc) {
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }

    trace_spapr_drc_set_isolation_state(spapr_drc_index(drc), state);

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    switch (state) {
    case SPAPR_DR_ISOLATION_STATE_ISOLATED:
        return drck->isolate(drc);

    case SPAPR_DR_ISOLATION_STATE_UNISOLATED:
        return drck->unisolate(drc);

    default:
        return RTAS_OUT_PARAM_ERROR;
    }
}

static uint32_t rtas_set_allocation_state(uint32_t idx, uint32_t state)
{
    SpaprDrc *drc = spapr_drc_by_index(idx);

    if (!drc || !object_dynamic_cast(OBJECT(drc), TYPE_SPAPR_DRC_LOGICAL)) {
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }

    trace_spapr_drc_set_allocation_state(spapr_drc_index(drc), state);

    switch (state) {
    case SPAPR_DR_ALLOCATION_STATE_USABLE:
        return drc_set_usable(drc);

    case SPAPR_DR_ALLOCATION_STATE_UNUSABLE:
        return drc_set_unusable(drc);

    default:
        return RTAS_OUT_PARAM_ERROR;
    }
}

static uint32_t rtas_set_dr_indicator(uint32_t idx, uint32_t state)
{
    SpaprDrc *drc = spapr_drc_by_index(idx);

    if (!drc || !object_dynamic_cast(OBJECT(drc), TYPE_SPAPR_DRC_PHYSICAL)) {
        return RTAS_OUT_NO_SUCH_INDICATOR;
    }
    if ((state != SPAPR_DR_INDICATOR_INACTIVE)
        && (state != SPAPR_DR_INDICATOR_ACTIVE)
        && (state != SPAPR_DR_INDICATOR_IDENTIFY)
        && (state != SPAPR_DR_INDICATOR_ACTION)) {
        return RTAS_OUT_PARAM_ERROR; /* bad state parameter */
    }

    trace_spapr_drc_set_dr_indicator(idx, state);
    SPAPR_DRC_PHYSICAL(drc)->dr_indicator = state;
    return RTAS_OUT_SUCCESS;
}

static void rtas_set_indicator(PowerPCCPU *cpu, SpaprMachineState *spapr,
                               uint32_t token,
                               uint32_t nargs, target_ulong args,
                               uint32_t nret, target_ulong rets)
{
    uint32_t type, idx, state;
    uint32_t ret = RTAS_OUT_SUCCESS;

    if (nargs != 3 || nret != 1) {
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }

    type = rtas_ld(args, 0);
    idx = rtas_ld(args, 1);
    state = rtas_ld(args, 2);

    switch (type) {
    case RTAS_SENSOR_TYPE_ISOLATION_STATE:
        ret = rtas_set_isolation_state(idx, state);
        break;
    case RTAS_SENSOR_TYPE_DR:
        ret = rtas_set_dr_indicator(idx, state);
        break;
    case RTAS_SENSOR_TYPE_ALLOCATION_STATE:
        ret = rtas_set_allocation_state(idx, state);
        break;
    default:
        ret = RTAS_OUT_NOT_SUPPORTED;
    }

out:
    rtas_st(rets, 0, ret);
}

static void rtas_get_sensor_state(PowerPCCPU *cpu, SpaprMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    uint32_t sensor_state = 0;
    SpaprDrc *drc;
    SpaprDrcClass *drck;
    uint32_t ret = RTAS_OUT_SUCCESS;

    if (nargs != 2 || nret != 2) {
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }

    sensor_type = rtas_ld(args, 0);
    sensor_index = rtas_ld(args, 1);

    if (sensor_type != RTAS_SENSOR_TYPE_ENTITY_SENSE) {
        /* currently only DR-related sensors are implemented */
        trace_spapr_rtas_get_sensor_state_not_supported(sensor_index,
                                                        sensor_type);
        ret = RTAS_OUT_NOT_SUPPORTED;
        goto out;
    }

    drc = spapr_drc_by_index(sensor_index);
    if (!drc) {
        trace_spapr_rtas_get_sensor_state_invalid(sensor_index);
        ret = RTAS_OUT_PARAM_ERROR;
        goto out;
    }
    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    sensor_state = drck->dr_entity_sense(drc);

out:
    rtas_st(rets, 0, ret);
    rtas_st(rets, 1, sensor_state);
}

/* configure-connector work area offsets, int32_t units for field
 * indexes, bytes for field offset/len values.
 *
 * as documented by PAPR+ v2.7, 13.5.3.5
 */
#define CC_IDX_NODE_NAME_OFFSET 2
#define CC_IDX_PROP_NAME_OFFSET 2
#define CC_IDX_PROP_LEN 3
#define CC_IDX_PROP_DATA_OFFSET 4
#define CC_VAL_DATA_OFFSET ((CC_IDX_PROP_DATA_OFFSET + 1) * 4)
#define CC_WA_LEN 4096

static void configure_connector_st(target_ulong addr, target_ulong offset,
                                   const void *buf, size_t len)
{
    cpu_physical_memory_write(ppc64_phys_to_real(addr + offset),
                              buf, MIN(len, CC_WA_LEN - offset));
}

static void rtas_ibm_configure_connector(PowerPCCPU *cpu,
                                         SpaprMachineState *spapr,
                                         uint32_t token, uint32_t nargs,
                                         target_ulong args, uint32_t nret,
                                         target_ulong rets)
{
    uint64_t wa_addr;
    uint64_t wa_offset;
    uint32_t drc_index;
    SpaprDrc *drc;
    SpaprDrcClass *drck;
    SpaprDRCCResponse resp = SPAPR_DR_CC_RESPONSE_CONTINUE;
    int rc;

    if (nargs != 2 || nret != 1) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    wa_addr = ((uint64_t)rtas_ld(args, 1) << 32) | rtas_ld(args, 0);

    drc_index = rtas_ld(wa_addr, 0);
    drc = spapr_drc_by_index(drc_index);
    if (!drc) {
        trace_spapr_rtas_ibm_configure_connector_invalid(drc_index);
        rc = RTAS_OUT_PARAM_ERROR;
        goto out;
    }

    if ((drc->state != SPAPR_DRC_STATE_LOGICAL_UNISOLATE)
        && (drc->state != SPAPR_DRC_STATE_PHYSICAL_UNISOLATE)
        && (drc->state != SPAPR_DRC_STATE_LOGICAL_CONFIGURED)
        && (drc->state != SPAPR_DRC_STATE_PHYSICAL_CONFIGURED)) {
        /*
         * Need to unisolate the device before configuring
         * or it should already be in configured state to
         * allow configure-connector be called repeatedly.
         */
        rc = SPAPR_DR_CC_RESPONSE_NOT_CONFIGURABLE;
        goto out;
    }

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    if (!drc->fdt) {
        Error *local_err = NULL;
        void *fdt;
        int fdt_size;

        fdt = create_device_tree(&fdt_size);

        if (drck->dt_populate(drc, spapr, fdt, &drc->fdt_start_offset,
                              &local_err)) {
            g_free(fdt);
            error_free(local_err);
            rc = SPAPR_DR_CC_RESPONSE_ERROR;
            goto out;
        }

        drc->fdt = fdt;
        drc->ccs_offset = drc->fdt_start_offset;
        drc->ccs_depth = 0;
    }

    do {
        uint32_t tag;
        const char *name;
        const struct fdt_property *prop;
        int fdt_offset_next, prop_len;

        tag = fdt_next_tag(drc->fdt, drc->ccs_offset, &fdt_offset_next);

        switch (tag) {
        case FDT_BEGIN_NODE:
            drc->ccs_depth++;
            name = fdt_get_name(drc->fdt, drc->ccs_offset, NULL);

            /* provide the name of the next OF node */
            wa_offset = CC_VAL_DATA_OFFSET;
            rtas_st(wa_addr, CC_IDX_NODE_NAME_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, name, strlen(name) + 1);
            resp = SPAPR_DR_CC_RESPONSE_NEXT_CHILD;
            break;
        case FDT_END_NODE:
            drc->ccs_depth--;
            if (drc->ccs_depth == 0) {
                uint32_t drc_index = spapr_drc_index(drc);

                /* done sending the device tree, move to configured state */
                trace_spapr_drc_set_configured(drc_index);
                drc->state = drck->ready_state;
                /*
                 * Ensure that we are able to send the FDT fragment
                 * again via configure-connector call if the guest requests.
                 */
                drc->ccs_offset = drc->fdt_start_offset;
                drc->ccs_depth = 0;
                fdt_offset_next = drc->fdt_start_offset;
                resp = SPAPR_DR_CC_RESPONSE_SUCCESS;
            } else {
                resp = SPAPR_DR_CC_RESPONSE_PREV_PARENT;
            }
            break;
        case FDT_PROP:
            prop = fdt_get_property_by_offset(drc->fdt, drc->ccs_offset,
                                              &prop_len);
            name = fdt_string(drc->fdt, fdt32_to_cpu(prop->nameoff));

            /* provide the name of the next OF property */
            wa_offset = CC_VAL_DATA_OFFSET;
            rtas_st(wa_addr, CC_IDX_PROP_NAME_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, name, strlen(name) + 1);

            /* provide the length and value of the OF property. data gets
             * placed immediately after NULL terminator of the OF property's
             * name string
             */
            wa_offset += strlen(name) + 1,
            rtas_st(wa_addr, CC_IDX_PROP_LEN, prop_len);
            rtas_st(wa_addr, CC_IDX_PROP_DATA_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, prop->data, prop_len);
            resp = SPAPR_DR_CC_RESPONSE_NEXT_PROPERTY;
            break;
        case FDT_END:
            resp = SPAPR_DR_CC_RESPONSE_ERROR;
        default:
            /* keep seeking for an actionable tag */
            break;
        }
        if (drc->ccs_offset >= 0) {
            drc->ccs_offset = fdt_offset_next;
        }
    } while (resp == SPAPR_DR_CC_RESPONSE_CONTINUE);

    rc = resp;
out:
    rtas_st(rets, 0, rc);
}

static void spapr_drc_register_types(void)
{
    type_register_static(&spapr_dr_connector_info);
    type_register_static(&spapr_drc_physical_info);
    type_register_static(&spapr_drc_logical_info);
    type_register_static(&spapr_drc_cpu_info);
    type_register_static(&spapr_drc_pci_info);
    type_register_static(&spapr_drc_lmb_info);
    type_register_static(&spapr_drc_phb_info);
    type_register_static(&spapr_drc_pmem_info);

    spapr_rtas_register(RTAS_SET_INDICATOR, "set-indicator",
                        rtas_set_indicator);
    spapr_rtas_register(RTAS_GET_SENSOR_STATE, "get-sensor-state",
                        rtas_get_sensor_state);
    spapr_rtas_register(RTAS_IBM_CONFIGURE_CONNECTOR, "ibm,configure-connector",
                        rtas_ibm_configure_connector);
}
type_init(spapr_drc_register_types)
