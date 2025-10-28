/*
 * virtio ccw target implementation
 *
 * Copyright 2012,2015 IBM Corp.
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *            Pierre Morel <pmorel@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/kvm.h"
#include "net/net.h"
#include "hw/virtio/virtio.h"
#include "migration/qemu-file-types.h"
#include "hw/virtio/virtio-net.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/s390x/adapter.h"
#include "hw/s390x/s390_flic.h"

#include "hw/s390x/ioinst.h"
#include "hw/s390x/css.h"
#include "virtio-ccw.h"
#include "trace.h"
#include "hw/s390x/css-bridge.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "system/replay.h"

#define NR_CLASSIC_INDICATOR_BITS 64

bool have_virtio_ccw = true;

static int virtio_ccw_dev_post_load(void *opaque, int version_id)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(opaque);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    CCWDeviceClass *ck = CCW_DEVICE_GET_CLASS(ccw_dev);

    ccw_dev->sch->driver_data = dev;
    if (ccw_dev->sch->thinint_active) {
        dev->routes.adapter.adapter_id = css_get_adapter_id(
                                         CSS_IO_ADAPTER_VIRTIO,
                                         dev->thinint_isc);
    }
    /* Re-fill subch_id after loading the subchannel states.*/
    if (ck->refill_ids) {
        ck->refill_ids(ccw_dev);
    }
    return 0;
}

typedef struct VirtioCcwDeviceTmp {
    VirtioCcwDevice *parent;
    uint16_t config_vector;
} VirtioCcwDeviceTmp;

static int virtio_ccw_dev_tmp_pre_save(void *opaque)
{
    VirtioCcwDeviceTmp *tmp = opaque;
    VirtioCcwDevice *dev = tmp->parent;
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);

    tmp->config_vector = vdev->config_vector;

    return 0;
}

static int virtio_ccw_dev_tmp_post_load(void *opaque, int version_id)
{
    VirtioCcwDeviceTmp *tmp = opaque;
    VirtioCcwDevice *dev = tmp->parent;
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);

    vdev->config_vector = tmp->config_vector;
    return 0;
}

const VMStateDescription vmstate_virtio_ccw_dev_tmp = {
    .name = "s390_virtio_ccw_dev_tmp",
    .pre_save = virtio_ccw_dev_tmp_pre_save,
    .post_load = virtio_ccw_dev_tmp_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(config_vector, VirtioCcwDeviceTmp),
        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_virtio_ccw_dev = {
    .name = "s390_virtio_ccw_dev",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_ccw_dev_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CCW_DEVICE(parent_obj, VirtioCcwDevice),
        VMSTATE_PTR_TO_IND_ADDR(indicators, VirtioCcwDevice),
        VMSTATE_PTR_TO_IND_ADDR(indicators2, VirtioCcwDevice),
        VMSTATE_PTR_TO_IND_ADDR(summary_indicator, VirtioCcwDevice),
        /*
         * Ugly hack because VirtIODevice does not migrate itself.
         * This also makes legacy via vmstate_save_state possible.
         */
        VMSTATE_WITH_TMP(VirtioCcwDevice, VirtioCcwDeviceTmp,
                         vmstate_virtio_ccw_dev_tmp),
        VMSTATE_STRUCT(routes, VirtioCcwDevice, 1, vmstate_adapter_routes,
                       AdapterRoutes),
        VMSTATE_UINT8(thinint_isc, VirtioCcwDevice),
        VMSTATE_INT32(revision, VirtioCcwDevice),
        VMSTATE_END_OF_LIST()
    }
};

static void virtio_ccw_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtioCcwDevice *dev);

VirtIODevice *virtio_ccw_get_vdev(SubchDev *sch)
{
    VirtIODevice *vdev = NULL;
    VirtioCcwDevice *dev = sch->driver_data;

    if (dev) {
        vdev = virtio_bus_get_device(&dev->bus);
    }
    return vdev;
}

static void virtio_ccw_start_ioeventfd(VirtioCcwDevice *dev)
{
    virtio_bus_start_ioeventfd(&dev->bus);
}

static void virtio_ccw_stop_ioeventfd(VirtioCcwDevice *dev)
{
    virtio_bus_stop_ioeventfd(&dev->bus);
}

static bool virtio_ccw_ioeventfd_enabled(DeviceState *d)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);

    return (dev->flags & VIRTIO_CCW_FLAG_USE_IOEVENTFD) != 0;
}

static int virtio_ccw_ioeventfd_assign(DeviceState *d, EventNotifier *notifier,
                                       int n, bool assign)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    SubchDev *sch = ccw_dev->sch;
    uint32_t sch_id = (css_build_subchannel_id(sch) << 16) | sch->schid;

    return s390_assign_subch_ioeventfd(notifier, sch_id, n, assign);
}

/* Communication blocks used by several channel commands. */
typedef struct VqInfoBlockLegacy {
    uint64_t queue;
    uint32_t align;
    uint16_t index;
    uint16_t num;
} QEMU_PACKED VqInfoBlockLegacy;

