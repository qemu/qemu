/*
 * QEMU Xen emulation: Grant table support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "qemu/module.h"
#include "qemu/lockable.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "exec/target_page.h"
#include "system/address-spaces.h"
#include "migration/vmstate.h"

#include "hw/sysbus.h"
#include "hw/xen/xen.h"
#include "hw/xen/xen_backend_ops.h"
#include "xen_overlay.h"
#include "xen_gnttab.h"
#include "xen_primary_console.h"

#include "system/kvm.h"
#include "system/kvm_xen.h"

#include "hw/xen/interface/memory.h"
#include "hw/xen/interface/grant_table.h"

#define TYPE_XEN_GNTTAB "xen-gnttab"
OBJECT_DECLARE_SIMPLE_TYPE(XenGnttabState, XEN_GNTTAB)

#define ENTRIES_PER_FRAME_V1 (XEN_PAGE_SIZE / sizeof(grant_entry_v1_t))

static struct gnttab_backend_ops emu_gnttab_backend_ops;

struct XenGnttabState {
    /*< private >*/
    SysBusDevice busdev;
    /*< public >*/

    QemuMutex gnt_lock;

    uint32_t nr_frames;
    uint32_t max_frames;

    union {
        grant_entry_v1_t *v1;
        /* Theoretically, v2 support could be added here. */
    } entries;

    MemoryRegion gnt_frames;
    MemoryRegion *gnt_aliases;
    uint64_t *gnt_frame_gpas;

    uint8_t *map_track;
};

struct XenGnttabState *xen_gnttab_singleton;

static void xen_gnttab_realize(DeviceState *dev, Error **errp)
{
    XenGnttabState *s = XEN_GNTTAB(dev);
    int i;

    if (xen_mode != XEN_EMULATE) {
        error_setg(errp, "Xen grant table support is for Xen emulation");
        return;
    }
    s->max_frames = kvm_xen_get_gnttab_max_frames();
    memory_region_init_ram(&s->gnt_frames, OBJECT(dev), "xen:grant_table",
                           XEN_PAGE_SIZE * s->max_frames, &error_abort);
    memory_region_set_enabled(&s->gnt_frames, true);
    s->entries.v1 = memory_region_get_ram_ptr(&s->gnt_frames);

    /* Create individual page-sizes aliases for overlays */
    s->gnt_aliases = (void *)g_new0(MemoryRegion, s->max_frames);
    s->gnt_frame_gpas = (void *)g_new(uint64_t, s->max_frames);
    for (i = 0; i < s->max_frames; i++) {
        memory_region_init_alias(&s->gnt_aliases[i], OBJECT(dev),
                                 NULL, &s->gnt_frames,
                                 i * XEN_PAGE_SIZE, XEN_PAGE_SIZE);
        s->gnt_frame_gpas[i] = INVALID_GPA;
    }

    s->nr_frames = 0;
    memset(s->entries.v1, 0, XEN_PAGE_SIZE * s->max_frames);
    s->entries.v1[GNTTAB_RESERVED_XENSTORE].flags = GTF_permit_access;
    s->entries.v1[GNTTAB_RESERVED_XENSTORE].frame = XEN_SPECIAL_PFN(XENSTORE);

    qemu_mutex_init(&s->gnt_lock);

    xen_gnttab_singleton = s;

    s->map_track = g_new0(uint8_t, s->max_frames * ENTRIES_PER_FRAME_V1);

    xen_gnttab_ops = &emu_gnttab_backend_ops;
}

static int xen_gnttab_post_load(void *opaque, int version_id)
{
    XenGnttabState *s = XEN_GNTTAB(opaque);
    uint32_t i;

    for (i = 0; i < s->nr_frames; i++) {
        if (s->gnt_frame_gpas[i] != INVALID_GPA) {
            xen_overlay_do_map_page(&s->gnt_aliases[i], s->gnt_frame_gpas[i]);
        }
    }
    return 0;
}

static bool xen_gnttab_is_needed(void *opaque)
{
    return xen_mode == XEN_EMULATE;
}

