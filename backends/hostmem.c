/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013-2014 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/hostmem.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/qapi-builtin-visit.h"
#include "qapi/visitor.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"
#include "qemu/mmap-alloc.h"

#ifdef CONFIG_NUMA
#include <numaif.h>
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_DEFAULT != MPOL_DEFAULT);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_PREFERRED != MPOL_PREFERRED);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_BIND != MPOL_BIND);
QEMU_BUILD_BUG_ON(HOST_MEM_POLICY_INTERLEAVE != MPOL_INTERLEAVE);
#endif

char *
host_memory_backend_get_name(HostMemoryBackend *backend)
{
    if (!backend->use_canonical_path) {
        return object_get_canonical_path_component(OBJECT(backend));
    }

    return object_get_canonical_path(OBJECT(backend));
}

static void
host_memory_backend_get_size(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value = backend->size;

    visit_type_size(v, name, &value, errp);
}

static void
host_memory_backend_set_size(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    Error *local_err = NULL;
    uint64_t value;

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(&local_err, "cannot change property %s of %s ",
                   name, object_get_typename(obj));
        goto out;
    }

    visit_type_size(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (!value) {
        error_setg(&local_err,
                   "property '%s' of %s doesn't take value '%" PRIu64 "'",
                   name, object_get_typename(obj), value);
        goto out;
    }
    backend->size = value;
out:
    error_propagate(errp, local_err);
}

static void
host_memory_backend_get_host_nodes(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *host_nodes = NULL;
    uint16List **node = &host_nodes;
    unsigned long value;

    value = find_first_bit(backend->host_nodes, MAX_NODES);
    if (value == MAX_NODES) {
        goto ret;
    }

    *node = g_malloc0(sizeof(**node));
    (*node)->value = value;
    node = &(*node)->next;

    do {
        value = find_next_bit(backend->host_nodes, MAX_NODES, value + 1);
        if (value == MAX_NODES) {
            break;
        }

        *node = g_malloc0(sizeof(**node));
        (*node)->value = value;
        node = &(*node)->next;
    } while (true);

ret:
    visit_type_uint16List(v, name, &host_nodes, errp);
}

static void
host_memory_backend_set_host_nodes(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
#ifdef CONFIG_NUMA
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint16List *l, *host_nodes = NULL;

    visit_type_uint16List(v, name, &host_nodes, errp);

    for (l = host_nodes; l; l = l->next) {
        if (l->value >= MAX_NODES) {
            error_setg(errp, "Invalid host-nodes value: %d", l->value);
            goto out;
        }
    }

    for (l = host_nodes; l; l = l->next) {
        bitmap_set(backend->host_nodes, l->value, 1);
    }

out:
    qapi_free_uint16List(host_nodes);
#else
    error_setg(errp, "NUMA node binding are not supported by this QEMU");
#endif
}

static int
host_memory_backend_get_policy(Object *obj, Error **errp G_GNUC_UNUSED)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    return backend->policy;
}

static void
host_memory_backend_set_policy(Object *obj, int policy, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    backend->policy = policy;

#ifndef CONFIG_NUMA
    if (policy != HOST_MEM_POLICY_DEFAULT) {
        error_setg(errp, "NUMA policies are not supported by this QEMU");
    }
#endif
}

static bool host_memory_backend_get_merge(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->merge;
}

static void host_memory_backend_set_merge(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!host_memory_backend_mr_inited(backend)) {
        backend->merge = value;
        return;
    }

    if (value != backend->merge) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_MERGEABLE : QEMU_MADV_UNMERGEABLE);
        backend->merge = value;
    }
}

static bool host_memory_backend_get_dump(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->dump;
}

static void host_memory_backend_set_dump(Object *obj, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!host_memory_backend_mr_inited(backend)) {
        backend->dump = value;
        return;
    }

    if (value != backend->dump) {
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        qemu_madvise(ptr, sz,
                     value ? QEMU_MADV_DODUMP : QEMU_MADV_DONTDUMP);
        backend->dump = value;
    }
}

static bool host_memory_backend_get_prealloc(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->prealloc;
}

