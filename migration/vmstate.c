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

static bool vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, JSONWriter *vmdesc,
                                    Error **errp);
static bool vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, Error **errp);
static bool vmstate_save_vmsd_v(QEMUFile *f, const VMStateDescription *vmsd,
                                void *opaque, JSONWriter *vmdesc,
                                int version_id, Error **errp);

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
 * Create a ptr marker field when there's a NULL pointer detected in the
 * array of a VMS_ARRAY_OF_POINTER VMSD field.  It's needed because we
 * can't dereference the NULL pointer.
 */
static const VMStateField *
vmsd_create_ptr_marker_field(const VMStateField *field)
{
    VMStateField *fake = g_new0(VMStateField, 1);

    /* It can only happen on an array of pointers! */
    assert(field->flags & VMS_ARRAY_OF_POINTER);

    /* Some of fake's properties should match the original's */
    fake->name = field->name;
    fake->version_id = field->version_id;

    /* Do not need "field_exists" check as it always exists */
    fake->field_exists = NULL;

    /* See vmstate_info_ptr_marker - use 1 byte to represent ptr status */
    fake->size = 1;
    fake->info = &vmstate_info_ptr_marker;
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

    trace_vmstate_n_elems(field->name, n_elems);
    return n_elems;
}

static int vmstate_size(void *opaque, const VMStateField *field)
{
    int size;

    if (field->flags & VMS_VBUFFER) {
        size = *(int32_t *)(opaque + field->size_offset);
        if (field->flags & VMS_MULTIPLY) {
            size *= field->size;
        }
    } else if (field->flags & VMS_ARRAY_OF_POINTER) {
        /*
         * For an array of pointer, the each element is always size of a
         * host pointer.
         */
        size = sizeof(void *);
    } else {
        size = field->size;
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

static bool vmstate_ptr_marker_load(QEMUFile *f, bool *load_field,
                                    Error **errp)
{
    int byte = qemu_get_byte(f);

    if (byte == VMS_MARKER_PTR_NULL) {
        /* When it's a null ptr marker, do not continue the load */
        *load_field = false;
        return true;
    }

    if (byte == VMS_MARKER_PTR_VALID) {
        /* We need to load the field right after the marker */
        *load_field = true;
        return true;
    }

    error_setg(errp, "Unexpected ptr marker: %d", byte);
    return false;
}

static bool vmstate_pre_load(const VMStateDescription *vmsd, void *opaque,
                             Error **errp)
{
    ERRP_GUARD();

    if (vmsd->pre_load_errp) {
        if (!vmsd->pre_load_errp(opaque, errp)) {
            error_prepend(errp, "pre load hook failed for: '%s', "
                          "version_id: %d, minimum version_id: %d: ",
                          vmsd->name, vmsd->version_id,
                          vmsd->minimum_version_id);
            return false;
        }
    } else if (vmsd->pre_load) {
        int ret = vmsd->pre_load(opaque);
        if (ret) {
            error_setg(errp, "pre load hook failed for: '%s', "
                       "version_id: %d, minimum version_id: %d, ret: %d",
                       vmsd->name, vmsd->version_id, vmsd->minimum_version_id,
                       ret);
            return false;
        }
    }

    return true;
}

static bool vmstate_load_field(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field, Error **errp)
{
    if (field->flags & VMS_STRUCT) {
        return vmstate_load_vmsd(f, field->vmsd, pv, field->vmsd->version_id,
                                 errp);
    } else if (field->flags & VMS_VSTRUCT) {
        return vmstate_load_vmsd(f, field->vmsd, pv, field->struct_version_id,
                                 errp);
    } else if (field->info->load) {
        return field->info->load(f, pv, size, field, errp);
    }

    if (field->info->get(f, pv, size, field) < 0) {
        error_setg(errp,
                   "Failed to load element of type %s for %s",
                   field->info->name, field->name);
        return false;
    }

    return true;
}

static bool vmstate_post_load(const VMStateDescription *vmsd,
                              void *opaque, int version_id, Error **errp)
{
    ERRP_GUARD();

    if (vmsd->post_load_errp) {
        if (!vmsd->post_load_errp(opaque, version_id, errp)) {
            error_prepend(errp, "post load hook failed for: %s, version_id: "
                          "%d, minimum_version: %d: ", vmsd->name,
                          vmsd->version_id, vmsd->minimum_version_id);
            return false;
        }
    } else if (vmsd->post_load) {
        int ret = vmsd->post_load(opaque, version_id);
        if (ret < 0) {
            error_setg(errp,
                       "post load hook failed for: %s, version_id: %d, "
                       "minimum_version: %d, ret: %d",
                       vmsd->name, vmsd->version_id, vmsd->minimum_version_id,
                       ret);
            return false;
        }
    }

    return true;
}

/*
 * Try to prepare loading the next element, the object pointer to be put
 * into @next_elem.  When @next_elem is NULL, it means we should skip
 * loading this element.
 *
 * Returns false for errors, in which case *errp will be set, migration
 * must be aborted.
 */
static bool vmstate_load_next(QEMUFile *f, const VMStateField *field,
                              void *first_elem, void **next_elem,
                              int size, int i, Error **errp)
{
    bool auto_alloc = field->flags & VMS_ARRAY_OF_POINTER_AUTO_ALLOC;
    void *ptr = first_elem + size * i, **pptr;
    bool load_field;

    if (!(field->flags & VMS_ARRAY_OF_POINTER)) {
        /* Simplest case, no pointer involved */
        *next_elem = ptr;
        return true;
    }

    /*
     * We're loading an array of pointers, switch to use pptr to make it
     * easier to read later
     */
    pptr = (void **)ptr;

    /*
     * If auto_alloc is on, making sure the user provided an array of NULL
     * pointers to start with
     */
    assert(!auto_alloc || *pptr == NULL);

    /*
     * When pointer is null, we must expect a ptr marker first.  Use cases:
     *
     * (1) _AUTO_ALLOC implies a ptr marker will always exist, or,
     *
     * (2) the element on destination is NULL, which expects the src to send a
     *     NULL-only marker.
     *
     * Here, checking against a NULL pointer will work for both.
     */
    if (!*pptr) {
        if (!vmstate_ptr_marker_load(f, &load_field, errp)) {
            trace_vmstate_load_field_error(field->name, -EINVAL);
            return false;
        }

        /*
         * If loading is needed, do pre-allocation first (otherwise keeping
         * *pptr==NULL to imply a skip below)
         */
        if (load_field) {
            /* Only applies when auto_alloc=on on the field */
            assert(auto_alloc);
            /*
             * NOTE: do not use vmstate_size() here, because we need the
             * object size, not entry size of the array.
             */
            *pptr = g_malloc0(field->size);
        }
    }

    /* Move the cursor to the next element for loading */
    *next_elem = *pptr;
    return true;
}

bool vmstate_load_vmsd(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, int version_id, Error **errp)
{
    ERRP_GUARD();
    const VMStateField *field = vmsd->fields;

    trace_vmstate_load_state(vmsd->name, version_id);

    if (version_id > vmsd->version_id) {
        error_setg(errp, "%s: incoming version_id %d is too new "
                   "for local version_id %d",
                   vmsd->name, version_id, vmsd->version_id);
        trace_vmstate_load_state_fail(vmsd->name, "too new");
        return false;
    }

    if  (version_id < vmsd->minimum_version_id) {
        error_setg(errp, "%s: incoming version_id %d is too old "
                   "for local minimum version_id %d",
                   vmsd->name, version_id, vmsd->minimum_version_id);
        trace_vmstate_load_state_fail(vmsd->name, "too old");
        return false;
    }

    if (!vmstate_pre_load(vmsd, opaque, errp)) {
        return false;
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
                void *curr_elem;
                bool ok;

                ok = vmstate_load_next(f, field, first_elem, &curr_elem,
                                       size, i, errp);
                if (!ok) {
                    return false;
                }

                if (!curr_elem) {
                    /* Implies a skip */
                    continue;
                }

                ok = vmstate_load_field(f, curr_elem, size, field, errp);

                if (ok) {
                    int ret = qemu_file_get_error(f);
                    if (ret < 0) {
                        error_setg(errp,
                                   "Failed to load %s state: stream error: %d",
                                   vmsd->name, ret);
                        trace_vmstate_load_field_error(field->name, ret);
                        return false;
                    }
                } else {
                    qemu_file_set_error(f, -EINVAL);
                    trace_vmstate_load_field_error(field->name, -EINVAL);
                    return false;
                }
            }
        } else if (field->flags & VMS_MUST_EXIST) {
            error_setg(errp, "Input validation failed: %s/%s version_id: %d",
                       vmsd->name, field->name, vmsd->version_id);
            return false;
        }
        field++;
    }
    assert(field->flags == VMS_END);

    if (!vmstate_subsection_load(f, vmsd, opaque, errp)) {
        qemu_file_set_error(f, -EINVAL);
        return false;
    }

    if (!vmstate_post_load(vmsd, opaque, version_id, errp)) {
        trace_vmstate_load_state_fail(vmsd->name, "post-load");
        return false;
    }

    trace_vmstate_load_state_success(vmsd->name);
    return true;
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

    if (field->flags & VMS_ARRAY_OF_POINTER_AUTO_ALLOC) {
        /*
         * This may involve two VMSD fields to be saved, one for the
         * marker to show if the pointer is NULL, followed by the real
         * vmstate object.  To make it simple at least for now, skip
         * compression for this one.
         */
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

static bool vmstate_pre_save(const VMStateDescription *vmsd, void *opaque,
                             Error **errp)
{
    ERRP_GUARD();

    if (vmsd->pre_save_errp) {
        if (!vmsd->pre_save_errp(opaque, errp)) {
            error_prepend(errp, "pre-save for %s failed: ", vmsd->name);
            return false;
        }
    } else if (vmsd->pre_save) {
        if (vmsd->pre_save(opaque) < 0) {
            error_setg(errp, "pre-save failed: %s", vmsd->name);
            return false;
        }
    }

    return true;
}

static bool vmstate_save_field(QEMUFile *f, void *pv, size_t size,
                               const VMStateField *field,
                               JSONWriter *vmdesc, Error **errp)
{
    if (field->flags & VMS_STRUCT) {
        return vmstate_save_vmsd(f, field->vmsd, pv, vmdesc, errp);
    } else if (field->flags & VMS_VSTRUCT) {
        return vmstate_save_vmsd_v(f, field->vmsd, pv, vmdesc,
                                   field->struct_version_id, errp);
    } else if (field->info->save) {
        return field->info->save(f, pv, size, field, vmdesc, errp);
    }

    if (field->info->put(f, pv, size, field, vmdesc) < 0) {
        error_setg(errp, "put failed");
        return false;
    }

    return true;
}

/*
 * Save a whole VMSD field, including its JSON blob separately when @vmdesc
 * is specified.
 */
static inline bool
vmstate_save_field_with_vmdesc(QEMUFile *f, void *pv, size_t size,
                               const VMStateDescription *vmsd,
                               const VMStateField *field, JSONWriter *vmdesc,
                               int i, int max, Error **errp)
{
    uint64_t old_offset, written_bytes;
    bool ok;

    vmsd_desc_field_start(vmsd, vmdesc, field, i, max);

    old_offset = qemu_file_transferred(f);
    ok = vmstate_save_field(f, pv, size, field, vmdesc, errp);
    written_bytes = qemu_file_transferred(f) - old_offset;

    vmsd_desc_field_end(vmsd, vmdesc, field, written_bytes);

    if (!ok) {
        error_prepend(errp, "Save of field %s/%s failed: ",
                      vmsd->name, field->name);
    }

    return ok;
}

static bool vmstate_save_vmsd_v(QEMUFile *f, const VMStateDescription *vmsd,
                                void *opaque, JSONWriter *vmdesc,
                                int version_id, Error **errp)
{
    ERRP_GUARD();
    bool ok = true;
    const VMStateField *field = vmsd->fields;

    trace_vmstate_save_state_top(vmsd->name);

    if (!vmstate_pre_save(vmsd, opaque, errp)) {
        trace_vmstate_save_state_pre_save_fail(vmsd->name);
        return false;
    }

    trace_vmstate_save_state_pre_save_success(vmsd->name);

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
            JSONWriter *vmdesc_loop = vmdesc;
            bool is_prev_null = false;
            /*
             * When this is enabled, it means we will always push a ptr
             * marker first for each element saying if it's populated.
             */
            bool use_dynamic_array =
                field->flags & VMS_ARRAY_OF_POINTER_AUTO_ALLOC;

            trace_vmstate_save_state_loop(vmsd->name, field->name, n_elems);
            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }

            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;
                const VMStateField *inner_field;
                /* maximum number of elements to compress in the JSON blob */
                int max_elems = vmsd_can_compress(field) ? (n_elems - i) : 1;
                bool use_marker_field, is_null = false;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    assert(curr_elem);
                    curr_elem = *(void **)curr_elem;
                    is_null = !curr_elem;
                }

                use_marker_field = use_dynamic_array || is_null;

                if (use_marker_field) {
                    inner_field = vmsd_create_ptr_marker_field(field);
                } else {
                    inner_field = field;
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
                if (vmdesc && max_elems > 1 &&
                    (field->flags & VMS_ARRAY_OF_POINTER) &&
                    is_null != is_prev_null) {

                    is_prev_null = is_null;
                    vmdesc_loop = vmdesc;

                    for (int j = i + 1; j < n_elems; j++) {
                        void *elem = *(void **)(first_elem + size * j);
                        bool elem_is_null = !elem;

                        if (is_null != elem_is_null) {
                            max_elems = j - i;
                            break;
                        }
                    }
                }

                ok = vmstate_save_field_with_vmdesc(f, curr_elem, size, vmsd,
                                                    inner_field, vmdesc_loop,
                                                    i, max_elems, errp);

                /* If we used a fake temp field.. free it now */
                if (use_marker_field) {
                    g_clear_pointer((gpointer *)&inner_field, g_free);
                }

                if (!ok) {
                    goto out;
                }

                /*
                 * If we're using dynamic array and the element is
                 * populated, save the real object right after the marker.
                 */
                if (use_dynamic_array && curr_elem) {
                    /*
                     * NOTE: do not use vmstate_size() here because we want
                     * to save the real VMSD object now.
                     */
                    ok = vmstate_save_field_with_vmdesc(f, curr_elem,
                                                        field->size, vmsd,
                                                        field, vmdesc_loop,
                                                        i, max_elems, errp);

                    if (!ok) {
                        goto out;
                    }
                }

                /* Compressed arrays only care about the first element */
                if (vmdesc_loop && max_elems > 1) {
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

    ok = vmstate_subsection_save(f, vmsd, opaque, vmdesc, errp);

out:
    if (vmsd->post_save) {
        vmsd->post_save(opaque);
    }
    return ok;
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

bool vmstate_save_vmsd(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, JSONWriter *vmdesc_id, Error **errp)
{
    return vmstate_save_vmsd_v(f, vmsd, opaque, vmdesc_id, vmsd->version_id,
                               errp);
}

static bool vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, Error **errp)
{
    ERRP_GUARD();
    trace_vmstate_subsection_load(vmsd->name);

    while (qemu_peek_byte(f, 0) == QEMU_VM_SUBSECTION) {
        char idstr[256], *idstr_ret;
        uint8_t version_id, len, size;
        const VMStateDescription *sub_vmsd;

        len = qemu_peek_byte(f, 1);
        if (len < strlen(vmsd->name) + 1) {
            /* subsection name has to be "section_name/a" */
            trace_vmstate_subsection_load_bad(vmsd->name, "(short)", "");
            return true;
        }
        size = qemu_peek_buffer(f, (uint8_t **)&idstr_ret, len, 2);
        if (size != len) {
            trace_vmstate_subsection_load_bad(vmsd->name, "(peek fail)", "");
            return true;
        }
        memcpy(idstr, idstr_ret, size);
        idstr[size] = 0;

        if (strncmp(vmsd->name, idstr, strlen(vmsd->name)) != 0) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(prefix)");
            /* it doesn't have a valid subsection name */
            return true;
        }
        sub_vmsd = vmstate_get_subsection(vmsd->subsections, idstr);
        if (sub_vmsd == NULL) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(lookup)");
            error_setg(errp, "VM subsection '%s' in '%s' does not exist",
                       idstr, vmsd->name);
            return false;
        }
        qemu_file_skip(f, 1); /* subsection */
        qemu_file_skip(f, 1); /* len */
        qemu_file_skip(f, len); /* idstr */
        version_id = qemu_get_be32(f);

        if (!vmstate_load_vmsd(f, sub_vmsd, opaque, version_id, errp)) {
            trace_vmstate_subsection_load_bad(vmsd->name, idstr, "(child)");
            error_prepend(errp,
                          "Loading VM subsection '%s' in '%s' failed: ",
                          idstr, vmsd->name);
            return false;
        }
    }

    trace_vmstate_subsection_load_good(vmsd->name);
    return true;
}

static bool vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                    void *opaque, JSONWriter *vmdesc,
                                    Error **errp)
{
    const VMStateDescription * const *sub = vmsd->subsections;
    bool vmdesc_has_subsections = false;

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
            if (!vmstate_save_vmsd(f, vmsdsub, opaque, vmdesc, errp)) {
                return false;
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

    return true;
}

int vmstate_save_state(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, JSONWriter *vmdesc_id, Error **errp)
{
    return vmstate_save_vmsd(f, vmsd, opaque, vmdesc_id, errp) ? 0 : -EINVAL;
}

int vmstate_load_state(QEMUFile *f, const VMStateDescription *vmsd,
                        void *opaque, int version_id, Error **errp)
{
    return vmstate_load_vmsd(f, vmsd, opaque, version_id, errp) ? 0 : -EINVAL;
}
