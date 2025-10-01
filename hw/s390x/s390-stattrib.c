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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "exec/target_page.h"
#include "migration/qemu-file.h"
#include "migration/register.h"
#include "hw/qdev-properties.h"
#include "hw/s390x/storage-attributes.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "cpu.h"

/* 512KiB cover 2GB of guest memory */
#define CMMA_BLOCK_SIZE  (512 * KiB)

#define STATTR_FLAG_EOS     0x01ULL
#define STATTR_FLAG_MORE    0x02ULL
#define STATTR_FLAG_ERROR   0x04ULL
#define STATTR_FLAG_DONE    0x08ULL

static S390StAttribState *s390_get_stattrib_device(void)
{
    S390StAttribState *sas;

    sas = S390_STATTRIB(object_resolve_path_type("", TYPE_S390_STATTRIB, NULL));
    assert(sas);
    return sas;
}

void s390_stattrib_init(void)
{
    Object *obj;

    obj = kvm_s390_stattrib_create();
    if (!obj) {
        obj = object_new(TYPE_QEMU_S390_STATTRIB);
    }

    object_property_add_child(qdev_get_machine(), TYPE_S390_STATTRIB,
                              obj);
    object_unref(obj);

    qdev_realize(DEVICE(obj), NULL, &error_fatal);
}

/* Console commands: */

void hmp_migrationmode(Monitor *mon, const QDict *qdict)
{
    S390StAttribState *sas = s390_get_stattrib_device();
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    uint64_t what = qdict_get_int(qdict, "mode");
    Error *local_err = NULL;
    int r;

    r = sac->set_migrationmode(sas, what, &local_err);
    if (r < 0) {
        monitor_printf(mon, "Error: %s", error_get_pretty(local_err));
        error_free(local_err);
    }
}

void hmp_info_cmma(Monitor *mon, const QDict *qdict)
{
    S390StAttribState *sas = s390_get_stattrib_device();
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    uint64_t addr = qdict_get_int(qdict, "addr");
    uint64_t buflen = qdict_get_try_int(qdict, "count", 8);
    uint8_t *vals;
    int cx, len;

    vals = g_try_malloc(buflen);
    if (!vals) {
        monitor_printf(mon, "Error: %s\n", strerror(errno));
        return;
    }

    len = sac->peek_stattr(sas, addr / TARGET_PAGE_SIZE, buflen, vals);
    if (len < 0) {
        monitor_printf(mon, "Error: %s", strerror(-len));
        goto out;
    }

    monitor_printf(mon, "  CMMA attributes, "
                   "pages %" PRIu64 "+%d (0x%" PRIx64 "):\n",
                   addr / TARGET_PAGE_SIZE, len, addr & ~TARGET_PAGE_MASK);
    for (cx = 0; cx < len; cx++) {
        if (cx % 8 == 7) {
            monitor_printf(mon, "%02x\n", vals[cx]);
        } else {
            monitor_printf(mon, "%02x", vals[cx]);
        }
    }
    monitor_printf(mon, "\n");

out:
    g_free(vals);
}

/* Migration support: */

static int cmma_load(QEMUFile *f, void *opaque, int version_id)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    uint64_t count, cur_gfn;
    int flags, ret = 0;
    ram_addr_t addr;
    uint8_t *buf;

    while (!ret) {
        addr = qemu_get_be64(f);
        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        switch (flags) {
        case STATTR_FLAG_MORE: {
            cur_gfn = addr / TARGET_PAGE_SIZE;
            count = qemu_get_be64(f);
            buf = g_try_malloc(count);
            if (!buf) {
                error_report("cmma_load could not allocate memory");
                ret = -ENOMEM;
                break;
            }

            qemu_get_buffer(f, buf, count);
            ret = sac->set_stattr(sas, cur_gfn, count, buf);
            if (ret < 0) {
                error_report("Error %d while setting storage attributes", ret);
            }
            g_free(buf);
            break;
        }
        case STATTR_FLAG_ERROR: {
            error_report("Storage attributes data is incomplete");
            ret = -EINVAL;
            break;
        }
        case STATTR_FLAG_DONE:
            /* This is after the last pre-copied value has been sent, nothing
             * more will be sent after this. Pre-copy has finished, and we
             * are done flushing all the remaining values. Now the target
             * system is about to take over. We synchronize the buffer to
             * apply the actual correct values where needed.
             */
             sac->synchronize(sas);
            break;
        case STATTR_FLAG_EOS:
            /* Normal exit */
            return 0;
        default:
            error_report("Unexpected storage attribute flag data: %#x", flags);
            ret = -EINVAL;
        }
    }

    return ret;
}

static int cmma_save_setup(QEMUFile *f, void *opaque, Error **errp)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    int res;
    /*
     * Signal that we want to start a migration, thus needing PGSTE dirty
     * tracking.
     */
    res = sac->set_migrationmode(sas, true, errp);
    if (res) {
        return res;
    }
    qemu_put_be64(f, STATTR_FLAG_EOS);
    return 0;
}

static void cmma_state_pending(void *opaque, uint64_t *must_precopy,
                               uint64_t *can_postcopy)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    long long res = sac->get_dirtycount(sas);

    if (res >= 0) {
        *must_precopy += res;
    }
}

