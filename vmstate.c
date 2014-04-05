#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "trace.h"

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque);
static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque);

static int vmstate_n_elems(void *opaque, VMStateField *field)
{
    int n_elems = 1;

    if (field->flags & VMS_ARRAY) {
        n_elems = field->num;
    } else if (field->flags & VMS_VARRAY_INT32) {
        n_elems = *(int32_t *)(opaque+field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT32) {
        n_elems = *(uint32_t *)(opaque+field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT16) {
        n_elems = *(uint16_t *)(opaque+field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT8) {
        n_elems = *(uint8_t *)(opaque+field->num_offset);
    }

    return n_elems;
}

static int vmstate_size(void *opaque, VMStateField *field)
{
    int size = field->size;

    if (field->flags & VMS_VBUFFER) {
        size = *(int32_t *)(opaque+field->size_offset);
        if (field->flags & VMS_MULTIPLY) {
            size *= field->size;
        }
    }

    return size;
}

static void *vmstate_base_addr(void *opaque, VMStateField *field)
{
    void *base_addr = opaque + field->offset;

    if (field->flags & VMS_POINTER) {
        base_addr = *(void **)base_addr + field->start;
    }

    return base_addr;
}

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id)
{
    VMStateField *field = vmsd->fields;
    int ret;

    if (version_id > vmsd->version_id) {
        return -EINVAL;
    }
    if  (version_id < vmsd->minimum_version_id) {
        if (vmsd->load_state_old &&
            version_id >= vmsd->minimum_version_id_old) {
            return vmsd->load_state_old(f, opaque, version_id);
        }
        return -EINVAL;
    }
    if (vmsd->pre_load) {
        int ret = vmsd->pre_load(opaque);
        if (ret) {
            return ret;
        }
    }
    while (field->name) {
        if ((field->field_exists &&
             field->field_exists(opaque, version_id)) ||
            (!field->field_exists &&
             field->version_id <= version_id)) {
            void *base_addr = vmstate_base_addr(opaque, field);
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);

            for (i = 0; i < n_elems; i++) {
                void *addr = base_addr + size * i;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    addr = *(void **)addr;
                }
                if (field->flags & VMS_STRUCT) {
                    ret = vmstate_load_state(f, field->vmsd, addr,
                                             field->vmsd->version_id);
                } else {
                    ret = field->info->get(f, addr, size);

                }
                if (ret >= 0) {
                    ret = qemu_file_get_error(f);
                }
                if (ret < 0) {
                    qemu_file_set_error(f, ret);
                    trace_vmstate_load_field_error(field->name, ret);
                    return ret;
                }
            }
        } else if (field->flags & VMS_MUST_EXIST) {
            fprintf(stderr, "Input validation failed: %s/%s\n",
                    vmsd->name, field->name);
            return -1;
        }
        field++;
    }
    ret = vmstate_subsection_load(f, vmsd, opaque);
    if (ret != 0) {
        return ret;
    }
    if (vmsd->post_load) {
        return vmsd->post_load(opaque, version_id);
    }
    return 0;
}

void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque)
{
    VMStateField *field = vmsd->fields;

    if (vmsd->pre_save) {
        vmsd->pre_save(opaque);
    }
    while (field->name) {
        if (!field->field_exists ||
            field->field_exists(opaque, vmsd->version_id)) {
            void *base_addr = vmstate_base_addr(opaque, field);
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);

            for (i = 0; i < n_elems; i++) {
                void *addr = base_addr + size * i;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    addr = *(void **)addr;
                }
                if (field->flags & VMS_STRUCT) {
                    vmstate_save_state(f, field->vmsd, addr);
                } else {
                    field->info->put(f, addr, size);
                }
            }
        } else {
            if (field->flags & VMS_MUST_EXIST) {
                fprintf(stderr, "Output state validation failed: %s/%s\n",
                        vmsd->name, field->name);
                assert(!(field->flags & VMS_MUST_EXIST));
            }
        }
        field++;
    }
    vmstate_subsection_save(f, vmsd, opaque);
}

static const VMStateDescription *
    vmstate_get_subsection(const VMStateSubsection *sub, char *idstr)
{
    while (sub && sub->needed) {
        if (strcmp(idstr, sub->vmsd->name) == 0) {
            return sub->vmsd;
        }
        sub++;
    }
    return NULL;
}

