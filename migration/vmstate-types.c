/*
 * VMStateInfo's for basic typse
 *
 * Copyright (c) 2009-2017 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cpu-float.h"
#include "qemu-file.h"
#include "migration.h"
#include "migration/vmstate.h"
#include "migration/client-options.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "trace.h"
#include "qapi/error.h"

/* bool */

static bool load_bool(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, Error **errp)
{
    bool *v = pv;
    *v = qemu_get_byte(f);
    return true;
}

static bool save_bool(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc,
                      Error **errp)
{
    bool *v = pv;
    qemu_put_byte(f, *v);
    return true;
}

const VMStateInfo vmstate_info_bool = {
    .name = "bool",
    .load = load_bool,
    .save = save_bool,
};

/* 8 bit int */

static bool load_int8(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, Error **errp)
{
    int8_t *v = pv;
    qemu_get_s8s(f, v);
    return true;
}

static bool save_int8(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc,
                      Error **errp)
{
    int8_t *v = pv;
    qemu_put_s8s(f, v);
    return true;
}

const VMStateInfo vmstate_info_int8 = {
    .name = "int8",
    .load = load_int8,
    .save = save_int8,
};

/* 16 bit int */

static bool load_int16(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, Error **errp)
{
    int16_t *v = pv;
    qemu_get_sbe16s(f, v);
    return true;
}

static bool save_int16(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    int16_t *v = pv;
    qemu_put_sbe16s(f, v);
    return true;
}

const VMStateInfo vmstate_info_int16 = {
    .name = "int16",
    .load = load_int16,
    .save = save_int16,
};

/* 32 bit int */

static bool load_int32(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, Error **errp)
{
    int32_t *v = pv;
    qemu_get_sbe32s(f, v);
    return true;
}

static bool save_int32(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    int32_t *v = pv;
    qemu_put_sbe32s(f, v);
    return true;
}

const VMStateInfo vmstate_info_int32 = {
    .name = "int32",
    .load = load_int32,
    .save = save_int32,
};

/* 32 bit int. See that the received value is the same than the one
   in the field */

static bool load_int32_equal(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field, Error **errp)
{
    ERRP_GUARD();
    int32_t *v = pv;
    int32_t v2;
    qemu_get_sbe32s(f, &v2);

    if (*v == v2) {
        return true;
    }

    error_setg(errp, "%" PRIx32 " != %" PRIx32, *v, v2);
    return false;
}

const VMStateInfo vmstate_info_int32_equal = {
    .name = "int32 equal",
    .load = load_int32_equal,
    .save = save_int32,
};

/* 32 bit int. Check that the received value is non-negative
 * and less than or equal to the one in the field.
 */

static bool load_int32_le(QEMUFile *f, void *pv, size_t size,
                          const VMStateField *field, Error **errp)
{
    int32_t *cur = pv;
    int32_t loaded;
    qemu_get_sbe32s(f, &loaded);

    if (loaded >= 0 && loaded <= *cur) {
        *cur = loaded;
        return true;
    }

    error_setg(errp, "Invalid value %" PRId32
               " expecting positive value <= %" PRId32,
               loaded, *cur);
    return false;
}

const VMStateInfo vmstate_info_int32_le = {
    .name = "int32 le",
    .load = load_int32_le,
    .save = save_int32,
};

/* 64 bit int */

static bool load_int64(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, Error **errp)
{
    int64_t *v = pv;
    qemu_get_sbe64s(f, v);
    return true;
}

static bool save_int64(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    int64_t *v = pv;
    qemu_put_sbe64s(f, v);
    return true;
}

const VMStateInfo vmstate_info_int64 = {
    .name = "int64",
    .load = load_int64,
    .save = save_int64,
};

/* 8 bit unsigned int */

static bool load_uint8(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, Error **errp)
{
    uint8_t *v = pv;
    qemu_get_8s(f, v);
    return true;
}

static bool save_uint8(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    uint8_t *v = pv;
    qemu_put_8s(f, v);
    return true;
}

const VMStateInfo vmstate_info_uint8 = {
    .name = "uint8",
    .load = load_uint8,
    .save = save_uint8,
};

/* 16 bit unsigned int */

static bool load_uint16(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, Error **errp)
{
    uint16_t *v = pv;
    qemu_get_be16s(f, v);
    return true;
}

static bool save_uint16(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    uint16_t *v = pv;
    qemu_put_be16s(f, v);
    return true;
}

const VMStateInfo vmstate_info_uint16 = {
    .name = "uint16",
    .load = load_uint16,
    .save = save_uint16,
};

