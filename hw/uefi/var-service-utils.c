/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - helper functions for ucs2 strings and tracing
 */
#include "qemu/osdep.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

#include "trace.h"

/* ------------------------------------------------------------------ */

/*
 * string helper functions.
 *
 * Most of the time uefi ucs2 strings are NULL-terminated, except
 * sometimes when they are not (for example in variable policies).
 */

gboolean uefi_str_is_valid(const uint16_t *str, size_t len,
                           gboolean must_be_null_terminated)
{
    size_t pos = 0;

    for (;;) {
        if (pos == len) {
            if (must_be_null_terminated) {
                return false;
            } else {
                return true;
            }
        }
        switch (str[pos]) {
        case 0:
            /* end of string */
            return true;
        case 0xd800 ... 0xdfff:
            /* reject surrogates */
            return false;
        default:
            /* char is good, check next */
            break;
        }
        pos++;
    }
}

size_t uefi_strlen(const uint16_t *str, size_t len)
{
    size_t pos = 0;

    for (;;) {
        if (pos == len) {
            return pos;
        }
        if (str[pos] == 0) {
            return pos;
        }
        pos++;
    }
}

gboolean uefi_str_equal_ex(const uint16_t *a, size_t alen,
                           const uint16_t *b, size_t blen,
                           gboolean wildcards_in_a)
{
    size_t pos = 0;

    alen = alen / 2;
    blen = blen / 2;
    for (;;) {
        if (pos == alen && pos == blen) {
            return true;
        }
        if (pos == alen && b[pos] == 0) {
            return true;
        }
        if (pos == blen && a[pos] == 0) {
            return true;
        }
        if (pos == alen || pos == blen) {
            return false;
        }
        if (a[pos] == 0 && b[pos] == 0) {
            return true;
        }

        if (wildcards_in_a && a[pos] == '#') {
            if (!isxdigit(b[pos])) {
                return false;
            }
        } else {
            if (a[pos] != b[pos]) {
                return false;
            }
        }
        pos++;
    }
}

gboolean uefi_str_equal(const uint16_t *a, size_t alen,
                        const uint16_t *b, size_t blen)
{
    return uefi_str_equal_ex(a, alen, b, blen, false);
}

char *uefi_ucs2_to_ascii(const uint16_t *ucs2, uint64_t ucs2_size)
{
    char *str = g_malloc0(ucs2_size / 2 + 1);
    int i;

    for (i = 0; i * 2 < ucs2_size; i++) {
        if (ucs2[i] == 0) {
            break;
        }
        if (ucs2[i] < 128) {
            str[i] = ucs2[i];
        } else {
            str[i] = '?';
        }
    }
    str[i] = 0;
    return str;
}

/* ------------------------------------------------------------------ */
/* time helper functions                                              */

int uefi_time_compare(efi_time *a, efi_time *b)
{
    if (a->year < b->year) {
        return -1;
    }
    if (a->year > b->year) {
        return 1;
    }

    if (a->month < b->month) {
        return -1;
    }
    if (a->month > b->month) {
        return 1;
    }

    if (a->day < b->day) {
        return -1;
    }
    if (a->day > b->day) {
        return 1;
    }

    if (a->hour < b->hour) {
        return -1;
    }
    if (a->hour > b->hour) {
        return 1;
    }

    if (a->minute < b->minute) {
        return -1;
    }
    if (a->minute > b->minute) {
        return 1;
    }

    if (a->second < b->second) {
        return -1;
    }
    if (a->second > b->second) {
        return 1;
    }

    if (a->nanosecond < b->nanosecond) {
        return -1;
    }
    if (a->nanosecond > b->nanosecond) {
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* tracing helper functions                                           */

void uefi_trace_variable(const char *action, QemuUUID guid,
                         const uint16_t *name, uint64_t name_size)
{
    QemuUUID be = qemu_uuid_bswap(guid);
    char *str_uuid = qemu_uuid_unparse_strdup(&be);
    char *str_name = uefi_ucs2_to_ascii(name, name_size);

    trace_uefi_variable(action, str_name, name_size, str_uuid);

    g_free(str_name);
    g_free(str_uuid);
}

void uefi_trace_status(const char *action, efi_status status)
{
    switch (status) {
    case EFI_SUCCESS:
        trace_uefi_status(action, "success");
        break;
    case EFI_INVALID_PARAMETER:
        trace_uefi_status(action, "invalid parameter");
        break;
    case EFI_UNSUPPORTED:
        trace_uefi_status(action, "unsupported");
        break;
    case EFI_BAD_BUFFER_SIZE:
        trace_uefi_status(action, "bad buffer size");
        break;
    case EFI_BUFFER_TOO_SMALL:
        trace_uefi_status(action, "buffer too small");
        break;
    case EFI_WRITE_PROTECTED:
        trace_uefi_status(action, "write protected");
        break;
    case EFI_OUT_OF_RESOURCES:
        trace_uefi_status(action, "out of resources");
        break;
    case EFI_NOT_FOUND:
        trace_uefi_status(action, "not found");
        break;
    case EFI_ACCESS_DENIED:
        trace_uefi_status(action, "access denied");
        break;
    case EFI_ALREADY_STARTED:
        trace_uefi_status(action, "already started");
        break;
    case EFI_SECURITY_VIOLATION:
        trace_uefi_status(action, "security violation");
        break;
    default:
        trace_uefi_status(action, "unknown error");
        break;
    }
}
