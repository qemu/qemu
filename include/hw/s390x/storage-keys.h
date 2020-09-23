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
    bool migration_enabled;

};


struct S390SKeysClass {
    DeviceClass parent_class;
    int (*skeys_enabled)(S390SKeysState *ks);
    int (*get_skeys)(S390SKeysState *ks, uint64_t start_gfn, uint64_t count,
                     uint8_t *keys);
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

S390SKeysState *s390_get_skeys_device(void);

void hmp_dump_skeys(Monitor *mon, const QDict *qdict);
void hmp_info_skeys(Monitor *mon, const QDict *qdict);

#endif /* S390_STORAGE_KEYS_H */