/* 32 bit unsigned int */

static bool load_uint32(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, Error **errp)
{
    uint32_t *v = pv;
    qemu_get_be32s(f, v);
    return true;
}

static bool save_uint32(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    uint32_t *v = pv;
    qemu_put_be32s(f, v);
    return true;
}

const VMStateInfo vmstate_info_uint32 = {
    .name = "uint32",
    .load = load_uint32,
    .save = save_uint32,
};

/* 32 bit uint. See that the received value is the same than the one
   in the field */

static bool load_uint32_equal(QEMUFile *f, void *pv, size_t size,
                              const VMStateField *field, Error **errp)
{
    ERRP_GUARD();
    uint32_t *v = pv;
    uint32_t v2;
    qemu_get_be32s(f, &v2);

    if (*v == v2) {
        return true;
    }

    error_setg(errp, "%" PRIx32 " != %" PRIx32, *v, v2);
    return false;
}

const VMStateInfo vmstate_info_uint32_equal = {
    .name = "uint32 equal",
    .load = load_uint32_equal,
    .save = save_uint32,
};

/* 64 bit unsigned int */

static bool load_uint64(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, Error **errp)
{
    uint64_t *v = pv;
    qemu_get_be64s(f, v);
    return true;
}

static bool save_uint64(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    uint64_t *v = pv;
    qemu_put_be64s(f, v);
    return true;
}

const VMStateInfo vmstate_info_uint64 = {
    .name = "uint64",
    .load = load_uint64,
    .save = save_uint64,
};

/* File descriptor communicated via SCM_RIGHTS */

static bool load_fd(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field, Error **errp)
{
    int32_t *v = pv;

    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        qemu_get_sbe32s(f, v);
        return true;
    }

    return qemu_file_get_fd(f, v) >= 0;
}

static bool save_fd(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc,
                    Error **errp)
{
    int32_t *v = pv;

    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        qemu_put_sbe32s(f, v);
        return true;
    }

    return qemu_file_put_fd(f, *v) >= 0;
}

const VMStateInfo vmstate_info_fd = {
    .name = "fd",
    .load = load_fd,
    .save = save_fd,
};

static bool load_ptr_marker(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field, Error **errp)

{
    /*
     * Load is done in vmstate core, see vmstate_ptr_marker_load().
     */
    g_assert_not_reached();
    return false;
}

static bool save_ptr_marker(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field, JSONWriter *vmdesc,
                            Error **errp)

{
    qemu_put_byte(f, pv ? VMS_MARKER_PTR_VALID : VMS_MARKER_PTR_NULL);
    return true;
}

const VMStateInfo vmstate_info_ptr_marker = {
    .name = "ptr-marker",
    .load = load_ptr_marker,
    .save = save_ptr_marker,
};

/* 64 bit unsigned int. See that the received value is the same than the one
   in the field */

static bool load_uint64_equal(QEMUFile *f, void *pv, size_t size,
                              const VMStateField *field, Error **errp)
{
    ERRP_GUARD();
    uint64_t *v = pv;
    uint64_t v2;

    qemu_get_be64s(f, &v2);

    if (*v == v2) {
        return true;
    }

    error_setg(errp, "%" PRIx64 " != %" PRIx64, *v, v2);
    return false;
}

const VMStateInfo vmstate_info_uint64_equal = {
    .name = "int64 equal",
    .load = load_uint64_equal,
    .save = save_uint64,
};

/* 8 bit int. See that the received value is the same than the one
   in the field */

static bool load_uint8_equal(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field, Error **errp)
{
    ERRP_GUARD();
    uint8_t *v = pv;
    uint8_t v2;

    qemu_get_8s(f, &v2);

    if (*v == v2) {
        return true;
    }

    error_setg(errp, "%x != %x", *v, v2);
    return false;
}

const VMStateInfo vmstate_info_uint8_equal = {
    .name = "uint8 equal",
    .load = load_uint8_equal,
    .save = save_uint8,
};

/* 16 bit unsigned int int. See that the received value is the same than the one
   in the field */

static bool load_uint16_equal(QEMUFile *f, void *pv, size_t size,
                              const VMStateField *field, Error **errp)
{
    ERRP_GUARD();
    uint16_t *v = pv;
    uint16_t v2;

    qemu_get_be16s(f, &v2);

    if (*v == v2) {
        return true;
    }

    error_setg(errp, "%x != %x", *v, v2);
    return false;
}

const VMStateInfo vmstate_info_uint16_equal = {
    .name = "uint16 equal",
    .load = load_uint16_equal,
    .save = save_uint16,
};

