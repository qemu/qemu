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
#include "cpu.h"
#include "qemu/cutils.h"
#include "hw/ppc/spapr_drc.h"
#include "qom/object.h"
#include "hw/qdev.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "hw/ppc/spapr.h" /* for RTAS return codes */
#include "hw/pci-host/spapr.h" /* spapr_phb_remove_pci_device_cb callback */
#include "trace.h"

#define DRC_CONTAINER_PATH "/dr-connector"
#define DRC_INDEX_TYPE_SHIFT 28
#define DRC_INDEX_ID_MASK ((1ULL << DRC_INDEX_TYPE_SHIFT) - 1)

sPAPRDRConnectorType spapr_drc_type(sPAPRDRConnector *drc)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    return 1 << drck->typeshift;
}

uint32_t spapr_drc_index(sPAPRDRConnector *drc)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    /* no set format for a drc index: it only needs to be globally
     * unique. this is how we encode the DRC type on bare-metal
     * however, so might as well do that here
     */
    return (drck->typeshift << DRC_INDEX_TYPE_SHIFT)
        | (drc->id & DRC_INDEX_ID_MASK);
}

static uint32_t set_isolation_state(sPAPRDRConnector *drc,
                                    sPAPRDRIsolationState state)
{
    trace_spapr_drc_set_isolation_state(spapr_drc_index(drc), state);

    /* if the guest is configuring a device attached to this DRC, we
     * should reset the configuration state at this point since it may
     * no longer be reliable (guest released device and needs to start
     * over, or unplug occurred so the FDT is no longer valid)
     */
    if (state == SPAPR_DR_ISOLATION_STATE_ISOLATED) {
        g_free(drc->ccs);
        drc->ccs = NULL;
    }

    if (state == SPAPR_DR_ISOLATION_STATE_UNISOLATED) {
        /* cannot unisolate a non-existent resource, and, or resources
         * which are in an 'UNUSABLE' allocation state. (PAPR 2.7, 13.5.3.5)
         */
        if (!drc->dev ||
            drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
            return RTAS_OUT_NO_SUCH_INDICATOR;
        }
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
    if (spapr_drc_type(drc) == SPAPR_DR_CONNECTOR_TYPE_LMB) {
        if ((state == SPAPR_DR_ISOLATION_STATE_ISOLATED) &&
             !drc->awaiting_release) {
            return RTAS_OUT_HW_ERROR;
        }
    }

    drc->isolation_state = state;

    if (drc->isolation_state == SPAPR_DR_ISOLATION_STATE_ISOLATED) {
        /* if we're awaiting release, but still in an unconfigured state,
         * it's likely the guest is still in the process of configuring
         * the device and is transitioning the devices to an ISOLATED
         * state as a part of that process. so we only complete the
         * removal when this transition happens for a device in a
         * configured state, as suggested by the state diagram from
         * PAPR+ 2.7, 13.4
         */
        if (drc->awaiting_release) {
            uint32_t drc_index = spapr_drc_index(drc);
            if (drc->configured) {
                trace_spapr_drc_set_isolation_state_finalizing(drc_index);
                spapr_drc_detach(drc, DEVICE(drc->dev), NULL);
            } else {
                trace_spapr_drc_set_isolation_state_deferring(drc_index);
            }
        }
        drc->configured = false;
    }

    return RTAS_OUT_SUCCESS;
}

static uint32_t set_allocation_state(sPAPRDRConnector *drc,
                                     sPAPRDRAllocationState state)
{
    trace_spapr_drc_set_allocation_state(spapr_drc_index(drc), state);

    if (state == SPAPR_DR_ALLOCATION_STATE_USABLE) {
        /* if there's no resource/device associated with the DRC, there's
         * no way for us to put it in an allocation state consistent with
         * being 'USABLE'. PAPR 2.7, 13.5.3.4 documents that this should
         * result in an RTAS return code of -3 / "no such indicator"
         */
        if (!drc->dev) {
            return RTAS_OUT_NO_SUCH_INDICATOR;
        }
    }

    if (spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->allocation_state = state;
        if (drc->awaiting_release &&
            drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
            uint32_t drc_index = spapr_drc_index(drc);
            trace_spapr_drc_set_allocation_state_finalizing(drc_index);
            spapr_drc_detach(drc, DEVICE(drc->dev), NULL);
        } else if (drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_USABLE) {
            drc->awaiting_allocation = false;
        }
    }
    return RTAS_OUT_SUCCESS;
}

static const char *spapr_drc_name(sPAPRDRConnector *drc)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

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

/* has the guest been notified of device attachment? */
static void set_signalled(sPAPRDRConnector *drc)
{
    drc->signalled = true;
}

/*
 * dr-entity-sense sensor value
 * returned via get-sensor-state RTAS calls
 * as expected by state diagram in PAPR+ 2.7, 13.4
 * based on the current allocation/indicator/power states
 * for the DR connector.
 */
static sPAPRDREntitySense physical_entity_sense(sPAPRDRConnector *drc)
{
    /* this assumes all PCI devices are assigned to a 'live insertion'
     * power domain, where QEMU manages power state automatically as
     * opposed to the guest. present, non-PCI resources are unaffected
     * by power state.
     */
    return drc->dev ? SPAPR_DR_ENTITY_SENSE_PRESENT
        : SPAPR_DR_ENTITY_SENSE_EMPTY;
}

static sPAPRDREntitySense logical_entity_sense(sPAPRDRConnector *drc)
{
    if (drc->dev
        && (drc->allocation_state != SPAPR_DR_ALLOCATION_STATE_UNUSABLE)) {
        return SPAPR_DR_ENTITY_SENSE_PRESENT;
    } else {
        return SPAPR_DR_ENTITY_SENSE_UNUSABLE;
    }
}

static void prop_get_index(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    uint32_t value = spapr_drc_index(drc);
    visit_type_uint32(v, name, &value, errp);
}

static void prop_get_fdt(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    Error *err = NULL;
    int fdt_offset_next, fdt_offset, fdt_depth;
    void *fdt;

    if (!drc->fdt) {
        visit_type_null(v, NULL, errp);
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
            error_setg(&error_abort, "device FDT in unexpected state: %d", tag);
        }
        fdt_offset = fdt_offset_next;
    } while (fdt_depth != 0);
}

