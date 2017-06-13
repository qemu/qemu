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

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qmp-commands.h"
#include "hw/s390x/storage-keys.h"
#include "qemu/error-report.h"
#include "sysemu/kvm.h"
#include "migration/register.h"

#define S390_SKEYS_BUFFER_SIZE 131072  /* Room for 128k storage keys */
#define S390_SKEYS_SAVE_FLAG_EOS 0x01
#define S390_SKEYS_SAVE_FLAG_SKEYS 0x02
#define S390_SKEYS_SAVE_FLAG_ERROR 0x04

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

static void write_keys(FILE *f, uint8_t *keys, uint64_t startgfn,
                       uint64_t count, Error **errp)
{
    uint64_t curpage = startgfn;
    uint64_t maxpage = curpage + count - 1;

    for (; curpage <= maxpage; curpage++) {
        uint8_t acc = (*keys & 0xF0) >> 4;
        int fp =  (*keys & 0x08);
        int ref = (*keys & 0x04);
        int ch = (*keys & 0x02);
        int res = (*keys & 0x01);

        fprintf(f, "page=%03" PRIx64 ": key(%d) => ACC=%X, FP=%d, REF=%d,"
                " ch=%d, reserved=%d\n",
                curpage, *keys, acc, fp, ref, ch, res);
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
        error_report_err(err);
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
    int fd;
    FILE *f;

    /* Quick check to see if guest is using storage keys*/
    if (!skeyclass->skeys_enabled(ss)) {
        error_setg(errp, "This guest is not using storage keys - "
                         "nothing to dump");
        return;
    }

    fd = qemu_open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        error_setg_file_open(errp, errno, filename);
        return;
    }
    f = fdopen(fd, "wb");
    if (!f) {
        close(fd);
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
    fclose(f);
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
                     "of memory: gfn=%" PRIx64 " count=%" PRId64,
                     start_gfn, count);
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
                     "of memory: gfn=%" PRIx64 " count=%" PRId64,
                     start_gfn, count);
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
    .class_size    = sizeof(S390SKeysClass),
};

static void s390_storage_keys_save(QEMUFile *f, void *opaque)
{
    S390SKeysState *ss = S390_SKEYS(opaque);
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    uint64_t pages_left = ram_size / TARGET_PAGE_SIZE;
    uint64_t read_count, eos = S390_SKEYS_SAVE_FLAG_EOS;
    vaddr cur_gfn = 0;
    int error = 0;
    uint8_t *buf;

    if (!skeyclass->skeys_enabled(ss)) {
        goto end_stream;
    }

    buf = g_try_malloc(S390_SKEYS_BUFFER_SIZE);
    if (!buf) {
        error_report("storage key save could not allocate memory");
        goto end_stream;
    }

    /* We only support initial memory. Standby memory is not handled yet. */
    qemu_put_be64(f, (cur_gfn * TARGET_PAGE_SIZE) | S390_SKEYS_SAVE_FLAG_SKEYS);
    qemu_put_be64(f, pages_left);

    while (pages_left) {
        read_count = MIN(pages_left, S390_SKEYS_BUFFER_SIZE);

        if (!error) {
            error = skeyclass->get_skeys(ss, cur_gfn, read_count, buf);
            if (error) {
                /*
                 * If error: we want to fill the stream with valid data instead
                 * of stopping early so we pad the stream with 0x00 values and
                 * use S390_SKEYS_SAVE_FLAG_ERROR to indicate failure to the
                 * reading side.
                 */
                error_report("S390_GET_KEYS error %d", error);
                memset(buf, 0, S390_SKEYS_BUFFER_SIZE);
                eos = S390_SKEYS_SAVE_FLAG_ERROR;
            }
        }

        qemu_put_buffer(f, buf, read_count);
        cur_gfn += read_count;
        pages_left -= read_count;
    }

    g_free(buf);
end_stream:
    qemu_put_be64(f, eos);
}

static int s390_storage_keys_load(QEMUFile *f, void *opaque, int version_id)
{
    S390SKeysState *ss = S390_SKEYS(opaque);
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    int ret = 0;

    while (!ret) {
        ram_addr_t addr;
        int flags;

        addr = qemu_get_be64(f);
        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        switch (flags) {
        case S390_SKEYS_SAVE_FLAG_SKEYS: {
            const uint64_t total_count = qemu_get_be64(f);
            uint64_t handled_count = 0, cur_count;
            uint64_t cur_gfn = addr / TARGET_PAGE_SIZE;
            uint8_t *buf = g_try_malloc(S390_SKEYS_BUFFER_SIZE);

            if (!buf) {
                error_report("storage key load could not allocate memory");
                ret = -ENOMEM;
                break;
            }

            while (handled_count < total_count) {
                cur_count = MIN(total_count - handled_count,
                                S390_SKEYS_BUFFER_SIZE);
                qemu_get_buffer(f, buf, cur_count);

                ret = skeyclass->set_skeys(ss, cur_gfn, cur_count, buf);
                if (ret < 0) {
                    error_report("S390_SET_KEYS error %d", ret);
                    break;
                }
                handled_count += cur_count;
                cur_gfn += cur_count;
            }
            g_free(buf);
            break;
        }
        case S390_SKEYS_SAVE_FLAG_ERROR: {
            error_report("Storage key data is incomplete");
            ret = -EINVAL;
            break;
        }
        case S390_SKEYS_SAVE_FLAG_EOS:
            /* normal exit */
            return 0;
        default:
            error_report("Unexpected storage key flag data: %#x", flags);
            ret = -EINVAL;
        }
    }

    return ret;
}

static inline bool s390_skeys_get_migration_enabled(Object *obj, Error **errp)
{
    S390SKeysState *ss = S390_SKEYS(obj);

    return ss->migration_enabled;
}

static SaveVMHandlers savevm_s390_storage_keys = {
    .save_state = s390_storage_keys_save,
    .load_state = s390_storage_keys_load,
};

static inline void s390_skeys_set_migration_enabled(Object *obj, bool value,
                                            Error **errp)
{
    S390SKeysState *ss = S390_SKEYS(obj);

    /* Prevent double registration of savevm handler */
    if (ss->migration_enabled == value) {
        return;
    }

    ss->migration_enabled = value;

    if (ss->migration_enabled) {
        register_savevm_live(NULL, TYPE_S390_SKEYS, 0, 1,
                             &savevm_s390_storage_keys, ss);
    } else {
        unregister_savevm(DEVICE(ss), TYPE_S390_SKEYS, ss);
    }
}

static void s390_skeys_instance_init(Object *obj)
{
    object_property_add_bool(obj, "migration-enabled",
                             s390_skeys_get_migration_enabled,
                             s390_skeys_set_migration_enabled, NULL);
    object_property_set_bool(obj, true, "migration-enabled", NULL);
}

static void s390_skeys_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo s390_skeys_info = {
    .name          = TYPE_S390_SKEYS,
    .parent        = TYPE_DEVICE,
    .instance_init = s390_skeys_instance_init,
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