typedef struct VqInfoBlock {
    uint64_t desc;
    uint32_t res0;
    uint16_t index;
    uint16_t num;
    uint64_t avail;
    uint64_t used;
} QEMU_PACKED VqInfoBlock;

typedef struct VqConfigBlock {
    uint16_t index;
    uint16_t num_max;
} QEMU_PACKED VqConfigBlock;

typedef struct VirtioFeatDesc {
    uint32_t features;
    uint8_t index;
} QEMU_PACKED VirtioFeatDesc;

typedef struct VirtioThinintInfo {
    hwaddr summary_indicator;
    hwaddr device_indicator;
    uint64_t ind_bit;
    uint8_t isc;
} QEMU_PACKED VirtioThinintInfo;

typedef struct VirtioRevInfo {
    uint16_t revision;
    uint16_t length;
    uint8_t data[];
} QEMU_PACKED VirtioRevInfo;

/* Specify where the virtqueues for the subchannel are in guest memory. */
static int virtio_ccw_set_vqs(SubchDev *sch, VqInfoBlock *info,
                              VqInfoBlockLegacy *linfo)
{
    VirtIODevice *vdev = virtio_ccw_get_vdev(sch);
    uint16_t index = info ? info->index : linfo->index;
    uint16_t num = info ? info->num : linfo->num;
    uint64_t desc = info ? info->desc : linfo->queue;

    if (index >= VIRTIO_QUEUE_MAX) {
        return -EINVAL;
    }

    /* Current code in virtio.c relies on 4K alignment. */
    if (linfo && desc && (linfo->align != 4096)) {
        return -EINVAL;
    }

    if (!vdev) {
        return -EINVAL;
    }

    if (info) {
        virtio_queue_set_rings(vdev, index, desc, info->avail, info->used);
    } else {
        virtio_queue_set_addr(vdev, index, desc);
    }
    if (!desc) {
        virtio_queue_set_vector(vdev, index, VIRTIO_NO_VECTOR);
    } else {
        if (info) {
            /* virtio-1 allows changing the ring size. */
            if (virtio_queue_get_max_num(vdev, index) < num) {
                /* Fail if we exceed the maximum number. */
                return -EINVAL;
            }
            virtio_queue_set_num(vdev, index, num);
            virtio_init_region_cache(vdev, index);
        } else if (virtio_queue_get_num(vdev, index) > num) {
            /* Fail if we don't have a big enough queue. */
            return -EINVAL;
        }
        /* We ignore possible increased num for legacy for compatibility. */
        virtio_queue_set_vector(vdev, index, index);
    }
    /* tell notify handler in case of config change */
    vdev->config_vector = VIRTIO_QUEUE_MAX;
    return 0;
}

static void virtio_ccw_reset_virtio(VirtioCcwDevice *dev)
{
    CcwDevice *ccw_dev = CCW_DEVICE(dev);

    virtio_bus_reset(&dev->bus);
    if (dev->indicators) {
        release_indicator(&dev->routes.adapter, dev->indicators);
        dev->indicators = NULL;
    }
    if (dev->indicators2) {
        release_indicator(&dev->routes.adapter, dev->indicators2);
        dev->indicators2 = NULL;
    }
    if (dev->summary_indicator) {
        release_indicator(&dev->routes.adapter, dev->summary_indicator);
        dev->summary_indicator = NULL;
    }
    ccw_dev->sch->thinint_active = false;
}

static int virtio_ccw_handle_set_vq(SubchDev *sch, CCW1 ccw, bool check_len,
                                    bool is_legacy)
{
    int ret;
    VqInfoBlock info;
    VqInfoBlockLegacy linfo;
    size_t info_len = is_legacy ? sizeof(linfo) : sizeof(info);

    if (check_len) {
        if (ccw.count != info_len) {
            return -EINVAL;
        }
    } else if (ccw.count < info_len) {
        /* Can't execute command. */
        return -EINVAL;
    }
    if (!ccw.cda) {
        return -EFAULT;
    }
    if (is_legacy) {
        ret = ccw_dstream_read(&sch->cds, linfo);
        if (ret) {
            return ret;
        }
        linfo.queue = be64_to_cpu(linfo.queue);
        linfo.align = be32_to_cpu(linfo.align);
        linfo.index = be16_to_cpu(linfo.index);
        linfo.num = be16_to_cpu(linfo.num);
        ret = virtio_ccw_set_vqs(sch, NULL, &linfo);
    } else {
        ret = ccw_dstream_read(&sch->cds, info);
        if (ret) {
            return ret;
        }
        info.desc = be64_to_cpu(info.desc);
        info.index = be16_to_cpu(info.index);
        info.num = be16_to_cpu(info.num);
        info.avail = be64_to_cpu(info.avail);
        info.used = be64_to_cpu(info.used);
        ret = virtio_ccw_set_vqs(sch, &info, NULL);
    }
    sch->curr_status.scsw.count = 0;
    return ret;
}

