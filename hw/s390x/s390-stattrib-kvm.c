/*
 * s390 storage attributes device -- KVM object
 *
 * Copyright 2016 IBM Corp.
 * Author(s): Claudio Imbrenda <imbrenda@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "migration/qemu-file.h"
#include "hw/s390x/storage-attributes.h"
#include "qemu/error-report.h"
#include "system/kvm.h"
#include "system/memory_mapping.h"
#include "system/ram_addr.h"
#include "kvm/kvm_s390x.h"
#include "qapi/error.h"

Object *kvm_s390_stattrib_create(void)
{
    if (kvm_enabled() &&
                kvm_check_extension(kvm_state, KVM_CAP_S390_CMMA_MIGRATION)) {
        return object_new(TYPE_KVM_S390_STATTRIB);
    }
    return NULL;
}

static void kvm_s390_stattrib_instance_init(Object *obj)
{
    KVMS390StAttribState *sas = KVM_S390_STATTRIB(obj);

    sas->still_dirty = 0;
}

static int kvm_s390_stattrib_read_helper(S390StAttribState *sa,
                                         uint64_t *start_gfn,
                                         uint32_t count,
                                         uint8_t *values,
                                         uint32_t flags)
{
    KVMS390StAttribState *sas = KVM_S390_STATTRIB(sa);
    int r;
    struct kvm_s390_cmma_log clog = {
        .values = (uint64_t)values,
        .start_gfn = *start_gfn,
        .count = count,
        .flags = flags,
    };

    r = kvm_vm_ioctl(kvm_state, KVM_S390_GET_CMMA_BITS, &clog);
    if (r < 0) {
        error_report("KVM_S390_GET_CMMA_BITS failed: %s", strerror(-r));
        return r;
    }

    *start_gfn = clog.start_gfn;
    sas->still_dirty = clog.remaining;
    return clog.count;
}

static int kvm_s390_stattrib_get_stattr(S390StAttribState *sa,
                                        uint64_t *start_gfn,
                                        uint32_t count,
                                        uint8_t *values)
{
    return kvm_s390_stattrib_read_helper(sa, start_gfn, count, values, 0);
}

static int kvm_s390_stattrib_peek_stattr(S390StAttribState *sa,
                                         uint64_t start_gfn,
                                         uint32_t count,
                                         uint8_t *values)
{
    return kvm_s390_stattrib_read_helper(sa, &start_gfn, count, values,
                                         KVM_S390_CMMA_PEEK);
}

static int kvm_s390_stattrib_set_stattr(S390StAttribState *sa,
                                        uint64_t start_gfn,
                                        uint32_t count,
                                        uint8_t *values)
{
    KVMS390StAttribState *sas = KVM_S390_STATTRIB(sa);
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(qdev_get_machine());
    unsigned long max = s390_get_memory_limit(s390ms) / TARGET_PAGE_SIZE;

    if (start_gfn + count > max) {
        error_report("Out of memory bounds when setting storage attributes");
        return -1;
    }
    if (!sas->incoming_buffer) {
        sas->incoming_buffer = g_malloc0(max);
    }

    memcpy(sas->incoming_buffer + start_gfn, values, count);

    return 0;
}

static void kvm_s390_stattrib_synchronize(S390StAttribState *sa)
{
    KVMS390StAttribState *sas = KVM_S390_STATTRIB(sa);
    S390CcwMachineState *s390ms = S390_CCW_MACHINE(qdev_get_machine());
    unsigned long max = s390_get_memory_limit(s390ms) / TARGET_PAGE_SIZE;
    unsigned long start_gfn, end_gfn, pages;
    GuestPhysBlockList guest_phys_blocks;
    GuestPhysBlock *block;
    int r;
    struct kvm_s390_cmma_log clog = {
        .flags = 0,
        .mask = ~0ULL,
    };

    if (!sas->incoming_buffer) {
        return;
    }
    guest_phys_blocks_init(&guest_phys_blocks);
    guest_phys_blocks_append(&guest_phys_blocks);

    QTAILQ_FOREACH(block, &guest_phys_blocks.head, next) {
        assert(QEMU_IS_ALIGNED(block->target_start, TARGET_PAGE_SIZE));
        assert(QEMU_IS_ALIGNED(block->target_end, TARGET_PAGE_SIZE));

        start_gfn = block->target_start / TARGET_PAGE_SIZE;
        end_gfn = block->target_end / TARGET_PAGE_SIZE;

        while (start_gfn < end_gfn) {
            /* Don't exceed the maximum buffer size. */
            pages = MIN(end_gfn - start_gfn, KVM_S390_SKEYS_MAX / 2);

            /*
             * If we ever get guest physical memory beyond the configured
             * memory limit, something went very wrong.
             */
            assert(start_gfn + pages <= max);

            clog.start_gfn = start_gfn;
            clog.count = pages;
            clog.values = (uint64_t)(sas->incoming_buffer + start_gfn);
            r = kvm_vm_ioctl(kvm_state, KVM_S390_SET_CMMA_BITS, &clog);
            if (r) {
                error_report("KVM_S390_SET_CMMA_BITS failed: %s", strerror(-r));
                goto out;
            }

            start_gfn += pages;
        }
    }

out:
    guest_phys_blocks_free(&guest_phys_blocks);
    g_free(sas->incoming_buffer);
    sas->incoming_buffer = NULL;
}

static int kvm_s390_stattrib_set_migrationmode(S390StAttribState *sa, bool val,
                                               Error **errp)
{
    struct kvm_device_attr attr = {
        .group = KVM_S390_VM_MIGRATION,
        .attr = val,
        .addr = 0,
    };
    int r;

    r = kvm_vm_ioctl(kvm_state, KVM_SET_DEVICE_ATTR, &attr);
    if (r) {
        error_setg_errno(errp, -r, "setting KVM_S390_VM_MIGRATION failed");
    }
    return r;
}

static long long kvm_s390_stattrib_get_dirtycount(S390StAttribState *sa)
{
    KVMS390StAttribState *sas = KVM_S390_STATTRIB(sa);
    uint8_t val[8];

    kvm_s390_stattrib_peek_stattr(sa, 0, 1, val);
    return sas->still_dirty;
}

static int kvm_s390_stattrib_get_active(S390StAttribState *sa)
{
    return kvm_s390_cmma_active();
}

static void kvm_s390_stattrib_class_init(ObjectClass *oc, void *data)
{
    S390StAttribClass *sac = S390_STATTRIB_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sac->get_stattr = kvm_s390_stattrib_get_stattr;
    sac->peek_stattr = kvm_s390_stattrib_peek_stattr;
    sac->set_stattr = kvm_s390_stattrib_set_stattr;
    sac->set_migrationmode = kvm_s390_stattrib_set_migrationmode;
    sac->get_dirtycount = kvm_s390_stattrib_get_dirtycount;
    sac->synchronize = kvm_s390_stattrib_synchronize;
    sac->get_active = kvm_s390_stattrib_get_active;

    /* Reason: Can only be instantiated one time (internally) */
    dc->user_creatable = false;
}

static const TypeInfo kvm_s390_stattrib_info = {
    .name          = TYPE_KVM_S390_STATTRIB,
    .parent        = TYPE_S390_STATTRIB,
    .instance_init = kvm_s390_stattrib_instance_init,
    .instance_size = sizeof(KVMS390StAttribState),
    .class_init    = kvm_s390_stattrib_class_init,
    .class_size    = sizeof(S390StAttribClass),
};

static void kvm_s390_stattrib_register_types(void)
{
    type_register_static(&kvm_s390_stattrib_info);
}

type_init(kvm_s390_stattrib_register_types)
