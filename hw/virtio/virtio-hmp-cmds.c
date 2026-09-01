/*
 * HMP commands related to virtio
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/qapi-commands-virtio.h"
#include "qobject/qdict.h"


static void hmp_virtio_dump_protocols(MonitorHMP *hmp,
                                      VhostDeviceProtocols *pcol)
{
    strList *pcol_list = pcol->protocols;
    while (pcol_list) {
        monitor_hmp_printf(hmp, "\t%s", pcol_list->value);
        pcol_list = pcol_list->next;
        if (pcol_list != NULL) {
            monitor_hmp_printf(hmp, ",\n");
        }
    }
    monitor_hmp_printf(hmp, "\n");
    if (pcol->has_unknown_protocols) {
        monitor_hmp_printf(hmp, "  unknown-protocols(0x%016"PRIx64")\n",
                           pcol->unknown_protocols);
    }
}

static void hmp_virtio_dump_status(MonitorHMP *hmp,
                                   VirtioDeviceStatus *status)
{
    strList *status_list = status->statuses;
    while (status_list) {
        monitor_hmp_printf(hmp, "\t%s", status_list->value);
        status_list = status_list->next;
        if (status_list != NULL) {
            monitor_hmp_printf(hmp, ",\n");
        }
    }
    monitor_hmp_printf(hmp, "\n");
    if (status->has_unknown_statuses) {
        monitor_hmp_printf(hmp, "  unknown-statuses(0x%016"PRIx32")\n",
                           status->unknown_statuses);
    }
}

static void hmp_virtio_dump_features(MonitorHMP *hmp,
                                     VirtioDeviceFeatures *features)
{
    strList *transport_list = features->transports;
    while (transport_list) {
        monitor_hmp_printf(hmp, "\t%s", transport_list->value);
        transport_list = transport_list->next;
        if (transport_list != NULL) {
            monitor_hmp_printf(hmp, ",\n");
        }
    }

    monitor_hmp_printf(hmp, "\n");
    strList *list = features->dev_features;
    if (list) {
        while (list) {
            monitor_hmp_printf(hmp, "\t%s", list->value);
            list = list->next;
            if (list != NULL) {
                monitor_hmp_printf(hmp, ",\n");
            }
        }
        monitor_hmp_printf(hmp, "\n");
    }

    if (features->has_unknown_dev_features) {
        monitor_hmp_printf(hmp, "  unknown-features(0x%016"PRIx64"%016"PRIx64")\n",
                           features->unknown_dev_features2,
                           features->unknown_dev_features);
    }
}

void hmp_virtio_query(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    VirtioInfoList *list = qmp_x_query_virtio(&err);
    VirtioInfoList *node;

    if (err != NULL) {
        hmp_handle_error(hmp, err);
        return;
    }

    if (list == NULL) {
        monitor_hmp_printf(hmp, "No VirtIO devices\n");
        return;
    }

    node = list;
    while (node) {
        monitor_hmp_printf(hmp, "%s [%s]\n", node->value->path,
                           node->value->name);
        node = node->next;
    }
    qapi_free_VirtioInfoList(list);
}

void hmp_virtio_status(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    VirtioStatus *s = qmp_x_query_virtio_status(path, &err);

    if (err != NULL) {
        hmp_handle_error(hmp, err);
        return;
    }

    monitor_hmp_printf(hmp, "%s:\n", path);
    monitor_hmp_printf(hmp, "  device_name:             %s %s\n",
                       s->name, s->vhost_dev ? "(vhost)" : "");
    monitor_hmp_printf(hmp, "  device_id:               %d\n", s->device_id);
    monitor_hmp_printf(hmp, "  vhost_started:           %s\n",
                       s->vhost_started ? "true" : "false");
    monitor_hmp_printf(hmp, "  bus_name:                %s\n", s->bus_name);
    monitor_hmp_printf(hmp, "  broken:                  %s\n",
                       s->broken ? "true" : "false");
    monitor_hmp_printf(hmp, "  disabled:                %s\n",
                       s->disabled ? "true" : "false");
    monitor_hmp_printf(hmp, "  disable_legacy_check:    %s\n",
                       s->disable_legacy_check ? "true" : "false");
    monitor_hmp_printf(hmp, "  started:                 %s\n",
                       s->started ? "true" : "false");
    monitor_hmp_printf(hmp, "  use_started:             %s\n",
                       s->use_started ? "true" : "false");
    monitor_hmp_printf(hmp, "  start_on_kick:           %s\n",
                       s->start_on_kick ? "true" : "false");
    monitor_hmp_printf(hmp, "  use_guest_notifier_mask: %s\n",
                       s->use_guest_notifier_mask ? "true" : "false");
    monitor_hmp_printf(hmp, "  vm_running:              %s\n",
                       s->vm_running ? "true" : "false");
    monitor_hmp_printf(hmp, "  num_vqs:                 %"PRId64"\n", s->num_vqs);
    monitor_hmp_printf(hmp, "  queue_sel:               %d\n",
                       s->queue_sel);
    monitor_hmp_printf(hmp, "  isr:                     %d\n", s->isr);
    monitor_hmp_printf(hmp, "  endianness:              %s\n",
                       s->device_endian);
    monitor_hmp_printf(hmp, "  status:\n");
    hmp_virtio_dump_status(hmp, s->status);
    monitor_hmp_printf(hmp, "  Guest features:\n");
    hmp_virtio_dump_features(hmp, s->guest_features);
    monitor_hmp_printf(hmp, "  Host features:\n");
    hmp_virtio_dump_features(hmp, s->host_features);
    monitor_hmp_printf(hmp, "  Backend features:\n");
    hmp_virtio_dump_features(hmp, s->backend_features);

    if (s->vhost_dev) {
        monitor_hmp_printf(hmp, "  VHost:\n");
        monitor_hmp_printf(hmp, "    nvqs:           %d\n",
                           s->vhost_dev->nvqs);
        monitor_hmp_printf(hmp, "    vq_index:       %"PRId64"\n",
                           s->vhost_dev->vq_index);
        monitor_hmp_printf(hmp, "    max_queues:     %"PRId64"\n",
                           s->vhost_dev->max_queues);
        monitor_hmp_printf(hmp, "    n_mem_sections: %"PRId64"\n",
                           s->vhost_dev->n_mem_sections);
        monitor_hmp_printf(hmp, "    n_tmp_sections: %"PRId64"\n",
                           s->vhost_dev->n_tmp_sections);
        monitor_hmp_printf(hmp, "    backend_cap:    %"PRId64"\n",
                           s->vhost_dev->backend_cap);
        monitor_hmp_printf(hmp, "    log_enabled:    %s\n",
                           s->vhost_dev->log_enabled ? "true" : "false");
        monitor_hmp_printf(hmp, "    log_size:       %"PRId64"\n",
                           s->vhost_dev->log_size);
        monitor_hmp_printf(hmp, "    Features:\n");
        hmp_virtio_dump_features(hmp, s->vhost_dev->features);
        monitor_hmp_printf(hmp, "    Acked features:\n");
        hmp_virtio_dump_features(hmp, s->vhost_dev->acked_features);
        monitor_hmp_printf(hmp, "    Protocol features:\n");
        hmp_virtio_dump_protocols(hmp, s->vhost_dev->protocol_features);
    }

    qapi_free_VirtioStatus(s);
}

void hmp_vhost_queue_status(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtVhostQueueStatus *s =
        qmp_x_query_virtio_vhost_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(hmp, err);
        return;
    }

    monitor_hmp_printf(hmp, "%s:\n", path);
    monitor_hmp_printf(hmp, "  device_name:          %s (vhost)\n",
                       s->name);
    monitor_hmp_printf(hmp, "  kick:                 %"PRId64"\n", s->kick);
    monitor_hmp_printf(hmp, "  call:                 %"PRId64"\n", s->call);
    monitor_hmp_printf(hmp, "  VRing:\n");
    monitor_hmp_printf(hmp, "    num:         %"PRId64"\n", s->num);
    monitor_hmp_printf(hmp, "    desc_phys:   0x%016"PRIx64"\n",
                       s->desc_phys);
    monitor_hmp_printf(hmp, "    desc_size:   %"PRId32"\n", s->desc_size);
    monitor_hmp_printf(hmp, "    avail_phys:  0x%016"PRIx64"\n",
                       s->avail_phys);
    monitor_hmp_printf(hmp, "    avail_size:  %"PRId32"\n", s->avail_size);
    monitor_hmp_printf(hmp, "    used_phys:   0x%016"PRIx64"\n",
                       s->used_phys);
    monitor_hmp_printf(hmp, "    used_size:   %"PRId32"\n", s->used_size);

    qapi_free_VirtVhostQueueStatus(s);
}

void hmp_virtio_queue_status(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtQueueStatus *s = qmp_x_query_virtio_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(hmp, err);
        return;
    }

    monitor_hmp_printf(hmp, "%s:\n", path);
    monitor_hmp_printf(hmp, "  device_name:          %s\n", s->name);
    monitor_hmp_printf(hmp, "  queue_index:          %d\n", s->queue_index);
    monitor_hmp_printf(hmp, "  inuse:                %d\n", s->inuse);
    monitor_hmp_printf(hmp, "  used_idx:             %d\n", s->used_idx);
    monitor_hmp_printf(hmp, "  signalled_used:       %d\n",
                       s->signalled_used);
    monitor_hmp_printf(hmp, "  signalled_used_valid: %s\n",
                       s->signalled_used_valid ? "true" : "false");
    if (s->has_last_avail_idx) {
        monitor_hmp_printf(hmp, "  last_avail_idx:       %d\n",
                           s->last_avail_idx);
    }
    if (s->has_shadow_avail_idx) {
        monitor_hmp_printf(hmp, "  shadow_avail_idx:     %d\n",
                           s->shadow_avail_idx);
    }
    monitor_hmp_printf(hmp, "  VRing:\n");
    monitor_hmp_printf(hmp, "    num:          %"PRId32"\n", s->vring_num);
    monitor_hmp_printf(hmp, "    num_default:  %"PRId32"\n",
                       s->vring_num_default);
    monitor_hmp_printf(hmp, "    align:        %"PRId32"\n",
                       s->vring_align);
    monitor_hmp_printf(hmp, "    desc:         0x%016"PRIx64"\n",
                       s->vring_desc);
    monitor_hmp_printf(hmp, "    avail:        0x%016"PRIx64"\n",
                       s->vring_avail);
    monitor_hmp_printf(hmp, "    used:         0x%016"PRIx64"\n",
                       s->vring_used);

    qapi_free_VirtQueueStatus(s);
}

void hmp_virtio_queue_element(MonitorHMP *hmp, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    int index = qdict_get_try_int(qdict, "index", -1);
    VirtioQueueElement *e;
    VirtioRingDescList *list;

    e = qmp_x_query_virtio_queue_element(path, queue, index != -1,
                                         index, &err);
    if (err != NULL) {
        hmp_handle_error(hmp, err);
        return;
    }

    monitor_hmp_printf(hmp, "%s:\n", path);
    monitor_hmp_printf(hmp, "  device_name: %s\n", e->name);
    monitor_hmp_printf(hmp, "  index:   %d\n", e->index);
    monitor_hmp_printf(hmp, "  desc:\n");
    monitor_hmp_printf(hmp, "    descs:\n");

    list = e->descs;
    while (list) {
        monitor_hmp_printf(hmp, "        addr 0x%"PRIx64" len %d",
                           list->value->addr, list->value->len);
        if (list->value->flags) {
            strList *flag = list->value->flags;
            monitor_hmp_printf(hmp, " (");
            while (flag) {
                monitor_hmp_printf(hmp, "%s", flag->value);
                flag = flag->next;
                if (flag) {
                    monitor_hmp_printf(hmp, ", ");
                }
            }
            monitor_hmp_printf(hmp, ")");
        }
        list = list->next;
        if (list) {
            monitor_hmp_printf(hmp, ",\n");
        }
    }
    monitor_hmp_printf(hmp, "\n");
    monitor_hmp_printf(hmp, "  avail:\n");
    monitor_hmp_printf(hmp, "    flags: %d\n", e->avail->flags);
    monitor_hmp_printf(hmp, "    idx:   %d\n", e->avail->idx);
    monitor_hmp_printf(hmp, "    ring:  %d\n", e->avail->ring);
    monitor_hmp_printf(hmp, "  used:\n");
    monitor_hmp_printf(hmp, "    flags: %d\n", e->used->flags);
    monitor_hmp_printf(hmp, "    idx:   %d\n", e->used->idx);

    qapi_free_VirtioQueueElement(e);
}