void spapr_drc_attach(sPAPRDRConnector *drc, DeviceState *d, void *fdt,
                      int fdt_start_offset, bool coldplug, Error **errp)
{
    trace_spapr_drc_attach(spapr_drc_index(drc));

    if (drc->isolation_state != SPAPR_DR_ISOLATION_STATE_ISOLATED) {
        error_setg(errp, "an attached device is still awaiting release");
        return;
    }
    if (spapr_drc_type(drc) == SPAPR_DR_CONNECTOR_TYPE_PCI) {
        g_assert(drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_USABLE);
    }
    g_assert(fdt || coldplug);

    /* NOTE: setting initial isolation state to UNISOLATED means we can't
     * detach unless guest has a userspace/kernel that moves this state
     * back to ISOLATED in response to an unplug event, or this is done
     * manually by the admin prior. if we force things while the guest
     * may be accessing the device, we can easily crash the guest, so we
     * we defer completion of removal in such cases to the reset() hook.
     */
    if (spapr_drc_type(drc) == SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->isolation_state = SPAPR_DR_ISOLATION_STATE_UNISOLATED;
    }
    drc->dr_indicator = SPAPR_DR_INDICATOR_ACTIVE;

    drc->dev = d;
    drc->fdt = fdt;
    drc->fdt_start_offset = fdt_start_offset;
    drc->configured = coldplug;
    /* 'logical' DR resources such as memory/cpus are in some cases treated
     * as a pool of resources from which the guest is free to choose from
     * based on only a count. for resources that can be assigned in this
     * fashion, we must assume the resource is signalled immediately
     * since a single hotplug request might make an arbitrary number of
     * such attached resources available to the guest, as opposed to
     * 'physical' DR resources such as PCI where each device/resource is
     * signalled individually.
     */
    drc->signalled = (spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PCI)
                     ? true : coldplug;

    if (spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->awaiting_allocation = true;
    }

    object_property_add_link(OBJECT(drc), "device",
                             object_get_typename(OBJECT(drc->dev)),
                             (Object **)(&drc->dev),
                             NULL, 0, NULL);
}

