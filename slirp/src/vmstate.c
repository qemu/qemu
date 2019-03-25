/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * VMState interpreter
 *
 * Copyright (c) 2009-2018 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 * copyright notice, this list of conditions and the following
 * disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <glib.h>

#include "stream.h"
#include "vmstate.h"

static int get_nullptr(SlirpIStream *f, void *pv, size_t size,
                       const VMStateField *field)
{
    if (slirp_istream_read_u8(f) == VMS_NULLPTR_MARKER) {
        return  0;
    }
    g_warning("vmstate: get_nullptr expected VMS_NULLPTR_MARKER");
    return -EINVAL;
}

static int put_nullptr(SlirpOStream *f, void *pv, size_t size,
                       const VMStateField *field)

{
    if (pv == NULL) {
        slirp_ostream_write_u8(f, VMS_NULLPTR_MARKER);
        return 0;
    }
    g_warning("vmstate: put_nullptr must be called with pv == NULL");
    return -EINVAL;
}

const VMStateInfo slirp_vmstate_info_nullptr = {
    .name = "uint64",
    .get  = get_nullptr,
    .put  = put_nullptr,
};

/* 8 bit unsigned int */

static int get_uint8(SlirpIStream *f, void *pv, size_t size, const VMStateField *field)
{
    uint8_t *v = pv;
    *v = slirp_istream_read_u8(f);
    return 0;
}

static int put_uint8(SlirpOStream *f, void *pv, size_t size, const VMStateField *field)
{
    uint8_t *v = pv;
    slirp_ostream_write_u8(f, *v);
    return 0;
}

const VMStateInfo slirp_vmstate_info_uint8 = {
    .name = "uint8",
    .get  = get_uint8,
    .put  = put_uint8,
};

/* 16 bit unsigned int */

static int get_uint16(SlirpIStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint16_t *v = pv;
    *v = slirp_istream_read_u16(f);
    return 0;
}

static int put_uint16(SlirpOStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint16_t *v = pv;
    slirp_ostream_write_u16(f, *v);
    return 0;
}

const VMStateInfo slirp_vmstate_info_uint16 = {
    .name = "uint16",
    .get  = get_uint16,
    .put  = put_uint16,
};

/* 32 bit unsigned int */

static int get_uint32(SlirpIStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint32_t *v = pv;
    *v = slirp_istream_read_u32(f);
    return 0;
}

static int put_uint32(SlirpOStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    uint32_t *v = pv;
    slirp_ostream_write_u32(f, *v);
    return 0;
}

const VMStateInfo slirp_vmstate_info_uint32 = {
    .name = "uint32",
    .get  = get_uint32,
    .put  = put_uint32,
};

/* 16 bit int */

static int get_int16(SlirpIStream *f, void *pv, size_t size, const VMStateField *field)
{
    int16_t *v = pv;
    *v = slirp_istream_read_i16(f);
    return 0;
}

static int put_int16(SlirpOStream *f, void *pv, size_t size, const VMStateField *field)
{
    int16_t *v = pv;
    slirp_ostream_write_i16(f, *v);
    return 0;
}

const VMStateInfo slirp_vmstate_info_int16 = {
    .name = "int16",
    .get  = get_int16,
    .put  = put_int16,
};

/* 32 bit int */

static int get_int32(SlirpIStream *f, void *pv, size_t size, const VMStateField *field)
{
    int32_t *v = pv;
    *v = slirp_istream_read_i32(f);
    return 0;
}

static int put_int32(SlirpOStream *f, void *pv, size_t size, const VMStateField *field)
{
    int32_t *v = pv;
    slirp_ostream_write_i32(f, *v);
    return 0;
}

const VMStateInfo slirp_vmstate_info_int32 = {
    .name = "int32",
    .get  = get_int32,
    .put  = put_int32,
};

/* vmstate_info_tmp, see VMSTATE_WITH_TMP, the idea is that we allocate
 * a temporary buffer and the pre_load/pre_save methods in the child vmsd
 * copy stuff from the parent into the child and do calculations to fill
 * in fields that don't really exist in the parent but need to be in the
 * stream.
 */
static int get_tmp(SlirpIStream *f, void *pv, size_t size, const VMStateField *field)
{
    int ret;
    const VMStateDescription *vmsd = field->vmsd;
    int version_id = field->version_id;
    void *tmp = g_malloc(size);

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    ret = slirp_vmstate_load_state(f, vmsd, tmp, version_id);
    g_free(tmp);
    return ret;
}

static int put_tmp(SlirpOStream *f, void *pv, size_t size, const VMStateField *field)
{
    const VMStateDescription *vmsd = field->vmsd;
    void *tmp = g_malloc(size);
    int ret;

    /* Writes the parent field which is at the start of the tmp */
    *(void **)tmp = pv;
    ret = slirp_vmstate_save_state(f, vmsd, tmp);
    g_free(tmp);

    return ret;
}

