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
#include "qapi/qmp/json-writer.h"
#include "qemu-file.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "trace.h"

static int vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, JSONWriter *vmdesc);
static int vmstate_subsection_load(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque);

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
                       void *opaque, int version_id)
{
    const VMStateField *field = vmsd->fields;
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
        error_report("%s: incoming version_id %d is too old "
                     "for local minimum version_id  %d",
                     vmsd->name, version_id, vmsd->minimum_version_id);
        trace_vmstate_load_state_end(vmsd->name, "too old", -EINVAL);
        return -EINVAL;
    }
    if (vmsd->pre_load) {
        ret = vmsd->pre_load(opaque);
        if (ret) {
            return ret;
        }
    }
    while (field->name) {
        trace_vmstate_load_state_field(vmsd->name, field->name);
        if (vmstate_field_exists(vmsd, field, opaque, version_id)) {
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
                } else if (field->flags & VMS_VSTRUCT) {
                    ret = vmstate_load_state(f, field->vmsd, curr_elem,
                                             field->struct_version_id);
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
    assert(field->flags == VMS_END);
    ret = vmstate_subsection_load(f, vmsd, opaque);
    if (ret != 0) {
        qemu_file_set_error(f, ret);
        return ret;
    }
    if (vmsd->post_load) {
        ret = vmsd->post_load(opaque, version_id);
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
                                const VMStateField *field, size_t size, int i)
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
                       void *opaque, JSONWriter *vmdesc_id)
{
    return vmstate_save_state_v(f, vmsd, opaque, vmdesc_id, vmsd->version_id, NULL);
}

int vmstate_save_state_with_err(QEMUFile *f, const VMStateDescription *vmsd,
                       void *opaque, JSONWriter *vmdesc_id, Error **errp)
{
    return vmstate_save_state_v(f, vmsd, opaque, vmdesc_id, vmsd->version_id, errp);
}

int vmstate_save_state_v(QEMUFile *f, const VMStateDescription *vmsd,
                         void *opaque, JSONWriter *vmdesc, int version_id, Error **errp)
{
    int ret = 0;
    const VMStateField *field = vmsd->fields;

    trace_vmstate_save_state_top(vmsd->name);

    if (vmsd->pre_save) {
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

            trace_vmstate_save_state_loop(vmsd->name, field->name, n_elems);
            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }
            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;

                vmsd_desc_field_start(vmsd, vmdesc_loop, field, i, n_elems);
                old_offset = qemu_file_transferred(f);
                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    assert(curr_elem);
                    curr_elem = *(void **)curr_elem;
                }
                if (!curr_elem && size) {
                    /* if null pointer write placeholder and do not follow */
                    assert(field->flags & VMS_ARRAY_OF_POINTER);
                    ret = vmstate_info_nullptr.put(f, curr_elem, size, NULL,
                                                   NULL);
                } else if (field->flags & VMS_STRUCT) {
                    ret = vmstate_save_state(f, field->vmsd, curr_elem,
                                             vmdesc_loop);
                } else if (field->flags & VMS_VSTRUCT) {
                    ret = vmstate_save_state_v(f, field->vmsd, curr_elem,
                                               vmdesc_loop,
                                               field->struct_version_id, errp);
                } else {
                    ret = field->info->put(f, curr_elem, size, field,
                                     vmdesc_loop);
                }
                if (ret) {
                    error_setg(errp, "Save of field %s/%s failed",
                                vmsd->name, field->name);
                    if (vmsd->post_save) {
                        vmsd->post_save(opaque);
                    }
                    return ret;
                }

                written_bytes = qemu_file_transferred(f) - old_offset;
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
    assert(field->flags == VMS_END);

    if (vmdesc) {
        json_writer_end_array(vmdesc);
    }

    ret = vmstate_subsection_save(f, vmsd, opaque, vmdesc);

    if (vmsd->post_save) {
        int ps_ret = vmsd->post_save(opaque);
        if (!ret) {
            ret = ps_ret;
        }
    }
    return ret;
}

static const VMStateDescription *
vmstate_get_subsection(const VMStateDescription **sub, char *idstr)
{
    while (sub && *sub) {
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

static int vmstate_subsection_save(QEMUFile *f, const VMStateDescription *vmsd,
                                   void *opaque, JSONWriter *vmdesc)
{
    const VMStateDescription **sub = vmsd->subsections;
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
            ret = vmstate_save_state(f, vmsdsub, opaque, vmdesc);
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