/* CPU_DoubleU type */

static bool load_cpudouble(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field, Error **errp)
{
    CPU_DoubleU *v = pv;
    qemu_get_be32s(f, &v->l.upper);
    qemu_get_be32s(f, &v->l.lower);
    return true;
}

static bool save_cpudouble(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field, JSONWriter *vmdesc,
                           Error **errp)
{
    CPU_DoubleU *v = pv;
    qemu_put_be32s(f, &v->l.upper);
    qemu_put_be32s(f, &v->l.lower);
    return true;
}

const VMStateInfo vmstate_info_cpudouble = {
    .name = "CPU_Double_U",
    .load = load_cpudouble,
    .save = save_cpudouble,
};

/* uint8_t buffers */

static bool load_buffer(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, Error **errp)
{
    uint8_t *v = pv;
    qemu_get_buffer(f, v, size);
    return true;
}

static bool save_buffer(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    uint8_t *v = pv;
    qemu_put_buffer(f, v, size);
    return true;
}

const VMStateInfo vmstate_info_buffer = {
    .name = "buffer",
    .load = load_buffer,
    .save = save_buffer,
};

/* unused buffers: space that was used for some fields that are
   not useful anymore */

static bool load_unused_buffer(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field, Error **errp)
{
    uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_get_buffer(f, buf, block_len);
    }

    return true;
}

static bool save_unused_buffer(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field, JSONWriter *vmdesc,
                               Error **errp)
{
    static const uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_put_buffer(f, buf, block_len);
    }

    return true;
}

const VMStateInfo vmstate_info_unused_buffer = {
    .name = "unused_buffer",
    .load = load_unused_buffer,
    .save = save_unused_buffer,
};

/* vmstate_info_tmp, see VMSTATE_WITH_TMP, the idea is that we allocate
 * a temporary buffer and the pre_load/pre_save methods in the child vmsd
 * copy stuff from the parent into the child and do calculations to fill
 * in fields that don't really exist in the parent but need to be in the
 * stream.
 */
static bool load_tmp(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    int version_id = field->version_id;
    g_autofree void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    return vmstate_load_vmsd(f, vmsd, tmp, version_id, errp);
}

static bool save_tmp(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc,
                     Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    g_autofree void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    return vmstate_save_vmsd(f, vmsd, tmp, vmdesc, errp);
}

const VMStateInfo vmstate_info_tmp = {
    .name = "tmp",
    .load = load_tmp,
    .save = save_tmp,
};

/* bitmaps (as defined by bitmap.h). Note that size here is the size
 * of the bitmap in bits. The on-the-wire format of a bitmap is 64
 * bit words with the bits in big endian order. The in-memory format
 * is an array of 'unsigned long', which may be either 32 or 64 bits.
 */
/* This is the number of 64 bit words sent over the wire */
#define BITS_TO_U64S(nr) DIV_ROUND_UP(nr, 64)
static bool load_bitmap(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, Error **errp)
{
    unsigned long *bmp = pv;
    int i, idx = 0;

    for (i = 0; i < BITS_TO_U64S(size); i++) {
        uint64_t w = qemu_get_be64(f);
        bmp[idx++] = w;
        if (sizeof(unsigned long) == 4 && idx < BITS_TO_LONGS(size)) {
            bmp[idx++] = w >> 32;
        }
    }

    return true;
}

static bool save_bitmap(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    unsigned long *bmp = pv;
    int i, idx = 0;

    for (i = 0; i < BITS_TO_U64S(size); i++) {
        uint64_t w = bmp[idx++];
        if (sizeof(unsigned long) == 4 && idx < BITS_TO_LONGS(size)) {
            w |= ((uint64_t)bmp[idx++]) << 32;
        }
        qemu_put_be64(f, w);
    }

    return true;
}

const VMStateInfo vmstate_info_bitmap = {
    .name = "bitmap",
    .load = load_bitmap,
    .save = save_bitmap,
};

/* get for QTAILQ
 * meta data about the QTAILQ is encoded in a VMStateField structure
 */
static bool load_qtailq(QEMUFile *f, void *pv, size_t unused_size,
                        const VMStateField *field, Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* size of a QTAILQ element */
    size_t size = field->size;
    /* offset of the QTAILQ entry in a QTAILQ element */
    size_t entry_offset = field->start;
    int version_id = field->version_id;
    void *elm;

    trace_load_qtailq(vmsd->name, version_id);
    if (version_id > vmsd->version_id) {
        error_setg(errp, "%s %s",  vmsd->name, "too new");
        return false;
    }
    if (version_id < vmsd->minimum_version_id) {
        error_setg(errp, "%s %s",  vmsd->name, "too old");
        return false;
    }

    while (qemu_get_byte(f)) {
        elm = g_malloc(size);
        if (!vmstate_load_vmsd(f, vmsd, elm, version_id, errp)) {
            g_free(elm);
            return false;
        }
        QTAILQ_RAW_INSERT_TAIL(pv, elm, entry_offset);
    }

    trace_load_qtailq_end(vmsd->name);
    return true;
}