const VMStateInfo slirp_vmstate_info_tmp = {
    .name = "tmp",
    .get = get_tmp,
    .put = put_tmp,
};

/* uint8_t buffers */

static int get_buffer(SlirpIStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    slirp_istream_read(f, pv, size);
    return 0;
}

static int put_buffer(SlirpOStream *f, void *pv, size_t size,
                      const VMStateField *field)
{
    slirp_ostream_write(f, pv, size);
    return 0;
}

const VMStateInfo slirp_vmstate_info_buffer = {
    .name = "buffer",
    .get  = get_buffer,
    .put  = put_buffer,
};

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

static int
vmstate_save_state_v(SlirpOStream *f, const VMStateDescription *vmsd,
                     void *opaque, int version_id)
{
    int ret = 0;
    const VMStateField *field = vmsd->fields;

    if (vmsd->pre_save) {
        ret = vmsd->pre_save(opaque);
        if (ret) {
            g_warning("pre-save failed: %s", vmsd->name);
            return ret;
        }
    }

    while (field->name) {
        if ((field->field_exists &&
             field->field_exists(opaque, version_id)) ||
            (!field->field_exists &&
             field->version_id <= version_id)) {
            void *first_elem = opaque + field->offset;
            int i, n_elems = vmstate_n_elems(opaque, field);
            int size = vmstate_size(opaque, field);

            if (field->flags & VMS_POINTER) {
                first_elem = *(void **)first_elem;
                assert(first_elem || !n_elems || !size);
            }
            for (i = 0; i < n_elems; i++) {
                void *curr_elem = first_elem + size * i;
                ret = 0;

                if (field->flags & VMS_ARRAY_OF_POINTER) {
                    assert(curr_elem);
                    curr_elem = *(void **)curr_elem;
                }
                if (!curr_elem && size) {
                    /* if null pointer write placeholder and do not follow */
                    assert(field->flags & VMS_ARRAY_OF_POINTER);
                    ret = slirp_vmstate_info_nullptr.put(f, curr_elem, size, NULL);
                } else if (field->flags & VMS_STRUCT) {
                    ret = slirp_vmstate_save_state(f, field->vmsd, curr_elem);
                } else if (field->flags & VMS_VSTRUCT) {
                    ret = vmstate_save_state_v(f, field->vmsd, curr_elem,
                                               field->struct_version_id);
                } else {
                    ret = field->info->put(f, curr_elem, size, field);
                }
                if (ret) {
                    g_warning("Save of field %s/%s failed",
                              vmsd->name, field->name);
                    return ret;
                }
            }
        } else {
            if (field->flags & VMS_MUST_EXIST) {
                g_warning("Output state validation failed: %s/%s",
                          vmsd->name, field->name);
                assert(!(field->flags & VMS_MUST_EXIST));
            }
        }
        field++;
    }

    return 0;
}

int slirp_vmstate_save_state(SlirpOStream *f, const VMStateDescription *vmsd,
                             void *opaque)
{
    return vmstate_save_state_v(f, vmsd, opaque, vmsd->version_id);
}

static void vmstate_handle_alloc(void *ptr, VMStateField *field, void *opaque)
{
    if (field->flags & VMS_POINTER && field->flags & VMS_ALLOC) {
        size_t size = vmstate_size(opaque, field);
        size *= vmstate_n_elems(opaque, field);
        if (size) {
            *(void **)ptr = g_malloc(size);
        }
    }
}

int slirp_vmstate_load_state(SlirpIStream *f, const VMStateDescription *vmsd,
                             void *opaque, int version_id)
{
    VMStateField *field = vmsd->fields;
    int ret = 0;

    if (version_id > vmsd->version_id) {
        g_warning("%s: incoming version_id %d is too new "
                  "for local version_id %d",
                  vmsd->name, version_id, vmsd->version_id);
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
                    ret = slirp_vmstate_info_nullptr.get(f, curr_elem, size, NULL);
                } else if (field->flags & VMS_STRUCT) {
                    ret = slirp_vmstate_load_state(f, field->vmsd, curr_elem,
                                             field->vmsd->version_id);
                } else if (field->flags & VMS_VSTRUCT) {
                    ret = slirp_vmstate_load_state(f, field->vmsd, curr_elem,
                                             field->struct_version_id);
                } else {
                    ret = field->info->get(f, curr_elem, size, field);
                }
                if (ret < 0) {
                    g_warning("Failed to load %s:%s", vmsd->name,
                              field->name);
                    return ret;
                }
            }
        } else if (field->flags & VMS_MUST_EXIST) {
            g_warning("Input validation failed: %s/%s",
                      vmsd->name, field->name);
            return -1;
        }
        field++;
    }
    if (vmsd->post_load) {
        ret = vmsd->post_load(opaque, version_id);
    }
    return ret;
}
