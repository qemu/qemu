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

#include "hw/boards.h"
#include "hw/s390x/storage-keys.h"
#include "qemu/error-report.h"

S390SKeysState *s390_get_skeys_device(void)
{
    S390SKeysState *ss;

    ss = S390_SKEYS(object_resolve_path_type("", TYPE_S390_SKEYS, NULL));
    assert(ss);
    return ss;
}

void s390_skeys_init(void)
{
    Object *obj;

    if (kvm_enabled()) {
        obj = object_new(TYPE_KVM_S390_SKEYS);
    } else {
        obj = object_new(TYPE_QEMU_S390_SKEYS);
    }
    object_property_add_child(qdev_get_machine(), TYPE_S390_SKEYS,
                              obj, NULL);
    object_unref(obj);

    qdev_init_nofail(DEVICE(obj));
}

static void qemu_s390_skeys_init(Object *obj)
{
    QEMUS390SKeysState *skeys = QEMU_S390_SKEYS(obj);
    MachineState *machine = MACHINE(qdev_get_machine());

    skeys->key_count = machine->maxram_size / TARGET_PAGE_SIZE;
    skeys->keydata = g_malloc0(skeys->key_count);
}

static int qemu_s390_skeys_enabled(S390SKeysState *ss)
{
    return 1;
}

/*
 * TODO: for memory hotplug support qemu_s390_skeys_set and qemu_s390_skeys_get
 * will have to make sure that the given gfn belongs to a memory region and not
 * a memory hole.
 */
static int qemu_s390_skeys_set(S390SKeysState *ss, uint64_t start_gfn,
                              uint64_t count, uint8_t *keys)
{
    QEMUS390SKeysState *skeydev = QEMU_S390_SKEYS(ss);
    int i;

    /* Check for uint64 overflow and access beyond end of key data */
    if (start_gfn + count > skeydev->key_count || start_gfn + count < count) {
        error_report("Error: Setting storage keys for page beyond the end "
                "of memory. gfn=%" PRIx64 " count=%" PRId64 "\n", start_gfn,
                count);
        return -EINVAL;
    }

    for (i = 0; i < count; i++) {
        skeydev->keydata[start_gfn + i] = keys[i];
    }
    return 0;
}

static int qemu_s390_skeys_get(S390SKeysState *ss, uint64_t start_gfn,
                               uint64_t count, uint8_t *keys)
{
    QEMUS390SKeysState *skeydev = QEMU_S390_SKEYS(ss);
    int i;

    /* Check for uint64 overflow and access beyond end of key data */
    if (start_gfn + count > skeydev->key_count || start_gfn + count < count) {
        error_report("Error: Getting storage keys for page beyond the end "
                "of memory. gfn=%" PRIx64 " count=%" PRId64 "\n", start_gfn,
                count);
        return -EINVAL;
    }

    for (i = 0; i < count; i++) {
        keys[i] = skeydev->keydata[start_gfn + i];
    }
    return 0;
}

static void qemu_s390_skeys_class_init(ObjectClass *oc, void *data)
{
    S390SKeysClass *skeyclass = S390_SKEYS_CLASS(oc);

    skeyclass->skeys_enabled = qemu_s390_skeys_enabled;
    skeyclass->get_skeys = qemu_s390_skeys_get;
    skeyclass->set_skeys = qemu_s390_skeys_set;
}

static const TypeInfo qemu_s390_skeys_info = {
    .name          = TYPE_QEMU_S390_SKEYS,
    .parent        = TYPE_S390_SKEYS,
    .instance_init = qemu_s390_skeys_init,
    .instance_size = sizeof(QEMUS390SKeysState),
    .class_init    = qemu_s390_skeys_class_init,
    .instance_size = sizeof(S390SKeysClass),
};

static void s390_skeys_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo s390_skeys_info = {
    .name          = TYPE_S390_SKEYS,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(S390SKeysState),
    .class_init    = s390_skeys_class_init,
    .class_size    = sizeof(S390SKeysClass),
    .abstract = true,
};

static void qemu_s390_skeys_register_types(void)
{
    type_register_static(&s390_skeys_info);
    type_register_static(&qemu_s390_skeys_info);
}

type_init(qemu_s390_skeys_register_types)
