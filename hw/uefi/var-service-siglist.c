/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - parse and generate efi signature databases
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/dma.h"

#include "hw/uefi/var-service.h"

/*
 * Add x509 certificate to list (with duplicate check).
 */
static void uefi_vars_siglist_add_x509(uefi_vars_siglist *siglist,
                                       QemuUUID *owner,
                                       void *data, uint64_t size)
{
    uefi_vars_cert *c;

    QTAILQ_FOREACH(c, &siglist->x509, next) {
        if (c->size != size) {
            continue;
        }
        if (memcmp(c->data, data, size) != 0) {
            continue;
        }
        return;
    }

    c = g_malloc(sizeof(*c) + size);
    c->owner = *owner;
    c->size = size;
    memcpy(c->data, data, size);
    QTAILQ_INSERT_TAIL(&siglist->x509, c, next);
}

/*
 * Add sha256 hash to list (with duplicate check).
 */
static void uefi_vars_siglist_add_sha256(uefi_vars_siglist *siglist,
                                         QemuUUID *owner,
                                         void *data)
{
    uefi_vars_hash *h;

    QTAILQ_FOREACH(h, &siglist->sha256, next) {
        if (memcmp(h->data, data, 32) != 0) {
            continue;
        }
        return;
    }

    h = g_malloc(sizeof(*h) + 32);
    h->owner = *owner;
    memcpy(h->data, data, 32);
    QTAILQ_INSERT_TAIL(&siglist->sha256, h, next);
}

void uefi_vars_siglist_init(uefi_vars_siglist *siglist)
{
    memset(siglist, 0, sizeof(*siglist));
    QTAILQ_INIT(&siglist->x509);
    QTAILQ_INIT(&siglist->sha256);
}

void uefi_vars_siglist_free(uefi_vars_siglist *siglist)
{
    uefi_vars_cert *c, *cs;
    uefi_vars_hash *h, *hs;

    QTAILQ_FOREACH_SAFE(c, &siglist->x509, next, cs) {
        QTAILQ_REMOVE(&siglist->x509, c, next);
        g_free(c);
    }
    QTAILQ_FOREACH_SAFE(h, &siglist->sha256, next, hs) {
        QTAILQ_REMOVE(&siglist->sha256, h, next);
        g_free(h);
    }
}

/*
 * Parse UEFI signature list.
 */
void uefi_vars_siglist_parse(uefi_vars_siglist *siglist,
                             void *data, uint64_t size)
{
    efi_siglist *efilist;
    uint64_t start;

    while (size) {
        if (size < sizeof(*efilist)) {
            break;
        }
        efilist = data;
        if (size < efilist->siglist_size) {
            break;
        }

        if (uadd64_overflow(sizeof(*efilist), efilist->header_size, &start)) {
            break;
        }
        if (efilist->sig_size <= sizeof(QemuUUID)) {
            break;
        }

        if (qemu_uuid_is_equal(&efilist->guid_type, &EfiCertX509Guid)) {
            if (start + efilist->sig_size != efilist->siglist_size) {
                break;
            }
            uefi_vars_siglist_add_x509(siglist,
                                       (QemuUUID *)(data + start),
                                       data + start + sizeof(QemuUUID),
                                       efilist->sig_size - sizeof(QemuUUID));

        } else if (qemu_uuid_is_equal(&efilist->guid_type, &EfiCertSha256Guid)) {
            if (efilist->sig_size != sizeof(QemuUUID) + 32) {
                break;
            }
            if (start + efilist->sig_size > efilist->siglist_size) {
                break;
            }
            while (start <= efilist->siglist_size - efilist->sig_size) {
                uefi_vars_siglist_add_sha256(siglist,
                                             (QemuUUID *)(data + start),
                                             data + start + sizeof(QemuUUID));
                start += efilist->sig_size;
            }

        } else {
            QemuUUID be = qemu_uuid_bswap(efilist->guid_type);
            char *str_uuid = qemu_uuid_unparse_strdup(&be);
            warn_report("%s: unknown type (%s)", __func__, str_uuid);
            g_free(str_uuid);
        }

        data += efilist->siglist_size;
        size -= efilist->siglist_size;
    }
}

uint64_t uefi_vars_siglist_blob_size(uefi_vars_siglist *siglist)
{
    uefi_vars_cert *c;
    uefi_vars_hash *h;
    uint64_t size = 0;

    QTAILQ_FOREACH(c, &siglist->x509, next) {
        size += sizeof(efi_siglist) + sizeof(QemuUUID) + c->size;
    }

    if (!QTAILQ_EMPTY(&siglist->sha256)) {
        size += sizeof(efi_siglist);
        QTAILQ_FOREACH(h, &siglist->sha256, next) {
            size += sizeof(QemuUUID) + 32;
        }
    }

    return size;
}

/*
 * Generate UEFI signature list.
 */
void uefi_vars_siglist_blob_generate(uefi_vars_siglist *siglist,
                                     void *data, uint64_t size)
{
    uefi_vars_cert *c;
    uefi_vars_hash *h;
    efi_siglist *efilist;
    uint64_t pos = 0, start;
    uint32_t i;

    QTAILQ_FOREACH(c, &siglist->x509, next) {
        efilist = data + pos;
        efilist->guid_type = EfiCertX509Guid;
        efilist->sig_size = sizeof(QemuUUID) + c->size;
        efilist->header_size = 0;

        start = pos + sizeof(efi_siglist);
        memcpy(data + start,
               &c->owner, sizeof(QemuUUID));
        memcpy(data + start + sizeof(QemuUUID),
               c->data, c->size);

        efilist->siglist_size = sizeof(efi_siglist) + efilist->sig_size;
        pos += efilist->siglist_size;
    }

    if (!QTAILQ_EMPTY(&siglist->sha256)) {
        efilist = data + pos;
        efilist->guid_type = EfiCertSha256Guid;
        efilist->sig_size = sizeof(QemuUUID) + 32;
        efilist->header_size = 0;

        i = 0;
        start = pos + sizeof(efi_siglist);
        QTAILQ_FOREACH(h, &siglist->sha256, next) {
            memcpy(data + start + efilist->sig_size * i,
                   &h->owner, sizeof(QemuUUID));
            memcpy(data + start + efilist->sig_size * i + sizeof(QemuUUID),
                   h->data, 32);
            i++;
        }

        efilist->siglist_size = sizeof(efi_siglist) + efilist->sig_size * i;
        pos += efilist->siglist_size;
    }

    assert(pos == size);
}
