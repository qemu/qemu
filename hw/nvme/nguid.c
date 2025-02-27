/*
 *  QEMU NVMe NGUID functions
 *
 * Copyright 2024 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "qapi/visitor.h"
#include "qemu/ctype.h"
#include "nvme.h"

#define NGUID_SEPARATOR '-'

#define NGUID_VALUE_AUTO "auto"

#define NGUID_FMT              \
    "%02hhx%02hhx%02hhx%02hhx" \
    "%02hhx%02hhx%02hhx%02hhx" \
    "%02hhx%02hhx%02hhx%02hhx" \
    "%02hhx%02hhx%02hhx%02hhx"

#define NGUID_STR_LEN (2 * NGUID_LEN + 1)

bool nvme_nguid_is_null(const NvmeNGUID *nguid)
{
    static NvmeNGUID null_nguid;
    return memcmp(nguid, &null_nguid, sizeof(NvmeNGUID)) == 0;
}

static void nvme_nguid_generate(NvmeNGUID *out)
{
    int i;
    uint32_t x;

    QEMU_BUILD_BUG_ON((NGUID_LEN % sizeof(x)) != 0);

    for (i = 0; i < NGUID_LEN; i += sizeof(x)) {
        x = g_random_int();
        memcpy(&out->data[i], &x, sizeof(x));
    }
}

/*
 * The Linux Kernel typically prints the NGUID of an NVMe namespace using the
 * same format as the UUID. For instance:
 *
 * $ cat /sys/class/block/nvme0n1/nguid
 * e9accd3b-8390-4e13-167c-f0593437f57d
 *
 * When there is no UUID but there is NGUID the Kernel will print the NGUID as
 * wwid and it won't use the UUID format:
 *
 * $ cat /sys/class/block/nvme0n1/wwid
 * eui.e9accd3b83904e13167cf0593437f57d
 *
 * The NGUID has different fields compared to the UUID, so the grouping used in
 * the UUID format has no relation with the 3 fields of the NGUID.
 *
 * This implementation won't expect a strict format as the UUID one and instead
 * it will admit any string of hexadecimal digits. Byte groups could be created
 * using the '-' separator. The number of bytes needs to be exactly 16 and the
 * separator '-' has to be exactly in a byte boundary. The following are
 * examples of accepted formats for the NGUID string:
 *
 * nguid="e9accd3b-8390-4e13-167c-f0593437f57d"
 * nguid="e9accd3b83904e13167cf0593437f57d"
 * nguid="FEDCBA9876543210-ABCDEF-0123456789"
 */
static bool nvme_nguid_is_valid(const char *str)
{
    int i;
    int digit_count = 0;

    for (i = 0; i < strlen(str); i++) {
        const char c = str[i];
        if (qemu_isxdigit(c)) {
            digit_count++;
            continue;
        }
        if (c == NGUID_SEPARATOR) {
            /*
             * We need to make sure the separator is in a byte boundary, the
             * string does not start with the separator and they are not back to
             * back "--".
             */
            if ((i > 0) && (str[i - 1] != NGUID_SEPARATOR) &&
                (digit_count % 2) == 0) {
                continue;
            }
        }
        return false;
    }
    /*
     * The string should have the correct byte length and not finish with the
     * separator
     */
    return (digit_count == (2 * NGUID_LEN)) && (str[i - 1] != NGUID_SEPARATOR);
}

static int nvme_nguid_parse(const char *str, NvmeNGUID *nguid)
{
    uint8_t *id = &nguid->data[0];
    int ret = 0;
    int i;
    const char *ptr = str;

    if (!nvme_nguid_is_valid(str)) {
        return -1;
    }

    for (i = 0; i < NGUID_LEN; i++) {
        ret = sscanf(ptr, "%02hhx", &id[i]);
        if (ret != 1) {
            return -1;
        }
        ptr += 2;
        if (*ptr == NGUID_SEPARATOR) {
            ptr++;
        }
    }

    return 0;
}

/*
 * When converted back to string this implementation will use a raw hex number
 * with no separators, for instance:
 *
 * "e9accd3b83904e13167cf0593437f57d"
 */
static void nvme_nguid_stringify(const NvmeNGUID *nguid, char *out)
{
    const uint8_t *id = &nguid->data[0];
    snprintf(out, NGUID_STR_LEN, NGUID_FMT,
             id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
             id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
}

static void get_nguid(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;
    NvmeNGUID *nguid = object_field_prop_ptr(obj, prop);
    char buffer[NGUID_STR_LEN];
    char *p = buffer;

    nvme_nguid_stringify(nguid, buffer);

    visit_type_str(v, name, &p, errp);
}

static void set_nguid(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;
    NvmeNGUID *nguid = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    if (!strcmp(str, NGUID_VALUE_AUTO)) {
        nvme_nguid_generate(nguid);
    } else if (nvme_nguid_parse(str, nguid) < 0) {
        error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
    }
    g_free(str);
}

const PropertyInfo qdev_prop_nguid = {
    .type  = "str",
    .description =
        "NGUID or \"" NGUID_VALUE_AUTO "\" for random value",
    .get   = get_nguid,
    .set   = set_nguid,
};
