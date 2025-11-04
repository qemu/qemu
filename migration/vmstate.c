/*
 * VMState interpreter
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
#include "migration.h"
#include "migration/vmstate.h"
#include "savevm.h"
#include "qapi/error.h"
#include "qobject/json-writer.h"
#include "qemu-file.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "trace.h"

static int vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, JSONWriter *vmdesc,
                                   Error **errp);
static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, Error **errp);

/* Whether this field should exist for either save or load the VM? */
static bool
vmstate_field_exists(const VMStateDescription *vmsd, const VMStateField *field,
                     void *opaque, int version_id)
{
    bool result;

    if (field->field_exists) {
        /* If there's the function checker, that's the solo truth */
        result = field->field_exists(opaque, version_id);
        trace_vmstate_field_exists(vmsd->name, field->name, field->version_id,
                                   version_id, result);
    } else {
        /*
         * Otherwise, we only save/load if field version is same or older.
         * For example, when loading from an old binary with old version,
         * we ignore new fields with newer version_ids.
         */
        result = field->version_id <= version_id;
    }

    return result;
}

/*
 * Create a fake nullptr field when there's a NULL pointer detected in the
 * array of a VMS_ARRAY_OF_POINTER VMSD field.  It's needed because we
 * can't dereference the NULL pointer.
 */
static const VMStateField *
vmsd_create_fake_nullptr_field(const VMStateField *field)
{
    VMStateField *fake = g_new0(VMStateField, 1);

    /* It can only happen on an array of pointers! */
    assert(field->flags & VMS_ARRAY_OF_POINTER);

    /* Some of fake's properties should match the original's */
    fake->name = field->name;
    fake->version_id = field->version_id;

    /* Do not need "field_exists" check as it always exists (which is null) */
    fake->field_exists = NULL;

    /* See vmstate_info_nullptr - use 1 byte to represent nullptr */
    fake->size = 1;
    fake->info = &vmstate_info_nullptr;
    fake->flags = VMS_SINGLE;

    /* All the rest fields shouldn't matter.. */

    return (const VMStateField *)fake;
}

