/*
 * Persistent reservation manager abstract class
 *
 * Copyright (c) 2017 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This code is licensed under the LGPL.
 *
 */

#include "qemu/osdep.h"
#include <scsi/sg.h>

#include "qapi/error.h"
#include "block/aio.h"
#include "block/thread-pool.h"
#include "scsi/pr-manager.h"
#include "trace.h"

typedef struct PRManagerData {
    PRManager *pr_mgr;
    struct sg_io_hdr *hdr;
    int fd;
} PRManagerData;

static int pr_manager_worker(void *opaque)
{
    PRManagerData *data = opaque;
    PRManager *pr_mgr = data->pr_mgr;
    PRManagerClass *pr_mgr_class =
        PR_MANAGER_GET_CLASS(pr_mgr);
    struct sg_io_hdr *hdr = data->hdr;
    int fd = data->fd;
    int r;

    g_free(data);
    trace_pr_manager_run(fd, hdr->cmdp[0], hdr->cmdp[1]);

    /* The reference was taken in pr_manager_execute.  */
    r = pr_mgr_class->run(pr_mgr, fd, hdr);
    object_unref(OBJECT(pr_mgr));
    return r;
}


BlockAIOCB *pr_manager_execute(PRManager *pr_mgr,
                               AioContext *ctx, int fd,
                               struct sg_io_hdr *hdr,
                               BlockCompletionFunc *complete,
                               void *opaque)
{
    PRManagerData *data = g_new(PRManagerData, 1);
    ThreadPool *pool = aio_get_thread_pool(ctx);

    trace_pr_manager_execute(fd, hdr->cmdp[0], hdr->cmdp[1], opaque);
    data->pr_mgr = pr_mgr;
    data->fd = fd;
    data->hdr = hdr;

    /* The matching object_unref is in pr_manager_worker.  */
    object_ref(OBJECT(pr_mgr));
    return thread_pool_submit_aio(pool, pr_manager_worker,
                                  data, complete, opaque);
}

static const TypeInfo pr_manager_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_PR_MANAGER,
    .class_size = sizeof(PRManagerClass),
    .abstract = true,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

PRManager *pr_manager_lookup(const char *id, Error **errp)
{
    Object *obj;
    PRManager *pr_mgr;

    obj = object_resolve_path_component(object_get_objects_root(), id);
    if (!obj) {
        error_setg(errp, "No persistent reservation manager with id '%s'", id);
        return NULL;
    }

    pr_mgr = (PRManager *)
        object_dynamic_cast(obj,
                            TYPE_PR_MANAGER);
    if (!pr_mgr) {
        error_setg(errp,
                   "Object with id '%s' is not a persistent reservation manager",
                   id);
        return NULL;
    }

    return pr_mgr;
}

static void
pr_manager_register_types(void)
{
    type_register_static(&pr_manager_info);
}


type_init(pr_manager_register_types);