static const VMStateDescription xen_gnttab_vmstate = {
    .name = "xen_gnttab",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = xen_gnttab_is_needed,
    .post_load = xen_gnttab_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(nr_frames, XenGnttabState),
        VMSTATE_VARRAY_UINT32(gnt_frame_gpas, XenGnttabState, nr_frames, 0,
                              vmstate_info_uint64, uint64_t),
        VMSTATE_END_OF_LIST()
    }
};

static void xen_gnttab_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xen_gnttab_realize;
    dc->vmsd = &xen_gnttab_vmstate;
}

static const TypeInfo xen_gnttab_info = {
    .name          = TYPE_XEN_GNTTAB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XenGnttabState),
    .class_init    = xen_gnttab_class_init,
};

void xen_gnttab_create(void)
{
    xen_gnttab_singleton = XEN_GNTTAB(sysbus_create_simple(TYPE_XEN_GNTTAB,
                                                           -1, NULL));
}

static void xen_gnttab_register_types(void)
{
    type_register_static(&xen_gnttab_info);
}

type_init(xen_gnttab_register_types)

int xen_gnttab_map_page(uint64_t idx, uint64_t gfn)
{
    XenGnttabState *s = xen_gnttab_singleton;
    uint64_t gpa = gfn << XEN_PAGE_SHIFT;

    if (!s) {
        return -ENOTSUP;
    }

    if (idx >= s->max_frames) {
        return -EINVAL;
    }

    BQL_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->gnt_lock);

    xen_overlay_do_map_page(&s->gnt_aliases[idx], gpa);

    s->gnt_frame_gpas[idx] = gpa;

    if (s->nr_frames <= idx) {
        s->nr_frames = idx + 1;
    }

    return 0;
}

int xen_gnttab_set_version_op(struct gnttab_set_version *set)
{
    int ret;

    switch (set->version) {
    case 1:
        ret = 0;
        break;

    case 2:
        /* Behave as before set_version was introduced. */
        ret = -ENOSYS;
        break;

    default:
        ret = -EINVAL;
    }

    set->version = 1;
    return ret;
}

int xen_gnttab_get_version_op(struct gnttab_get_version *get)
{
    if (get->dom != DOMID_SELF && get->dom != xen_domid) {
        return -ESRCH;
    }

    get->version = 1;
    return 0;
}

int xen_gnttab_query_size_op(struct gnttab_query_size *size)
{
    XenGnttabState *s = xen_gnttab_singleton;

    if (!s) {
        return -ENOTSUP;
    }

    if (size->dom != DOMID_SELF && size->dom != xen_domid) {
        size->status = GNTST_bad_domain;
        return 0;
    }

    size->status = GNTST_okay;
    size->nr_frames = s->nr_frames;
    size->max_nr_frames = s->max_frames;
    return 0;
}

/* Track per-open refs, to allow close() to clean up. */
struct active_ref {
    MemoryRegionSection mrs;
    void *virtaddr;
    uint32_t refcnt;
    int prot;
};

static void gnt_unref(XenGnttabState *s, grant_ref_t ref,
                      MemoryRegionSection *mrs, int prot)
{
    if (mrs && mrs->mr) {
        if (prot & PROT_WRITE) {
            memory_region_set_dirty(mrs->mr, mrs->offset_within_region,
                                    XEN_PAGE_SIZE);
        }
        memory_region_unref(mrs->mr);
        mrs->mr = NULL;
    }
    assert(s->map_track[ref] != 0);

    if (--s->map_track[ref] == 0) {
        grant_entry_v1_t *gnt_p = &s->entries.v1[ref];
        qatomic_and(&gnt_p->flags, (uint16_t)~(GTF_reading | GTF_writing));
    }
}