static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque)
{
    while (qemu_peek_byte(f, 0) == QEMU_VM_SUBSECTION) {
        char idstr[256];
        int ret;
        uint8_t version_id, len, size;
        const VMStateDescription *sub_vmsd;

        len = qemu_peek_byte(f, 1);
        if (len < strlen(vmsd->name) + 1) {
            /* subsection name has be be "section_name/a" */
            return 0;
        }
        size = qemu_peek_buffer(f, (uint8_t *)idstr, len, 2);
        if (size != len) {
            return 0;
        }
        idstr[size] = 0;

        if (strncmp(vmsd->name, idstr, strlen(vmsd->name)) != 0) {
            /* it don't have a valid subsection name */
            return 0;
        }
        sub_vmsd = vmstate_get_subsection(vmsd->subsections, idstr);
        if (sub_vmsd == NULL) {
            return -ENOENT;
        }
        qemu_file_skip(f, 1); /* subsection */
        qemu_file_skip(f, 1); /* len */
        qemu_file_skip(f, len); /* idstr */
        version_id = qemu_get_be32(f);

        ret = vmstate_load_state(f, sub_vmsd, opaque, version_id);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque)
{
    const VMStateSubsection *sub = vmsd->subsections;

    while (sub && sub->needed) {
        if (sub->needed(opaque)) {
            const VMStateDescription *vmsd = sub->vmsd;
            uint8_t len;

            qemu_put_byte(f, QEMU_VM_SUBSECTION);
            len = strlen(vmsd->name);
            qemu_put_byte(f, len);
            qemu_put_buffer(f, (uint8_t *)vmsd->name, len);
            qemu_put_be32(f, vmsd->version_id);
            vmstate_save_state(f, vmsd, opaque);
        }
        sub++;
    }
}

/* bool */

static int get_bool(QEMUFile *f, void *pv, size_t size)
{
    bool *v = pv;
    *v = qemu_get_byte(f);
    return 0;
}

static void put_bool(QEMUFile *f, void *pv, size_t size)
{
    bool *v = pv;
    qemu_put_byte(f, *v);
}

const VMStateInfo vmstate_info_bool = {
    .name = "bool",
    .get  = get_bool,
    .put  = put_bool,
};

/* 8 bit int */

static int get_int8(QEMUFile *f, void *pv, size_t size)
{
    int8_t *v = pv;
    qemu_get_s8s(f, v);
    return 0;
}

static void put_int8(QEMUFile *f, void *pv, size_t size)
{
    int8_t *v = pv;
    qemu_put_s8s(f, v);
}

const VMStateInfo vmstate_info_int8 = {
    .name = "int8",
    .get  = get_int8,
    .put  = put_int8,
};

/* 16 bit int */

static int get_int16(QEMUFile *f, void *pv, size_t size)
{
    int16_t *v = pv;
    qemu_get_sbe16s(f, v);
    return 0;
}

static void put_int16(QEMUFile *f, void *pv, size_t size)
{
    int16_t *v = pv;
    qemu_put_sbe16s(f, v);
}

const VMStateInfo vmstate_info_int16 = {
    .name = "int16",
    .get  = get_int16,
    .put  = put_int16,
};

/* 32 bit int */

static int get_int32(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    qemu_get_sbe32s(f, v);
    return 0;
}

static void put_int32(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    qemu_put_sbe32s(f, v);
}

const VMStateInfo vmstate_info_int32 = {
    .name = "int32",
    .get  = get_int32,
    .put  = put_int32,
};

/* 32 bit int. See that the received value is the same than the one
   in the field */

static int get_int32_equal(QEMUFile *f, void *pv, size_t size)
{
    int32_t *v = pv;
    int32_t v2;
    qemu_get_sbe32s(f, &v2);

    if (*v == v2) {
        return 0;
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

static int get_int32_le(QEMUFile *f, void *pv, size_t size)
{
    int32_t *cur = pv;
    int32_t loaded;
    qemu_get_sbe32s(f, &loaded);

    if (loaded >= 0 && loaded <= *cur) {
        *cur = loaded;
        return 0;
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_int32_le = {
    .name = "int32 le",
    .get  = get_int32_le,
    .put  = put_int32,
};

/* 64 bit int */

static int get_int64(QEMUFile *f, void *pv, size_t size)
{
    int64_t *v = pv;
    qemu_get_sbe64s(f, v);
    return 0;
}

static void put_int64(QEMUFile *f, void *pv, size_t size)
{
    int64_t *v = pv;
    qemu_put_sbe64s(f, v);
}

const VMStateInfo vmstate_info_int64 = {
    .name = "int64",
    .get  = get_int64,
    .put  = put_int64,
};

/* 8 bit unsigned int */

static int get_uint8(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_get_8s(f, v);
    return 0;
}

static void put_uint8(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_put_8s(f, v);
}

const VMStateInfo vmstate_info_uint8 = {
    .name = "uint8",
    .get  = get_uint8,
    .put  = put_uint8,
};

/* 16 bit unsigned int */

static int get_uint16(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    qemu_get_be16s(f, v);
    return 0;
}

static void put_uint16(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    qemu_put_be16s(f, v);
}

const VMStateInfo vmstate_info_uint16 = {
    .name = "uint16",
    .get  = get_uint16,
    .put  = put_uint16,
};

/* 32 bit unsigned int */

static int get_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    qemu_get_be32s(f, v);
    return 0;
}

static void put_uint32(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    qemu_put_be32s(f, v);
}

const VMStateInfo vmstate_info_uint32 = {
    .name = "uint32",
    .get  = get_uint32,
    .put  = put_uint32,
};

/* 32 bit uint. See that the received value is the same than the one
   in the field */

static int get_uint32_equal(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    uint32_t v2;
    qemu_get_be32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint32_equal = {
    .name = "uint32 equal",
    .get  = get_uint32_equal,
    .put  = put_uint32,
};

/* 64 bit unsigned int */

static int get_uint64(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    qemu_get_be64s(f, v);
    return 0;
}

static void put_uint64(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    qemu_put_be64s(f, v);
}

const VMStateInfo vmstate_info_uint64 = {
    .name = "uint64",
    .get  = get_uint64,
    .put  = put_uint64,
};

/* 64 bit unsigned int. See that the received value is the same than the one
   in the field */

static int get_uint64_equal(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;
    uint64_t v2;
    qemu_get_be64s(f, &v2);

    if (*v == v2) {
        return 0;
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

static int get_uint8_equal(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    uint8_t v2;
    qemu_get_8s(f, &v2);

    if (*v == v2) {
        return 0;
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

static int get_uint16_equal(QEMUFile *f, void *pv, size_t size)
{
    uint16_t *v = pv;
    uint16_t v2;
    qemu_get_be16s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint16_equal = {
    .name = "uint16 equal",
    .get  = get_uint16_equal,
    .put  = put_uint16,
};

/* floating point */

static int get_float64(QEMUFile *f, void *pv, size_t size)
{
    float64 *v = pv;

    *v = make_float64(qemu_get_be64(f));
    return 0;
}

static void put_float64(QEMUFile *f, void *pv, size_t size)
{
    uint64_t *v = pv;

    qemu_put_be64(f, float64_val(*v));
}

const VMStateInfo vmstate_info_float64 = {
    .name = "float64",
    .get  = get_float64,
    .put  = put_float64,
};

/* uint8_t buffers */

static int get_buffer(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_get_buffer(f, v, size);
    return 0;
}

static void put_buffer(QEMUFile *f, void *pv, size_t size)
{
    uint8_t *v = pv;
    qemu_put_buffer(f, v, size);
}

const VMStateInfo vmstate_info_buffer = {
    .name = "buffer",
    .get  = get_buffer,
    .put  = put_buffer,
};

/* unused buffers: space that was used for some fields that are
   not useful anymore */

static int get_unused_buffer(QEMUFile *f, void *pv, size_t size)
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

static void put_unused_buffer(QEMUFile *f, void *pv, size_t size)
{
    static const uint8_t buf[1024];
    int block_len;

    while (size > 0) {
        block_len = MIN(sizeof(buf), size);
        size -= block_len;
        qemu_put_buffer(f, buf, block_len);
    }
}

const VMStateInfo vmstate_info_unused_buffer = {
    .name = "unused_buffer",
    .get  = get_unused_buffer,
    .put  = put_unused_buffer,
};

/* bitmaps (as defined by bitmap.h). Note that size here is the size
 * of the bitmap in bits. The on-the-wire format of a bitmap is 64
 * bit words with the bits in big endian order. The in-memory format
 * is an array of 'unsigned long', which may be either 32 or 64 bits.
 */
/* This is the number of 64 bit words sent over the wire */
#define BITS_TO_U64S(nr) DIV_ROUND_UP(nr, 64)
static int get_bitmap(QEMUFile *f, void *pv, size_t size)
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

static void put_bitmap(QEMUFile *f, void *pv, size_t size)
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
}

const VMStateInfo vmstate_info_bitmap = {
    .name = "bitmap",
    .get = get_bitmap,
    .put = put_bitmap,
};
