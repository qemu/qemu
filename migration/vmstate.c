#include "qemu/osdep.h"
#include "qemu-common.h"
#include "migration/migration.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "trace.h"
#include "migration/qjson.h"

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, QJSON *vmdesc);
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

    if (field->flags & VMS_MULTIPLY_ELEMENTS) {
        n_elems *= field->num;
    }

    trace_vmstate_n_elems(field->name, n_elems);
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

static void vmstate_handle_alloc(void *ptr, VMStateField *field, void *opaque)
{
    if (field->flags & VMS_POINTER && field->flags & VMS_ALLOC) {
        gsize size = vmstate_size(opaque, field);
        size *= vmstate_n_elems(opaque, field);
        if (size) {
            *(void **)ptr = g_malloc(size);
        }
    }
}

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id)
{
    VMStateField *field = vmsd->fields;
    int ret = 0;

    trace_vmstate_load_state(vmsd->name, version_id);
    if (version_id > vmsd->version_id) {
        error_report("%s: incoming version_id %d is too new "
                     "for local version_id %d",
                     vmsd->name, version_id, vmsd->version_id);
        trace_vmstate_load_state_end(vmsd->name, "too new", -EINVAL);
        return -EINVAL;
    }
    if  (version_id < vmsd->minimum_version_id) {
        if (vmsd->load_state_old &&
            version_id >= vmsd->minimum_version_id_old) {
            ret = vmsd->load_state_old(f, opaque, version_id);
            trace_vmstate_load_state_end(vmsd->name, "old path", ret);
            return ret;
        }
        error_report("%s: incoming version_id %d is too old "
                     "for local minimum version_id  %d",
                     vmsd->name, version_id, vmsd->minimum_version_id);
        trace_vmstate_load_state_end(vmsd->name, "too old", -EINVAL);
        return -EINVAL;
    }
    if (vmsd->pre_load) {
        int ret = vmsd->pre_load(opaque);
        if (ret) {
            return ret;
        }
    }
    while (field->name) {
        trace_vmstate_load_state_field(vmsd->name, field->name);
        if ((field->field_exists &&
             field->field_exists(opaque, version_id)) ||
            (!field->field_exists &&
             field->version_id <= version_id)) {
            void *first_elem = opaque + field->offset;
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);

            vmstate_handle_alloc(first_elem, field, opaque);
            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }
            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    curr_elem = *(void **)curr_elem;
                }
                if (!curr_elem && size) {
                    /* if null pointer check placeholder and do not follow */
                    assert(field->flags & VMS_ARRAY_OF_POINTER);
                    ret = vmstate_info_nullptr.get(f, curr_elem, size, NULL);
                } else if (field->flags & VMS_STRUCT) {
                    ret = vmstate_load_state(f, field->vmsd, curr_elem,
                                             field->vmsd->version_id);
                } else {
                    ret = field->info->get(f, curr_elem, size, field);
                }
                if (ret >= 0) {
                    ret = qemu_file_get_error(f);
                }
                if (ret < 0) {
                    qemu_file_set_error(f, ret);
                    error_report("Failed to load %s:%s", vmsd->name,
                                 field->name);
                    trace_vmstate_load_field_error(field->name, ret);
                    return ret;
                }
            }
        } else if (field->flags & VMS_MUST_EXIST) {
            error_report("Input validation failed: %s/%s",
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
        ret = vmsd->post_load(opaque, version_id);
    }
    trace_vmstate_load_state_end(vmsd->name, "end", ret);
    return ret;
}

static int vmfield_name_num(VMStateField *start, VMStateField *search)
{
    VMStateField *field;
    int found = 0;

    for (field = start; field->name; field++) {
        if (!strcmp(field->name, search->name)) {
            if (field == search) {
                return found;
            }
            found++;
        }
    }

    return -1;
}

static bool vmfield_name_is_unique(VMStateField *start, VMStateField *search)
{
    VMStateField *field;
    int found = 0;

    for (field = start; field->name; field++) {
        if (!strcmp(field->name, search->name)) {
            found++;
            /* name found more than once, so it's not unique */
            if (found > 1) {
                return false;
            }
        }
    }

    return true;
}

static const char *vmfield_get_type_name(VMStateField *field)
{
    const char *type = "unknown";

    if (field->flags & VMS_STRUCT) {
        type = "struct";
    } else if (field->info->name) {
        type = field->info->name;
    }

    return type;
}

static bool vmsd_can_compress(VMStateField *field)
{
    if (field->field_exists) {
        /* Dynamically existing fields mess up compression */
        return false;
    }

    if (field->flags & VMS_STRUCT) {
        VMStateField *sfield = field->vmsd->fields;
        while (sfield->name) {
            if (!vmsd_can_compress(sfield)) {
                /* Child elements can't compress, so can't we */
                return false;
            }
            sfield++;
        }

        if (field->vmsd->subsections) {
            /* Subsections may come and go, better don't compress */
            return false;
        }
    }

    return true;
}

