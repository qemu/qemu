/*
 * IOThread Virtqueue Mapping
 *
 * Copyright Red Hat, Inc
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef HW_VIRTIO_IOTHREAD_VQ_MAPPING_H
#define HW_VIRTIO_IOTHREAD_VQ_MAPPING_H

#include "qapi/error.h"
#include "qapi/qapi-types-virtio.h"

/**
 * iothread_vq_mapping_apply:
 * @list: The mapping of virtqueues to IOThreads.
 * @vq_aio_context: The array of AioContext pointers to fill in.
 * @num_queues: The length of @vq_aio_context.
 * @errp: If an error occurs, a pointer to the area to store the error.
 *
 * Fill in the AioContext for each virtqueue in the @vq_aio_context array given
 * the iothread-vq-mapping parameter in @list.
 *
 * iothread_vq_mapping_cleanup() must be called to free IOThread object
 * references after this function returns success.
 *
 * Returns: %true on success, %false on failure.
 **/
bool iothread_vq_mapping_apply(
        IOThreadVirtQueueMappingList *list,
        AioContext **vq_aio_context,
        uint16_t num_queues,
        Error **errp);

/**
 * iothread_vq_mapping_cleanup:
 * @list: The mapping of virtqueues to IOThreads.
 *
 * Release IOThread object references that were acquired by
 * iothread_vq_mapping_apply().
 */
void iothread_vq_mapping_cleanup(IOThreadVirtQueueMappingList *list);

#endif /* HW_VIRTIO_IOTHREAD_VQ_MAPPING_H */