static int virtio_ccw_cb(SubchDev *sch, CCW1 ccw)
{
    int ret;
    VirtioRevInfo revinfo;
    uint8_t status;
    VirtioFeatDesc features;
    hwaddr indicators;
    VqConfigBlock vq_config;
    VirtioCcwDevice *dev = sch->driver_data;
    VirtIODevice *vdev = virtio_ccw_get_vdev(sch);
    bool check_len;
    int len;
    VirtioThinintInfo thinint;

    if (!dev) {
        return -EINVAL;
    }

    trace_virtio_ccw_interpret_ccw(sch->cssid, sch->ssid, sch->schid,
                                   ccw.cmd_code);
    check_len = !((ccw.flags & CCW_FLAG_SLI) && !(ccw.flags & CCW_FLAG_DC));

    if (dev->revision < 0 && ccw.cmd_code != CCW_CMD_SET_VIRTIO_REV) {
        if (dev->force_revision_1) {
            /*
             * virtio-1 drivers must start with negotiating to a revision >= 1,
             * so post a command reject for all other commands
             */
            return -ENOSYS;
        } else {
            /*
             * If the driver issues any command that is not SET_VIRTIO_REV,
             * we'll have to operate the device in legacy mode.
             */
            dev->revision = 0;
        }
    }

    /* Look at the command. */
    switch (ccw.cmd_code) {
    case CCW_CMD_SET_VQ:
        ret = virtio_ccw_handle_set_vq(sch, ccw, check_len, dev->revision < 1);
        break;
    case CCW_CMD_VDEV_RESET:
        virtio_ccw_reset_virtio(dev);
        ret = 0;
        break;
    case CCW_CMD_READ_FEAT:
        if (check_len) {
            if (ccw.count != sizeof(features)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(features)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            VirtioDeviceClass *vdc = VIRTIO_DEVICE_GET_CLASS(vdev);

            ccw_dstream_advance(&sch->cds, sizeof(features.features));
            ret = ccw_dstream_read(&sch->cds, features.index);
            if (ret) {
                break;
            }
            if (features.index == 0) {
                if (dev->revision >= 1) {
                    /* Don't offer legacy features for modern devices. */
                    features.features = (uint32_t)
                        (vdev->host_features & ~vdc->legacy_features);
                } else {
                    features.features = (uint32_t)vdev->host_features;
                }
            } else if ((features.index == 1) && (dev->revision >= 1)) {
                /*
                 * Only offer feature bits beyond 31 if the guest has
                 * negotiated at least revision 1.
                 */
                features.features = (uint32_t)(vdev->host_features >> 32);
            } else {
                /* Return zeroes if the guest supports more feature bits. */
                features.features = 0;
            }
            ccw_dstream_rewind(&sch->cds);
            features.features = cpu_to_le32(features.features);
            ret = ccw_dstream_write(&sch->cds, features.features);
            if (!ret) {
                sch->curr_status.scsw.count = ccw.count - sizeof(features);
            }
        }
        break;
    case CCW_CMD_WRITE_FEAT:
        if (check_len) {
            if (ccw.count != sizeof(features)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(features)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read(&sch->cds, features);
            if (ret) {
                break;
            }
            features.features = le32_to_cpu(features.features);
            if (features.index == 0) {
                virtio_set_features(vdev,
                                    (vdev->guest_features & 0xffffffff00000000ULL) |
                                    features.features);
            } else if ((features.index == 1) && (dev->revision >= 1)) {
                /*
                 * If the guest did not negotiate at least revision 1,
                 * we did not offer it any feature bits beyond 31. Such a
                 * guest passing us any bit here is therefore buggy.
                 */
                virtio_set_features(vdev,
                                    (vdev->guest_features & 0x00000000ffffffffULL) |
                                    ((uint64_t)features.features << 32));
            } else {
                /*
                 * If the guest supports more feature bits, assert that it
                 * passes us zeroes for those we don't support.
                 */
                if (features.features) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "Guest bug: features[%i]=%x (expected 0)",
                                  features.index, features.features);
                    /* XXX: do a unit check here? */
                }
            }
            sch->curr_status.scsw.count = ccw.count - sizeof(features);
            ret = 0;
        }
        break;
    case CCW_CMD_READ_CONF:
        if (check_len) {
            if (ccw.count > vdev->config_len) {
                ret = -EINVAL;
                break;
            }
        }
        len = MIN(ccw.count, vdev->config_len);
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            virtio_bus_get_vdev_config(&dev->bus, vdev->config);
            ret = ccw_dstream_write_buf(&sch->cds, vdev->config, len);
            if (ret) {
                sch->curr_status.scsw.count = ccw.count - len;
            }
        }
        break;
    case CCW_CMD_WRITE_CONF:
        if (check_len) {
            if (ccw.count > vdev->config_len) {
                ret = -EINVAL;
                break;
            }
        }
        len = MIN(ccw.count, vdev->config_len);
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read_buf(&sch->cds, vdev->config, len);
            if (!ret) {
                virtio_bus_set_vdev_config(&dev->bus, vdev->config);
                sch->curr_status.scsw.count = ccw.count - len;
            }
        }
        break;
    case CCW_CMD_READ_STATUS:
        if (check_len) {
            if (ccw.count != sizeof(status)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(status)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            address_space_stb(&address_space_memory, ccw.cda, vdev->status,
                                        MEMTXATTRS_UNSPECIFIED, NULL);
            sch->curr_status.scsw.count = ccw.count - sizeof(vdev->status);
            ret = 0;
        }
        break;
    case CCW_CMD_WRITE_STATUS:
        if (check_len) {
            if (ccw.count != sizeof(status)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(status)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read(&sch->cds, status);
            if (ret) {
                break;
            }
            if (!(status & VIRTIO_CONFIG_S_DRIVER_OK)) {
                virtio_ccw_stop_ioeventfd(dev);
            }
            if (virtio_set_status(vdev, status) == 0) {
                if (vdev->status == 0) {
                    virtio_ccw_reset_virtio(dev);
                }
                if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
                    virtio_ccw_start_ioeventfd(dev);
                }
                sch->curr_status.scsw.count = ccw.count - sizeof(status);
                ret = 0;
            } else {
                /* Trigger a command reject. */
                ret = -ENOSYS;
            }
        }
        break;
    case CCW_CMD_SET_IND:
        if (check_len) {
            if (ccw.count != sizeof(indicators)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(indicators)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (sch->thinint_active) {
            /* Trigger a command reject. */
            ret = -ENOSYS;
            break;
        }
        if (virtio_get_num_queues(vdev) > NR_CLASSIC_INDICATOR_BITS) {
            /* More queues than indicator bits --> trigger a reject */
            ret = -ENOSYS;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read(&sch->cds, indicators);
            if (ret) {
                break;
            }
            indicators = be64_to_cpu(indicators);
            dev->indicators = get_indicator(indicators, sizeof(uint64_t));
            sch->curr_status.scsw.count = ccw.count - sizeof(indicators);
            ret = 0;
        }
        break;
    case CCW_CMD_SET_CONF_IND:
        if (check_len) {
            if (ccw.count != sizeof(indicators)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(indicators)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read(&sch->cds, indicators);
            if (ret) {
                break;
            }
            indicators = be64_to_cpu(indicators);
            dev->indicators2 = get_indicator(indicators, sizeof(uint64_t));
            sch->curr_status.scsw.count = ccw.count - sizeof(indicators);
            ret = 0;
        }
        break;
    case CCW_CMD_READ_VQ_CONF:
        if (check_len) {
            if (ccw.count != sizeof(vq_config)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(vq_config)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else {
            ret = ccw_dstream_read(&sch->cds, vq_config.index);
            if (ret) {
                break;
            }
            vq_config.index = be16_to_cpu(vq_config.index);
            if (vq_config.index >= VIRTIO_QUEUE_MAX) {
                ret = -EINVAL;
                break;
            }
            vq_config.num_max = virtio_queue_get_num(vdev,
                                                     vq_config.index);
            vq_config.num_max = cpu_to_be16(vq_config.num_max);
            ret = ccw_dstream_write(&sch->cds, vq_config.num_max);
            if (!ret) {
                sch->curr_status.scsw.count = ccw.count - sizeof(vq_config);
            }
        }
        break;
    case CCW_CMD_SET_IND_ADAPTER:
        if (check_len) {
            if (ccw.count != sizeof(thinint)) {
                ret = -EINVAL;
                break;
            }
        } else if (ccw.count < sizeof(thinint)) {
            /* Can't execute command. */
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
        } else if (dev->indicators && !sch->thinint_active) {
            /* Trigger a command reject. */
            ret = -ENOSYS;
        } else {
            if (ccw_dstream_read(&sch->cds, thinint)) {
                ret = -EFAULT;
            } else {
                thinint.ind_bit = be64_to_cpu(thinint.ind_bit);
                thinint.summary_indicator =
                    be64_to_cpu(thinint.summary_indicator);
                thinint.device_indicator =
                    be64_to_cpu(thinint.device_indicator);

                dev->summary_indicator =
                    get_indicator(thinint.summary_indicator, sizeof(uint8_t));
                dev->indicators =
                    get_indicator(thinint.device_indicator,
                                  thinint.ind_bit / 8 + 1);
                dev->thinint_isc = thinint.isc;
                dev->routes.adapter.ind_offset = thinint.ind_bit;
                dev->routes.adapter.summary_offset = 7;
                dev->routes.adapter.adapter_id = css_get_adapter_id(
                                                 CSS_IO_ADAPTER_VIRTIO,
                                                 dev->thinint_isc);
                sch->thinint_active = ((dev->indicators != NULL) &&
                                       (dev->summary_indicator != NULL));
                sch->curr_status.scsw.count = ccw.count - sizeof(thinint);
                ret = 0;
            }
        }
        break;
    case CCW_CMD_SET_VIRTIO_REV:
        len = sizeof(revinfo);
        if (ccw.count < len) {
            ret = -EINVAL;
            break;
        }
        if (!ccw.cda) {
            ret = -EFAULT;
            break;
        }
        ret = ccw_dstream_read_buf(&sch->cds, &revinfo, 4);
        if (ret < 0) {
            break;
        }
        revinfo.revision = be16_to_cpu(revinfo.revision);
        revinfo.length = be16_to_cpu(revinfo.length);
        if (ccw.count < len + revinfo.length ||
            (check_len && ccw.count > len + revinfo.length)) {
            ret = -EINVAL;
            break;
        }
        /*
         * Once we start to support revisions with additional data, we'll
         * need to fetch it here. Nothing to do for now, though.
         */
        if (dev->revision >= 0 ||
            revinfo.revision > virtio_ccw_rev_max(dev) ||
            (dev->force_revision_1 && !revinfo.revision)) {
            ret = -ENOSYS;
            break;
        }
        ret = 0;
        dev->revision = revinfo.revision;
        break;
    default:
        ret = -ENOSYS;
        break;
    }
    return ret;
}

static void virtio_sch_disable_cb(SubchDev *sch)
{
    VirtioCcwDevice *dev = sch->driver_data;

    dev->revision = -1;
}

static void virtio_ccw_device_realize(VirtioCcwDevice *dev, Error **errp)
{
    VirtIOCCWDeviceClass *k = VIRTIO_CCW_DEVICE_GET_CLASS(dev);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    CCWDeviceClass *ck = CCW_DEVICE_GET_CLASS(ccw_dev);
    SubchDev *sch;
    Error *err = NULL;
    int i;

    sch = css_create_sch(ccw_dev->devno, errp);
    if (!sch) {
        return;
    }
    if (!virtio_ccw_rev_max(dev) && dev->force_revision_1) {
        error_setg(&err, "Invalid value of property max_rev "
                   "(is %d expected >= 1)", virtio_ccw_rev_max(dev));
        goto out_err;
    }

    sch->driver_data = dev;
    sch->ccw_cb = virtio_ccw_cb;
    sch->disable_cb = virtio_sch_disable_cb;
    sch->id.reserved = 0xff;
    sch->id.cu_type = VIRTIO_CCW_CU_TYPE;
    sch->do_subchannel_work = do_subchannel_work_virtual;
    sch->irb_cb = build_irb_virtual;
    ccw_dev->sch = sch;
    dev->indicators = NULL;
    dev->revision = -1;
    for (i = 0; i < ADAPTER_ROUTES_MAX_GSI; i++) {
        dev->routes.gsi[i] = -1;
    }
    css_sch_build_virtual_schib(sch, 0, VIRTIO_CCW_CHPID_TYPE);

    trace_virtio_ccw_new_device(
        sch->cssid, sch->ssid, sch->schid, sch->devno,
        ccw_dev->devno.valid ? "user-configured" : "auto-configured");

    /* fd-based ioevents can't be synchronized in record/replay */
    if (replay_mode != REPLAY_MODE_NONE) {
        dev->flags &= ~VIRTIO_CCW_FLAG_USE_IOEVENTFD;
    }

    if (k->realize) {
        k->realize(dev, &err);
        if (err) {
            goto out_err;
        }
    }

    ck->realize(ccw_dev, &err);
    if (err) {
        goto out_err;
    }

    return;

out_err:
    error_propagate(errp, err);
    css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
    ccw_dev->sch = NULL;
    g_free(sch);
}

static void virtio_ccw_device_unrealize(VirtioCcwDevice *dev)
{
    VirtIOCCWDeviceClass *dc = VIRTIO_CCW_DEVICE_GET_CLASS(dev);
    CcwDevice *ccw_dev = CCW_DEVICE(dev);
    SubchDev *sch = ccw_dev->sch;

    if (dc->unrealize) {
        dc->unrealize(dev);
    }

    if (sch) {
        css_subch_assign(sch->cssid, sch->ssid, sch->schid, sch->devno, NULL);
        g_free(sch);
        ccw_dev->sch = NULL;
    }
    if (dev->indicators) {
        release_indicator(&dev->routes.adapter, dev->indicators);
        dev->indicators = NULL;
    }
}

/* DeviceState to VirtioCcwDevice. Note: used on datapath,
 * be careful and test performance if you change this.
 */
static inline VirtioCcwDevice *to_virtio_ccw_dev_fast(DeviceState *d)
{
    CcwDevice *ccw_dev = to_ccw_dev_fast(d);

    return container_of(ccw_dev, VirtioCcwDevice, parent_obj);
}

static uint8_t virtio_set_ind_atomic(SubchDev *sch, uint64_t ind_loc,
                                     uint8_t to_be_set)
{
    uint8_t expected, actual;
    hwaddr len = 1;
    /* avoid  multiple fetches */
    uint8_t volatile *ind_addr;

    ind_addr = cpu_physical_memory_map(ind_loc, &len, true);
    if (!ind_addr) {
        error_report("%s(%x.%x.%04x): unable to access indicator",
                     __func__, sch->cssid, sch->ssid, sch->schid);
        return -1;
    }
    actual = *ind_addr;
    do {
        expected = actual;
        actual = qatomic_cmpxchg(ind_addr, expected, expected | to_be_set);
    } while (actual != expected);
    trace_virtio_ccw_set_ind(ind_loc, actual, actual | to_be_set);
    cpu_physical_memory_unmap((void *)ind_addr, len, 1, len);

    return actual;
}

static void virtio_ccw_notify(DeviceState *d, uint16_t vector)
{
    VirtioCcwDevice *dev = to_virtio_ccw_dev_fast(d);
    CcwDevice *ccw_dev = to_ccw_dev_fast(d);
    SubchDev *sch = ccw_dev->sch;
    uint64_t indicators;

    if (vector == VIRTIO_NO_VECTOR) {
        return;
    }
    /*
     * vector < VIRTIO_QUEUE_MAX: notification for a virtqueue
     * vector == VIRTIO_QUEUE_MAX: configuration change notification
     * bits beyond that are unused and should never be notified for
     */
    assert(vector <= VIRTIO_QUEUE_MAX);

    if (vector < VIRTIO_QUEUE_MAX) {
        if (!dev->indicators) {
            return;
        }
        if (sch->thinint_active) {
            /*
             * In the adapter interrupt case, indicators points to a
             * memory area that may be (way) larger than 64 bit and
             * ind_bit indicates the start of the indicators in a big
             * endian notation.
             */
            uint64_t ind_bit = dev->routes.adapter.ind_offset;

            virtio_set_ind_atomic(sch, dev->indicators->addr +
                                  (ind_bit + vector) / 8,
                                  0x80 >> ((ind_bit + vector) % 8));
            if (!virtio_set_ind_atomic(sch, dev->summary_indicator->addr,
                                       0x01)) {
                css_adapter_interrupt(CSS_IO_ADAPTER_VIRTIO, dev->thinint_isc);
            }
        } else {
            assert(vector < NR_CLASSIC_INDICATOR_BITS);
            indicators = address_space_ldq(&address_space_memory,
                                           dev->indicators->addr,
                                           MEMTXATTRS_UNSPECIFIED,
                                           NULL);
            indicators |= 1ULL << vector;
            address_space_stq(&address_space_memory, dev->indicators->addr,
                              indicators, MEMTXATTRS_UNSPECIFIED, NULL);
            css_conditional_io_interrupt(sch);
        }
    } else {
        if (!dev->indicators2) {
            return;
        }
        indicators = address_space_ldq(&address_space_memory,
                                       dev->indicators2->addr,
                                       MEMTXATTRS_UNSPECIFIED,
                                       NULL);
        indicators |= 1ULL;
        address_space_stq(&address_space_memory, dev->indicators2->addr,
                          indicators, MEMTXATTRS_UNSPECIFIED, NULL);
        css_conditional_io_interrupt(sch);
    }
}

static void virtio_ccw_reset_hold(Object *obj, ResetType type)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(obj);
    VirtIOCCWDeviceClass *vdc = VIRTIO_CCW_DEVICE_GET_CLASS(dev);

    virtio_ccw_reset_virtio(dev);

    if (vdc->parent_phases.hold) {
        vdc->parent_phases.hold(obj, type);
    }
}

static void virtio_ccw_vmstate_change(DeviceState *d, bool running)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);

    if (running) {
        virtio_ccw_start_ioeventfd(dev);
    } else {
        virtio_ccw_stop_ioeventfd(dev);
    }
}

static bool virtio_ccw_query_guest_notifiers(DeviceState *d)
{
    CcwDevice *dev = CCW_DEVICE(d);

    return !!(dev->sch->curr_status.pmcw.flags & PMCW_FLAGS_MASK_ENA);
}

static int virtio_ccw_get_mappings(VirtioCcwDevice *dev)
{
    int r;
    CcwDevice *ccw_dev = CCW_DEVICE(dev);

    if (!ccw_dev->sch->thinint_active) {
        return -EINVAL;
    }

    r = map_indicator(&dev->routes.adapter, dev->summary_indicator);
    if (r) {
        return r;
    }
    r = map_indicator(&dev->routes.adapter, dev->indicators);
    if (r) {
        return r;
    }
    dev->routes.adapter.summary_addr = dev->summary_indicator->map;
    dev->routes.adapter.ind_addr = dev->indicators->map;

    return 0;
}

static int virtio_ccw_setup_irqroutes(VirtioCcwDevice *dev, int nvqs)
{
    int i;
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    int ret;
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    ret = virtio_ccw_get_mappings(dev);
    if (ret) {
        return ret;
    }
    for (i = 0; i < nvqs; i++) {
        if (!virtio_queue_get_num(vdev, i)) {
            break;
        }
    }
    dev->routes.num_routes = i;
    return fsc->add_adapter_routes(fs, &dev->routes);
}

static void virtio_ccw_release_irqroutes(VirtioCcwDevice *dev, int nvqs)
{
    S390FLICState *fs = s390_get_flic();
    S390FLICStateClass *fsc = s390_get_flic_class(fs);

    fsc->release_adapter_routes(fs, &dev->routes);
}

static int virtio_ccw_add_irqfd(VirtioCcwDevice *dev, int n)
{
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);

    return kvm_irqchip_add_irqfd_notifier_gsi(kvm_state, notifier, NULL,
                                              dev->routes.gsi[n]);
}

