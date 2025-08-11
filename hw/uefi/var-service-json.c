/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - serialize non-volatile varstore from/to json,
 *                    using qapi
 *
 * tools which can read/write these json files:
 *  - https://gitlab.com/kraxel/virt-firmware
 *  - https://github.com/awslabs/python-uefivars
 */
#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

#include "qobject/qobject.h"
#include "qobject/qjson.h"

#include "qapi/dealloc-visitor.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qapi/qapi-types-uefi.h"
#include "qapi/qapi-visit-uefi.h"

static char *generate_hexstr(void *data, size_t len)
{
    static const char hex[] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    uint8_t *src = data;
    char *dest;
    size_t i;

    dest = g_malloc(len * 2 + 1);
    for (i = 0; i < len * 2;) {
        dest[i++] = hex[*src >> 4];
        dest[i++] = hex[*src & 15];
        src++;
    }
    dest[i++] = 0;

    return dest;
}

static UefiVarStore *uefi_vars_to_qapi(uefi_vars_state *uv)
{
    UefiVarStore *vs;
    UefiVariableList **tail;
    UefiVariable *v;
    QemuUUID be;
    uefi_variable *var;

    vs = g_new0(UefiVarStore, 1);
    vs->version = 2;
    tail = &vs->variables;

    QTAILQ_FOREACH(var, &uv->variables, next) {
        if (!(var->attributes & EFI_VARIABLE_NON_VOLATILE)) {
            continue;
        }

        v = g_new0(UefiVariable, 1);
        be = qemu_uuid_bswap(var->guid);
        v->guid = qemu_uuid_unparse_strdup(&be);
        v->name = uefi_ucs2_to_ascii(var->name, var->name_size);
        v->attr = var->attributes;

        v->data = generate_hexstr(var->data, var->data_size);

        if (var->attributes &
            EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) {
            v->time = generate_hexstr(&var->time, sizeof(var->time));
            if (var->digest && var->digest_size) {
                v->digest = generate_hexstr(var->digest, var->digest_size);
            }
        }

        QAPI_LIST_APPEND(tail, v);
    }
    return vs;
}

static unsigned parse_hexchar(char c)
{
    switch (c) {
    case '0' ... '9': return c - '0';
    case 'a' ... 'f': return c - 'a' + 0xa;
    case 'A' ... 'F': return c - 'A' + 0xA;
    default: return 0;
    }
}

static void parse_hexstr(void *dest, char *src, int len)
{
    uint8_t *data = dest;
    size_t i;

    for (i = 0; i < len; i += 2) {
        *(data++) =
            parse_hexchar(src[i]) << 4 |
            parse_hexchar(src[i + 1]);
    }
}

static void uefi_vars_from_qapi(uefi_vars_state *uv, UefiVarStore *vs)
{
    UefiVariableList *item;
    UefiVariable *v;
    QemuUUID be;
    uefi_variable *var;
    uint8_t *data;
    size_t i, len;

    for (item = vs->variables; item != NULL; item = item->next) {
        v = item->value;

        var = g_new0(uefi_variable, 1);
        var->attributes = v->attr;
        qemu_uuid_parse(v->guid, &be);
        var->guid = qemu_uuid_bswap(be);

        len = strlen(v->name);
        var->name_size = len * 2 + 2;
        var->name = g_malloc(var->name_size);
        for (i = 0; i <= len; i++) {
            var->name[i] = v->name[i];
        }

        len = strlen(v->data);
        var->data_size = len / 2;
        var->data = data = g_malloc(var->data_size);
        parse_hexstr(var->data, v->data, len);

        if (v->time && strlen(v->time) == 32) {
            parse_hexstr(&var->time, v->time, 32);
        }

        if (v->digest) {
            len = strlen(v->digest);
            var->digest_size = len / 2;
            var->digest = g_malloc(var->digest_size);
            parse_hexstr(var->digest, v->digest, len);
        }

        QTAILQ_INSERT_TAIL(&uv->variables, var, next);
    }
}

static GString *uefi_vars_to_json(uefi_vars_state *uv)
{
    UefiVarStore *vs = uefi_vars_to_qapi(uv);
    QObject *qobj = NULL;
    Visitor *v;
    GString *gstr;

    v = qobject_output_visitor_new(&qobj);
    if (visit_type_UefiVarStore(v, NULL, &vs, NULL)) {
        visit_complete(v, &qobj);
    }
    visit_free(v);
    qapi_free_UefiVarStore(vs);

    gstr = qobject_to_json_pretty(qobj, true);
    qobject_unref(qobj);

    return gstr;
}

void uefi_vars_json_init(uefi_vars_state *uv, Error **errp)
{
    if (uv->jsonfile) {
        uv->jsonfd = qemu_create(uv->jsonfile, O_RDWR | O_BINARY, 0666, errp);
    }
}

void uefi_vars_json_save(uefi_vars_state *uv)
{
    g_autoptr(GString) gstr = NULL;
    int rc;

    if (uv->jsonfd == -1) {
        return;
    }

    gstr = uefi_vars_to_json(uv);

    rc = lseek(uv->jsonfd, 0, SEEK_SET);
    if (rc < 0) {
        warn_report("%s: lseek error", __func__);
        return;
    }

    rc = ftruncate(uv->jsonfd, 0);
    if (rc != 0) {
        warn_report("%s: ftruncate error", __func__);
        return;
    }

    rc = write(uv->jsonfd, gstr->str, gstr->len);
    if (rc != gstr->len) {
        warn_report("%s: write error", __func__);
        return;
    }

    fsync(uv->jsonfd);
}

void uefi_vars_json_load(uefi_vars_state *uv, Error **errp)
{
    UefiVarStore *vs;
    QObject *qobj;
    Visitor *v;
    char *str;
    ssize_t len;
    int rc;

    if (uv->jsonfd == -1) {
        return;
    }

    len = lseek(uv->jsonfd, 0, SEEK_END);
    if (len < 0) {
        warn_report("%s: lseek error", __func__);
        return;
    }
    if (len == 0) {
        /* empty file */
        return;
    }

    str = g_malloc(len + 1);
    lseek(uv->jsonfd, 0, SEEK_SET);
    rc = read(uv->jsonfd, str, len);
    if (rc != len) {
        warn_report("%s: read error", __func__);
        g_free(str);
        return;
    }
    str[len] = 0;

    qobj = qobject_from_json(str, errp);
    v = qobject_input_visitor_new(qobj);
    visit_type_UefiVarStore(v, NULL, &vs, errp);
    visit_free(v);

    if (!(*errp)) {
        uefi_vars_from_qapi(uv, vs);
        uefi_vars_update_storage(uv);
    }

    qapi_free_UefiVarStore(vs);
    qobject_unref(qobj);
    g_free(str);
}