/* save for QTAILQ */
static bool save_qtailq(QEMUFile *f, void *pv, size_t unused_size,
                        const VMStateField *field, JSONWriter *vmdesc,
                        Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* offset of the QTAILQ entry in a QTAILQ element*/
    size_t entry_offset = field->start;
    void *elm;

    trace_save_qtailq(vmsd->name, vmsd->version_id);

    QTAILQ_RAW_FOREACH(elm, pv, entry_offset) {
        qemu_put_byte(f, true);
        if (!vmstate_save_vmsd(f, vmsd, elm, vmdesc, errp)) {
            return false;
        }
    }
    qemu_put_byte(f, false);

    trace_save_qtailq_end(vmsd->name);

    return true;
}
const VMStateInfo vmstate_info_qtailq = {
    .name = "qtailq",
    .load = load_qtailq,
    .save = save_qtailq,
};

struct save_gtree_data {
    QEMUFile *f;
    const VMStateDescription *key_vmsd;
    const VMStateDescription *val_vmsd;
    JSONWriter *vmdesc;
    Error **errp;
    bool failed;
};

/*
 * save_gtree_elem - func for g_tree_foreach, return true to stop
 * iteration.
 */
static gboolean save_gtree_elem(gpointer key, gpointer value, gpointer data)
{
    struct save_gtree_data *capsule = (struct save_gtree_data *)data;
    QEMUFile *f = capsule->f;

    qemu_put_byte(f, true);

    /* put the key */
    if (!capsule->key_vmsd) {
        qemu_put_be64(f, (uint64_t)(uintptr_t)(key)); /* direct key */
    } else {
        if (!vmstate_save_vmsd(f, capsule->key_vmsd, key, capsule->vmdesc,
                               capsule->errp)) {
            capsule->failed = true;
            return true;
        }
    }

    /* put the data */
    if (!vmstate_save_vmsd(f, capsule->val_vmsd, value, capsule->vmdesc,
                           capsule->errp)) {
        capsule->failed = true;
        return true;
    }
    return false;
}

static bool save_gtree(QEMUFile *f, void *pv, size_t unused_size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    bool direct_key = (!field->start);
    const VMStateDescription *key_vmsd = direct_key ? NULL : &field->vmsd[1];
    const VMStateDescription *val_vmsd = &field->vmsd[0];
    const char *key_vmsd_name = direct_key ? "direct" : key_vmsd->name;
    struct save_gtree_data capsule = {
        .f = f,
        .key_vmsd = key_vmsd,
        .val_vmsd = val_vmsd,
        .vmdesc = vmdesc,
        .errp = errp,
        .failed = false};
    GTree **pval = pv;
    GTree *tree = *pval;
    uint32_t nnodes = g_tree_nnodes(tree);

    trace_save_gtree(field->name, key_vmsd_name, val_vmsd->name, nnodes);
    qemu_put_be32(f, nnodes);
    g_tree_foreach(tree, save_gtree_elem, (gpointer)&capsule);
    qemu_put_byte(f, false);
    if (capsule.failed) {
        trace_save_gtree_end(field->name, key_vmsd_name, val_vmsd->name);
        return false;
    }

    trace_save_gtree_end(field->name, key_vmsd_name, val_vmsd->name);
    return true;
}

