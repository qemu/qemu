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

/* #define DEBUG_SPAPR_DRC */

#ifdef DEBUG_SPAPR_DRC
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#define DPRINTFN(fmt, ...) \
    do { DPRINTF(fmt, ## __VA_ARGS__); fprintf(stderr, "\n"); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#define DPRINTFN(fmt, ...) \
    do { } while (0)
#endif

#define DRC_CONTAINER_PATH "/dr-connector"
#define DRC_INDEX_TYPE_SHIFT 28
#define DRC_INDEX_ID_MASK ((1ULL << DRC_INDEX_TYPE_SHIFT) - 1)

static sPAPRDRConnectorTypeShift get_type_shift(sPAPRDRConnectorType type)
{
    uint32_t shift = 0;

    /* make sure this isn't SPAPR_DR_CONNECTOR_TYPE_ANY, or some
     * other wonky value.
     */
    g_assert(is_power_of_2(type));

    while (type != (1 << shift)) {
        shift++;
    }
    return shift;
}

static uint32_t get_index(sPAPRDRConnector *drc)
{
    /* no set format for a drc index: it only needs to be globally
     * unique. this is how we encode the DRC type on bare-metal
     * however, so might as well do that here
     */
    return (get_type_shift(drc->type) << DRC_INDEX_TYPE_SHIFT) |
            (drc->id & DRC_INDEX_ID_MASK);
}

static uint32_t set_isolation_state(sPAPRDRConnector *drc,
                                    sPAPRDRIsolationState state)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    DPRINTFN("drc: %x, set_isolation_state: %x", get_index(drc), state);

    if (state == SPAPR_DR_ISOLATION_STATE_UNISOLATED) {
        /* cannot unisolate a non-existant resource, and, or resources
         * which are in an 'UNUSABLE' allocation state. (PAPR 2.7, 13.5.3.5)
         */
        if (!drc->dev ||
            drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
            return RTAS_OUT_NO_SUCH_INDICATOR;
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
            if (drc->configured) {
                DPRINTFN("finalizing device removal");
                drck->detach(drc, DEVICE(drc->dev), drc->detach_cb,
                             drc->detach_cb_opaque, NULL);
            } else {
                DPRINTFN("deferring device removal on unconfigured device\n");
            }
        }
        drc->configured = false;
    }

    return RTAS_OUT_SUCCESS;
}

static uint32_t set_indicator_state(sPAPRDRConnector *drc,
                                    sPAPRDRIndicatorState state)
{
    DPRINTFN("drc: %x, set_indicator_state: %x", get_index(drc), state);
    drc->indicator_state = state;
    return RTAS_OUT_SUCCESS;
}

static uint32_t set_allocation_state(sPAPRDRConnector *drc,
                                     sPAPRDRAllocationState state)
{
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);

    DPRINTFN("drc: %x, set_allocation_state: %x", get_index(drc), state);

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

    if (drc->type != SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->allocation_state = state;
        if (drc->awaiting_release &&
            drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
            DPRINTFN("finalizing device removal");
            drck->detach(drc, DEVICE(drc->dev), drc->detach_cb,
                         drc->detach_cb_opaque, NULL);
        }
    }
    return RTAS_OUT_SUCCESS;
}

static uint32_t get_type(sPAPRDRConnector *drc)
{
    return drc->type;
}

static const char *get_name(sPAPRDRConnector *drc)
{
    return drc->name;
}

static const void *get_fdt(sPAPRDRConnector *drc, int *fdt_start_offset)
{
    if (fdt_start_offset) {
        *fdt_start_offset = drc->fdt_start_offset;
    }
    return drc->fdt;
}

