/*
 * TPM utility functions
 *
 *  Copyright (c) 2010 - 2015 IBM Corporation
 *  Authors:
 *    Stefan Berger <stefanb@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "tpm_int.h"
#include "system/memory.h"
#include "hw/qdev-properties.h"
#include "system/tpm_backend.h"
#include "system/tpm_util.h"
#include "trace.h"

/* tpm backend property */

static void get_tpm(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    TPMBackend **be = object_field_prop_ptr(obj, opaque);
    char *p;

    p = g_strdup(*be ? (*be)->id : "");
    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_tpm(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    const Property *prop = opaque;
    TPMBackend *s, **be = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    s = qemu_find_tpm_be(str);
    if (s == NULL) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(obj), name, str);
    } else if (tpm_backend_init(s, TPM_IF(obj), errp) == 0) {
        *be = s; /* weak reference, avoid cyclic ref */
    }
    g_free(str);
}

static void release_tpm(Object *obj, const char *name, void *opaque)
{
    const Property *prop = opaque;
    TPMBackend **be = object_field_prop_ptr(obj, prop);

    if (*be) {
        tpm_backend_reset(*be);
    }
}

const PropertyInfo qdev_prop_tpm = {
    .type  = "str",
    .description = "ID of a tpm to use as a backend",
    .get   = get_tpm,
    .set   = set_tpm,
    .release = release_tpm,
};

/*
 * Write an error message in the given output buffer.
 */
void tpm_util_write_fatal_error_response(uint8_t *out, uint32_t out_len)
{
    if (out_len >= sizeof(struct tpm_resp_hdr)) {
        tpm_cmd_set_tag(out, TPM_TAG_RSP_COMMAND);
        tpm_cmd_set_size(out, sizeof(struct tpm_resp_hdr));
        tpm_cmd_set_error(out, TPM_FAIL);
    }
}

bool tpm_util_is_selftest(const uint8_t *in, uint32_t in_len)
{
    if (in_len >= sizeof(struct tpm_req_hdr)) {
        return tpm_cmd_get_ordinal(in) == TPM_ORD_ContinueSelfTest;
    }

    return false;
}

/*
 * Send request to a TPM device. We expect a response within one second.
 */
static int tpm_util_request(int fd,
                            const void *request,
                            size_t requestlen,
                            void *response,
                            size_t responselen)
{
    GPollFD fds[1] = { {.fd = fd, .events = G_IO_IN } };
    int n;

    n = write(fd, request, requestlen);
    if (n < 0) {
        return -errno;
    }
    if (n != requestlen) {
        return -EFAULT;
    }

    /* wait for a second */
    n = RETRY_ON_EINTR(g_poll(fds, 1, 1000));
    if (n != 1) {
        return -errno;
    }

    n = read(fd, response, responselen);
    if (n < sizeof(struct tpm_resp_hdr)) {
        return -EFAULT;
    }

    /* check the header */
    if (tpm_cmd_get_size(response) != n) {
        return -EMSGSIZE;
    }

    return 0;
}

/*
 * A basic test of a TPM device. We expect a well formatted response header
 * (error response is fine).
 */
static int tpm_util_test(int fd,
                         const void *request,
                         size_t requestlen,
                         uint16_t *return_tag)
{
    char buf[1024];
    ssize_t ret;

    ret = tpm_util_request(fd, request, requestlen,
                           buf, sizeof(buf));
    if (ret < 0) {
        return ret;
    }

    *return_tag = tpm_cmd_get_tag(buf);

    return 0;
}

/*
 * Probe for the TPM device in the back
 * Returns 0 on success with the version of the probed TPM set, 1 on failure.
 */
int tpm_util_test_tpmdev(int tpm_fd, TPMVersion *tpm_version)
{
    /*
     * Sending a TPM1.2 command to a TPM2 should return a TPM1.2
     * header (tag = 0xc4) and error code (TPM_BADTAG = 0x1e)
     *
     * Sending a TPM2 command to a TPM 2 will give a TPM 2 tag in the
     * header.
     * Sending a TPM2 command to a TPM 1.2 will give a TPM 1.2 tag
     * in the header and an error code.
     */
    const struct tpm_req_hdr test_req = {
        .tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
        .len = cpu_to_be32(sizeof(test_req)),
        .ordinal = cpu_to_be32(TPM_ORD_GetTicks),
    };

    const struct tpm_req_hdr test_req_tpm2 = {
        .tag = cpu_to_be16(TPM2_ST_NO_SESSIONS),
        .len = cpu_to_be32(sizeof(test_req_tpm2)),
        .ordinal = cpu_to_be32(TPM2_CC_ReadClock),
    };
    uint16_t return_tag;
    int ret;

    /* Send TPM 2 command */
    ret = tpm_util_test(tpm_fd, &test_req_tpm2,
                        sizeof(test_req_tpm2), &return_tag);
    /* TPM 2 would respond with a tag of TPM2_ST_NO_SESSIONS */
    if (!ret && return_tag == TPM2_ST_NO_SESSIONS) {
        *tpm_version = TPM_VERSION_2_0;
        return 0;
    }

    /* Send TPM 1.2 command */
    ret = tpm_util_test(tpm_fd, &test_req,
                        sizeof(test_req), &return_tag);
    if (!ret && return_tag == TPM_TAG_RSP_COMMAND) {
        *tpm_version = TPM_VERSION_1_2;
        /* this is a TPM 1.2 */
        return 0;
    }

    *tpm_version = TPM_VERSION_UNSPEC;

    return 1;
}