static void virtio_ccw_remove_irqfd(VirtioCcwDevice *dev, int n)
{
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);
    int ret;

    ret = kvm_irqchip_remove_irqfd_notifier_gsi(kvm_state, notifier,
                                                dev->routes.gsi[n]);
    assert(ret == 0);
}

static int virtio_ccw_set_guest_notifier(VirtioCcwDevice *dev, int n,
                                         bool assign, bool with_irqfd)
{
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    VirtQueue *vq = virtio_get_queue(vdev, n);
    EventNotifier *notifier = virtio_queue_get_guest_notifier(vq);
    VirtioDeviceClass *k = VIRTIO_DEVICE_GET_CLASS(vdev);

    if (assign) {
        int r = event_notifier_init(notifier, 0);

        if (r < 0) {
            return r;
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, true, with_irqfd);
        if (with_irqfd) {
            r = virtio_ccw_add_irqfd(dev, n);
            if (r) {
                virtio_queue_set_guest_notifier_fd_handler(vq, false,
                                                           with_irqfd);
                return r;
            }
        }
        /*
         * We do not support individual masking for channel devices, so we
         * need to manually trigger any guest masking callbacks here.
         */
        if (k->guest_notifier_mask && vdev->use_guest_notifier_mask) {
            k->guest_notifier_mask(vdev, n, false);
        }
        /* get lost events and re-inject */
        if (k->guest_notifier_pending &&
            k->guest_notifier_pending(vdev, n)) {
            event_notifier_set(notifier);
        }
    } else {
        if (k->guest_notifier_mask && vdev->use_guest_notifier_mask) {
            k->guest_notifier_mask(vdev, n, true);
        }
        if (with_irqfd) {
            virtio_ccw_remove_irqfd(dev, n);
        }
        virtio_queue_set_guest_notifier_fd_handler(vq, false, with_irqfd);
        event_notifier_cleanup(notifier);
    }
    return 0;
}