void spapr_drc_detach(sPAPRDRConnector *drc, DeviceState *d, Error **errp)
{
    trace_spapr_drc_detach(spapr_drc_index(drc));

    /* if we've signalled device presence to the guest, or if the guest
     * has gone ahead and configured the device (via manually-executed
     * device add via drmgr in guest, namely), we need to wait
     * for the guest to quiesce the device before completing detach.
     * Otherwise, we can assume the guest hasn't seen it and complete the
     * detach immediately. Note that there is a small race window
     * just before, or during, configuration, which is this context
     * refers mainly to fetching the device tree via RTAS.
     * During this window the device access will be arbitrated by
     * associated DRC, which will simply fail the RTAS calls as invalid.
     * This is recoverable within guest and current implementations of
     * drmgr should be able to cope.
     */
    if (!drc->signalled && !drc->configured) {
        /* if the guest hasn't seen the device we can't rely on it to
         * set it back to an isolated state via RTAS, so do it here manually
         */
        drc->isolation_state = SPAPR_DR_ISOLATION_STATE_ISOLATED;
    }

    if (drc->isolation_state != SPAPR_DR_ISOLATION_STATE_ISOLATED) {
        trace_spapr_drc_awaiting_isolated(spapr_drc_index(drc));
        drc->awaiting_release = true;
        return;
    }

    if (spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PCI &&
        drc->allocation_state != SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
        trace_spapr_drc_awaiting_unusable(spapr_drc_index(drc));
        drc->awaiting_release = true;
        return;
    }

    if (drc->awaiting_allocation) {
        drc->awaiting_release = true;
        trace_spapr_drc_awaiting_allocation(spapr_drc_index(drc));
        return;
    }

    drc->dr_indicator = SPAPR_DR_INDICATOR_INACTIVE;

    /* Calling release callbacks based on spapr_drc_type(drc). */
    switch (spapr_drc_type(drc)) {
    case SPAPR_DR_CONNECTOR_TYPE_CPU:
        spapr_core_release(drc->dev);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PCI:
        spapr_phb_remove_pci_device_cb(drc->dev);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_LMB:
        spapr_lmb_release(drc->dev);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PHB:
    case SPAPR_DR_CONNECTOR_TYPE_VIO:
    default:
        g_assert(false);
    }

    drc->awaiting_release = false;
    g_free(drc->fdt);
    drc->fdt = NULL;
    drc->fdt_start_offset = 0;
    object_property_del(OBJECT(drc), "device", NULL);
    drc->dev = NULL;
}

static bool release_pending(sPAPRDRConnector *drc)
{
    return drc->awaiting_release;
}

static void reset(DeviceState *d)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    trace_spapr_drc_reset(spapr_drc_index(drc));

    g_free(drc->ccs);
    drc->ccs = NULL;

    /* immediately upon reset we can safely assume DRCs whose devices
     * are pending removal can be safely removed, and that they will
     * subsequently be left in an ISOLATED state. move the DRC to this
     * state in these cases (which will in turn complete any pending
     * device removals)
     */
    if (drc->awaiting_release) {
        drck->set_isolation_state(drc, SPAPR_DR_ISOLATION_STATE_ISOLATED);
        /* generally this should also finalize the removal, but if the device
         * hasn't yet been configured we normally defer removal under the
         * assumption that this transition is taking place as part of device
         * configuration. so check if we're still waiting after this, and
         * force removal if we are
         */
        if (drc->awaiting_release) {
            spapr_drc_detach(drc, DEVICE(drc->dev), NULL);
        }

        /* non-PCI devices may be awaiting a transition to UNUSABLE */
        if (spapr_drc_type(drc) != SPAPR_DR_CONNECTOR_TYPE_PCI &&
            drc->awaiting_release) {
            drck->set_allocation_state(drc, SPAPR_DR_ALLOCATION_STATE_UNUSABLE);
        }
    }

    if (drck->dr_entity_sense(drc) == SPAPR_DR_ENTITY_SENSE_PRESENT) {
        drck->set_signalled(drc);
    }
}

static bool spapr_drc_needed(void *opaque)
{
    sPAPRDRConnector *drc = (sPAPRDRConnector *)opaque;
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    bool rc = false;
    sPAPRDREntitySense value = drck->dr_entity_sense(drc);

    /* If no dev is plugged in there is no need to migrate the DRC state */
    if (value != SPAPR_DR_ENTITY_SENSE_PRESENT) {
        return false;
    }

    /*
     * If there is dev plugged in, we need to migrate the DRC state when
     * it is different from cold-plugged state
     */
    switch (spapr_drc_type(drc)) {
    case SPAPR_DR_CONNECTOR_TYPE_PCI:
    case SPAPR_DR_CONNECTOR_TYPE_CPU:
    case SPAPR_DR_CONNECTOR_TYPE_LMB:
        rc = !((drc->isolation_state == SPAPR_DR_ISOLATION_STATE_UNISOLATED) &&
               (drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_USABLE) &&
               drc->configured && drc->signalled && !drc->awaiting_release);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PHB:
    case SPAPR_DR_CONNECTOR_TYPE_VIO:
    default:
        g_assert_not_reached();
    }
    return rc;
}