static int cmma_save(QEMUFile *f, void *opaque, int final)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    uint8_t *buf;
    int r, cx, reallen = 0, ret = 0;
    uint32_t buflen = CMMA_BLOCK_SIZE;
    uint64_t start_gfn = sas->migration_cur_gfn;

    buf = g_try_malloc(buflen);
    if (!buf) {
        error_report("Could not allocate memory to save storage attributes");
        return -ENOMEM;
    }

    while (final ? 1 : migration_rate_exceeded(f) == 0) {
        reallen = sac->get_stattr(sas, &start_gfn, buflen, buf);
        if (reallen < 0) {
            g_free(buf);
            return reallen;
        }

        ret = 1;
        if (!reallen) {
            break;
        }
        qemu_put_be64(f, (start_gfn << TARGET_PAGE_BITS) | STATTR_FLAG_MORE);
        qemu_put_be64(f, reallen);
        for (cx = 0; cx < reallen; cx++) {
            qemu_put_byte(f, buf[cx]);
        }
        if (!sac->get_dirtycount(sas)) {
            break;
        }
    }

    sas->migration_cur_gfn = start_gfn + reallen;
    g_free(buf);
    if (final) {
        qemu_put_be64(f, STATTR_FLAG_DONE);
    }
    qemu_put_be64(f, STATTR_FLAG_EOS);

    r = qemu_file_get_error(f);
    if (r < 0) {
        return r;
    }

    return ret;
}

static int cmma_save_iterate(QEMUFile *f, void *opaque)
{
    return cmma_save(f, opaque, 0);
}

static int cmma_save_complete(QEMUFile *f, void *opaque)
{
    return cmma_save(f, opaque, 1);
}

static void cmma_save_cleanup(void *opaque)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    sac->set_migrationmode(sas, false, NULL);
}

static bool cmma_active(void *opaque)
{
    S390StAttribState *sas = S390_STATTRIB(opaque);
    S390StAttribClass *sac = S390_STATTRIB_GET_CLASS(sas);
    return sac->get_active(sas);
}

/* QEMU object: */

static void qemu_s390_stattrib_instance_init(Object *obj)
{
}

static int qemu_s390_peek_stattr_stub(S390StAttribState *sa, uint64_t start_gfn,
                                     uint32_t count, uint8_t *values)
{
    return 0;
}
static void qemu_s390_synchronize_stub(S390StAttribState *sa)
{
}
static int qemu_s390_get_stattr_stub(S390StAttribState *sa, uint64_t *start_gfn,
                                     uint32_t count, uint8_t *values)
{
    return 0;
}
static long long qemu_s390_get_dirtycount_stub(S390StAttribState *sa)
{
    return 0;
}
static int qemu_s390_set_migrationmode_stub(S390StAttribState *sa, bool value,
                                            Error **errp)
{
    return 0;
}

static int qemu_s390_get_active(S390StAttribState *sa)
{
    return true;
}

static void qemu_s390_stattrib_class_init(ObjectClass *oc, const void *data)
{
    S390StAttribClass *sa_cl = S390_STATTRIB_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sa_cl->synchronize = qemu_s390_synchronize_stub;
    sa_cl->get_stattr = qemu_s390_get_stattr_stub;
    sa_cl->set_stattr = qemu_s390_peek_stattr_stub;
    sa_cl->peek_stattr = qemu_s390_peek_stattr_stub;
    sa_cl->set_migrationmode = qemu_s390_set_migrationmode_stub;
    sa_cl->get_dirtycount = qemu_s390_get_dirtycount_stub;
    sa_cl->get_active = qemu_s390_get_active;

    /* Reason: Can only be instantiated one time (internally) */
    dc->user_creatable = false;
}

static const TypeInfo qemu_s390_stattrib_info = {
    .name          = TYPE_QEMU_S390_STATTRIB,
    .parent        = TYPE_S390_STATTRIB,
    .instance_init = qemu_s390_stattrib_instance_init,
    .instance_size = sizeof(QEMUS390StAttribState),
    .class_init    = qemu_s390_stattrib_class_init,
    .class_size    = sizeof(S390StAttribClass),
};

/* Generic abstract object: */

static SaveVMHandlers savevm_s390_stattrib_handlers = {
    .save_setup = cmma_save_setup,
    .save_live_iterate = cmma_save_iterate,
    .save_complete = cmma_save_complete,
    .state_pending_exact = cmma_state_pending,
    .state_pending_estimate = cmma_state_pending,
    .save_cleanup = cmma_save_cleanup,
    .load_state = cmma_load,
    .is_active = cmma_active,
};

static void s390_stattrib_realize(DeviceState *dev, Error **errp)
{
    bool ambiguous = false;

    object_resolve_path_type("", TYPE_S390_STATTRIB, &ambiguous);
    if (ambiguous) {
        error_setg(errp, "storage_attributes device already exists");
        return;
    }

    register_savevm_live(TYPE_S390_STATTRIB, 0, 0,
                         &savevm_s390_stattrib_handlers, dev);
}

static void s390_stattrib_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->realize = s390_stattrib_realize;
}

static void s390_stattrib_instance_init(Object *obj)
{
    S390StAttribState *sas = S390_STATTRIB(obj);

    sas->migration_cur_gfn = 0;
}

static const TypeInfo s390_stattrib_info = {
    .name          = TYPE_S390_STATTRIB,
    .parent        = TYPE_DEVICE,
    .instance_init = s390_stattrib_instance_init,
    .instance_size = sizeof(S390StAttribState),
    .class_init    = s390_stattrib_class_init,
    .class_size    = sizeof(S390StAttribClass),
    .abstract      = true,
};

static void s390_stattrib_register_types(void)
{
    type_register_static(&s390_stattrib_info);
    type_register_static(&qemu_s390_stattrib_info);
}

type_init(s390_stattrib_register_types)