static void host_memory_backend_set_prealloc(Object *obj, bool value,
                                             Error **errp)
{
    Error *local_err = NULL;
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (!host_memory_backend_mr_inited(backend)) {
        backend->prealloc = value;
        return;
    }

    if (value && !backend->prealloc) {
        int fd = memory_region_get_fd(&backend->mr);
        void *ptr = memory_region_get_ram_ptr(&backend->mr);
        uint64_t sz = memory_region_size(&backend->mr);

        os_mem_prealloc(fd, ptr, sz, backend->prealloc_threads, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        backend->prealloc = true;
    }
}

static void host_memory_backend_get_prealloc_threads(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    visit_type_uint32(v, name, &backend->prealloc_threads, errp);
}

static void host_memory_backend_set_prealloc_threads(Object *obj, Visitor *v,
    const char *name, void *opaque, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    Error *local_err = NULL;
    uint32_t value;

    visit_type_uint32(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }
    if (value <= 0) {
        error_setg(&local_err,
                   "property '%s' of %s doesn't take value '%d'",
                   name, object_get_typename(obj), value);
        goto out;
    }
    backend->prealloc_threads = value;
out:
    error_propagate(errp, local_err);
}

static void host_memory_backend_init(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    MachineState *machine = MACHINE(qdev_get_machine());

    /* TODO: convert access to globals to compat properties */
    backend->merge = machine_mem_merge(machine);
    backend->dump = machine_dump_guest_core(machine);
}

static void host_memory_backend_post_init(Object *obj)
{
    object_apply_compat_props(obj);
}

bool host_memory_backend_mr_inited(HostMemoryBackend *backend)
{
    /*
     * NOTE: We forbid zero-length memory backend, so here zero means
     * "we haven't inited the backend memory region yet".
     */
    return memory_region_size(&backend->mr) != 0;
}

MemoryRegion *host_memory_backend_get_memory(HostMemoryBackend *backend)
{
    return host_memory_backend_mr_inited(backend) ? &backend->mr : NULL;
}

void host_memory_backend_set_mapped(HostMemoryBackend *backend, bool mapped)
{
    backend->is_mapped = mapped;
}

bool host_memory_backend_is_mapped(HostMemoryBackend *backend)
{
    return backend->is_mapped;
}

#ifdef __linux__
size_t host_memory_backend_pagesize(HostMemoryBackend *memdev)
{
    Object *obj = OBJECT(memdev);
    char *path = object_property_get_str(obj, "mem-path", NULL);
    size_t pagesize = qemu_mempath_getpagesize(path);

    g_free(path);
    return pagesize;
}
#else
size_t host_memory_backend_pagesize(HostMemoryBackend *memdev)
{
    return qemu_real_host_page_size;
}
#endif

static void
host_memory_backend_memory_complete(UserCreatable *uc, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(uc);
    HostMemoryBackendClass *bc = MEMORY_BACKEND_GET_CLASS(uc);
    Error *local_err = NULL;
    void *ptr;
    uint64_t sz;

    if (bc->alloc) {
        bc->alloc(backend, &local_err);
        if (local_err) {
            goto out;
        }

        ptr = memory_region_get_ram_ptr(&backend->mr);
        sz = memory_region_size(&backend->mr);

        if (backend->merge) {
            qemu_madvise(ptr, sz, QEMU_MADV_MERGEABLE);
        }
        if (!backend->dump) {
            qemu_madvise(ptr, sz, QEMU_MADV_DONTDUMP);
        }
#ifdef CONFIG_NUMA
        unsigned long lastbit = find_last_bit(backend->host_nodes, MAX_NODES);
        /* lastbit == MAX_NODES means maxnode = 0 */
        unsigned long maxnode = (lastbit + 1) % (MAX_NODES + 1);
        /* ensure policy won't be ignored in case memory is preallocated
         * before mbind(). note: MPOL_MF_STRICT is ignored on hugepages so
         * this doesn't catch hugepage case. */
        unsigned flags = MPOL_MF_STRICT | MPOL_MF_MOVE;

        /* check for invalid host-nodes and policies and give more verbose
         * error messages than mbind(). */
        if (maxnode && backend->policy == MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be empty for policy default,"
                       " or you should explicitly specify a policy other"
                       " than default");
            return;
        } else if (maxnode == 0 && backend->policy != MPOL_DEFAULT) {
            error_setg(errp, "host-nodes must be set for policy %s",
                       HostMemPolicy_str(backend->policy));
            return;
        }

        /* We can have up to MAX_NODES nodes, but we need to pass maxnode+1
         * as argument to mbind() due to an old Linux bug (feature?) which
         * cuts off the last specified node. This means backend->host_nodes
         * must have MAX_NODES+1 bits available.
         */
        assert(sizeof(backend->host_nodes) >=
               BITS_TO_LONGS(MAX_NODES + 1) * sizeof(unsigned long));
        assert(maxnode <= MAX_NODES);
        if (mbind(ptr, sz, backend->policy,
                  maxnode ? backend->host_nodes : NULL, maxnode + 1, flags)) {
            if (backend->policy != MPOL_DEFAULT || errno != ENOSYS) {
                error_setg_errno(errp, errno,
                                 "cannot bind memory to host NUMA nodes");
                return;
            }
        }
#endif
        /* Preallocate memory after the NUMA policy has been instantiated.
         * This is necessary to guarantee memory is allocated with
         * specified NUMA policy in place.
         */
        if (backend->prealloc) {
            os_mem_prealloc(memory_region_get_fd(&backend->mr), ptr, sz,
                            backend->prealloc_threads, &local_err);
            if (local_err) {
                goto out;
            }
        }
    }
out:
    error_propagate(errp, local_err);
}