static void set_configured(sPAPRDRConnector *drc)
{
    DPRINTFN("drc: %x, set_configured", get_index(drc));

    if (drc->isolation_state != SPAPR_DR_ISOLATION_STATE_UNISOLATED) {
        /* guest should be not configuring an isolated device */
        DPRINTFN("drc: %x, set_configured: skipping isolated device",
                 get_index(drc));
        return;
    }
    drc->configured = true;
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
static uint32_t entity_sense(sPAPRDRConnector *drc, sPAPRDREntitySense *state)
{
    if (drc->dev) {
        if (drc->type != SPAPR_DR_CONNECTOR_TYPE_PCI &&
            drc->allocation_state == SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
            /* for logical DR, we return a state of UNUSABLE
             * iff the allocation state UNUSABLE.
             * Otherwise, report the state as USABLE/PRESENT,
             * as we would for PCI.
             */
            *state = SPAPR_DR_ENTITY_SENSE_UNUSABLE;
        } else {
            /* this assumes all PCI devices are assigned to
             * a 'live insertion' power domain, where QEMU
             * manages power state automatically as opposed
             * to the guest. present, non-PCI resources are
             * unaffected by power state.
             */
            *state = SPAPR_DR_ENTITY_SENSE_PRESENT;
        }
    } else {
        if (drc->type == SPAPR_DR_CONNECTOR_TYPE_PCI) {
            /* PCI devices, and only PCI devices, use EMPTY
             * in cases where we'd otherwise use UNUSABLE
             */
            *state = SPAPR_DR_ENTITY_SENSE_EMPTY;
        } else {
            *state = SPAPR_DR_ENTITY_SENSE_UNUSABLE;
        }
    }

    DPRINTFN("drc: %x, entity_sense: %x", get_index(drc), state);
    return RTAS_OUT_SUCCESS;
}

static void prop_get_index(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    uint32_t value = (uint32_t)drck->get_index(drc);
    visit_type_uint32(v, name, &value, errp);
}

static void prop_get_type(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    uint32_t value = (uint32_t)drck->get_type(drc);
    visit_type_uint32(v, name, &value, errp);
}

static char *prop_get_name(Object *obj, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    return g_strdup(drck->get_name(drc));
}

static void prop_get_entity_sense(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    uint32_t value;

    drck->entity_sense(drc, &value);
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
            visit_end_struct(v);
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
            visit_end_list(v);
            break;
        }
        default:
            error_setg(&error_abort, "device FDT in unexpected state: %d", tag);
        }
        fdt_offset = fdt_offset_next;
    } while (fdt_depth != 0);
}

static void attach(sPAPRDRConnector *drc, DeviceState *d, void *fdt,
                   int fdt_start_offset, bool coldplug, Error **errp)
{
    DPRINTFN("drc: %x, attach", get_index(drc));

    if (drc->isolation_state != SPAPR_DR_ISOLATION_STATE_ISOLATED) {
        error_setg(errp, "an attached device is still awaiting release");
        return;
    }
    if (drc->type == SPAPR_DR_CONNECTOR_TYPE_PCI) {
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
    if (drc->type == SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->isolation_state = SPAPR_DR_ISOLATION_STATE_UNISOLATED;
    }
    drc->indicator_state = SPAPR_DR_INDICATOR_STATE_ACTIVE;

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
    drc->signalled = (drc->type != SPAPR_DR_CONNECTOR_TYPE_PCI)
                     ? true : coldplug;

    object_property_add_link(OBJECT(drc), "device",
                             object_get_typename(OBJECT(drc->dev)),
                             (Object **)(&drc->dev),
                             NULL, 0, NULL);
}

static void detach(sPAPRDRConnector *drc, DeviceState *d,
                   spapr_drc_detach_cb *detach_cb,
                   void *detach_cb_opaque, Error **errp)
{
    DPRINTFN("drc: %x, detach", get_index(drc));

    drc->detach_cb = detach_cb;
    drc->detach_cb_opaque = detach_cb_opaque;

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
        DPRINTFN("awaiting transition to isolated state before removal");
        drc->awaiting_release = true;
        return;
    }

    if (drc->type != SPAPR_DR_CONNECTOR_TYPE_PCI &&
        drc->allocation_state != SPAPR_DR_ALLOCATION_STATE_UNUSABLE) {
        DPRINTFN("awaiting transition to unusable state before removal");
        drc->awaiting_release = true;
        return;
    }

    drc->indicator_state = SPAPR_DR_INDICATOR_STATE_INACTIVE;

    if (drc->detach_cb) {
        drc->detach_cb(drc->dev, drc->detach_cb_opaque);
    }

    drc->awaiting_release = false;
    g_free(drc->fdt);
    drc->fdt = NULL;
    drc->fdt_start_offset = 0;
    object_property_del(OBJECT(drc), "device", NULL);
    drc->dev = NULL;
    drc->detach_cb = NULL;
    drc->detach_cb_opaque = NULL;
}

static bool release_pending(sPAPRDRConnector *drc)
{
    return drc->awaiting_release;
}

static void reset(DeviceState *d)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    sPAPRDREntitySense state;

    DPRINTFN("drc reset: %x", drck->get_index(drc));
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
            drck->detach(drc, DEVICE(drc->dev), drc->detach_cb,
                         drc->detach_cb_opaque, NULL);
        }

        /* non-PCI devices may be awaiting a transition to UNUSABLE */
        if (drc->type != SPAPR_DR_CONNECTOR_TYPE_PCI &&
            drc->awaiting_release) {
            drck->set_allocation_state(drc, SPAPR_DR_ALLOCATION_STATE_UNUSABLE);
        }
    }

    drck->entity_sense(drc, &state);
    if (state == SPAPR_DR_ENTITY_SENSE_PRESENT) {
        drck->set_signalled(drc);
    }
}