static int virtio_ccw_set_guest_notifiers(DeviceState *d, int nvqs,
                                          bool assigned)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    CcwDevice *ccw_dev = CCW_DEVICE(d);
    bool with_irqfd = ccw_dev->sch->thinint_active && kvm_irqfds_enabled();
    int r, n;

    if (with_irqfd && assigned) {
        /* irq routes need to be set up before assigning irqfds */
        r = virtio_ccw_setup_irqroutes(dev, nvqs);
        if (r < 0) {
            goto irqroute_error;
        }
    }
    for (n = 0; n < nvqs; n++) {
        if (!virtio_queue_get_num(vdev, n)) {
            break;
        }
        r = virtio_ccw_set_guest_notifier(dev, n, assigned, with_irqfd);
        if (r < 0) {
            goto assign_error;
        }
    }
    if (with_irqfd && !assigned) {
        /* release irq routes after irqfds have been released */
        virtio_ccw_release_irqroutes(dev, nvqs);
    }
    return 0;

assign_error:
    while (--n >= 0) {
        virtio_ccw_set_guest_notifier(dev, n, !assigned, false);
    }
irqroute_error:
    if (with_irqfd && assigned) {
        virtio_ccw_release_irqroutes(dev, nvqs);
    }
    return r;
}

static void virtio_ccw_save_queue(DeviceState *d, int n, QEMUFile *f)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);

    qemu_put_be16(f, virtio_queue_vector(vdev, n));
}