static int vmstate_n_elems(void *opaque, const VMStateField *field)
{
    int n_elems = 1;

    if (field->flags & VMS_ARRAY) {
        n_elems = field->num;
    } else if (field->flags & VMS_VARRAY_INT32) {
        n_elems = *(int32_t *)(opaque + field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT32) {
        n_elems = *(uint32_t *)(opaque + field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT16) {
        n_elems = *(uint16_t *)(opaque + field->num_offset);
    } else if (field->flags & VMS_VARRAY_UINT8) {
        n_elems = *(uint8_t *)(opaque + field->num_offset);
    }

    if (field->flags & VMS_MULTIPLY_ELEMENTS) {
        n_elems *= field->num;
    }

    trace_vmstate_n_elems(field->name, n_elems);
    return n_elems;
}

static int vmstate_size(void *opaque, const VMStateField *field)
{
    int size = field->size;

    if (field->flags & VMS_VBUFFER) {
        size = *(int32_t *)(opaque + field->size_offset);
        if (field->flags & VMS_MULTIPLY) {
            size *= field->size;
        }
    }

    return size;
}

static void vmstate_handle_alloc(void *ptr, const VMStateField *field,
                                 void *opaque)
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
                       void *opaque, int version_id, Error **errp)
{
    ERRP_GUARD();
    const VMStateField *field = vmsd->fields;
    int ret = 0;

    trace_vmstate_load_state(vmsd->name, version_id);
    if (version_id > vmsd->version_id) {
        error_setg(errp, "%s: incoming version_id %d is too new "
                   "for local version_id %d",
                   vmsd->name, version_id, vmsd->version_id);
        trace_vmstate_load_state_end(vmsd->name, "too new", -EINVAL);
        return -EINVAL;
    }
    if  (version_id < vmsd->minimum_version_id) {
        error_setg(errp, "%s: incoming version_id %d is too old "
                   "for local minimum version_id %d",
                   vmsd->name, version_id, vmsd->minimum_version_id);
        trace_vmstate_load_state_end(vmsd->name, "too old", -EINVAL);
        return -EINVAL;
    }
    if (vmsd->pre_load_errp) {
        if (!vmsd->pre_load_errp(opaque, errp)) {
            error_prepend(errp, "pre load hook failed for: '%s', "
                          "version_id: %d, minimum version_id: %d: ",
                          vmsd->name, vmsd->version_id,
                          vmsd->minimum_version_id);
            return -EINVAL;
        }
    } else if (vmsd->pre_load) {
        ret = vmsd->pre_load(opaque);
        if (ret) {
            error_setg(errp, "pre load hook failed for: '%s', "
                       "version_id: %d, minimum version_id: %d, ret: %d",
                       vmsd->name, vmsd->version_id, vmsd->minimum_version_id,
                       ret);
            return ret;
        }
    }
    while (field->name) {
        bool exists = vmstate_field_exists(vmsd, field, opaque, version_id);
        trace_vmstate_load_state_field(vmsd->name, field->name, exists);
        if (exists) {
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
                const VMStateField *inner_field;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    curr_elem = *(void **)curr_elem;
                }

                if (!curr_elem && size) {
                    /*
                     * If null pointer found (which should only happen in
                     * an array of pointers), use null placeholder and do
                     * not follow.
                     */
                    inner_field = vmsd_create_fake_nullptr_field(field);
                } else {
                    inner_field = field;
                }

                if (inner_field->flags & VMS_STRUCT) {
                    ret = vmstate_load_state(f, inner_field->vmsd, curr_elem,
                                             inner_field->vmsd->version_id,
                                             errp);
                } else if (inner_field->flags & VMS_VSTRUCT) {
                    ret = vmstate_load_state(f, inner_field->vmsd, curr_elem,
                                             inner_field->struct_version_id,
                                             errp);
                } else {
                    ret = inner_field->info->get(f, curr_elem, size,
                                                 inner_field);
                    if (ret < 0) {
                        error_setg(errp,
                                   "Failed to load element of type %s for %s: "
                                   "%d", inner_field->info->name,
                                   inner_field->name, ret);
                    }
                }

                /* If we used a fake temp field.. free it now */
                if (inner_field != field) {
                    g_clear_pointer((gpointer *)&inner_field, g_free);
                }

                if (ret >= 0) {
                    ret = qemu_file_get_error(f);
                    if (ret < 0) {
                        error_setg(errp,
                                   "Failed to load %s state: stream error: %d",
                                   vmsd->name, ret);
                    }
                }
                if (ret < 0) {
                    qemu_file_set_error(f, ret);
                    trace_vmstate_load_field_error(field->name, ret);
                    return ret;
                }
            }
        } else if (field->flags & VMS_MUST_EXIST) {
            error_setg(errp, "Input validation failed: %s/%s version_id: %d",
                       vmsd->name, field->name, vmsd->version_id);
            return -1;
        }
        field++;
    }
    assert(field->flags == VMS_END);
    ret = vmstate_subsection_load(f, vmsd, opaque, errp);
    if (ret != 0) {
        qemu_file_set_error(f, ret);
        return ret;
    }
    if (vmsd->post_load_errp) {
        if (!vmsd->post_load_errp(opaque, version_id, errp)) {
            error_prepend(errp, "post load hook failed for: %s, version_id: "
                          "%d, minimum_version: %d: ", vmsd->name,
                          vmsd->version_id, vmsd->minimum_version_id);
            ret = -EINVAL;
        }
    } else if (vmsd->post_load) {
        ret = vmsd->post_load(opaque, version_id);
        if (ret < 0) {
            error_setg(errp,
                       "post load hook failed for: %s, version_id: %d, "
                       "minimum_version: %d, ret: %d",
                       vmsd->name, vmsd->version_id, vmsd->minimum_version_id,
                       ret);
        }
    }
    trace_vmstate_load_state_end(vmsd->name, "end", ret);
    return ret;
}

