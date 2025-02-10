/*
 * AWS nitro-enclave machine
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#ifndef HW_I386_NITRO_ENCLAVE_H
#define HW_I386_NITRO_ENCLAVE_H

#include "crypto/hash.h"
#include "hw/i386/microvm.h"
#include "qom/object.h"
#include "hw/virtio/virtio-nsm.h"

/* Machine type options */
#define NITRO_ENCLAVE_VSOCK_CHARDEV_ID "vsock"
#define NITRO_ENCLAVE_ID    "id"
#define NITRO_ENCLAVE_PARENT_ROLE "parent-role"
#define NITRO_ENCLAVE_PARENT_ID "parent-id"

struct NitroEnclaveMachineClass {
    MicrovmMachineClass parent;

    void (*parent_init)(MachineState *state);
    void (*parent_reset)(MachineState *machine, ResetType type);
};

struct NitroEnclaveMachineState {
    MicrovmMachineState parent;

    /* Machine type options */
    char *vsock;
    /* Enclave identifier */
    char *id;
    /* Parent instance IAM role ARN */
    char *parent_role;
    /* Parent instance identifier */
    char *parent_id;

    /* Machine state */
    VirtIONSM *vnsm;

    /* kernel + ramdisks + cmdline SHA384 hash */
    uint8_t image_hash[QCRYPTO_HASH_DIGEST_LEN_SHA384];
    /* kernel + boot ramdisk + cmdline SHA384 hash */
    uint8_t bootstrap_hash[QCRYPTO_HASH_DIGEST_LEN_SHA384];
    /* application ramdisk(s) SHA384 hash */
    uint8_t app_hash[QCRYPTO_HASH_DIGEST_LEN_SHA384];
    /* certificate fingerprint SHA384 hash */
    uint8_t fingerprint_hash[QCRYPTO_HASH_DIGEST_LEN_SHA384];
    bool signature_found;
};

#define TYPE_NITRO_ENCLAVE_MACHINE MACHINE_TYPE_NAME("nitro-enclave")
OBJECT_DECLARE_TYPE(NitroEnclaveMachineState, NitroEnclaveMachineClass,
                    NITRO_ENCLAVE_MACHINE)

#endif