static bool
host_memory_backend_can_be_deleted(UserCreatable *uc)
{
    if (host_memory_backend_is_mapped(MEMORY_BACKEND(uc))) {
        return false;
    } else {
        return true;
    }
}

static bool host_memory_backend_get_share(Object *o, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    return backend->share;
}

static void host_memory_backend_set_share(Object *o, bool value, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(o);

    if (host_memory_backend_mr_inited(backend)) {
        error_setg(errp, "cannot change property value");
        return;
    }
    backend->share = value;
}

static bool
host_memory_backend_get_use_canonical_path(Object *obj, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    return backend->use_canonical_path;
}

static void
host_memory_backend_set_use_canonical_path(Object *obj, bool value,
                                           Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    backend->use_canonical_path = value;
}

static void
host_memory_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = host_memory_backend_memory_complete;
    ucc->can_be_deleted = host_memory_backend_can_be_deleted;

    object_class_property_add_bool(oc, "merge",
        host_memory_backend_get_merge,
        host_memory_backend_set_merge, &error_abort);
    object_class_property_set_description(oc, "merge",
        "Mark memory as mergeable", &error_abort);
    object_class_property_add_bool(oc, "dump",
        host_memory_backend_get_dump,
        host_memory_backend_set_dump, &error_abort);
    object_class_property_set_description(oc, "dump",
        "Set to 'off' to exclude from core dump", &error_abort);
    object_class_property_add_bool(oc, "prealloc",
        host_memory_backend_get_prealloc,
        host_memory_backend_set_prealloc, &error_abort);
    object_class_property_set_description(oc, "prealloc",
        "Preallocate memory", &error_abort);
    object_class_property_add(oc, "prealloc-threads", "int",
        host_memory_backend_get_prealloc_threads,
        host_memory_backend_set_prealloc_threads,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "prealloc-threads",
        "Number of CPU threads to use for prealloc", &error_abort);
    object_class_property_add(oc, "size", "int",
        host_memory_backend_get_size,
        host_memory_backend_set_size,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "size",
        "Size of the memory region (ex: 500M)", &error_abort);
    object_class_property_add(oc, "host-nodes", "int",
        host_memory_backend_get_host_nodes,
        host_memory_backend_set_host_nodes,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "host-nodes",
        "Binds memory to the list of NUMA host nodes", &error_abort);
    object_class_property_add_enum(oc, "policy", "HostMemPolicy",
        &HostMemPolicy_lookup,
        host_memory_backend_get_policy,
        host_memory_backend_set_policy, &error_abort);
    object_class_property_set_description(oc, "policy",
        "Set the NUMA policy", &error_abort);
    object_class_property_add_bool(oc, "share",
        host_memory_backend_get_share, host_memory_backend_set_share,
        &error_abort);
    object_class_property_set_description(oc, "share",
        "Mark the memory as private to QEMU or shared", &error_abort);
    object_class_property_add_bool(oc, "x-use-canonical-path-for-ramblock-id",
        host_memory_backend_get_use_canonical_path,
        host_memory_backend_set_use_canonical_path, &error_abort);
}

static const TypeInfo host_memory_backend_info = {
    .name = TYPE_MEMORY_BACKEND,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(HostMemoryBackendClass),
    .class_init = host_memory_backend_class_init,
    .instance_size = sizeof(HostMemoryBackend),
    .instance_init = host_memory_backend_init,
    .instance_post_init = host_memory_backend_post_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&host_memory_backend_info);
}

type_init(register_types);
