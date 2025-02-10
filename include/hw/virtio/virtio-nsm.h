/*
 * AWS Nitro Secure Module (NSM) device
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef QEMU_VIRTIO_NSM_H
#define QEMU_VIRTIO_NSM_H

#include "crypto/hash.h"
#include "hw/virtio/virtio.h"
#include "qom/object.h"

#define NSM_MAX_PCRS 32

#define TYPE_VIRTIO_NSM "virtio-nsm-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIONSM, VIRTIO_NSM)
#define VIRTIO_NSM_GET_PARENT_CLASS(obj) \
    OBJECT_GET_PARENT_CLASS(obj, TYPE_VIRTIO_NSM)

struct PCRInfo {
    bool locked;
    uint8_t data[QCRYPTO_HASH_DIGEST_LEN_SHA384];
};

struct VirtIONSM {
    VirtIODevice parent_obj;

    /* Only one vq - guest puts request and response buffers on it */
    VirtQueue *vq;

    /* NSM State */
    uint16_t max_pcrs;
    struct PCRInfo pcrs[NSM_MAX_PCRS];
    char *digest;
    char *module_id;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t version_patch;

    bool (*extend_pcr)(VirtIONSM *vnsm, int ind, uint8_t *data, uint16_t len);
    void (*lock_pcr)(VirtIONSM *vnsm, int ind);
};

#endif
