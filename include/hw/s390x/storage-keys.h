/*
 * s390 storage key device
 *
 * Copyright 2015 IBM Corp.
 * Author(s): Jason J. Herne <jjherne@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_STORAGE_KEYS_H
#define S390_STORAGE_KEYS_H

#include "hw/qdev-core.h"
#include "monitor/monitor.h"
#include "qom/object.h"

#define TYPE_S390_SKEYS "s390-skeys"
OBJECT_DECLARE_TYPE(S390SKeysState, S390SKeysClass, S390_SKEYS)

struct S390SKeysState {
    DeviceState parent_obj;
};


struct S390SKeysClass {
    DeviceClass parent_class;

    /**
     * @skeys_are_enabled:
     *
     * Check whether storage keys are enabled. If not enabled, they were not
     * enabled lazily either by the guest via a storage key instruction or
     * by the host during migration.
     *
     * If disabled, everything not explicitly triggered by the guest,
     * such as outgoing migration or dirty/change tracking, should not touch
     * storage keys and should not lazily enable it.
     *
     * @ks: the #S390SKeysState
     *
     * Returns false if not enabled and true if enabled.
     */
    bool (*skeys_are_enabled)(S390SKeysState *ks);

    /**
     * @enable_skeys:
     *
     * Lazily enable storage keys. If this function is not implemented,
     * setting a storage key will lazily enable storage keys implicitly
     * instead. TCG guests have to make sure to flush the TLB of all CPUs
     * if storage keys were not enabled before this call.
     *
     * @ks: the #S390SKeysState
     *
     * Returns false if not enabled before this call, and true if already
     * enabled.
     */
    bool (*enable_skeys)(S390SKeysState *ks);

    /**
     * @get_skeys:
     *
     * Get storage keys for the given PFN range. This call will fail if
     * storage keys have not been lazily enabled yet.
     *
     * Callers have to validate that a GFN is valid before this call.
     *
     * @ks: the #S390SKeysState
     * @start_gfn: the start GFN to get storage keys for
     * @count: the number of storage keys to get
     * @keys: the byte array where storage keys will be stored to
     *
     * Returns 0 on success, returns an error if getting a storage key failed.
     */
    int (*get_skeys)(S390SKeysState *ks, uint64_t start_gfn, uint64_t count,
                     uint8_t *keys);
    /**
     * @set_skeys:
     *
     * Set storage keys for the given PFN range. This call will fail if
     * storage keys have not been lazily enabled yet and implicit
     * enablement is not supported.
     *
     * Callers have to validate that a GFN is valid before this call.
     *
     * @ks: the #S390SKeysState
     * @start_gfn: the start GFN to set storage keys for
     * @count: the number of storage keys to set
     * @keys: the byte array where storage keys will be read from
     *
     * Returns 0 on success, returns an error if setting a storage key failed.
     */
    int (*set_skeys)(S390SKeysState *ks, uint64_t start_gfn, uint64_t count,
                     uint8_t *keys);
};

#define TYPE_KVM_S390_SKEYS "s390-skeys-kvm"
#define TYPE_QEMU_S390_SKEYS "s390-skeys-qemu"
typedef struct QEMUS390SKeysState QEMUS390SKeysState;
DECLARE_INSTANCE_CHECKER(QEMUS390SKeysState, QEMU_S390_SKEYS,
                         TYPE_QEMU_S390_SKEYS)

struct QEMUS390SKeysState {
    S390SKeysState parent_obj;
    uint8_t *keydata;
    uint32_t key_count;
};

void s390_skeys_init(void);
/**
 * @s390_skeys_get: See S390SKeysClass::get_skeys()
 */
int s390_skeys_get(S390SKeysState *ks, uint64_t start_gfn,
                   uint64_t count, uint8_t *keys);
/**
 * @s390_skeys_set: See S390SKeysClass::set_skeys()
 */
int s390_skeys_set(S390SKeysState *ks, uint64_t start_gfn,
                   uint64_t count, uint8_t *keys);

S390SKeysState *s390_get_skeys_device(void);

void s390_qmp_dump_skeys(const char *filename, Error **errp);
void hmp_dump_skeys(Monitor *mon, const QDict *qdict);
void hmp_info_skeys(Monitor *mon, const QDict *qdict);

#define TYPE_DUMP_SKEYS_INTERFACE "dump-skeys-interface"

typedef struct DumpSKeysInterface DumpSKeysInterface;
DECLARE_CLASS_CHECKERS(DumpSKeysInterface, DUMP_SKEYS_INTERFACE,
                       TYPE_DUMP_SKEYS_INTERFACE)

struct DumpSKeysInterface {
    InterfaceClass parent_class;

    /**
     * @qmp_dump_skeys: Callback to dump guest's storage keys to @filename.
     */
    void (*qmp_dump_skeys)(const char *filename, Error **errp);
};

#endif /* S390_STORAGE_KEYS_H */