static uint64_t gnt_ref(XenGnttabState *s, grant_ref_t ref, int prot)
{
    uint16_t mask = GTF_type_mask | GTF_sub_page;
    grant_entry_v1_t gnt, *gnt_p;
    int retries = 0;

    if (ref >= s->max_frames * ENTRIES_PER_FRAME_V1 ||
        s->map_track[ref] == UINT8_MAX) {
        return INVALID_GPA;
    }

    if (prot & PROT_WRITE) {
        mask |= GTF_readonly;
    }

    gnt_p = &s->entries.v1[ref];

    /*
     * The guest can legitimately be changing the GTF_readonly flag. Allow
     * that, but don't let a malicious guest cause a livelock.
     */
    for (retries = 0; retries < 5; retries++) {
        uint16_t new_flags;

        /* Read the entry before an atomic operation on its flags */
        gnt = *(volatile grant_entry_v1_t *)gnt_p;

        if ((gnt.flags & mask) != GTF_permit_access ||
            gnt.domid != DOMID_QEMU) {
            return INVALID_GPA;
        }

        new_flags = gnt.flags | GTF_reading;
        if (prot & PROT_WRITE) {
            new_flags |= GTF_writing;
        }

        if (qatomic_cmpxchg(&gnt_p->flags, gnt.flags, new_flags) == gnt.flags) {
            return (uint64_t)gnt.frame << XEN_PAGE_SHIFT;
        }
    }

    return INVALID_GPA;
}

struct xengntdev_handle {
    GHashTable *active_maps;
};

static int xen_be_gnttab_set_max_grants(struct xengntdev_handle *xgt,
                                        uint32_t nr_grants)
{
    return 0;
}

static void *xen_be_gnttab_map_refs(struct xengntdev_handle *xgt,
                                    uint32_t count, uint32_t domid,
                                    uint32_t *refs, int prot)
{
    XenGnttabState *s = xen_gnttab_singleton;
    struct active_ref *act;

    if (!s) {
        errno = ENOTSUP;
        return NULL;
    }

    if (domid != xen_domid) {
        errno = EINVAL;
        return NULL;
    }

    if (!count || count > 4096) {
        errno = EINVAL;
        return NULL;
    }

    /*
     * Making a contiguous mapping from potentially discontiguous grant
     * references would be... distinctly non-trivial. We don't support it.
     * Even changing the API to return an array of pointers, one per page,
     * wouldn't be simple to use in PV backends because some structures
     * actually cross page boundaries (e.g. 32-bit blkif_response ring
     * entries are 12 bytes).
     */
    if (count != 1) {
        errno = EINVAL;
        return NULL;
    }

    QEMU_LOCK_GUARD(&s->gnt_lock);

    act = g_hash_table_lookup(xgt->active_maps, GINT_TO_POINTER(refs[0]));
    if (act) {
        if ((prot & PROT_WRITE) && !(act->prot & PROT_WRITE)) {
            if (gnt_ref(s, refs[0], prot) == INVALID_GPA) {
                return NULL;
            }
            act->prot |= PROT_WRITE;
        }
        act->refcnt++;
    } else {
        uint64_t gpa = gnt_ref(s, refs[0], prot);
        if (gpa == INVALID_GPA) {
            errno = EINVAL;
            return NULL;
        }

        act = g_new0(struct active_ref, 1);
        act->prot = prot;
        act->refcnt = 1;
        act->mrs = memory_region_find(get_system_memory(), gpa, XEN_PAGE_SIZE);

        if (act->mrs.mr &&
            !int128_lt(act->mrs.size, int128_make64(XEN_PAGE_SIZE)) &&
            memory_region_get_ram_addr(act->mrs.mr) != RAM_ADDR_INVALID) {
            act->virtaddr = qemu_map_ram_ptr(act->mrs.mr->ram_block,
                                             act->mrs.offset_within_region);
        }
        if (!act->virtaddr) {
            gnt_unref(s, refs[0], &act->mrs, 0);
            g_free(act);
            errno = EINVAL;
            return NULL;
        }

        s->map_track[refs[0]]++;
        g_hash_table_insert(xgt->active_maps, GINT_TO_POINTER(refs[0]), act);
    }

    return act->virtaddr;
}

static gboolean do_unmap(gpointer key, gpointer value, gpointer user_data)
{
    XenGnttabState *s = user_data;
    grant_ref_t gref = GPOINTER_TO_INT(key);
    struct active_ref *act = value;

    gnt_unref(s, gref, &act->mrs, act->prot);
    g_free(act);
    return true;
}