static int vmfield_name_num(const VMStateField *start,
                            const VMStateField *search)
{
    const VMStateField *field;
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

static bool vmfield_name_is_unique(const VMStateField *start,
                                   const VMStateField *search)
{
    const VMStateField *field;
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

static const char *vmfield_get_type_name(const VMStateField *field)
{
    const char *type = "unknown";

    if (field->flags & VMS_STRUCT) {
        type = "struct";
    } else if (field->flags & VMS_VSTRUCT) {
        type = "vstruct";
    } else if (field->info->name) {
        type = field->info->name;
    }

    return type;
}

static bool vmsd_can_compress(const VMStateField *field)
{
    if (field->field_exists) {
        /* Dynamically existing fields mess up compression */
        return false;
    }

    if (field->flags & VMS_STRUCT) {
        const VMStateField *sfield = field->vmsd->fields;
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

static void vmsd_desc_field_start(const VMStateDescription *vmsd,
                                  JSONWriter *vmdesc,
                                  const VMStateField *field, int i, int max)
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

    json_writer_start_object(vmdesc, NULL);
    json_writer_str(vmdesc, "name", name);
    if (is_array) {
        if (can_compress) {
            json_writer_int64(vmdesc, "array_len", max);
        } else {
            json_writer_int64(vmdesc, "index", i);
        }
    }
    json_writer_str(vmdesc, "type", vmfield_get_type_name(field));

    if (field->flags & VMS_STRUCT) {
        json_writer_start_object(vmdesc, "struct");
    }

    g_free(name);
}

static void vmsd_desc_field_end(const VMStateDescription *vmsd,
                                JSONWriter *vmdesc,
                                const VMStateField *field, size_t size)
{
    if (!vmdesc) {
        return;
    }

    if (field->flags & VMS_STRUCT) {
        /* We printed a struct in between, close its child object */
        json_writer_end_object(vmdesc);
    }

    json_writer_int64(vmdesc, "size", size);
    json_writer_end_object(vmdesc);
}


bool vmstate_section_needed(const VMStateDescription *vmsd, void *opaque)
{
    if (vmsd->needed && !vmsd->needed(opaque)) {
        /* optional section not needed */
        return false;
    }
    return true;
}


int vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, JSONWriter *vmdesc_id, Error **errp)
{
    return vmstate_save_state_v(f, vmsd, opaque, vmdesc_id, vmsd->version_id, errp);
}

int vmstate_save_state_v(QEMUFile *f, const VMStateDescription *vmsd,
                         void *opaque, JSONWriter *vmdesc, int version_id, Error **errp)
{
    ERRP_GUARD();
    int ret = 0;
    const VMStateField *field = vmsd->fields;

    trace_vmstate_save_state_top(vmsd->name);

    if (vmsd->pre_save_errp) {
        ret = vmsd->pre_save_errp(opaque, errp) ? 0 : -EINVAL;
        trace_vmstate_save_state_pre_save_res(vmsd->name, ret);
        if (ret < 0) {
            error_prepend(errp, "pre-save for %s failed: ", vmsd->name);
            return ret;
        }
    } else if (vmsd->pre_save) {
        ret = vmsd->pre_save(opaque);
        trace_vmstate_save_state_pre_save_res(vmsd->name, ret);
        if (ret) {
            error_setg(errp, "pre-save failed: %s", vmsd->name);
            return ret;
        }
    }

    if (vmdesc) {
        json_writer_str(vmdesc, "vmsd_name", vmsd->name);
        json_writer_int64(vmdesc, "version", version_id);
        json_writer_start_array(vmdesc, "fields");
    }

    while (field->name) {
        if (vmstate_field_exists(vmsd, field, opaque, version_id)) {
            void *first_elem = opaque + field->offset;
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);
            uint64_t old_offset, written_bytes;
            JSONWriter *vmdesc_loop = vmdesc;
            bool is_prev_null = false;

            trace_vmstate_save_state_loop(vmsd->name, field->name, n_elems);
            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }

            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;
                const VMStateField *inner_field;
                bool is_null;
                int max_elems = n_elems - i;

                old_offset = qemu_file_transferred(f);
                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    assert(curr_elem);
                    curr_elem = *(void **)curr_elem;
                }

                if (!curr_elem && size) {
                    /*
                     * If null pointer found (which should only happen in
                     * an array of pointers), use null placeholder and do
                     * not follow.
                     */
                    inner_field = vmsd_create_fake_nullptr_field(field);
                    is_null = true;
                } else {
                    inner_field = field;
                    is_null = false;
                }

                /*
                 * This logic only matters when dumping VM Desc.
                 *
                 * Due to the fake nullptr handling above, if there's mixed
                 * null/non-null data, it doesn't make sense to emit a
                 * compressed array representation spanning the entire array
                 * because the field types will be different (e.g. struct
                 * vs. nullptr). Search ahead for the next null/non-null element
                 * and start a new compressed array if found.
                 */
                if (vmdesc && (field->flags & VMS_ARRAY_OF_POINTER) &&
                    is_null != is_prev_null) {

                    is_prev_null = is_null;
                    vmdesc_loop = vmdesc;

                    for (int j = i + 1; j < n_elems; j++) {
                        void *elem = *(void **)(first_elem + size * j);
                        bool elem_is_null = !elem && size;

                        if (is_null != elem_is_null) {
                            max_elems = j - i;
                            break;
                        }
                    }
                }

                vmsd_desc_field_start(vmsd, vmdesc_loop, inner_field,
                                      i, max_elems);

                if (inner_field->flags & VMS_STRUCT) {
                    ret = vmstate_save_state(f, inner_field->vmsd,
                                             curr_elem, vmdesc_loop, errp);
                } else if (inner_field->flags & VMS_VSTRUCT) {
                    ret = vmstate_save_state_v(f, inner_field->vmsd,
                                               curr_elem, vmdesc_loop,
                                               inner_field->struct_version_id,
                                               errp);
                } else {
                    ret = inner_field->info->put(f, curr_elem, size,
                                                 inner_field, vmdesc_loop);
                }

                written_bytes = qemu_file_transferred(f) - old_offset;
                vmsd_desc_field_end(vmsd, vmdesc_loop, inner_field,
                                    written_bytes);

                /* If we used a fake temp field.. free it now */
                if (is_null) {
                    g_clear_pointer((gpointer *)&inner_field, g_free);
                }

                if (ret) {
                    error_setg(errp, "Save of field %s/%s failed",
                                vmsd->name, field->name);
                    if (vmsd->post_save) {
                        vmsd->post_save(opaque);
                    }
                    return ret;
                }

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
    assert(field->flags == VMS_END);

    if (vmdesc) {
        json_writer_end_array(vmdesc);
    }

    ret = vmstate_subsection_save(f, vmsd, opaque, vmdesc, errp);

    if (vmsd->post_save) {
        int ps_ret = vmsd->post_save(opaque);
        if (!ret && ps_ret) {
            ret = ps_ret;
            error_setg(errp, "post-save failed: %s", vmsd->name);
        }
    }
    return ret;
}

static const VMStateDescription *
vmstate_get_subsection(const VMStateDescription * const *sub,
                       const char *idstr)
{
    if (sub) {
        for (const VMStateDescription *s = *sub; s ; s = *++sub) {
            if (strcmp(idstr, s->name) == 0) {
                return s;
            }
        }
    }
    return NULL;
}

static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, Error **errp)
{
    ERRP_GUARD();
    trace_vmstate_subsection_load(vmsd->name);

    while (qemu_peek_byte(f, 0) == QEMU_VM_SUBSECTION) {
        char idstr[256], *idstr_ret;
        int ret;
        uint8_t version_id, len, size;
        const VMStateDescription *sub_vmsd;

        len = qemu_peek_byte(f, 1);
        if (len < strlen(vmsd->name) + 1) {
            /* subsection name has to be "section_name/a" */
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
            error_setg(errp, "VM subsection '%s' in '%s' does not exist",
                       idstr, vmsd->name);
            return -ENOENT;
        }
        qemu_file_skip(f, 1); /* subsection */
        qemu_file_skip(f, 1); /* len */
        qemu_file_skip(f, len); /* idstr */
        version_id = qemu_get_be32(f);

        ret = vmstate_load_state(f, sub_vmsd, opaque, version_id, errp);
        if (ret) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(child)");
            error_prepend(errp,
                          "Loading VM subsection '%s' in '%s' failed: %d: ",
                          idstr, vmsd->name, ret);
            return ret;
        }
    }

    trace_vmstate_subsection_load_good(vmsd->name);
    return 0;
}