static const VMStateDescription vmstate_spapr_drc = {
    .name = "spapr_drc",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = spapr_drc_needed,
    .fields  = (VMStateField []) {
        VMSTATE_UINT32(isolation_state, sPAPRDRConnector),
        VMSTATE_UINT32(allocation_state, sPAPRDRConnector),
        VMSTATE_UINT32(dr_indicator, sPAPRDRConnector),
        VMSTATE_BOOL(configured, sPAPRDRConnector),
        VMSTATE_BOOL(awaiting_release, sPAPRDRConnector),
        VMSTATE_BOOL(awaiting_allocation, sPAPRDRConnector),
        VMSTATE_BOOL(signalled, sPAPRDRConnector),
        VMSTATE_END_OF_LIST()
    }
};

static void realize(DeviceState *d, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    Object *root_container;
    char link_name[256];
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
    snprintf(link_name, sizeof(link_name), "%x", spapr_drc_index(drc));
    child_name = object_get_canonical_path_component(OBJECT(drc));
    trace_spapr_drc_realize_child(spapr_drc_index(drc), child_name);
    object_property_add_alias(root_container, link_name,
                              drc->owner, child_name, &err);
    if (err) {
        error_report_err(err);
        object_unref(OBJECT(drc));
    }
    g_free(child_name);
    vmstate_register(DEVICE(drc), spapr_drc_index(drc), &vmstate_spapr_drc,
                     drc);
    trace_spapr_drc_realize_complete(spapr_drc_index(drc));
}

static void unrealize(DeviceState *d, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    Object *root_container;
    char name[256];
    Error *err = NULL;

    trace_spapr_drc_unrealize(spapr_drc_index(drc));
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);
    snprintf(name, sizeof(name), "%x", spapr_drc_index(drc));
    object_property_del(root_container, name, &err);
    if (err) {
        error_report_err(err);
        object_unref(OBJECT(drc));
    }
}

sPAPRDRConnector *spapr_dr_connector_new(Object *owner, const char *type,
                                         uint32_t id)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(object_new(type));
    char *prop_name;

    drc->id = id;
    drc->owner = owner;
    prop_name = g_strdup_printf("dr-connector[%"PRIu32"]",
                                spapr_drc_index(drc));
    object_property_add_child(owner, prop_name, OBJECT(drc), NULL);
    object_property_set_bool(OBJECT(drc), true, "realized", NULL);
    g_free(prop_name);

    /* PCI slot always start in a USABLE state, and stay there */
    if (spapr_drc_type(drc) == SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->allocation_state = SPAPR_DR_ALLOCATION_STATE_USABLE;
    }

    return drc;
}

static void spapr_dr_connector_instance_init(Object *obj)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);

    object_property_add_uint32_ptr(obj, "id", &drc->id, NULL);
    object_property_add(obj, "index", "uint32", prop_get_index,
                        NULL, NULL, NULL, NULL);
    object_property_add(obj, "fdt", "struct", prop_get_fdt,
                        NULL, NULL, NULL, NULL);
}

static void spapr_dr_connector_class_init(ObjectClass *k, void *data)
{
    DeviceClass *dk = DEVICE_CLASS(k);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    dk->reset = reset;
    dk->realize = realize;
    dk->unrealize = unrealize;
    drck->set_isolation_state = set_isolation_state;
    drck->set_allocation_state = set_allocation_state;
    drck->release_pending = release_pending;
    drck->set_signalled = set_signalled;
    /*
     * Reason: it crashes FIXME find and document the real reason
     */
    dk->user_creatable = false;
}

static void spapr_drc_physical_class_init(ObjectClass *k, void *data)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->dr_entity_sense = physical_entity_sense;
}

static void spapr_drc_logical_class_init(ObjectClass *k, void *data)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->dr_entity_sense = logical_entity_sense;
}

static void spapr_drc_cpu_class_init(ObjectClass *k, void *data)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_CPU;
    drck->typename = "CPU";
    drck->drc_name_prefix = "CPU ";
}

static void spapr_drc_pci_class_init(ObjectClass *k, void *data)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_PCI;
    drck->typename = "28";
    drck->drc_name_prefix = "C";
}

