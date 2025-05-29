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
#include "qemu/units.h"
#include "exec/target_page.h"
#include "hw/s390x/s390-virtio-ccw.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/storage-keys.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qobject/qdict.h"
#include "qemu/error-report.h"
#include "system/memory_mapping.h"
#include "system/address-spaces.h"
#include "system/kvm.h"
#include "migration/qemu-file-types.h"
#include "migration/register.h"
#include "trace.h"

#define S390_SKEYS_BUFFER_SIZE (128 * KiB)  /* Room for 128k storage keys */
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
                              obj);
    object_unref(obj);

    qdev_realize(DEVICE(obj), NULL, &error_fatal);
}

int s390_skeys_get(S390SKeysState *ks, uint64_t start_gfn,
                   uint64_t count, uint8_t *keys)
{
    S390SKeysClass *kc = S390_SKEYS_GET_CLASS(ks);
    int rc;

    rc = kc->get_skeys(ks, start_gfn, count, keys);
    if (rc) {
        trace_s390_skeys_get_nonzero(rc);
    }
    return rc;
}

int s390_skeys_set(S390SKeysState *ks, uint64_t start_gfn,
                   uint64_t count, uint8_t *keys)
{
    S390SKeysClass *kc = S390_SKEYS_GET_CLASS(ks);
    int rc;

    rc = kc->set_skeys(ks, start_gfn, count, keys);
    if (rc) {
        trace_s390_skeys_set_nonzero(rc);
    }
    return rc;
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
    if (!skeyclass->skeys_are_enabled(ss)) {
        monitor_printf(mon, "Error: This guest is not using storage keys\n");
        return;
    }

    if (!address_space_access_valid(&address_space_memory,
                                    addr & TARGET_PAGE_MASK, TARGET_PAGE_SIZE,
                                    false, MEMTXATTRS_UNSPECIFIED)) {
        monitor_printf(mon, "Error: The given address is not valid\n");
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

void s390_qmp_dump_skeys(const char *filename, Error **errp)
{
    S390SKeysState *ss = s390_get_skeys_device();
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    GuestPhysBlockList guest_phys_blocks;
    GuestPhysBlock *block;
    uint64_t pages, gfn;
    Error *lerr = NULL;
    uint8_t *buf;
    int ret;
    int fd;
    FILE *f;

    /* Quick check to see if guest is using storage keys*/
    if (!skeyclass->skeys_are_enabled(ss)) {
        error_setg(errp, "This guest is not using storage keys - "
                         "nothing to dump");
        return;
    }

    fd = qemu_open_old(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
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

    assert(bql_locked());
    guest_phys_blocks_init(&guest_phys_blocks);
    guest_phys_blocks_append(&guest_phys_blocks);

    QTAILQ_FOREACH(block, &guest_phys_blocks.head, next) {
        assert(QEMU_IS_ALIGNED(block->target_start, TARGET_PAGE_SIZE));
        assert(QEMU_IS_ALIGNED(block->target_end, TARGET_PAGE_SIZE));

        gfn = block->target_start / TARGET_PAGE_SIZE;
        pages = (block->target_end - block->target_start) / TARGET_PAGE_SIZE;

        while (pages) {
            const uint64_t cur_pages = MIN(pages, S390_SKEYS_BUFFER_SIZE);

            ret = skeyclass->get_skeys(ss, gfn, cur_pages, buf);
            if (ret < 0) {
                error_setg_errno(errp, -ret, "get_keys error");
                goto out_free;
            }

            /* write keys to stream */
            write_keys(f, buf, gfn, cur_pages, &lerr);
            if (lerr) {
                goto out_free;
            }

            gfn += cur_pages;
            pages -= cur_pages;
        }
    }

out_free:
    guest_phys_blocks_free(&guest_phys_blocks);
    error_propagate(errp, lerr);
    g_free(buf);
out:
    fclose(f);
}

static bool qemu_s390_skeys_are_enabled(S390SKeysState *ss)
{
    QEMUS390SKeysState *skeys = QEMU_S390_SKEYS(ss);

    /* Lockless check is sufficient. */
    return !!skeys->keydata;
}

static bool qemu_s390_enable_skeys(S390SKeysState *ss)
{
    QEMUS390SKeysState *skeys = QEMU_S390_SKEYS(ss);
    static gsize initialized;

    if (likely(skeys->keydata)) {
        return true;
    }

    /*
     * TODO: Modern Linux doesn't use storage keys unless running KVM guests
     *       that use storage keys. Therefore, we keep it simple for now.
     *
     * 1) We should initialize to "referenced+changed" for an initial
     *    over-indication. Let's avoid touching megabytes of data for now and
     *    assume that any sane user will issue a storage key instruction before
     *    actually relying on this data.
     * 2) Relying on ram_size and allocating a big array is ugly. We should
     *    allocate and manage storage key data per RAMBlock or optimally using
     *    some sparse data structure.
     * 3) We only ever have a single S390SKeysState, so relying on
     *    g_once_init_enter() is good enough.
     */
    if (g_once_init_enter(&initialized)) {
        S390CcwMachineState *s390ms = S390_CCW_MACHINE(qdev_get_machine());

        skeys->key_count = s390_get_memory_limit(s390ms) / TARGET_PAGE_SIZE;
        skeys->keydata = g_malloc0(skeys->key_count);
        g_once_init_leave(&initialized, 1);
    }
    return false;
}

static int qemu_s390_skeys_set(S390SKeysState *ss, uint64_t start_gfn,
                              uint64_t count, uint8_t *keys)
{
    QEMUS390SKeysState *skeydev = QEMU_S390_SKEYS(ss);
    int i;

    /* Check for uint64 overflow and access beyond end of key data */
    if (unlikely(!skeydev->keydata || start_gfn + count > skeydev->key_count ||
                  start_gfn + count < count)) {
        error_report("Error: Setting storage keys for pages with unallocated "
                     "storage key memory: gfn=%" PRIx64 " count=%" PRId64,
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
    if (unlikely(!skeydev->keydata || start_gfn + count > skeydev->key_count ||
                  start_gfn + count < count)) {
        error_report("Error: Getting storage keys for pages with unallocated "
                     "storage key memory: gfn=%" PRIx64 " count=%" PRId64,
                     start_gfn, count);
        return -EINVAL;
    }

    for (i = 0; i < count; i++) {
        keys[i] = skeydev->keydata[start_gfn + i];
    }
    return 0;
}

static void qemu_s390_skeys_class_init(ObjectClass *oc, const void *data)
{
    S390SKeysClass *skeyclass = S390_SKEYS_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    skeyclass->skeys_are_enabled = qemu_s390_skeys_are_enabled;
    skeyclass->enable_skeys = qemu_s390_enable_skeys;
    skeyclass->get_skeys = qemu_s390_skeys_get;
    skeyclass->set_skeys = qemu_s390_skeys_set;

    /* Reason: Internal device (only one skeys device for the whole memory) */
    dc->user_creatable = false;
}

static void s390_storage_keys_save(QEMUFile *f, void *opaque)
{
    S390SKeysState *ss = S390_SKEYS(opaque);
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    GuestPhysBlockList guest_phys_blocks;
    GuestPhysBlock *block;
    uint64_t pages, gfn;
    int error = 0;
    uint8_t *buf;

    if (!skeyclass->skeys_are_enabled(ss)) {
        goto end_stream;
    }

    buf = g_try_malloc(S390_SKEYS_BUFFER_SIZE);
    if (!buf) {
        error_report("storage key save could not allocate memory");
        goto end_stream;
    }

    guest_phys_blocks_init(&guest_phys_blocks);
    guest_phys_blocks_append(&guest_phys_blocks);

    /* Send each contiguous physical memory range separately. */
    QTAILQ_FOREACH(block, &guest_phys_blocks.head, next) {
        assert(QEMU_IS_ALIGNED(block->target_start, TARGET_PAGE_SIZE));
        assert(QEMU_IS_ALIGNED(block->target_end, TARGET_PAGE_SIZE));

        gfn = block->target_start / TARGET_PAGE_SIZE;
        pages = (block->target_end - block->target_start) / TARGET_PAGE_SIZE;
        qemu_put_be64(f, block->target_start | S390_SKEYS_SAVE_FLAG_SKEYS);
        qemu_put_be64(f, pages);

        while (pages) {
            const uint64_t cur_pages = MIN(pages, S390_SKEYS_BUFFER_SIZE);

            if (!error) {
                error = skeyclass->get_skeys(ss, gfn, cur_pages, buf);
                if (error) {
                    /*
                     * Create a valid stream with all 0x00 and indicate
                     * S390_SKEYS_SAVE_FLAG_ERROR to the destination.
                     */
                    error_report("S390_GET_KEYS error %d", error);
                    memset(buf, 0, S390_SKEYS_BUFFER_SIZE);
                }
            }

            qemu_put_buffer(f, buf, cur_pages);
            gfn += cur_pages;
            pages -= cur_pages;
        }

        if (error) {
            break;
        }
    }

    guest_phys_blocks_free(&guest_phys_blocks);
    g_free(buf);
end_stream:
    if (error) {
        qemu_put_be64(f, S390_SKEYS_SAVE_FLAG_ERROR);
    } else {
        qemu_put_be64(f, S390_SKEYS_SAVE_FLAG_EOS);
    }
}

static int s390_storage_keys_load(QEMUFile *f, void *opaque, int version_id)
{
    S390SKeysState *ss = S390_SKEYS(opaque);
    S390SKeysClass *skeyclass = S390_SKEYS_GET_CLASS(ss);
    int ret = 0;

    /*
     * Make sure to lazy-enable if required to be done explicitly. No need to
     * flush any TLB as the VM is not running yet.
     */
    if (skeyclass->enable_skeys) {
        skeyclass->enable_skeys(ss);
    }

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

static SaveVMHandlers savevm_s390_storage_keys = {
    .save_state = s390_storage_keys_save,
    .load_state = s390_storage_keys_load,
};

static void s390_skeys_realize(DeviceState *dev, Error **errp)
{
    S390SKeysState *ss = S390_SKEYS(dev);

    register_savevm_live(TYPE_S390_SKEYS, 0, 1, &savevm_s390_storage_keys, ss);
}

static void s390_skeys_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->hotpluggable = false;
    dc->realize = s390_skeys_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo s390_skeys_types[] = {
    {
        .name           = TYPE_DUMP_SKEYS_INTERFACE,
        .parent         = TYPE_INTERFACE,
        .class_size     = sizeof(DumpSKeysInterface),
    },
    {
        .name           = TYPE_S390_SKEYS,
        .parent         = TYPE_DEVICE,
        .instance_size  = sizeof(S390SKeysState),
        .class_init     = s390_skeys_class_init,
        .class_size     = sizeof(S390SKeysClass),
        .abstract       = true,
    },
    {
        .name           = TYPE_QEMU_S390_SKEYS,
        .parent         = TYPE_S390_SKEYS,
        .instance_size  = sizeof(QEMUS390SKeysState),
        .class_init     = qemu_s390_skeys_class_init,
        .class_size     = sizeof(S390SKeysClass),
    },
};

DEFINE_TYPES(s390_skeys_types)
