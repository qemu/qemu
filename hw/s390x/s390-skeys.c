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
#include "qmp-commands.h"
#include "hw/s390x/storage-keys.h"
#include "qemu/error-report.h"

#define S390_SKEYS_BUFFER_SIZE 131072  /* Room for 128k storage keys */

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

static void write_keys(QEMUFile *f, uint8_t *keys, uint64_t startgfn,
                       uint64_t count, Error **errp)
{
    uint64_t curpage = startgfn;
    uint64_t maxpage = curpage + count - 1;
    const char *fmt = "page=%03" PRIx64 ": key(%d) => ACC=%X, FP=%d, REF=%d,"
                      " ch=%d, reserved=%d\n";
    char buf[128];
    int len;

    for (; curpage <= maxpage; curpage++) {
        uint8_t acc = (*keys & 0xF0) >> 4;
        int fp =  (*keys & 0x08);
        int ref = (*keys & 0x04);
        int ch = (*keys & 0x02);
        int res = (*keys & 0x01);

        len = snprintf(buf, sizeof(buf), fmt, curpage,
                       *keys, acc, fp, ref, ch, res);
        assert(len < sizeof(buf));
        qemu_put_buffer(f, (uint8_t *)buf, len);
        keys++;
    }
}

void hmp_info_skeys(Monitor *mon, const QDict *qdict)
{
    S390SKeysState *ss = s390_get_skeys_device();
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    uint64_t addr = qdict_get_int(qdict, "addr");
    uint8_t key;
    int r;

    /* Quick check to see if guest is using storage keys*/
    if (!skeyclass->skeys_enabled(ss)) {
        monitor_printf(mon, "Error: This guest is not using storage keys\n");
        return;
    }

    r = skeyclass->get_skeys(ss, addr / TARGET_PAGE_SIZE, 1, &key);
    if (r < 0) {
        monitor_printf(mon, "Error: %s\n", strerror(-r));
        return;
    }

    monitor_printf(mon, "  key: 0x%X\n", key);
}

void hmp_dump_skeys(Monitor *mon, const QDict *qdict)
{
    const char *filename = qdict_get_str(qdict, "filename");
    Error *err = NULL;

    qmp_dump_skeys(filename, &err);
    if (err) {
        monitor_printf(mon, "%s\n", error_get_pretty(err));
        error_free(err);
    }
}

void qmp_dump_skeys(const char *filename, Error **errp)
{
    S390SKeysState *ss = s390_get_skeys_device();
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    const uint64_t total_count = ram_size / TARGET_PAGE_SIZE;
    uint64_t handled_count = 0, cur_count;
    Error *lerr = NULL;
    vaddr cur_gfn = 0;
    uint8_t *buf;
    int ret;
    QEMUFile *f;

    /* Quick check to see if guest is using storage keys*/
    if (!skeyclass->skeys_enabled(ss)) {
        error_setg(errp, "This guest is not using storage keys - "
                         "nothing to dump");
        return;
    }

    f = qemu_fopen(filename, "wb");
    if (!f) {
        error_setg_file_open(errp, errno, filename);
        return;
    }

    buf = g_try_malloc(S390_SKEYS_BUFFER_SIZE);
    if (!buf) {
        error_setg(errp, "Could not allocate memory");
        goto out;
    }

    /* we'll only dump initial memory for now */
    while (handled_count < total_count) {
        /* Calculate how many keys to ask for & handle overflow case */
        cur_count = MIN(total_count - handled_count, S390_SKEYS_BUFFER_SIZE);

        ret = skeyclass->get_skeys(ss, cur_gfn, cur_count, buf);
        if (ret < 0) {
            error_setg(errp, "get_keys error %d", ret);
            goto out_free;
        }

        /* write keys to stream */
        write_keys(f, buf, cur_gfn, cur_count, &lerr);
        if (lerr) {
            goto out_free;
        }

        cur_gfn += cur_count;
        handled_count += cur_count;
    }

out_free:
    error_propagate(errp, lerr);
    g_free(buf);
out:
    qemu_fclose(f);
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
                "of memory: gfn=%" PRIx64 " count=%" PRId64 "\n", start_gfn,
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
                "of memory: gfn=%" PRIx64 " count=%" PRId64 "\n", start_gfn,
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