static void realize(DeviceState *d, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    Object *root_container;
    char link_name[256];
    gchar *child_name;
    Error *err = NULL;

    DPRINTFN("drc realize: %x", drck->get_index(drc));
    /* NOTE: we do this as part of realize/unrealize due to the fact
     * that the guest will communicate with the DRC via RTAS calls
     * referencing the global DRC index. By unlinking the DRC
     * from DRC_CONTAINER_PATH/<drc_index> we effectively make it
     * inaccessible by the guest, since lookups rely on this path
     * existing in the composition tree
     */
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);
    snprintf(link_name, sizeof(link_name), "%x", drck->get_index(drc));
    child_name = object_get_canonical_path_component(OBJECT(drc));
    DPRINTFN("drc child name: %s", child_name);
    object_property_add_alias(root_container, link_name,
                              drc->owner, child_name, &err);
    if (err) {
        error_report_err(err);
        object_unref(OBJECT(drc));
    }
    g_free(child_name);
    DPRINTFN("drc realize complete");
}

static void unrealize(DeviceState *d, Error **errp)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(d);
    sPAPRDRConnectorClass *drck = SPAPR_DR_CONNECTOR_GET_CLASS(drc);
    Object *root_container;
    char name[256];
    Error *err = NULL;

    DPRINTFN("drc unrealize: %x", drck->get_index(drc));
    root_container = container_get(object_get_root(), DRC_CONTAINER_PATH);
    snprintf(name, sizeof(name), "%x", drck->get_index(drc));
    object_property_del(root_container, name, &err);
    if (err) {
        error_report_err(err);
        object_unref(OBJECT(drc));
    }
}

sPAPRDRConnector *spapr_dr_connector_new(Object *owner,
                                         sPAPRDRConnectorType type,
                                         uint32_t id)
{
    sPAPRDRConnector *drc =
        SPAPR_DR_CONNECTOR(object_new(TYPE_SPAPR_DR_CONNECTOR));
    char *prop_name;

    g_assert(type);

    drc->type = type;
    drc->id = id;
    drc->owner = owner;
    prop_name = g_strdup_printf("dr-connector[%"PRIu32"]", get_index(drc));
    object_property_add_child(owner, prop_name, OBJECT(drc), NULL);
    object_property_set_bool(OBJECT(drc), true, "realized", NULL);
    g_free(prop_name);

    /* human-readable name for a DRC to encode into the DT
     * description. this is mainly only used within a guest in place
     * of the unique DRC index.
     *
     * in the case of VIO/PCI devices, it corresponds to a
     * "location code" that maps a logical device/function (DRC index)
     * to a physical (or virtual in the case of VIO) location in the
     * system by chaining together the "location label" for each
     * encapsulating component.
     *
     * since this is more to do with diagnosing physical hardware
     * issues than guest compatibility, we choose location codes/DRC
     * names that adhere to the documented format, but avoid encoding
     * the entire topology information into the label/code, instead
     * just using the location codes based on the labels for the
     * endpoints (VIO/PCI adaptor connectors), which is basically
     * just "C" followed by an integer ID.
     *
     * DRC names as documented by PAPR+ v2.7, 13.5.2.4
     * location codes as documented by PAPR+ v2.7, 12.3.1.5
     */
    switch (drc->type) {
    case SPAPR_DR_CONNECTOR_TYPE_CPU:
        drc->name = g_strdup_printf("CPU %d", id);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_PHB:
        drc->name = g_strdup_printf("PHB %d", id);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_VIO:
    case SPAPR_DR_CONNECTOR_TYPE_PCI:
        drc->name = g_strdup_printf("C%d", id);
        break;
    case SPAPR_DR_CONNECTOR_TYPE_LMB:
        drc->name = g_strdup_printf("LMB %d", id);
        break;
    default:
        g_assert(false);
    }

    /* PCI slot always start in a USABLE state, and stay there */
    if (drc->type == SPAPR_DR_CONNECTOR_TYPE_PCI) {
        drc->allocation_state = SPAPR_DR_ALLOCATION_STATE_USABLE;
    }

    return drc;
}

