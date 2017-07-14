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

#include <hw/qdev.h>
#include "monitor/monitor.h"

#define TYPE_S390_STATTRIB "s390-storage_attributes"
#define TYPE_QEMU_S390_STATTRIB "s390-storage_attributes-qemu"
#define TYPE_KVM_S390_STATTRIB "s390-storage_attributes-kvm"

#define S390_STATTRIB(obj) \
    OBJECT_CHECK(S390StAttribState, (obj), TYPE_S390_STATTRIB)

typedef struct S390StAttribState {
    DeviceState parent_obj;
    uint64_t migration_cur_gfn;
    bool migration_enabled;
} S390StAttribState;

#define S390_STATTRIB_CLASS(klass) \
    OBJECT_CLASS_CHECK(S390StAttribClass, (klass), TYPE_S390_STATTRIB)
#define S390_STATTRIB_GET_CLASS(obj) \
    OBJECT_GET_CLASS(S390StAttribClass, (obj), TYPE_S390_STATTRIB)

typedef struct S390StAttribClass {
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
} S390StAttribClass;

#define QEMU_S390_STATTRIB(obj) \
    OBJECT_CHECK(QEMUS390StAttribState, (obj), TYPE_QEMU_S390_STATTRIB)

typedef struct QEMUS390StAttribState {
    S390StAttribState parent_obj;
} QEMUS390StAttribState;

#define KVM_S390_STATTRIB(obj) \
    OBJECT_CHECK(KVMS390StAttribState, (obj), TYPE_KVM_S390_STATTRIB)

typedef struct KVMS390StAttribState {
    S390StAttribState parent_obj;
    uint64_t still_dirty;
    uint8_t *incoming_buffer;
} KVMS390StAttribState;

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