static int vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, JSONWriter *vmdesc,
                                   Error **errp)
{
    const VMStateDescription * const *sub = vmsd->subsections;
    bool vmdesc_has_subsections = false;
    int ret = 0;

    trace_vmstate_subsection_save_top(vmsd->name);
    while (sub && *sub) {
        if (vmstate_section_needed(*sub, opaque)) {
            const VMStateDescription *vmsdsub = *sub;
            uint8_t len;

            trace_vmstate_subsection_save_loop(vmsd->name, vmsdsub->name);
            if (vmdesc) {
                /* Only create subsection array when we have any */
                if (!vmdesc_has_subsections) {
                    json_writer_start_array(vmdesc, "subsections");
                    vmdesc_has_subsections = true;
                }

                json_writer_start_object(vmdesc, NULL);
            }

            qemu_put_byte(f, QEMU_VM_SUBSECTION);
            len = strlen(vmsdsub->name);
            qemu_put_byte(f, len);
            qemu_put_buffer(f, (uint8_t *)vmsdsub->name, len);
            qemu_put_be32(f, vmsdsub->version_id);
            ret = vmstate_save_state(f, vmsdsub, opaque, vmdesc, errp);
            if (ret) {
                return ret;
            }

            if (vmdesc) {
                json_writer_end_object(vmdesc);
            }
        }
        sub++;
    }

    if (vmdesc_has_subsections) {
        json_writer_end_array(vmdesc);
    }

    return ret;
}