static void spapr_dr_connector_instance_init(Object *obj)
{
    sPAPRDRConnector *drc = SPAPR_DR_CONNECTOR(obj);

    object_property_add_uint32_ptr(obj, "isolation-state",
                                   &drc->isolation_state, NULL);
    object_property_add_uint32_ptr(obj, "indicator-state",
                                   &drc->indicator_state, NULL);
    object_property_add_uint32_ptr(obj, "allocation-state",
                                   &drc->allocation_state, NULL);
    object_property_add_uint32_ptr(obj, "id", &drc->id, NULL);
    object_property_add(obj, "index", "uint32", prop_get_index,
                        NULL, NULL, NULL, NULL);
    object_property_add(obj, "connector_type", "uint32", prop_get_type,
                        NULL, NULL, NULL, NULL);
    object_property_add_str(obj, "name", prop_get_name, NULL, NULL);
    object_property_add(obj, "entity-sense", "uint32", prop_get_entity_sense,
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
    drck->set_indicator_state = set_indicator_state;
    drck->set_allocation_state = set_allocation_state;
    drck->get_index = get_index;
    drck->get_type = get_type;
    drck->get_name = get_name;
    drck->get_fdt = get_fdt;
    drck->set_configured = set_configured;
    drck->entity_sense = entity_sense;
    drck->attach = attach;
    drck->detach = detach;
    drck->release_pending = release_pending;
    drck->set_signalled = set_signalled;
    /*
     * Reason: it crashes FIXME find and document the real reason
     */
    dk->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo spapr_dr_connector_info = {
    .name          = TYPE_SPAPR_DR_CONNECTOR,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(sPAPRDRConnector),
    .instance_init = spapr_dr_connector_instance_init,
    .class_size    = sizeof(sPAPRDRConnectorClass),
    .class_init    = spapr_dr_connector_class_init,
};

static void spapr_drc_register_types(void)
{
    type_register_static(&spapr_dr_connector_info);
}

type_init(spapr_drc_register_types)

/* helper functions for external users */

sPAPRDRConnector *spapr_dr_connector_by_index(uint32_t index)
{
    Object *obj;
    char name[256];

    snprintf(name, sizeof(name), "%s/%x", DRC_CONTAINER_PATH, index);
    obj = object_resolve_path(name, NULL);

    return !obj ? NULL : SPAPR_DR_CONNECTOR(obj);
}

sPAPRDRConnector *spapr_dr_connector_by_id(sPAPRDRConnectorType type,
                                           uint32_t id)
{
    return spapr_dr_connector_by_index(
            (get_type_shift(type) << DRC_INDEX_TYPE_SHIFT) |
            (id & DRC_INDEX_ID_MASK));
}

/* generate a string the describes the DRC to encode into the
 * device tree.
 *
 * as documented by PAPR+ v2.7, 13.5.2.6 and C.6.1
 */
static const char *spapr_drc_get_type_str(sPAPRDRConnectorType type)
{
    switch (type) {
    case SPAPR_DR_CONNECTOR_TYPE_CPU:
        return "CPU";
    case SPAPR_DR_CONNECTOR_TYPE_PHB:
        return "PHB";
    case SPAPR_DR_CONNECTOR_TYPE_VIO:
        return "SLOT";
    case SPAPR_DR_CONNECTOR_TYPE_PCI:
        return "28";
    case SPAPR_DR_CONNECTOR_TYPE_LMB:
        return "MEM";
    default:
        g_assert(false);
    }

    return NULL;
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

        if ((drc->type & drc_type_mask) == 0) {
            continue;
        }

        drc_count++;

        /* ibm,drc-indexes */
        drc_index = cpu_to_be32(drck->get_index(drc));
        g_array_append_val(drc_indexes, drc_index);

        /* ibm,drc-power-domains */
        drc_power_domain = cpu_to_be32(-1);
        g_array_append_val(drc_power_domains, drc_power_domain);

        /* ibm,drc-names */
        drc_names = g_string_append(drc_names, drck->get_name(drc));
        drc_names = g_string_insert_len(drc_names, -1, "\0", 1);

        /* ibm,drc-types */
        drc_types = g_string_append(drc_types,
                                    spapr_drc_get_type_str(drc->type));
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
        fprintf(stderr, "Couldn't create ibm,drc-indexes property\n");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-power-domains",
                      drc_power_domains->data,
                      drc_power_domains->len * sizeof(uint32_t));
    if (ret) {
        fprintf(stderr, "Couldn't finalize ibm,drc-power-domains property\n");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-names",
                      drc_names->str, drc_names->len);
    if (ret) {
        fprintf(stderr, "Couldn't finalize ibm,drc-names property\n");
        goto out;
    }

    ret = fdt_setprop(fdt, fdt_offset, "ibm,drc-types",
                      drc_types->str, drc_types->len);
    if (ret) {
        fprintf(stderr, "Couldn't finalize ibm,drc-types property\n");
        goto out;
    }

out:
    g_array_free(drc_indexes, true);
    g_array_free(drc_power_domains, true);
    g_string_free(drc_names, true);
    g_string_free(drc_types, true);

    return ret;
}