static bool load_gtree(QEMUFile *f, void *pv, size_t unused_size,
                       const VMStateField *field, Error **errp)
{
    bool direct_key = (!field->start);
    const VMStateDescription *key_vmsd = direct_key ? NULL : &field->vmsd[1];
    const VMStateDescription *val_vmsd = &field->vmsd[0];
    const char *key_vmsd_name = direct_key ? "direct" : key_vmsd->name;
    int version_id = field->version_id;
    size_t key_size = field->start;
    size_t val_size = field->size;
    int nnodes, count = 0;
    GTree **pval = pv;
    GTree *tree = *pval;
    void *key, *val;

    /* in case of direct key, the key vmsd can be {}, ie. check fields */
    if (!direct_key && version_id > key_vmsd->version_id) {
        error_setg(errp, "%s %s",  key_vmsd->name, "too new");
        return false;
    }
    if (!direct_key && version_id < key_vmsd->minimum_version_id) {
        error_setg(errp, "%s %s",  key_vmsd->name, "too old");
        return false;
    }
    if (version_id > val_vmsd->version_id) {
        error_setg(errp, "%s %s",  val_vmsd->name, "too new");
        return false;
    }
    if (version_id < val_vmsd->minimum_version_id) {
        error_setg(errp, "%s %s",  val_vmsd->name, "too old");
        return false;
    }

    nnodes = qemu_get_be32(f);
    trace_load_gtree(field->name, key_vmsd_name, val_vmsd->name, nnodes);

    while (qemu_get_byte(f)) {
        if ((++count) > nnodes) {
            break;
        }
        if (direct_key) {
            key = (void *)(uintptr_t)qemu_get_be64(f);
        } else {
            key = g_malloc0(key_size);
            if (!vmstate_load_vmsd(f, key_vmsd, key, version_id, errp)) {
                goto key_error;
            }
        }
        val = g_malloc0(val_size);
        if (!vmstate_load_vmsd(f, val_vmsd, val, version_id, errp)) {
            goto val_error;
        }
        g_tree_insert(tree, key, val);
    }
    if (count != nnodes) {
        error_setg(errp, "%s inconsistent stream when loading the gtree",
                   field->name);
        return false;
    }

    trace_load_gtree_end(field->name, key_vmsd_name, val_vmsd->name);
    return true;

val_error:
    g_free(val);

key_error:
    if (!direct_key) {
        g_free(key);
    }
    return false;
}


const VMStateInfo vmstate_info_gtree = {
    .name = "gtree",
    .load = load_gtree,
    .save = save_gtree,
};

static bool save_qlist(QEMUFile *f, void *pv, size_t unused_size,
                       const VMStateField *field, JSONWriter *vmdesc,
                       Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* offset of the QTAILQ entry in a QTAILQ element*/
    size_t entry_offset = field->start;
    void *elm;

    trace_save_qlist(field->name, vmsd->name, vmsd->version_id);
    QLIST_RAW_FOREACH(elm, pv, entry_offset) {
        qemu_put_byte(f, true);
        if (!vmstate_save_vmsd(f, vmsd, elm, vmdesc, errp)) {
            return false;
        }
    }
    qemu_put_byte(f, false);
    trace_save_qlist_end(field->name, vmsd->name);

    return true;
}

static bool load_qlist(QEMUFile *f, void *pv, size_t unused_size,
                       const VMStateField *field, Error **errp)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* size of a QLIST element */
    size_t size = field->size;
    /* offset of the QLIST entry in a QLIST element */
    size_t entry_offset = field->start;
    int version_id = field->version_id;
    void *elm, *prev = NULL;

    trace_load_qlist(field->name, vmsd->name, vmsd->version_id);
    if (version_id > vmsd->version_id) {
        error_setg(errp, "%s %s",  vmsd->name, "too new");
        return false;
    }
    if (version_id < vmsd->minimum_version_id) {
        error_setg(errp, "%s %s",  vmsd->name, "too old");
        return false;
    }

    while (qemu_get_byte(f)) {
        elm = g_malloc(size);
        if (!vmstate_load_vmsd(f, vmsd, elm, version_id, errp)) {
            g_free(elm);
            return false;
        }
        if (!prev) {
            QLIST_RAW_INSERT_HEAD(pv, elm, entry_offset);
        } else {
            QLIST_RAW_INSERT_AFTER(pv, prev, elm, entry_offset);
        }
        prev = elm;
    }
    trace_load_qlist_end(field->name, vmsd->name);

    return true;
}

const VMStateInfo vmstate_info_qlist = {
    .name = "qlist",
    .load = load_qlist,
    .save = save_qlist,
};

static int get_g_byte_array(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field)
{
    GByteArray *byte_array = *(GByteArray **)pv;
    uint32_t len = qemu_get_be32(f);

    g_byte_array_set_size(byte_array, len);
    qemu_get_buffer(f, byte_array->data, len);
    return 0;
}

static int put_g_byte_array(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field, JSONWriter *vmdesc)
{
    GByteArray *byte_array = *(GByteArray **)pv;

    qemu_put_be32(f, byte_array->len);
    qemu_put_buffer(f, byte_array->data, byte_array->len);

    return 0;
}

const VMStateInfo vmstate_info_g_byte_array = {
    .name = "GByteArray",
    .get  = get_g_byte_array,
    .put  = put_g_byte_array,
};