static void spapr_drc_lmb_class_init(ObjectClass *k, void *data)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_CLASS(k);

    drck->typeshift = SPAPR_DR_CONNECTOR_TYPE_SHIFT_LMB;
    drck->typename = "MEM";
    drck->drc_name_prefix = "LMB ";
}

static const TypeInfo spapr_dr_connector_info = {
    .name          = TYPE_SPAPR_DR_CONNECTOR,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(sPAPRDRConnector),
    .instance_init = spapr_dr_connector_instance_init,
    .class_size    = sizeof(sPAPRDRConnectorClass),
    .class_init    = spapr_dr_connector_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_physical_info = {
    .name          = TYPE_SPAPR_DRC_PHYSICAL,
    .parent        = TYPE_SPAPR_DR_CONNECTOR,
    .instance_size = sizeof(sPAPRDRConnector),
    .class_init    = spapr_drc_physical_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_logical_info = {
    .name          = TYPE_SPAPR_DRC_LOGICAL,
    .parent        = TYPE_SPAPR_DR_CONNECTOR,
    .instance_size = sizeof(sPAPRDRConnector),
    .class_init    = spapr_drc_logical_class_init,
    .abstract      = true,
};

static const TypeInfo spapr_drc_cpu_info = {
    .name          = TYPE_SPAPR_DRC_CPU,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .instance_size = sizeof(sPAPRDRConnector),
    .class_init    = spapr_drc_cpu_class_init,
};

static const TypeInfo spapr_drc_pci_info = {
    .name          = TYPE_SPAPR_DRC_PCI,
    .parent        = TYPE_SPAPR_DRC_PHYSICAL,
    .instance_size = sizeof(sPAPRDRConnector),
    .class_init    = spapr_drc_pci_class_init,
};

static const TypeInfo spapr_drc_lmb_info = {
    .name          = TYPE_SPAPR_DRC_LMB,
    .parent        = TYPE_SPAPR_DRC_LOGICAL,
    .instance_size = sizeof(sPAPRDRConnector),
    .class_init    = spapr_drc_lmb_class_init,
};

/* helper functions for external users */

sPAPRDRConnector *spapr_drc_by_index(uint32_t index)
{
    Object *obj;
    char name[256];

    snprintf(name, sizeof(name), "%s/%x", DRC_CONTAINER_PATH, index);
    obj = object_resolve_path(name, NULL);

    return !obj ? NULL : SPAPR_DR_CONNECTOR(obj);
}

sPAPRDRConnector *spapr_drc_by_id(const char *type, uint32_t id)
{
    sPAPRDRConnectorClass *drck
        = SPAPR_DR_CONNECTOR_CLASS(object_class_by_name(type));

    return spapr_drc_by_index(drck->typeshift << DRC_INDEX_TYPE_SHIFT
                              | (id & DRC_INDEX_ID_MASK));
}

/**
 * spapr_drc_populate_dt
 *
 * @fdt: libfdt device tree
 * @path: path in the DT to generate properties
 * @owner: parent Object/DeviceState for which to generate DRC
 *         descriptions for
 * @drc_type_mask: mask of sPAPRDRConnectorType values corresponding
 *   to the types of DRCs to generate entries for
 *
 * generate OF properties to describe DRC topology/indices to guests
 *
 * as documented in PAPR+ v2.1, 13.5.2
 */
int spapr_drc_populate_dt(void *fdt, int fdt_offset, Object *owner,
                          uint32_t drc_type_mask)
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
        sPAPRDRConnector *drc;
        sPAPRDRConnectorClass *drck;
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
        drc_names = g_string_append(drc_names, spapr_drc_name(drc));
        drc_names = g_string_insert_len(drc_names, -1, "\0", 1);

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

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-indexes",
                      drc_indexes->data,
                      drc_indexes->len * sizeof(uint32_t));
    if (ret) {
        error_report("Couldn't create ibm,drc-indexes property");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-power-domains",
                      drc_power_domains->data,
                      drc_power_domains->len * sizeof(uint32_t));
    if (ret) {
        error_report("Couldn't finalize ibm,drc-power-domains property");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-names",
                      drc_names->str, drc_names->len);
    if (ret) {
        error_report("Couldn't finalize ibm,drc-names property");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-types",
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
    sPAPRDRConnector *drc = spapr_drc_by_index(idx);
    sPAPRDRConnectorClass *drck;

    if (!drc) {
        return RTAS_OUT_PARAM_ERROR;
    }

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    return drck->set_isolation_state(drc, state);
}

static uint32_t rtas_set_allocation_state(uint32_t idx, uint32_t state)
{
    sPAPRDRConnector *drc = spapr_drc_by_index(idx);
    sPAPRDRConnectorClass *drck;

    if (!drc) {
        return RTAS_OUT_PARAM_ERROR;
    }

    drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    return drck->set_allocation_state(drc, state);
}

static uint32_t rtas_set_dr_indicator(uint32_t idx, uint32_t state)
{
    sPAPRDRConnector *drc = spapr_drc_by_index(idx);

    if (!drc) {
        return RTAS_OUT_PARAM_ERROR;
    }

    trace_spapr_drc_set_dr_indicator(idx, state);
    drc->dr_indicator = state;
    return RTAS_OUT_SUCCESS;
}

static void rtas_set_indicator(PowerPCCPU *cpu, sPAPRMachineState *spapr,
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

static void rtas_get_sensor_state(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                  uint32_t token, uint32_t nargs,
                                  target_ulong args, uint32_t nret,
                                  target_ulong rets)
{
    uint32_t sensor_type;
    uint32_t sensor_index;
    uint32_t sensor_state = 0;
    sPAPRDRConnector *drc;
    sPAPRDRConnectorClass *drck;
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
                                         sPAPRMachineState *spapr,
                                         uint32_t token, uint32_t nargs,
                                         target_ulong args, uint32_t nret,
                                         target_ulong rets)
{
    uint64_t wa_addr;
    uint64_t wa_offset;
    uint32_t drc_index;
    sPAPRDRConnector *drc;
    sPAPRConfigureConnectorState *ccs;
    sPAPRDRCCResponse resp = SPAPR_DR_CC_RESPONSE_CONTINUE;
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

    if (!drc->fdt) {
        trace_spapr_rtas_ibm_configure_connector_missing_fdt(drc_index);
        rc = SPAPR_DR_CC_RESPONSE_NOT_CONFIGURABLE;
        goto out;
    }

    ccs = drc->ccs;
    if (!ccs) {
        ccs = g_new0(sPAPRConfigureConnectorState, 1);
        ccs->fdt_offset = drc->fdt_start_offset;
        drc->ccs = ccs;
    }

    do {
        uint32_t tag;
        const char *name;
        const struct fdt_property *prop;
        int fdt_offset_next, prop_len;

        tag = fdt_next_tag(drc->fdt, ccs->fdt_offset, &fdt_offset_next);

        switch (tag) {
        case FDT_BEGIN_NODE:
            ccs->fdt_depth++;
            name = fdt_get_name(drc->fdt, ccs->fdt_offset, NULL);

            /* provide the name of the next OF node */
            wa_offset = CC_VAL_DATA_OFFSET;
            rtas_st(wa_addr, CC_IDX_NODE_NAME_OFFSET, wa_offset);
            configure_connector_st(wa_addr, wa_offset, name, strlen(name) + 1);
            resp = SPAPR_DR_CC_RESPONSE_NEXT_CHILD;
            break;
        case FDT_END_NODE:
            ccs->fdt_depth--;
            if (ccs->fdt_depth == 0) {
                sPAPRDRIsolationState state = drc->isolation_state;
                uint32_t drc_index = spapr_drc_index(drc);
                /* done sending the device tree, don't need to track
                 * the state anymore
                 */
                trace_spapr_drc_set_configured(drc_index);
                if (state == SPAPR_DR_ISOLATION_STATE_UNISOLATED) {
                    drc->configured = true;
                } else {
                    /* guest should be not configuring an isolated device */
                    trace_spapr_drc_set_configured_skipping(drc_index);
                }
                g_free(ccs);
                drc->ccs = NULL;
                ccs = NULL;
                resp = SPAPR_DR_CC_RESPONSE_SUCCESS;
            } else {
                resp = SPAPR_DR_CC_RESPONSE_PREV_PARENT;
            }
            break;
        case FDT_PROP:
            prop = fdt_get_property_by_offset(drc->fdt, ccs->fdt_offset,
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
        if (ccs) {
            ccs->fdt_offset = fdt_offset_next;
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

    spapr_rtas_register(RTAS_SET_INDICATOR, "set-indicator",
                        rtas_set_indicator);
    spapr_rtas_register(RTAS_GET_SENSOR_STATE, "get-sensor-state",
                        rtas_get_sensor_state);
    spapr_rtas_register(RTAS_IBM_CONFIGURE_CONNECTOR, "ibm,configure-connector",
                        rtas_ibm_configure_connector);
}
type_init(spapr_drc_register_types)