static int virtio_ccw_load_queue(DeviceState *d, int n, QEMUFile *f)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    uint16_t vector;

    qemu_get_be16s(f, &vector);
    virtio_queue_set_vector(vdev, n , vector);

    return 0;
}

static void virtio_ccw_save_config(DeviceState *d, QEMUFile *f)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    Error *local_err = NULL;
    int ret;

    ret = vmstate_save_state(f, &vmstate_virtio_ccw_dev, dev, NULL, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
    }
}

static int virtio_ccw_load_config(DeviceState *d, QEMUFile *f)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    Error *local_err = NULL;
    int ret;

    ret = vmstate_load_state(f, &vmstate_virtio_ccw_dev, dev, 1, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
    }
    return ret;
}

static void virtio_ccw_pre_plugged(DeviceState *d, Error **errp)
{
   VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
   VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);

    if (dev->max_rev >= 1) {
        virtio_add_feature(&vdev->host_features, VIRTIO_F_VERSION_1);
    }
}

/* This is called by virtio-bus just after the device is plugged. */
static void virtio_ccw_device_plugged(DeviceState *d, Error **errp)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);
    VirtIODevice *vdev = virtio_bus_get_device(&dev->bus);
    CcwDevice *ccw_dev = CCW_DEVICE(d);
    SubchDev *sch = ccw_dev->sch;
    int n = virtio_get_num_queues(vdev);

    if (!virtio_has_feature(vdev->host_features, VIRTIO_F_VERSION_1)) {
        dev->max_rev = 0;
    }

    if (!virtio_ccw_rev_max(dev) && !virtio_legacy_allowed(vdev)) {
        /*
         * To avoid migration issues, we allow legacy mode when legacy
         * check is disabled in the old machine types (< 5.1).
         */
        if (virtio_legacy_check_disabled(vdev)) {
            warn_report("device requires revision >= 1, but for backward "
                        "compatibility max_revision=0 is allowed");
        } else {
            error_setg(errp, "Invalid value of property max_rev "
                       "(is %d expected >= 1)", virtio_ccw_rev_max(dev));
            return;
        }
    }

    if (virtio_get_num_queues(vdev) > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "The number of virtqueues %d "
                   "exceeds virtio limit %d", n,
                   VIRTIO_QUEUE_MAX);
        return;
    }
    if (virtio_get_num_queues(vdev) > ADAPTER_ROUTES_MAX_GSI) {
        error_setg(errp, "The number of virtqueues %d "
                   "exceeds flic adapter route limit %d", n,
                   ADAPTER_ROUTES_MAX_GSI);
        return;
    }

    sch->id.cu_model = virtio_bus_get_vdev_id(&dev->bus);


    css_generate_sch_crws(sch->cssid, sch->ssid, sch->schid,
                          d->hotplugged, 1);
}