static int xen_be_gnttab_unmap(struct xengntdev_handle *xgt,
                               void *start_address, uint32_t *refs,
                               uint32_t count)
{
    XenGnttabState *s = xen_gnttab_singleton;
    struct active_ref *act;

    if (!s) {
        return -ENOTSUP;
    }

    if (count != 1) {
        return -EINVAL;
    }

    QEMU_LOCK_GUARD(&s->gnt_lock);

    act = g_hash_table_lookup(xgt->active_maps, GINT_TO_POINTER(refs[0]));
    if (!act) {
        return -ENOENT;
    }

    if (act->virtaddr != start_address) {
        return -EINVAL;
    }

    if (!--act->refcnt) {
        do_unmap(GINT_TO_POINTER(refs[0]), act, s);
        g_hash_table_remove(xgt->active_maps, GINT_TO_POINTER(refs[0]));
    }

    return 0;
}

/*
 * This looks a bit like the one for true Xen in xen-operations.c but
 * in emulation we don't support multi-page mappings. And under Xen we
 * *want* the multi-page mappings so we have fewer bounces through the
 * kernel and the hypervisor. So the code paths end up being similar,
 * but different.
 */
static int xen_be_gnttab_copy(struct xengntdev_handle *xgt, bool to_domain,
                              uint32_t domid, XenGrantCopySegment *segs,
                              uint32_t nr_segs, Error **errp)
{
    int prot = to_domain ? PROT_WRITE : PROT_READ;
    unsigned int i;

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];
        void *page;
        uint32_t ref = to_domain ? seg->dest.foreign.ref :
            seg->source.foreign.ref;

        page = xen_be_gnttab_map_refs(xgt, 1, domid, &ref, prot);
        if (!page) {
            if (errp) {
                error_setg_errno(errp, errno,
                                 "xen_be_gnttab_map_refs failed");
            }
            return -errno;
        }

        if (to_domain) {
            memcpy(page + seg->dest.foreign.offset, seg->source.virt,
                   seg->len);
        } else {
            memcpy(seg->dest.virt, page + seg->source.foreign.offset,
                   seg->len);
        }

        if (xen_be_gnttab_unmap(xgt, page, &ref, 1)) {
            if (errp) {
                error_setg_errno(errp, errno, "xen_be_gnttab_unmap failed");
            }
            return -errno;
        }
    }

    return 0;
}

static struct xengntdev_handle *xen_be_gnttab_open(void)
{
    struct xengntdev_handle *xgt = g_new0(struct xengntdev_handle, 1);

    xgt->active_maps = g_hash_table_new(g_direct_hash, g_direct_equal);
    return xgt;
}

static int xen_be_gnttab_close(struct xengntdev_handle *xgt)
{
    XenGnttabState *s = xen_gnttab_singleton;

    if (!s) {
        return -ENOTSUP;
    }

    g_hash_table_foreach_remove(xgt->active_maps, do_unmap, s);
    g_hash_table_destroy(xgt->active_maps);
    g_free(xgt);
    return 0;
}

static struct gnttab_backend_ops emu_gnttab_backend_ops = {
    .open = xen_be_gnttab_open,
    .close = xen_be_gnttab_close,
    .grant_copy = xen_be_gnttab_copy,
    .set_max_grants = xen_be_gnttab_set_max_grants,
    .map_refs = xen_be_gnttab_map_refs,
    .unmap = xen_be_gnttab_unmap,
};

int xen_gnttab_reset(void)
{
    XenGnttabState *s = xen_gnttab_singleton;

    if (!s) {
        return -ENOTSUP;
    }

    QEMU_LOCK_GUARD(&s->gnt_lock);

    s->nr_frames = 0;

    memset(s->entries.v1, 0, XEN_PAGE_SIZE * s->max_frames);
    s->entries.v1[GNTTAB_RESERVED_XENSTORE].flags = GTF_permit_access;
    s->entries.v1[GNTTAB_RESERVED_XENSTORE].frame = XEN_SPECIAL_PFN(XENSTORE);

    if (xen_primary_console_get_pfn()) {
        s->entries.v1[GNTTAB_RESERVED_CONSOLE].flags = GTF_permit_access;
        s->entries.v1[GNTTAB_RESERVED_CONSOLE].frame = XEN_SPECIAL_PFN(CONSOLE);
    }

    return 0;
}