static void vmsd_desc_field_start(const VMStateDescription *vmsd, QJSON *vmdesc,
                                  VMStateField *field, int i, int max)
{
    char *name, *old_name;
    bool is_array = max > 1;
    bool can_compress = vmsd_can_compress(field);

    if (!vmdesc) {
        return;
    }

    name = g_strdup(field->name);

    /* Field name is not unique, need to make it unique */
    if (!vmfield_name_is_unique(vmsd->fields, field)) {
        int num = vmfield_name_num(vmsd->fields, field);
        old_name = name;
        name = g_strdup_printf("%s[%d]", name, num);
        g_free(old_name);
    }

    json_start_object(vmdesc, NULL);
    json_prop_str(vmdesc, "name", name);
    if (is_array) {
        if (can_compress) {
            json_prop_int(vmdesc, "array_len", max);
        } else {
            json_prop_int(vmdesc, "index", i);
        }
    }
    json_prop_str(vmdesc, "type", vmfield_get_type_name(field));

    if (field->flags & VMS_STRUCT) {
        json_start_object(vmdesc, "struct");
    }

    g_free(name);
}

static void vmsd_desc_field_end(const VMStateDescription *vmsd, QJSON *vmdesc,
                                VMStateField *field, size_t size, int i)
{
    if (!vmdesc) {
        return;
    }

    if (field->flags & VMS_STRUCT) {
        /* We printed a struct in between, close its child object */
        json_end_object(vmdesc);
    }

    json_prop_int(vmdesc, "size", size);
    json_end_object(vmdesc);
}


bool vmstate_save_needed(const VMStateDescription *vmsd, void *opaque)
{
    if (vmsd->needed && !vmsd->needed(opaque)) {
        /* optional section not needed */
        return false;
    }
    return true;
}


void vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque, QJSON *vmdesc)
{
    VMStateField *field = vmsd->fields;

    trace_vmstate_save_state_top(vmsd->name);

    if (vmsd->pre_save) {
        vmsd->pre_save(opaque);
    }

    if (vmdesc) {
        json_prop_str(vmdesc, "vmsd_name", vmsd->name);
        json_prop_int(vmdesc, "version", vmsd->version_id);
        json_start_array(vmdesc, "fields");
    }

    while (field->name) {
        if (!field->field_exists ||
            field->field_exists(opaque, vmsd->version_id)) {
            void *first_elem = opaque + field->offset;
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);
            int64_t old_offset, written_bytes;
            QJSON *vmdesc_loop = vmdesc;

            trace_vmstate_save_state_loop(vmsd->name, field->name, n_elems);
            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }
            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;

                vmsd_desc_field_start(vmsd, vmdesc_loop, field, i, n_elems);
                old_offset = qemu_ftell_fast(f);
                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    assert(curr_elem);
                    curr_elem = *(void **)curr_elem;
                }
                if (!curr_elem && size) {
                    /* if null pointer write placeholder and do not follow */
                    assert(field->flags & VMS_ARRAY_OF_POINTER);
                    vmstate_info_nullptr.put(f, curr_elem, size, NULL, NULL);
                } else if (field->flags & VMS_STRUCT) {
                    vmstate_save_state(f, field->vmsd, curr_elem, vmdesc_loop);
                } else {
                    field->info->put(f, curr_elem, size, field, vmdesc_loop);
                }

                written_bytes = qemu_ftell_fast(f) - old_offset;
                vmsd_desc_field_end(vmsd, vmdesc_loop, field, written_bytes, i);

                /* Compressed arrays only care about the first element */
                if (vmdesc_loop && vmsd_can_compress(field)) {
                    vmdesc_loop = NULL;
                }
            }
        } else {
            if (field->flags & VMS_MUST_EXIST) {
                error_report("Output state validation failed: %s/%s",
                        vmsd->name, field->name);
                assert(!(field->flags & VMS_MUST_EXIST));
            }
        }
        field++;
    }

    if (vmdesc) {
        json_end_array(vmdesc);
    }

    vmstate_subsection_save(f, vmsd, opaque, vmdesc);
}

static const VMStateDescription *
vmstate_get_subsection(const VMStateDescription **sub, char *idstr)
{
    while (sub && *sub && (*sub)->needed) {
        if (strcmp(idstr, (*sub)->name) == 0) {
            return *sub;
        }
        sub++;
    }
    return NULL;
}