static void virtio_ccw_device_unplugged(DeviceState *d)
{
    VirtioCcwDevice *dev = VIRTIO_CCW_DEVICE(d);

    virtio_ccw_stop_ioeventfd(dev);
}
/**************** Virtio-ccw Bus Device Descriptions *******************/

static void virtio_ccw_busdev_realize(DeviceState *dev, Error **errp)
{
    VirtioCcwDevice *_dev = (VirtioCcwDevice *)dev;

    virtio_ccw_bus_new(&_dev->bus, sizeof(_dev->bus), _dev);
    virtio_ccw_device_realize(_dev, errp);
}

static void virtio_ccw_busdev_unrealize(DeviceState *dev)
{
    VirtioCcwDevice *_dev = (VirtioCcwDevice *)dev;

    virtio_ccw_device_unrealize(_dev);
}

static void virtio_ccw_busdev_unplug(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    VirtioCcwDevice *_dev = to_virtio_ccw_dev_fast(dev);

    virtio_ccw_stop_ioeventfd(_dev);
}

static void virtio_ccw_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    CCWDeviceClass *k = CCW_DEVICE_CLASS(dc);
    VirtIOCCWDeviceClass *vdc = VIRTIO_CCW_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->unplug = virtio_ccw_busdev_unplug;
    dc->realize = virtio_ccw_busdev_realize;
    dc->unrealize = virtio_ccw_busdev_unrealize;
    resettable_class_set_parent_phases(rc, NULL, virtio_ccw_reset_hold, NULL,
                                       &vdc->parent_phases);
}

