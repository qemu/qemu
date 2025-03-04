/*
 * Multifd device state migration
 *
 * Copyright (C) 2024,2025 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/lockable.h"
#include "migration/misc.h"
#include "multifd.h"

static struct {
    QemuMutex queue_job_mutex;

    MultiFDSendData *send_data;
} *multifd_send_device_state;

size_t multifd_device_state_payload_size(void)
{
    return sizeof(MultiFDDeviceState_t);
}

void multifd_device_state_send_setup(void)
{
    assert(!multifd_send_device_state);
    multifd_send_device_state = g_malloc(sizeof(*multifd_send_device_state));

    qemu_mutex_init(&multifd_send_device_state->queue_job_mutex);

    multifd_send_device_state->send_data = multifd_send_data_alloc();
}

void multifd_device_state_send_cleanup(void)
{
    g_clear_pointer(&multifd_send_device_state->send_data,
                    multifd_send_data_free);

    qemu_mutex_destroy(&multifd_send_device_state->queue_job_mutex);

    g_clear_pointer(&multifd_send_device_state, g_free);
}

void multifd_send_data_clear_device_state(MultiFDDeviceState_t *device_state)
{
    g_clear_pointer(&device_state->idstr, g_free);
    g_clear_pointer(&device_state->buf, g_free);
}

static void multifd_device_state_fill_packet(MultiFDSendParams *p)
{
    MultiFDDeviceState_t *device_state = &p->data->u.device_state;
    MultiFDPacketDeviceState_t *packet = p->packet_device_state;

    packet->hdr.flags = cpu_to_be32(p->flags);
    strncpy(packet->idstr, device_state->idstr, sizeof(packet->idstr) - 1);
    packet->idstr[sizeof(packet->idstr) - 1] = 0;
    packet->instance_id = cpu_to_be32(device_state->instance_id);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);
}

static void multifd_prepare_header_device_state(MultiFDSendParams *p)
{
    p->iov[0].iov_len = sizeof(*p->packet_device_state);
    p->iov[0].iov_base = p->packet_device_state;
    p->iovs_num++;
}

void multifd_device_state_send_prepare(MultiFDSendParams *p)
{
    MultiFDDeviceState_t *device_state = &p->data->u.device_state;

    assert(multifd_payload_device_state(p->data));

    multifd_prepare_header_device_state(p);

    assert(!(p->flags & MULTIFD_FLAG_SYNC));

    p->next_packet_size = device_state->buf_len;
    if (p->next_packet_size > 0) {
        p->iov[p->iovs_num].iov_base = device_state->buf;
        p->iov[p->iovs_num].iov_len = p->next_packet_size;
        p->iovs_num++;
    }

    p->flags |= MULTIFD_FLAG_NOCOMP | MULTIFD_FLAG_DEVICE_STATE;

    multifd_device_state_fill_packet(p);
}

bool multifd_queue_device_state(char *idstr, uint32_t instance_id,
                                char *data, size_t len)
{
    /* Device state submissions can come from multiple threads */
    QEMU_LOCK_GUARD(&multifd_send_device_state->queue_job_mutex);
    MultiFDDeviceState_t *device_state;

    assert(multifd_payload_empty(multifd_send_device_state->send_data));

    multifd_set_payload_type(multifd_send_device_state->send_data,
                             MULTIFD_PAYLOAD_DEVICE_STATE);
    device_state = &multifd_send_device_state->send_data->u.device_state;
    device_state->idstr = g_strdup(idstr);
    device_state->instance_id = instance_id;
    device_state->buf = g_memdup2(data, len);
    device_state->buf_len = len;

    if (!multifd_send(&multifd_send_device_state->send_data)) {
        multifd_send_data_clear(multifd_send_device_state->send_data);
        return false;
    }

    return true;
}
