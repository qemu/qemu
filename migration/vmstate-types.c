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

static int get_bool(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field)
{
    bool *v = pv;
    *v = qemu_get_byte(f);
    return 0;
}

static int put_bool(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    bool *v = pv;
    qemu_put_byte(f, *v);
    return 0;
}

const VMStateInfo vmstate_info_bool = {
    .name = "bool",
    .get  = get_bool,
    .put  = put_bool,
};

/* 8 bit int */

static int get_int8(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field)
{
    int8_t *v = pv;
    qemu_get_s8s(f, v);
    return 0;
}

static int put_int8(QEMUFile *f, void *pv, size_t size,
                    const VMStateField *field, JSONWriter *vmdesc)
{
    int8_t *v = pv;
    qemu_put_s8s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_int8 = {
    .name = "int8",
    .get  = get_int8,
    .put  = put_int8,
};

/* 16 bit int */

static int get_int16(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    int16_t *v = pv;
    qemu_get_sbe16s(f, v);
    return 0;
}

static int put_int16(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    int16_t *v = pv;
    qemu_put_sbe16s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_int16 = {
    .name = "int16",
    .get  = get_int16,
    .put  = put_int16,
};

/* 32 bit int */

static int get_int32(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    int32_t *v = pv;
    qemu_get_sbe32s(f, v);
    return 0;
}

static int put_int32(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    int32_t *v = pv;
    qemu_put_sbe32s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_int32 = {
    .name = "int32",
    .get  = get_int32,
    .put  = put_int32,
};

/* 32 bit int. See that the received value is the same than the one
   in the field */

static int get_int32_equal(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field)
{
    int32_t *v = pv;
    int32_t v2;
    qemu_get_sbe32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx32 " != %" PRIx32, *v, v2);
    if (field->err_hint) {
        error_printf("%s\n", field->err_hint);
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_int32_equal = {
    .name = "int32 equal",
    .get  = get_int32_equal,
    .put  = put_int32,
};

/* 32 bit int. Check that the received value is non-negative
 * and less than or equal to the one in the field.
 */

static int get_int32_le(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field)
{
    int32_t *cur = pv;
    int32_t loaded;
    qemu_get_sbe32s(f, &loaded);

    if (loaded >= 0 && loaded <= *cur) {
        *cur = loaded;
        return 0;
    }
    error_report("Invalid value %" PRId32
                 " expecting positive value <= %" PRId32,
                 loaded, *cur);
    return -EINVAL;
}

const VMStateInfo vmstate_info_int32_le = {
    .name = "int32 le",
    .get  = get_int32_le,
    .put  = put_int32,
};

/* 64 bit int */

static int get_int64(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    int64_t *v = pv;
    qemu_get_sbe64s(f, v);
    return 0;
}

static int put_int64(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    int64_t *v = pv;
    qemu_put_sbe64s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_int64 = {
    .name = "int64",
    .get  = get_int64,
    .put  = put_int64,
};

/* 8 bit unsigned int */

static int get_uint8(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field)
{
    uint8_t *v = pv;
    qemu_get_8s(f, v);
    return 0;
}

static int put_uint8(QEMUFile *f, void *pv, size_t size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    uint8_t *v = pv;
    qemu_put_8s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_uint8 = {
    .name = "uint8",
    .get  = get_uint8,
    .put  = put_uint8,
};

/* 16 bit unsigned int */

static int get_uint16(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint16_t *v = pv;
    qemu_get_be16s(f, v);
    return 0;
}

static int put_uint16(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    uint16_t *v = pv;
    qemu_put_be16s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_uint16 = {
    .name = "uint16",
    .get  = get_uint16,
    .put  = put_uint16,
};

/* 32 bit unsigned int */

static int get_uint32(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint32_t *v = pv;
    qemu_get_be32s(f, v);
    return 0;
}

static int put_uint32(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    uint32_t *v = pv;
    qemu_put_be32s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_uint32 = {
    .name = "uint32",
    .get  = get_uint32,
    .put  = put_uint32,
};

/* 32 bit uint. See that the received value is the same than the one
   in the field */

static int get_uint32_equal(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field)
{
    uint32_t *v = pv;
    uint32_t v2;
    qemu_get_be32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx32 " != %" PRIx32, *v, v2);
    if (field->err_hint) {
        error_printf("%s\n", field->err_hint);
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint32_equal = {
    .name = "uint32 equal",
    .get  = get_uint32_equal,
    .put  = put_uint32,
};

/* 64 bit unsigned int */

static int get_uint64(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint64_t *v = pv;
    qemu_get_be64s(f, v);
    return 0;
}

static int put_uint64(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    uint64_t *v = pv;
    qemu_put_be64s(f, v);
    return 0;
}

const VMStateInfo vmstate_info_uint64 = {
    .name = "uint64",
    .get  = get_uint64,
    .put  = put_uint64,
};

/* File descriptor communicated via SCM_RIGHTS */

static int get_fd(QEMUFile *f, void *pv, size_t size,
                  const VMStateField *field)
{
    int32_t *v = pv;
    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        qemu_get_sbe32s(f, v);
        return 0;
    }
    *v = qemu_file_get_fd(f);
    return 0;
}

static int put_fd(QEMUFile *f, void *pv, size_t size,
                  const VMStateField *field, JSONWriter *vmdesc)
{
    int32_t *v = pv;
    if (migrate_mode() == MIG_MODE_CPR_EXEC) {
        qemu_put_sbe32s(f, v);
        return 0;
    }
    return qemu_file_put_fd(f, *v);
}

const VMStateInfo vmstate_info_fd = {
    .name = "fd",
    .get  = get_fd,
    .put  = put_fd,
};

static int get_nullptr(QEMUFile *f, void *pv, size_t size,
                       const VMStateField *field)

{
    if (qemu_get_byte(f) == VMS_NULLPTR_MARKER) {
        return  0;
    }
    error_report("vmstate: get_nullptr expected VMS_NULLPTR_MARKER");
    return -EINVAL;
}

static int put_nullptr(QEMUFile *f, void *pv, size_t size,
                        const VMStateField *field, JSONWriter *vmdesc)

{
    if (pv == NULL) {
        qemu_put_byte(f, VMS_NULLPTR_MARKER);
        return 0;
    }
    error_report("vmstate: put_nullptr must be called with pv == NULL");
    return -EINVAL;
}

const VMStateInfo vmstate_info_nullptr = {
    .name = "nullptr",
    .get  = get_nullptr,
    .put  = put_nullptr,
};

/* 64 bit unsigned int. See that the received value is the same than the one
   in the field */

static int get_uint64_equal(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field)
{
    uint64_t *v = pv;
    uint64_t v2;
    qemu_get_be64s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx64 " != %" PRIx64, *v, v2);
    if (field->err_hint) {
        error_printf("%s\n", field->err_hint);
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint64_equal = {
    .name = "int64 equal",
    .get  = get_uint64_equal,
    .put  = put_uint64,
};

/* 8 bit int. See that the received value is the same than the one
   in the field */

static int get_uint8_equal(QEMUFile *f, void *pv, size_t size,
                           const VMStateField *field)
{
    uint8_t *v = pv;
    uint8_t v2;
    qemu_get_8s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%x != %x", *v, v2);
    if (field->err_hint) {
        error_printf("%s\n", field->err_hint);
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint8_equal = {
    .name = "uint8 equal",
    .get  = get_uint8_equal,
    .put  = put_uint8,
};

/* 16 bit unsigned int int. See that the received value is the same than the one
   in the field */

static int get_uint16_equal(QEMUFile *f, void *pv, size_t size,
                            const VMStateField *field)
{
    uint16_t *v = pv;
    uint16_t v2;
    qemu_get_be16s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%x != %x", *v, v2);
    if (field->err_hint) {
        error_printf("%s\n", field->err_hint);
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint16_equal = {
    .name = "uint16 equal",
    .get  = get_uint16_equal,
    .put  = put_uint16,
};

/* CPU_DoubleU type */

static int get_cpudouble(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field)
{
    CPU_DoubleU *v = pv;
    qemu_get_be32s(f, &v->l.upper);
    qemu_get_be32s(f, &v->l.lower);
    return 0;
}

static int put_cpudouble(QEMUFile *f, void *pv, size_t size,
                         const VMStateField *field, JSONWriter *vmdesc)
{
    CPU_DoubleU *v = pv;
    qemu_put_be32s(f, &v->l.upper);
    qemu_put_be32s(f, &v->l.lower);
    return 0;
}

const VMStateInfo vmstate_info_cpudouble = {
    .name = "CPU_Double_U",
    .get  = get_cpudouble,
    .put  = put_cpudouble,
};

/* uint8_t buffers */

static int get_buffer(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint8_t *v = pv;
    qemu_get_buffer(f, v, size);
    return 0;
}

static int put_buffer(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    uint8_t *v = pv;
    qemu_put_buffer(f, v, size);
    return 0;
}

const VMStateInfo vmstate_info_buffer = {
    .name = "buffer",
    .get  = get_buffer,
    .put  = put_buffer,
};

/* unused buffers: space that was used for some fields that are
   not useful anymore */

static int get_unused_buffer(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field)
{
    uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_get_buffer(f, buf, block_len);
    }
   return 0;
}

static int put_unused_buffer(QEMUFile *f, void *pv, size_t size,
                             const VMStateField *field, JSONWriter *vmdesc)
{
    static const uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_put_buffer(f, buf, block_len);
    }

    return 0;
}

const VMStateInfo vmstate_info_unused_buffer = {
    .name = "unused_buffer",
    .get  = get_unused_buffer,
    .put  = put_unused_buffer,
};

/* vmstate_info_tmp, see VMSTATE_WITH_TMP, the idea is that we allocate
 * a temporary buffer and the pre_load/pre_save methods in the child vmsd
 * copy stuff from the parent into the child and do calculations to fill
 * in fields that don't really exist in the parent but need to be in the
 * stream.
 */
static int get_tmp(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field)
{
    int ret;
    Error *local_err = NULL;
    const VMStateDescription *vmsd = field->vmsd;
    int version_id = field->version_id;
    void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    ret = vmstate_load_state(f, vmsd, tmp, version_id, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
    }
    g_free(tmp);
    return ret;
}

static int put_tmp(QEMUFile *f, void *pv, size_t size,
                   const VMStateField *field, JSONWriter *vmdesc)
{
    const VMStateDescription *vmsd = field->vmsd;
    void *tmp = g_malloc(size);
    int ret;
    Error *local_err = NULL;

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    ret = vmstate_save_state(f, vmsd, tmp, vmdesc, &local_err);
    if (ret) {
        error_report_err(local_err);
    }
    g_free(tmp);

    return ret;
}

const VMStateInfo vmstate_info_tmp = {
    .name = "tmp",
    .get = get_tmp,
    .put = put_tmp,
};

/* bitmaps (as defined by bitmap.h). Note that size here is the size
 * of the bitmap in bits. The on-the-wire format of a bitmap is 64
 * bit words with the bits in big endian order. The in-memory format
 * is an array of 'unsigned long', which may be either 32 or 64 bits.
 */
/* This is the number of 64 bit words sent over the wire */
#define BITS_TO_U64S(nr) DIV_ROUND_UP(nr, 64)
static int get_bitmap(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field)
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
    return 0;
}

static int put_bitmap(QEMUFile *f, void *pv, size_t size,
                      const VMStateField *field, JSONWriter *vmdesc)
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

    return 0;
}

const VMStateInfo vmstate_info_bitmap = {
    .name = "bitmap",
    .get = get_bitmap,
    .put = put_bitmap,
};

/* get for QTAILQ
 * meta data about the QTAILQ is encoded in a VMStateField structure
 */
static int get_qtailq(QEMUFile *f, void *pv, size_t unused_size,
                      const VMStateField *field)
{
    int ret = 0;
    Error *local_err = NULL;
    const VMStateDescription *vmsd = field->vmsd;
    /* size of a QTAILQ element */
    size_t size = field->size;
    /* offset of the QTAILQ entry in a QTAILQ element */
    size_t entry_offset = field->start;
    int version_id = field->version_id;
    void *elm;

    trace_get_qtailq(vmsd->name, version_id);
    if (version_id > vmsd->version_id) {
        error_report("%s %s",  vmsd->name, "too new");
        trace_get_qtailq_end(vmsd->name, "too new", -EINVAL);

        return -EINVAL;
    }
    if (version_id < vmsd->minimum_version_id) {
        error_report("%s %s",  vmsd->name, "too old");
        trace_get_qtailq_end(vmsd->name, "too old", -EINVAL);
        return -EINVAL;
    }

    while (qemu_get_byte(f)) {
        elm = g_malloc(size);
        ret = vmstate_load_state(f, vmsd, elm, version_id, &local_err);
        if (ret) {
            error_report_err(local_err);
            return ret;
        }
        QTAILQ_RAW_INSERT_TAIL(pv, elm, entry_offset);
    }

    trace_get_qtailq_end(vmsd->name, "end", ret);
    return ret;
}

/* put for QTAILQ */
static int put_qtailq(QEMUFile *f, void *pv, size_t unused_size,
                      const VMStateField *field, JSONWriter *vmdesc)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* offset of the QTAILQ entry in a QTAILQ element*/
    size_t entry_offset = field->start;
    void *elm;
    int ret;
    Error *local_err = NULL;

    trace_put_qtailq(vmsd->name, vmsd->version_id);

    QTAILQ_RAW_FOREACH(elm, pv, entry_offset) {
        qemu_put_byte(f, true);
        ret = vmstate_save_state(f, vmsd, elm, vmdesc, &local_err);
        if (ret) {
            error_report_err(local_err);
            return ret;
        }
    }
    qemu_put_byte(f, false);

    trace_put_qtailq_end(vmsd->name, "end");

    return 0;
}
const VMStateInfo vmstate_info_qtailq = {
    .name = "qtailq",
    .get  = get_qtailq,
    .put  = put_qtailq,
};

struct put_gtree_data {
    QEMUFile *f;
    const VMStateDescription *key_vmsd;
    const VMStateDescription *val_vmsd;
    JSONWriter *vmdesc;
    int ret;
};

static gboolean put_gtree_elem(gpointer key, gpointer value, gpointer data)
{
    struct put_gtree_data *capsule = (struct put_gtree_data *)data;
    QEMUFile *f = capsule->f;
    int ret;
    Error *local_err = NULL;

    qemu_put_byte(f, true);

    /* put the key */
    if (!capsule->key_vmsd) {
        qemu_put_be64(f, (uint64_t)(uintptr_t)(key)); /* direct key */
    } else {
        ret = vmstate_save_state(f, capsule->key_vmsd, key, capsule->vmdesc,
                                 &local_err);
        if (ret) {
            error_report_err(local_err);
            capsule->ret = ret;
            return true;
        }
    }

    /* put the data */
    ret = vmstate_save_state(f, capsule->val_vmsd, value, capsule->vmdesc,
                             &local_err);
    if (ret) {
        error_report_err(local_err);
        capsule->ret = ret;
        return true;
    }
    return false;
}

static int put_gtree(QEMUFile *f, void *pv, size_t unused_size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    bool direct_key = (!field->start);
    const VMStateDescription *key_vmsd = direct_key ? NULL : &field->vmsd[1];
    const VMStateDescription *val_vmsd = &field->vmsd[0];
    const char *key_vmsd_name = direct_key ? "direct" : key_vmsd->name;
    struct put_gtree_data capsule = {
        .f = f,
        .key_vmsd = key_vmsd,
        .val_vmsd = val_vmsd,
        .vmdesc = vmdesc,
        .ret = 0};
    GTree **pval = pv;
    GTree *tree = *pval;
    uint32_t nnodes = g_tree_nnodes(tree);
    int ret;

    trace_put_gtree(field->name, key_vmsd_name, val_vmsd->name, nnodes);
    qemu_put_be32(f, nnodes);
    g_tree_foreach(tree, put_gtree_elem, (gpointer)&capsule);
    qemu_put_byte(f, false);
    ret = capsule.ret;
    if (ret) {
        error_report("%s : failed to save gtree (%d)", field->name, ret);
    }
    trace_put_gtree_end(field->name, key_vmsd_name, val_vmsd->name, ret);
    return ret;
}

static int get_gtree(QEMUFile *f, void *pv, size_t unused_size,
                     const VMStateField *field)
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
    int ret = 0;
    Error *local_err = NULL;

    /* in case of direct key, the key vmsd can be {}, ie. check fields */
    if (!direct_key && version_id > key_vmsd->version_id) {
        error_report("%s %s",  key_vmsd->name, "too new");
        return -EINVAL;
    }
    if (!direct_key && version_id < key_vmsd->minimum_version_id) {
        error_report("%s %s",  key_vmsd->name, "too old");
        return -EINVAL;
    }
    if (version_id > val_vmsd->version_id) {
        error_report("%s %s",  val_vmsd->name, "too new");
        return -EINVAL;
    }
    if (version_id < val_vmsd->minimum_version_id) {
        error_report("%s %s",  val_vmsd->name, "too old");
        return -EINVAL;
    }

    nnodes = qemu_get_be32(f);
    trace_get_gtree(field->name, key_vmsd_name, val_vmsd->name, nnodes);

    while (qemu_get_byte(f)) {
        if ((++count) > nnodes) {
            ret = -EINVAL;
            break;
        }
        if (direct_key) {
            key = (void *)(uintptr_t)qemu_get_be64(f);
        } else {
            key = g_malloc0(key_size);
            ret = vmstate_load_state(f, key_vmsd, key, version_id, &local_err);
            if (ret) {
                error_report_err(local_err);
                goto key_error;
            }
        }
        val = g_malloc0(val_size);
        ret = vmstate_load_state(f, val_vmsd, val, version_id, &local_err);
        if (ret) {
            error_report_err(local_err);
            goto val_error;
        }
        g_tree_insert(tree, key, val);
    }
    if (count != nnodes) {
        error_report("%s inconsistent stream when loading the gtree",
                     field->name);
        return -EINVAL;
    }
    trace_get_gtree_end(field->name, key_vmsd_name, val_vmsd->name, ret);
    return ret;
val_error:
    g_free(val);
key_error:
    if (!direct_key) {
        g_free(key);
    }
    trace_get_gtree_end(field->name, key_vmsd_name, val_vmsd->name, ret);
    return ret;
}


const VMStateInfo vmstate_info_gtree = {
    .name = "gtree",
    .get  = get_gtree,
    .put  = put_gtree,
};

static int put_qlist(QEMUFile *f, void *pv, size_t unused_size,
                     const VMStateField *field, JSONWriter *vmdesc)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* offset of the QTAILQ entry in a QTAILQ element*/
    size_t entry_offset = field->start;
    void *elm;
    int ret;
    Error *local_err = NULL;

    trace_put_qlist(field->name, vmsd->name, vmsd->version_id);
    QLIST_RAW_FOREACH(elm, pv, entry_offset) {
        qemu_put_byte(f, true);
        ret = vmstate_save_state(f, vmsd, elm, vmdesc, &local_err);
        if (ret) {
            error_report_err(local_err);
            return ret;
        }
    }
    qemu_put_byte(f, false);
    trace_put_qlist_end(field->name, vmsd->name);

    return 0;
}

static int get_qlist(QEMUFile *f, void *pv, size_t unused_size,
                     const VMStateField *field)
{
    int ret = 0;
    Error *local_err = NULL;
    const VMStateDescription *vmsd = field->vmsd;
    /* size of a QLIST element */
    size_t size = field->size;
    /* offset of the QLIST entry in a QLIST element */
    size_t entry_offset = field->start;
    int version_id = field->version_id;
    void *elm, *prev = NULL;

    trace_get_qlist(field->name, vmsd->name, vmsd->version_id);
    if (version_id > vmsd->version_id) {
        error_report("%s %s",  vmsd->name, "too new");
        return -EINVAL;
    }
    if (version_id < vmsd->minimum_version_id) {
        error_report("%s %s",  vmsd->name, "too old");
        return -EINVAL;
    }

    while (qemu_get_byte(f)) {
        elm = g_malloc(size);
        ret = vmstate_load_state(f, vmsd, elm, version_id, &local_err);
        if (ret) {
            error_report_err(local_err);
            g_free(elm);
            return ret;
        }
        if (!prev) {
            QLIST_RAW_INSERT_HEAD(pv, elm, entry_offset);
        } else {
            QLIST_RAW_INSERT_AFTER(pv, prev, elm, entry_offset);
        }
        prev = elm;
    }
    trace_get_qlist_end(field->name, vmsd->name);

    return ret;
}

const VMStateInfo vmstate_info_qlist = {
    .name = "qlist",
    .get  = get_qlist,
    .put  = put_qlist,
};