static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque)
{
    trace_vmstate_subsection_load(vmsd->name);

    while (qemu_peek_byte(f, 0) == QEMU_VM_SUBSECTION) {
        char idstr[256], *idstr_ret;
        int ret;
        uint8_t version_id, len, size;
        const VMStateDescription *sub_vmsd;

        len = qemu_peek_byte(f, 1);
        if (len < strlen(vmsd->name) + 1) {
            /* subsection name has be be "section_name/a" */
            trace_vmstate_subsection_load_bad(vmsd->name, "(short)", "");
            return 0;
        }
        size = qemu_peek_buffer(f, (uint8_t **)&idstr_ret, len, 2);
        if (size != len) {
            trace_vmstate_subsection_load_bad(vmsd->name, "(peek fail)", "");
            return 0;
        }
        memcpy(idstr, idstr_ret, size);
        idstr[size] = 0;

        if (strncmp(vmsd->name, idstr, strlen(vmsd->name)) != 0) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(prefix)");
            /* it doesn't have a valid subsection name */
            return 0;
        }
        sub_vmsd = vmstate_get_subsection(vmsd->subsections, idstr);
        if (sub_vmsd == NULL) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(lookup)");
            return -ENOENT;
        }
        qemu_file_skip(f, 1); /* subsection */
        qemu_file_skip(f, 1); /* len */
        qemu_file_skip(f, len); /* idstr */
        version_id = qemu_get_be32(f);

        ret = vmstate_load_state(f, sub_vmsd, opaque, version_id);
        if (ret) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(child)");
            return ret;
        }
    }

    trace_vmstate_subsection_load_good(vmsd->name);
    return 0;
}

static void vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, QJSON *vmdesc)
{
    const VMStateDescription **sub = vmsd->subsections;
    bool subsection_found = false;

    trace_vmstate_subsection_save_top(vmsd->name);
    while (sub && *sub && (*sub)->needed) {
        if ((*sub)->needed(opaque)) {
            const VMStateDescription *vmsdsub = *sub;
            uint8_t len;

            trace_vmstate_subsection_save_loop(vmsd->name, vmsdsub->name);
            if (vmdesc) {
                /* Only create subsection array when we have any */
                if (!subsection_found) {
                    json_start_array(vmdesc, "subsections");
                    subsection_found = true;
                }

                json_start_object(vmdesc, NULL);
            }

            qemu_put_byte(f, QEMU_VM_SUBSECTION);
            len = strlen(vmsdsub->name);
            qemu_put_byte(f, len);
            qemu_put_buffer(f, (uint8_t *)vmsdsub->name, len);
            qemu_put_be32(f, vmsdsub->version_id);
            vmstate_save_state(f, vmsdsub, opaque, vmdesc);

            if (vmdesc) {
                json_end_object(vmdesc);
            }
        }
        sub++;
    }

    if (vmdesc && subsection_found) {
        json_end_array(vmdesc);
    }
}

/* bool */

static int get_bool(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    bool *v = pv;
    *v = qemu_get_byte(f);
    return 0;
}

static int put_bool(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                    QJSON *vmdesc)
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

static int get_int8(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int8_t *v = pv;
    qemu_get_s8s(f, v);
    return 0;
}

static int put_int8(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                     QJSON *vmdesc)
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

static int get_int16(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int16_t *v = pv;
    qemu_get_sbe16s(f, v);
    return 0;
}

static int put_int16(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                     QJSON *vmdesc)
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

static int get_int32(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int32_t *v = pv;
    qemu_get_sbe32s(f, v);
    return 0;
}

static int put_int32(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                     QJSON *vmdesc)
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
                           VMStateField *field)
{
    int32_t *v = pv;
    int32_t v2;
    qemu_get_sbe32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx32 " != %" PRIx32, *v, v2);
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

static int get_int32_le(QEMUFile *f, void *pv, size_t size, VMStateField *field)
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

static int get_int64(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int64_t *v = pv;
    qemu_get_sbe64s(f, v);
    return 0;
}

