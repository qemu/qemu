/*
 * IOThread Virtqueue Mapping
 *
 * Copyright Red Hat, Inc
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "qemu/osdep.h"
#include "system/iothread.h"
#include "hw/virtio/iothread-vq-mapping.h"

static bool
iothread_vq_mapping_validate(IOThreadVirtQueueMappingList *list, uint16_t
        num_queues, Error **errp)
{
    g_autofree unsigned long *vqs = bitmap_new(num_queues);
    g_autoptr(GHashTable) iothreads =
        g_hash_table_new(g_str_hash, g_str_equal);

    for (IOThreadVirtQueueMappingList *node = list; node; node = node->next) {
        const char *name = node->value->iothread;
        uint16List *vq;

        if (!iothread_by_id(name)) {
            error_setg(errp, "IOThread \"%s\" object does not exist", name);
            return false;
        }

        if (!g_hash_table_add(iothreads, (gpointer)name)) {
            error_setg(errp,
                    "duplicate IOThread name \"%s\" in iothread-vq-mapping",
                    name);
            return false;
        }

        if (node != list) {
            if (!!node->value->vqs != !!list->value->vqs) {
                error_setg(errp, "either all items in iothread-vq-mapping "
                                 "must have vqs or none of them must have it");
                return false;
            }
        }

        for (vq = node->value->vqs; vq; vq = vq->next) {
            if (vq->value >= num_queues) {
                error_setg(errp, "vq index %u for IOThread \"%s\" must be "
                        "less than num_queues %u in iothread-vq-mapping",
                        vq->value, name, num_queues);
                return false;
            }

            if (test_and_set_bit(vq->value, vqs)) {
                error_setg(errp, "cannot assign vq %u to IOThread \"%s\" "
                        "because it is already assigned", vq->value, name);
                return false;
            }
        }
    }

    if (list->value->vqs) {
        for (uint16_t i = 0; i < num_queues; i++) {
            if (!test_bit(i, vqs)) {
                error_setg(errp,
                        "missing vq %u IOThread assignment in iothread-vq-mapping",
                        i);
                return false;
            }
        }
    }

    return true;
}

bool iothread_vq_mapping_apply(
        IOThreadVirtQueueMappingList *list,
        AioContext **vq_aio_context,
        uint16_t num_queues,
        Error **errp)
{
    IOThreadVirtQueueMappingList *node;
    size_t num_iothreads = 0;
    size_t cur_iothread = 0;

    if (!iothread_vq_mapping_validate(list, num_queues, errp)) {
        return false;
    }

    for (node = list; node; node = node->next) {
        num_iothreads++;
    }

    for (node = list; node; node = node->next) {
        IOThread *iothread = iothread_by_id(node->value->iothread);
        AioContext *ctx = iothread_get_aio_context(iothread);

        /* Released in virtio_blk_vq_aio_context_cleanup() */
        object_ref(OBJECT(iothread));

        if (node->value->vqs) {
            uint16List *vq;

            /* Explicit vq:IOThread assignment */
            for (vq = node->value->vqs; vq; vq = vq->next) {
                assert(vq->value < num_queues);
                vq_aio_context[vq->value] = ctx;
            }
        } else {
            /* Round-robin vq:IOThread assignment */
            for (unsigned i = cur_iothread; i < num_queues;
                 i += num_iothreads) {
                vq_aio_context[i] = ctx;
            }
        }

        cur_iothread++;
    }

    return true;
}

void iothread_vq_mapping_cleanup(IOThreadVirtQueueMappingList *list)
{
    IOThreadVirtQueueMappingList *node;

    for (node = list; node; node = node->next) {
        IOThread *iothread = iothread_by_id(node->value->iothread);
        object_unref(OBJECT(iothread));
    }
}