static const TypeInfo virtio_ccw_device_info = {
    .name = TYPE_VIRTIO_CCW_DEVICE,
    .parent = TYPE_CCW_DEVICE,
    .instance_size = sizeof(VirtioCcwDevice),
    .class_init = virtio_ccw_device_class_init,
    .class_size = sizeof(VirtIOCCWDeviceClass),
    .abstract = true,
};

/* virtio-ccw-bus */

static void virtio_ccw_bus_new(VirtioBusState *bus, size_t bus_size,
                               VirtioCcwDevice *dev)
{
    DeviceState *qdev = DEVICE(dev);
    char virtio_bus_name[] = "virtio-bus";

    qbus_init(bus, bus_size, TYPE_VIRTIO_CCW_BUS, qdev, virtio_bus_name);
}

static void virtio_ccw_bus_class_init(ObjectClass *klass, const void *data)
{
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
    BusClass *bus_class = BUS_CLASS(klass);

    bus_class->max_dev = 1;
    k->notify = virtio_ccw_notify;
    k->vmstate_change = virtio_ccw_vmstate_change;
    k->query_guest_notifiers = virtio_ccw_query_guest_notifiers;
    k->set_guest_notifiers = virtio_ccw_set_guest_notifiers;
    k->save_queue = virtio_ccw_save_queue;
    k->load_queue = virtio_ccw_load_queue;
    k->save_config = virtio_ccw_save_config;
    k->load_config = virtio_ccw_load_config;
    k->pre_plugged = virtio_ccw_pre_plugged;
    k->device_plugged = virtio_ccw_device_plugged;
    k->device_unplugged = virtio_ccw_device_unplugged;
    k->ioeventfd_enabled = virtio_ccw_ioeventfd_enabled;
    k->ioeventfd_assign = virtio_ccw_ioeventfd_assign;
}

static const TypeInfo virtio_ccw_bus_info = {
    .name = TYPE_VIRTIO_CCW_BUS,
    .parent = TYPE_VIRTIO_BUS,
    .instance_size = sizeof(VirtioCcwBusState),
    .class_size = sizeof(VirtioCcwBusClass),
    .class_init = virtio_ccw_bus_class_init,
};

static void virtio_ccw_register(void)
{
    type_register_static(&virtio_ccw_bus_info);
    type_register_static(&virtio_ccw_device_info);
}

type_init(virtio_ccw_register)
