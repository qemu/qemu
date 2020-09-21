/*
 * s390 storage attributes device
 *
 * Copyright 2016 IBM Corp.
 * Author(s): Claudio Imbrenda <imbrenda@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef S390_STORAGE_ATTRIBUTES_H
#define S390_STORAGE_ATTRIBUTES_H

#include "hw/qdev-core.h"
#include "monitor/monitor.h"
#include "qom/object.h"

#define TYPE_S390_STATTRIB "s390-storage_attributes"
#define TYPE_QEMU_S390_STATTRIB "s390-storage_attributes-qemu"
#define TYPE_KVM_S390_STATTRIB "s390-storage_attributes-kvm"

OBJECT_DECLARE_TYPE(S390StAttribState, S390StAttribClass, S390_STATTRIB)

struct S390StAttribState {
    DeviceState parent_obj;
    uint64_t migration_cur_gfn;
    bool migration_enabled;
};


struct S390StAttribClass {
    DeviceClass parent_class;
    /* Return value: < 0 on error, or new count */
    int (*get_stattr)(S390StAttribState *sa, uint64_t *start_gfn,
                      uint32_t count, uint8_t *values);
    int (*peek_stattr)(S390StAttribState *sa, uint64_t start_gfn,
                       uint32_t count, uint8_t *values);
    int (*set_stattr)(S390StAttribState *sa, uint64_t start_gfn,
                      uint32_t count, uint8_t *values);
    void (*synchronize)(S390StAttribState *sa);
    int (*set_migrationmode)(S390StAttribState *sa, bool value);
    int (*get_active)(S390StAttribState *sa);
    long long (*get_dirtycount)(S390StAttribState *sa);
};

typedef struct QEMUS390StAttribState QEMUS390StAttribState;
DECLARE_INSTANCE_CHECKER(QEMUS390StAttribState, QEMU_S390_STATTRIB,
                         TYPE_QEMU_S390_STATTRIB)

struct QEMUS390StAttribState {
    S390StAttribState parent_obj;
};

typedef struct KVMS390StAttribState KVMS390StAttribState;
DECLARE_INSTANCE_CHECKER(KVMS390StAttribState, KVM_S390_STATTRIB,
                         TYPE_KVM_S390_STATTRIB)

struct KVMS390StAttribState {
    S390StAttribState parent_obj;
    uint64_t still_dirty;
    uint8_t *incoming_buffer;
};

void s390_stattrib_init(void);

#ifdef CONFIG_KVM
Object *kvm_s390_stattrib_create(void);
#else
static inline Object *kvm_s390_stattrib_create(void)
{
    return NULL;
}
#endif

void hmp_info_cmma(Monitor *mon, const QDict *qdict);
void hmp_migrationmode(Monitor *mon, const QDict *qdict);

#endif /* S390_STORAGE_ATTRIBUTES_H */
