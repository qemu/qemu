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


static void hmp_virtio_dump_protocols(Monitor *mon,
                                      VhostDeviceProtocols *pcol)
{
    strList *pcol_list = pcol->protocols;
    while (pcol_list) {
        monitor_printf(mon, "\t%s", pcol_list->value);
        pcol_list = pcol_list->next;
        if (pcol_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    if (pcol->has_unknown_protocols) {
        monitor_printf(mon, "  unknown-protocols(0x%016"PRIx64")\n",
                       pcol->unknown_protocols);
    }
}

static void hmp_virtio_dump_status(Monitor *mon,
                                   VirtioDeviceStatus *status)
{
    strList *status_list = status->statuses;
    while (status_list) {
        monitor_printf(mon, "\t%s", status_list->value);
        status_list = status_list->next;
        if (status_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    if (status->has_unknown_statuses) {
        monitor_printf(mon, "  unknown-statuses(0x%016"PRIx32")\n",
                       status->unknown_statuses);
    }
}

static void hmp_virtio_dump_features(Monitor *mon,
                                     VirtioDeviceFeatures *features)
{
    strList *transport_list = features->transports;
    while (transport_list) {
        monitor_printf(mon, "\t%s", transport_list->value);
        transport_list = transport_list->next;
        if (transport_list != NULL) {
            monitor_printf(mon, ",\n");
        }
    }

    monitor_printf(mon, "\n");
    strList *list = features->dev_features;
    if (list) {
        while (list) {
            monitor_printf(mon, "\t%s", list->value);
            list = list->next;
            if (list != NULL) {
                monitor_printf(mon, ",\n");
            }
        }
        monitor_printf(mon, "\n");
    }

    if (features->has_unknown_dev_features) {
        monitor_printf(mon, "  unknown-features(0x%016"PRIx64"%016"PRIx64")\n",
                       features->unknown_dev_features2,
                       features->unknown_dev_features);
    }
}

void hmp_virtio_query(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    VirtioInfoList *list = qmp_x_query_virtio(&err);
    VirtioInfoList *node;

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    if (list == NULL) {
        monitor_printf(mon, "No VirtIO devices\n");
        return;
    }

    node = list;
    while (node) {
        monitor_printf(mon, "%s [%s]\n", node->value->path,
                       node->value->name);
        node = node->next;
    }
    qapi_free_VirtioInfoList(list);
}

void hmp_virtio_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    VirtioStatus *s = qmp_x_query_virtio_status(path, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:             %s %s\n",
                   s->name, s->vhost_dev ? "(vhost)" : "");
    monitor_printf(mon, "  device_id:               %d\n", s->device_id);
    monitor_printf(mon, "  vhost_started:           %s\n",
                   s->vhost_started ? "true" : "false");
    monitor_printf(mon, "  bus_name:                %s\n", s->bus_name);
    monitor_printf(mon, "  broken:                  %s\n",
                   s->broken ? "true" : "false");
    monitor_printf(mon, "  disabled:                %s\n",
                   s->disabled ? "true" : "false");
    monitor_printf(mon, "  disable_legacy_check:    %s\n",
                   s->disable_legacy_check ? "true" : "false");
    monitor_printf(mon, "  started:                 %s\n",
                   s->started ? "true" : "false");
    monitor_printf(mon, "  use_started:             %s\n",
                   s->use_started ? "true" : "false");
    monitor_printf(mon, "  start_on_kick:           %s\n",
                   s->start_on_kick ? "true" : "false");
    monitor_printf(mon, "  use_guest_notifier_mask: %s\n",
                   s->use_guest_notifier_mask ? "true" : "false");
    monitor_printf(mon, "  vm_running:              %s\n",
                   s->vm_running ? "true" : "false");
    monitor_printf(mon, "  num_vqs:                 %"PRId64"\n", s->num_vqs);
    monitor_printf(mon, "  queue_sel:               %d\n",
                   s->queue_sel);
    monitor_printf(mon, "  isr:                     %d\n", s->isr);
    monitor_printf(mon, "  endianness:              %s\n",
                   s->device_endian);
    monitor_printf(mon, "  status:\n");
    hmp_virtio_dump_status(mon, s->status);
    monitor_printf(mon, "  Guest features:\n");
    hmp_virtio_dump_features(mon, s->guest_features);
    monitor_printf(mon, "  Host features:\n");
    hmp_virtio_dump_features(mon, s->host_features);
    monitor_printf(mon, "  Backend features:\n");
    hmp_virtio_dump_features(mon, s->backend_features);

    if (s->vhost_dev) {
        monitor_printf(mon, "  VHost:\n");
        monitor_printf(mon, "    nvqs:           %d\n",
                       s->vhost_dev->nvqs);
        monitor_printf(mon, "    vq_index:       %"PRId64"\n",
                       s->vhost_dev->vq_index);
        monitor_printf(mon, "    max_queues:     %"PRId64"\n",
                       s->vhost_dev->max_queues);
        monitor_printf(mon, "    n_mem_sections: %"PRId64"\n",
                       s->vhost_dev->n_mem_sections);
        monitor_printf(mon, "    n_tmp_sections: %"PRId64"\n",
                       s->vhost_dev->n_tmp_sections);
        monitor_printf(mon, "    backend_cap:    %"PRId64"\n",
                       s->vhost_dev->backend_cap);
        monitor_printf(mon, "    log_enabled:    %s\n",
                       s->vhost_dev->log_enabled ? "true" : "false");
        monitor_printf(mon, "    log_size:       %"PRId64"\n",
                       s->vhost_dev->log_size);
        monitor_printf(mon, "    Features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->features);
        monitor_printf(mon, "    Acked features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->acked_features);
        monitor_printf(mon, "    Backend features:\n");
        hmp_virtio_dump_features(mon, s->vhost_dev->backend_features);
        monitor_printf(mon, "    Protocol features:\n");
        hmp_virtio_dump_protocols(mon, s->vhost_dev->protocol_features);
    }

    qapi_free_VirtioStatus(s);
}

void hmp_vhost_queue_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtVhostQueueStatus *s =
        qmp_x_query_virtio_vhost_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:          %s (vhost)\n",
                   s->name);
    monitor_printf(mon, "  kick:                 %"PRId64"\n", s->kick);
    monitor_printf(mon, "  call:                 %"PRId64"\n", s->call);
    monitor_printf(mon, "  VRing:\n");
    monitor_printf(mon, "    num:         %"PRId64"\n", s->num);
    monitor_printf(mon, "    desc:        0x%016"PRIx64"\n", s->desc);
    monitor_printf(mon, "    desc_phys:   0x%016"PRIx64"\n",
                   s->desc_phys);
    monitor_printf(mon, "    desc_size:   %"PRId32"\n", s->desc_size);
    monitor_printf(mon, "    avail:       0x%016"PRIx64"\n", s->avail);
    monitor_printf(mon, "    avail_phys:  0x%016"PRIx64"\n",
                   s->avail_phys);
    monitor_printf(mon, "    avail_size:  %"PRId32"\n", s->avail_size);
    monitor_printf(mon, "    used:        0x%016"PRIx64"\n", s->used);
    monitor_printf(mon, "    used_phys:   0x%016"PRIx64"\n",
                   s->used_phys);
    monitor_printf(mon, "    used_size:   %"PRId32"\n", s->used_size);

    qapi_free_VirtVhostQueueStatus(s);
}

void hmp_virtio_queue_status(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;
    const char *path = qdict_get_try_str(qdict, "path");
    int queue = qdict_get_int(qdict, "queue");
    VirtQueueStatus *s = qmp_x_query_virtio_queue_status(path, queue, &err);

    if (err != NULL) {
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name:          %s\n", s->name);
    monitor_printf(mon, "  queue_index:          %d\n", s->queue_index);
    monitor_printf(mon, "  inuse:                %d\n", s->inuse);
    monitor_printf(mon, "  used_idx:             %d\n", s->used_idx);
    monitor_printf(mon, "  signalled_used:       %d\n",
                   s->signalled_used);
    monitor_printf(mon, "  signalled_used_valid: %s\n",
                   s->signalled_used_valid ? "true" : "false");
    if (s->has_last_avail_idx) {
        monitor_printf(mon, "  last_avail_idx:       %d\n",
                       s->last_avail_idx);
    }
    if (s->has_shadow_avail_idx) {
        monitor_printf(mon, "  shadow_avail_idx:     %d\n",
                       s->shadow_avail_idx);
    }
    monitor_printf(mon, "  VRing:\n");
    monitor_printf(mon, "    num:          %"PRId32"\n", s->vring_num);
    monitor_printf(mon, "    num_default:  %"PRId32"\n",
                   s->vring_num_default);
    monitor_printf(mon, "    align:        %"PRId32"\n",
                   s->vring_align);
    monitor_printf(mon, "    desc:         0x%016"PRIx64"\n",
                   s->vring_desc);
    monitor_printf(mon, "    avail:        0x%016"PRIx64"\n",
                   s->vring_avail);
    monitor_printf(mon, "    used:         0x%016"PRIx64"\n",
                   s->vring_used);

    qapi_free_VirtQueueStatus(s);
}

void hmp_virtio_queue_element(Monitor *mon, const QDict *qdict)
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
        hmp_handle_error(mon, err);
        return;
    }

    monitor_printf(mon, "%s:\n", path);
    monitor_printf(mon, "  device_name: %s\n", e->name);
    monitor_printf(mon, "  index:   %d\n", e->index);
    monitor_printf(mon, "  desc:\n");
    monitor_printf(mon, "    descs:\n");

    list = e->descs;
    while (list) {
        monitor_printf(mon, "        addr 0x%"PRIx64" len %d",
                       list->value->addr, list->value->len);
        if (list->value->flags) {
            strList *flag = list->value->flags;
            monitor_printf(mon, " (");
            while (flag) {
                monitor_printf(mon, "%s", flag->value);
                flag = flag->next;
                if (flag) {
                    monitor_printf(mon, ", ");
                }
            }
            monitor_printf(mon, ")");
        }
        list = list->next;
        if (list) {
            monitor_printf(mon, ",\n");
        }
    }
    monitor_printf(mon, "\n");
    monitor_printf(mon, "  avail:\n");
    monitor_printf(mon, "    flags: %d\n", e->avail->flags);
    monitor_printf(mon, "    idx:   %d\n", e->avail->idx);
    monitor_printf(mon, "    ring:  %d\n", e->avail->ring);
    monitor_printf(mon, "  used:\n");
    monitor_printf(mon, "    flags: %d\n", e->used->flags);
    monitor_printf(mon, "    idx:   %d\n", e->used->idx);

    qapi_free_VirtioQueueElement(e);
}