int tpm_util_get_buffer_size(int tpm_fd, TPMVersion tpm_version,
                             size_t *buffersize)
{
    int ret;

    switch (tpm_version) {
    case TPM_VERSION_1_2: {
        const struct tpm_req_get_buffer_size {
            struct tpm_req_hdr hdr;
            uint32_t capability;
            uint32_t len;
            uint32_t subcap;
        } QEMU_PACKED tpm_get_buffer_size = {
            .hdr = {
                .tag = cpu_to_be16(TPM_TAG_RQU_COMMAND),
                .len = cpu_to_be32(sizeof(tpm_get_buffer_size)),
                .ordinal = cpu_to_be32(TPM_ORD_GetCapability),
            },
            .capability = cpu_to_be32(TPM_CAP_PROPERTY),
            .len = cpu_to_be32(sizeof(uint32_t)),
            .subcap = cpu_to_be32(TPM_CAP_PROP_INPUT_BUFFER),
        };
        struct tpm_resp_get_buffer_size {
            struct tpm_resp_hdr hdr;
            uint32_t len;
            uint32_t buffersize;
        } QEMU_PACKED tpm_resp;

        ret = tpm_util_request(tpm_fd, &tpm_get_buffer_size,
                               sizeof(tpm_get_buffer_size),
                               &tpm_resp, sizeof(tpm_resp));
        if (ret < 0) {
            return ret;
        }

        if (be32_to_cpu(tpm_resp.hdr.len) != sizeof(tpm_resp) ||
            be32_to_cpu(tpm_resp.len) != sizeof(uint32_t)) {
            trace_tpm_util_get_buffer_size_hdr_len(
                be32_to_cpu(tpm_resp.hdr.len),
                sizeof(tpm_resp));
            trace_tpm_util_get_buffer_size_len(be32_to_cpu(tpm_resp.len),
                                               sizeof(uint32_t));
            error_report("tpm_util: Got unexpected response to "
                         "TPM_GetCapability; errcode: 0x%x",
                         be32_to_cpu(tpm_resp.hdr.errcode));
            return -EFAULT;
        }
        *buffersize = be32_to_cpu(tpm_resp.buffersize);
        break;
    }
    case TPM_VERSION_2_0: {
        const struct tpm2_req_get_buffer_size {
            struct tpm_req_hdr hdr;
            uint32_t capability;
            uint32_t property;
            uint32_t count;
        } QEMU_PACKED tpm2_get_buffer_size = {
            .hdr = {
                .tag = cpu_to_be16(TPM2_ST_NO_SESSIONS),
                .len = cpu_to_be32(sizeof(tpm2_get_buffer_size)),
                .ordinal = cpu_to_be32(TPM2_CC_GetCapability),
            },
            .capability = cpu_to_be32(TPM2_CAP_TPM_PROPERTIES),
            .property = cpu_to_be32(TPM2_PT_MAX_COMMAND_SIZE),
            .count = cpu_to_be32(2), /* also get TPM2_PT_MAX_RESPONSE_SIZE */
        };
        struct tpm2_resp_get_buffer_size {
            struct tpm_resp_hdr hdr;
            uint8_t more;
            uint32_t capability;
            uint32_t count;
            uint32_t property1;
            uint32_t value1;
            uint32_t property2;
            uint32_t value2;
        } QEMU_PACKED tpm2_resp;

        ret = tpm_util_request(tpm_fd, &tpm2_get_buffer_size,
                               sizeof(tpm2_get_buffer_size),
                               &tpm2_resp, sizeof(tpm2_resp));
        if (ret < 0) {
            return ret;
        }

        if (be32_to_cpu(tpm2_resp.hdr.len) != sizeof(tpm2_resp) ||
            be32_to_cpu(tpm2_resp.count) != 2) {
            trace_tpm_util_get_buffer_size_hdr_len2(
                be32_to_cpu(tpm2_resp.hdr.len),
                sizeof(tpm2_resp));
            trace_tpm_util_get_buffer_size_len2(
                be32_to_cpu(tpm2_resp.count), 2);
            error_report("tpm_util: Got unexpected response to "
                         "TPM2_GetCapability; errcode: 0x%x",
                         be32_to_cpu(tpm2_resp.hdr.errcode));
            return -EFAULT;
        }
        *buffersize = MAX(be32_to_cpu(tpm2_resp.value1),
                          be32_to_cpu(tpm2_resp.value2));
        break;
    }
    case TPM_VERSION_UNSPEC:
        return -EFAULT;
    }

    trace_tpm_util_get_buffer_size(*buffersize);

    return 0;
}

void tpm_sized_buffer_reset(TPMSizedBuffer *tsb)
{
    g_free(tsb->buffer);
    tsb->buffer = NULL;
    tsb->size = 0;
}

void tpm_util_show_buffer(const unsigned char *buffer,
                          size_t buffer_size, const char *string)
{
    g_autoptr(GString) str = NULL;
    size_t len, i, l;

    if (!trace_event_get_state_backends(TRACE_TPM_UTIL_SHOW_BUFFER_CONTENT)) {
        return;
    }
    len = MIN(tpm_cmd_get_size(buffer), buffer_size);
    trace_tpm_util_show_buffer_header(string, len);

    for (i = 0; i < len; i += l) {
        if (str) {
            g_string_append_c(str, '\n');
        }
        l = MIN(len, 16);
        str = qemu_hexdump_line(str, buffer, l, 1, 0);
    }

    g_string_ascii_up(str);
    trace_tpm_util_show_buffer_content(str->str);
}