static int put_int64(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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

static int get_uint8(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    uint8_t *v = pv;
    qemu_get_8s(f, v);
    return 0;
}

static int put_uint8(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                     QJSON *vmdesc)
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

static int get_uint16(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    uint16_t *v = pv;
    qemu_get_be16s(f, v);
    return 0;
}

static int put_uint16(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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

static int get_uint32(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    uint32_t *v = pv;
    qemu_get_be32s(f, v);
    return 0;
}

static int put_uint32(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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
                            VMStateField *field)
{
    uint32_t *v = pv;
    uint32_t v2;
    qemu_get_be32s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx32 " != %" PRIx32, *v, v2);
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint32_equal = {
    .name = "uint32 equal",
    .get  = get_uint32_equal,
    .put  = put_uint32,
};

/* 64 bit unsigned int */

static int get_uint64(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    uint64_t *v = pv;
    qemu_get_be64s(f, v);
    return 0;
}

static int put_uint64(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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

static int get_nullptr(QEMUFile *f, void *pv, size_t size, VMStateField *field)

{
    if (qemu_get_byte(f) == VMS_NULLPTR_MARKER) {
        return  0;
    }
    error_report("vmstate: get_nullptr expected VMS_NULLPTR_MARKER");
    return -EINVAL;
}

static int put_nullptr(QEMUFile *f, void *pv, size_t size,
                        VMStateField *field, QJSON *vmdesc)

{
    if (pv == NULL) {
        qemu_put_byte(f, VMS_NULLPTR_MARKER);
        return 0;
    }
    error_report("vmstate: put_nullptr must be called with pv == NULL");
    return -EINVAL;
}

const VMStateInfo vmstate_info_nullptr = {
    .name = "uint64",
    .get  = get_nullptr,
    .put  = put_nullptr,
};

/* 64 bit unsigned int. See that the received value is the same than the one
   in the field */

static int get_uint64_equal(QEMUFile *f, void *pv, size_t size,
                            VMStateField *field)
{
    uint64_t *v = pv;
    uint64_t v2;
    qemu_get_be64s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%" PRIx64 " != %" PRIx64, *v, v2);
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
                           VMStateField *field)
{
    uint8_t *v = pv;
    uint8_t v2;
    qemu_get_8s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%x != %x", *v, v2);
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
                            VMStateField *field)
{
    uint16_t *v = pv;
    uint16_t v2;
    qemu_get_be16s(f, &v2);

    if (*v == v2) {
        return 0;
    }
    error_report("%x != %x", *v, v2);
    return -EINVAL;
}

const VMStateInfo vmstate_info_uint16_equal = {
    .name = "uint16 equal",
    .get  = get_uint16_equal,
    .put  = put_uint16,
};

/* floating point */

static int get_float64(QEMUFile *f, void *pv, size_t size,
                       VMStateField *field)
{
    float64 *v = pv;

    *v = make_float64(qemu_get_be64(f));
    return 0;
}

static int put_float64(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                       QJSON *vmdesc)
{
    uint64_t *v = pv;

    qemu_put_be64(f, float64_val(*v));
    return 0;
}

const VMStateInfo vmstate_info_float64 = {
    .name = "float64",
    .get  = get_float64,
    .put  = put_float64,
};

/* CPU_DoubleU type */

static int get_cpudouble(QEMUFile *f, void *pv, size_t size,
                         VMStateField *field)
{
    CPU_DoubleU *v = pv;
    qemu_get_be32s(f, &v->l.upper);
    qemu_get_be32s(f, &v->l.lower);
    return 0;
}

static int put_cpudouble(QEMUFile *f, void *pv, size_t size,
                         VMStateField *field, QJSON *vmdesc)
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
                      VMStateField *field)
{
    uint8_t *v = pv;
    qemu_get_buffer(f, v, size);
    return 0;
}

static int put_buffer(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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
                             VMStateField *field)
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
                             VMStateField *field, QJSON *vmdesc)
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
static int get_tmp(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int ret;
    const VMStateDescription *vmsd = field->vmsd;
    int version_id = field->version_id;
    void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    ret = vmstate_load_state(f, vmsd, tmp, version_id);
    g_free(tmp);
    return ret;
}

static int put_tmp(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                    QJSON *vmdesc)
{
    const VMStateDescription *vmsd = field->vmsd;
    void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    vmstate_save_state(f, vmsd, tmp, vmdesc);
    g_free(tmp);

    return 0;
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
static int get_bitmap(QEMUFile *f, void *pv, size_t size, VMStateField *field)
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

static int put_bitmap(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
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
                      VMStateField *field)
{
    int ret = 0;
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
        ret = vmstate_load_state(f, vmsd, elm, version_id);
        if (ret) {
            return ret;
        }
        QTAILQ_RAW_INSERT_TAIL(pv, elm, entry_offset);
    }

    trace_get_qtailq_end(vmsd->name, "end", ret);
    return ret;
}

/* put for QTAILQ */
static int put_qtailq(QEMUFile *f, void *pv, size_t unused_size,
                      VMStateField *field, QJSON *vmdesc)
{
    const VMStateDescription *vmsd = field->vmsd;
    /* offset of the QTAILQ entry in a QTAILQ element*/
    size_t entry_offset = field->start;
    void *elm;

    trace_put_qtailq(vmsd->name, vmsd->version_id);

    QTAILQ_RAW_FOREACH(elm, pv, entry_offset) {
        qemu_put_byte(f, true);
        vmstate_save_state(f, vmsd, elm, vmdesc);
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
