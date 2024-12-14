/*
 * EIF (Enclave Image Format) related helpers
 *
 * Copyright (c) 2024 Dorjoy Chowdhury <dorjoychy111@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.  See the COPYING file in the
 * top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "crypto/hash.h"
#include "crypto/x509-utils.h"
#include <zlib.h> /* for crc32 */
#include <cbor.h>

#include "hw/core/eif.h"

#define MAX_SECTIONS 32

/* members are ordered according to field order in .eif file */
typedef struct EifHeader {
    uint8_t  magic[4]; /* must be .eif in ascii i.e., [46, 101, 105, 102] */
    uint16_t version;
    uint16_t flags;
    uint64_t default_memory;
    uint64_t default_cpus;
    uint16_t reserved;
    uint16_t section_cnt;
    uint64_t section_offsets[MAX_SECTIONS];
    uint64_t section_sizes[MAX_SECTIONS];
    uint32_t unused;
    uint32_t eif_crc32;
} QEMU_PACKED EifHeader;

/* members are ordered according to field order in .eif file */
typedef struct EifSectionHeader {
    /*
     * 0 = invalid, 1 = kernel, 2 = cmdline, 3 = ramdisk, 4 = signature,
     * 5 = metadata
     */
    uint16_t section_type;
    uint16_t flags;
    uint64_t section_size;
} QEMU_PACKED EifSectionHeader;

enum EifSectionTypes {
    EIF_SECTION_INVALID = 0,
    EIF_SECTION_KERNEL = 1,
    EIF_SECTION_CMDLINE = 2,
    EIF_SECTION_RAMDISK = 3,
    EIF_SECTION_SIGNATURE = 4,
    EIF_SECTION_METADATA = 5,
    EIF_SECTION_MAX = 6,
};

static const char *section_type_to_string(uint16_t type)
{
    const char *str;
    switch (type) {
    case EIF_SECTION_INVALID:
        str = "invalid";
        break;
    case EIF_SECTION_KERNEL:
        str = "kernel";
        break;
    case EIF_SECTION_CMDLINE:
        str = "cmdline";
        break;
    case EIF_SECTION_RAMDISK:
        str = "ramdisk";
        break;
    case EIF_SECTION_SIGNATURE:
        str = "signature";
        break;
    case EIF_SECTION_METADATA:
        str = "metadata";
        break;
    default:
        str = "unknown";
        break;
    }

    return str;
}

static bool read_eif_header(FILE *f, EifHeader *header, uint32_t *crc,
                            Error **errp)
{
    size_t got;
    size_t header_size = sizeof(*header);

    got = fread(header, 1, header_size, f);
    if (got != header_size) {
        error_setg(errp, "Failed to read EIF header");
        return false;
    }

    if (memcmp(header->magic, ".eif", 4) != 0) {
        error_setg(errp, "Invalid EIF image. Magic mismatch.");
        return false;
    }

    /* Exclude header->eif_crc32 field from CRC calculation */
    *crc = crc32(*crc, (uint8_t *)header, header_size - 4);

    header->version = be16_to_cpu(header->version);
    header->flags = be16_to_cpu(header->flags);
    header->default_memory = be64_to_cpu(header->default_memory);
    header->default_cpus = be64_to_cpu(header->default_cpus);
    header->reserved = be16_to_cpu(header->reserved);
    header->section_cnt = be16_to_cpu(header->section_cnt);

    for (int i = 0; i < MAX_SECTIONS; ++i) {
        header->section_offsets[i] = be64_to_cpu(header->section_offsets[i]);
    }

    for (int i = 0; i < MAX_SECTIONS; ++i) {
        header->section_sizes[i] = be64_to_cpu(header->section_sizes[i]);
        if (header->section_sizes[i] > SSIZE_MAX) {
            error_setg(errp, "Invalid EIF image. Section size out of bounds");
            return false;
        }
    }

    header->unused = be32_to_cpu(header->unused);
    header->eif_crc32 = be32_to_cpu(header->eif_crc32);
    return true;
}

static bool read_eif_section_header(FILE *f, EifSectionHeader *section_header,
                                    uint32_t *crc, Error **errp)
{
    size_t got;
    size_t section_header_size = sizeof(*section_header);

    got = fread(section_header, 1, section_header_size, f);
    if (got != section_header_size) {
        error_setg(errp, "Failed to read EIF section header");
        return false;
    }

    *crc = crc32(*crc, (uint8_t *)section_header, section_header_size);

    section_header->section_type = be16_to_cpu(section_header->section_type);
    section_header->flags = be16_to_cpu(section_header->flags);
    section_header->section_size = be64_to_cpu(section_header->section_size);
    return true;
}

/*
 * Upon success, the caller is responsible for unlinking and freeing *tmp_path.
 */
static bool get_tmp_file(const char *template, char **tmp_path, Error **errp)
{
    int tmp_fd;

    *tmp_path = NULL;
    tmp_fd = g_file_open_tmp(template, tmp_path, NULL);
    if (tmp_fd < 0 || *tmp_path == NULL) {
        error_setg(errp, "Failed to create temporary file for template %s",
                   template);
        return false;
    }

    close(tmp_fd);
    return true;
}

static void safe_fclose(FILE *f)
{
    if (f) {
        fclose(f);
    }
}

static void safe_unlink(char *f)
{
    if (f) {
        unlink(f);
    }
}

/*
 * Upon success, the caller is reponsible for unlinking and freeing *kernel_path
 */
static bool read_eif_kernel(FILE *f, uint64_t size, char **kernel_path,
                            QCryptoHash *hash0, QCryptoHash *hash1,
                            uint32_t *crc, Error **errp)
{
    size_t got;
    FILE *tmp_file = NULL;
    uint8_t *kernel = g_try_malloc(size);
    if (!kernel) {
        error_setg(errp, "Out of memory reading kernel section");
        goto cleanup;
    }

    *kernel_path = NULL;
    if (!get_tmp_file("eif-kernel-XXXXXX", kernel_path, errp)) {
        goto cleanup;
    }

    tmp_file = fopen(*kernel_path, "wb");
    if (tmp_file == NULL) {
        error_setg_errno(errp, errno, "Failed to open temporary file %s",
                         *kernel_path);
        goto cleanup;
    }

    got = fread(kernel, 1, size, f);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF kernel section data");
        goto cleanup;
    }

    got = fwrite(kernel, 1, size, tmp_file);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to write EIF kernel section data to temporary"
                   " file");
        goto cleanup;
    }

    *crc = crc32(*crc, kernel, size);
    if (qcrypto_hash_update(hash0, (char *)kernel, size, errp) != 0 ||
        qcrypto_hash_update(hash1, (char *)kernel, size, errp) != 0) {
        goto cleanup;
    }
    g_free(kernel);
    fclose(tmp_file);

    return true;

 cleanup:
    safe_fclose(tmp_file);

    safe_unlink(*kernel_path);
    g_free(*kernel_path);
    *kernel_path = NULL;

    g_free(kernel);
    return false;
}

static bool read_eif_cmdline(FILE *f, uint64_t size, char *cmdline,
                             QCryptoHash *hash0, QCryptoHash *hash1,
                             uint32_t *crc, Error **errp)
{
    size_t got = fread(cmdline, 1, size, f);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF cmdline section data");
        return false;
    }

    *crc = crc32(*crc, (uint8_t *)cmdline, size);
    if (qcrypto_hash_update(hash0, cmdline, size, errp) != 0 ||
        qcrypto_hash_update(hash1, cmdline, size, errp) != 0) {
        return false;
    }
    return true;
}

static bool read_eif_ramdisk(FILE *eif, FILE *initrd, uint64_t size,
                             QCryptoHash *hash0, QCryptoHash *h, uint32_t *crc,
                             Error **errp)
{
    size_t got;
    bool ret = false;
    uint8_t *ramdisk = g_try_malloc(size);
    if (!ramdisk) {
        error_setg(errp, "Out of memory reading initrd section");
        goto cleanup;
    }

    got = fread(ramdisk, 1, size, eif);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF ramdisk section data");
        goto cleanup;
    }

    got = fwrite(ramdisk, 1, size, initrd);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to write EIF ramdisk data to temporary file");
        goto cleanup;
    }

    *crc = crc32(*crc, ramdisk, size);
    if (qcrypto_hash_update(hash0, (char *)ramdisk, size, errp) != 0 ||
        qcrypto_hash_update(h, (char *)ramdisk, size, errp) != 0) {
        goto cleanup;
    }
    ret = true;

 cleanup:
    g_free(ramdisk);
    return ret;
}

static bool get_signature_fingerprint_sha384(FILE *eif, uint64_t size,
                                             uint8_t *sha384,
                                             uint32_t *crc,
                                             Error **errp)
{
    size_t got;
    g_autofree uint8_t *sig = NULL;
    g_autofree uint8_t *cert = NULL;
    cbor_item_t *item = NULL;
    cbor_item_t *pcr0 = NULL;
    size_t len;
    size_t hash_len = QCRYPTO_HASH_DIGEST_LEN_SHA384;
    struct cbor_pair *pair;
    struct cbor_load_result result;
    bool ret = false;

    sig = g_try_malloc(size);
    if (!sig) {
        error_setg(errp, "Out of memory reading signature section");
        goto cleanup;
    }

    got = fread(sig, 1, size, eif);
    if ((uint64_t) got != size) {
        error_setg(errp, "Failed to read EIF signature section data");
        goto cleanup;
    }

    *crc = crc32(*crc, sig, size);

    item = cbor_load(sig, size, &result);
    if (!item || result.error.code != CBOR_ERR_NONE) {
        error_setg(errp, "Failed to load signature section data as CBOR");
        goto cleanup;
    }
    if (!cbor_isa_array(item) || cbor_array_size(item) < 1) {
        error_setg(errp, "Invalid signature CBOR");
        goto cleanup;
    }
    pcr0 = cbor_array_get(item, 0);
    if (!pcr0) {
        error_setg(errp, "Failed to get PCR0 signature");
        goto cleanup;
    }
    if (!cbor_isa_map(pcr0) || cbor_map_size(pcr0) != 2) {
        error_setg(errp, "Invalid signature CBOR");
        goto cleanup;
    }
    pair = cbor_map_handle(pcr0);
    if (!cbor_isa_string(pair->key) || cbor_string_length(pair->key) != 19 ||
        memcmp(cbor_string_handle(pair->key), "signing_certificate", 19) != 0) {
        error_setg(errp, "Invalid signautre CBOR");
        goto cleanup;
    }
    if (!cbor_isa_array(pair->value)) {
        error_setg(errp, "Invalid signature CBOR");
        goto cleanup;
    }
    len = cbor_array_size(pair->value);
    if (len == 0) {
        error_setg(errp, "Invalid signature CBOR");
        goto cleanup;
    }
    cert = g_try_malloc(len);
    if (!cert) {
        error_setg(errp, "Out of memory reading signature section");
        goto cleanup;
    }

    for (int i = 0; i < len; ++i) {
        cbor_item_t *tmp = cbor_array_get(pair->value, i);
        if (!tmp) {
            error_setg(errp, "Invalid signature CBOR");
            goto cleanup;
        }
        if (!cbor_isa_uint(tmp) || cbor_int_get_width(tmp) != CBOR_INT_8) {
            cbor_decref(&tmp);
            error_setg(errp, "Invalid signature CBOR");
            goto cleanup;
        }
        cert[i] = cbor_get_uint8(tmp);
        cbor_decref(&tmp);
    }

    if (qcrypto_get_x509_cert_fingerprint(cert, len, QCRYPTO_HASH_ALGO_SHA384,
                                          sha384, &hash_len, errp)) {
        goto cleanup;
    }

    ret = true;

 cleanup:
    if (pcr0) {
        cbor_decref(&pcr0);
    }
    if (item) {
        cbor_decref(&item);
    }
    return ret;
}

/* Expects file to have offset 0 before this function is called */
static long get_file_size(FILE *f, Error **errp)
{
    long size;

    if (fseek(f, 0, SEEK_END) != 0) {
        error_setg_errno(errp, errno, "Failed to seek to the end of file");
        return -1;
    }

    size = ftell(f);
    if (size == -1) {
        error_setg_errno(errp, errno, "Failed to get offset");
        return -1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        error_setg_errno(errp, errno, "Failed to seek back to the start");
        return -1;
    }

    return size;
}

static bool get_SHA384_hash(QCryptoHash *h, uint8_t *hash, Error **errp)
{
    size_t hash_len = QCRYPTO_HASH_DIGEST_LEN_SHA384;
    return qcrypto_hash_finalize_bytes(h, &hash, &hash_len, errp) == 0;
}

/*
 * Upon success, the caller is reponsible for unlinking and freeing
 * *kernel_path, *initrd_path and freeing *cmdline.
 */
bool read_eif_file(const char *eif_path, const char *machine_initrd,
                   char **kernel_path, char **initrd_path, char **cmdline,
                   uint8_t *image_hash, uint8_t *bootstrap_hash,
                   uint8_t *app_hash, uint8_t *fingerprint_hash,
                   bool *signature_found, Error **errp)
{
    FILE *f = NULL;
    FILE *machine_initrd_f = NULL;
    FILE *initrd_path_f = NULL;
    long machine_initrd_size;
    uint32_t crc = 0;
    EifHeader eif_header;
    bool seen_sections[EIF_SECTION_MAX] = {false};
    /* kernel + ramdisks + cmdline SHA384 hash */
    g_autoptr(QCryptoHash) hash0 = NULL;
    /* kernel + boot ramdisk + cmdline SHA384 hash */
    g_autoptr(QCryptoHash) hash1 = NULL;
    /* application ramdisk(s) SHA384 hash */
    g_autoptr(QCryptoHash) hash2 = NULL;

    *signature_found = false;
    *kernel_path = *initrd_path = *cmdline = NULL;

    hash0 = qcrypto_hash_new(QCRYPTO_HASH_ALGO_SHA384, errp);
    if (!hash0) {
        goto cleanup;
    }
    hash1 = qcrypto_hash_new(QCRYPTO_HASH_ALGO_SHA384, errp);
    if (!hash1) {
        goto cleanup;
    }
    hash2 = qcrypto_hash_new(QCRYPTO_HASH_ALGO_SHA384, errp);
    if (!hash2) {
        goto cleanup;
    }

    f = fopen(eif_path, "rb");
    if (f == NULL) {
        error_setg_errno(errp, errno, "Failed to open %s", eif_path);
        goto cleanup;
    }

    if (!read_eif_header(f, &eif_header, &crc, errp)) {
        goto cleanup;
    }

    if (eif_header.version < 4) {
        error_setg(errp, "Expected EIF version 4 or greater");
        goto cleanup;
    }

    if (eif_header.flags != 0) {
        error_setg(errp, "Expected EIF flags to be 0");
        goto cleanup;
    }

    if (eif_header.section_cnt > MAX_SECTIONS) {
        error_setg(errp, "EIF header section count must not be greater than "
                   "%d but found %d", MAX_SECTIONS, eif_header.section_cnt);
        goto cleanup;
    }

    for (int i = 0; i < eif_header.section_cnt; ++i) {
        EifSectionHeader hdr;
        uint16_t section_type;

        if (eif_header.section_offsets[i] > OFF_MAX) {
            error_setg(errp, "Invalid EIF image. Section offset out of bounds");
            goto cleanup;
        }
        if (fseek(f, eif_header.section_offsets[i], SEEK_SET) != 0) {
            error_setg_errno(errp, errno, "Failed to offset to %" PRIu64 " in EIF file",
                             eif_header.section_offsets[i]);
            goto cleanup;
        }

        if (!read_eif_section_header(f, &hdr, &crc, errp)) {
            goto cleanup;
        }

        if (hdr.flags != 0) {
            error_setg(errp, "Expected EIF section header flags to be 0");
            goto cleanup;
        }

        if (eif_header.section_sizes[i] != hdr.section_size) {
            error_setg(errp, "EIF section size mismatch between header and "
                       "section header: header %" PRIu64 ", section header %" PRIu64,
                       eif_header.section_sizes[i],
                       hdr.section_size);
            goto cleanup;
        }

        section_type = hdr.section_type;

        switch (section_type) {
        case EIF_SECTION_KERNEL:
            if (seen_sections[EIF_SECTION_KERNEL]) {
                error_setg(errp, "Invalid EIF image. More than 1 kernel "
                           "section");
                goto cleanup;
            }

            if (!read_eif_kernel(f, hdr.section_size, kernel_path, hash0,
                                 hash1, &crc, errp)) {
                goto cleanup;
            }

            break;
        case EIF_SECTION_CMDLINE:
        {
            uint64_t size;
            if (seen_sections[EIF_SECTION_CMDLINE]) {
                error_setg(errp, "Invalid EIF image. More than 1 cmdline "
                           "section");
                goto cleanup;
            }
            size = hdr.section_size;
            *cmdline = g_try_malloc(size + 1);
            if (!*cmdline) {
                error_setg(errp, "Out of memory reading command line section");
                goto cleanup;
            }
            if (!read_eif_cmdline(f, size, *cmdline, hash0, hash1, &crc,
                                  errp)) {
                goto cleanup;
            }
            (*cmdline)[size] = '\0';

            break;
        }
        case EIF_SECTION_RAMDISK:
        {
            QCryptoHash *h = hash2;
            if (!seen_sections[EIF_SECTION_RAMDISK]) {
                /*
                 * If this is the first time we are seeing a ramdisk section,
                 * we need to:
                 * 1) hash it into bootstrap (hash1) instead of app (hash2)
                 *    along with image (hash0)
                 * 2) create the initrd temporary file.
                 */
                h = hash1;
                if (!get_tmp_file("eif-initrd-XXXXXX", initrd_path, errp)) {
                    goto cleanup;
                }
                initrd_path_f = fopen(*initrd_path, "wb");
                if (initrd_path_f == NULL) {
                    error_setg_errno(errp, errno, "Failed to open file %s",
                                     *initrd_path);
                    goto cleanup;
                }
            }

            if (!read_eif_ramdisk(f, initrd_path_f, hdr.section_size, hash0, h,
                                  &crc, errp)) {
                goto cleanup;
            }

            break;
        }
        case EIF_SECTION_SIGNATURE:
            *signature_found = true;
            if (!get_signature_fingerprint_sha384(f, hdr.section_size,
                                                  fingerprint_hash, &crc,
                                                  errp)) {
                goto cleanup;
            }
            break;
        default:
            /* other sections including invalid or unknown sections */
        {
            uint8_t *buf;
            size_t got;
            uint64_t size = hdr.section_size;
            buf = g_try_malloc(size);
            if (!buf) {
                error_setg(errp, "Out of memory reading unknown section");
                goto cleanup;
            }
            got = fread(buf, 1, size, f);
            if ((uint64_t) got != size) {
                g_free(buf);
                error_setg(errp, "Failed to read EIF %s section data",
                           section_type_to_string(section_type));
                goto cleanup;
            }
            crc = crc32(crc, buf, size);
            g_free(buf);
            break;
        }
        }

        if (section_type < EIF_SECTION_MAX) {
            seen_sections[section_type] = true;
        }
    }

    if (!seen_sections[EIF_SECTION_KERNEL]) {
        error_setg(errp, "Invalid EIF image. No kernel section.");
        goto cleanup;
    }
    if (!seen_sections[EIF_SECTION_CMDLINE]) {
        error_setg(errp, "Invalid EIF image. No cmdline section.");
        goto cleanup;
    }
    if (!seen_sections[EIF_SECTION_RAMDISK]) {
        error_setg(errp, "Invalid EIF image. No ramdisk section.");
        goto cleanup;
    }

    if (eif_header.eif_crc32 != crc) {
        error_setg(errp, "CRC mismatch. Expected %u but header has %u.",
                   crc, eif_header.eif_crc32);
        goto cleanup;
    }

    /*
     * Let's append the initrd file from "-initrd" option if any. Although
     * we pass the crc pointer to read_eif_ramdisk, it is not useful anymore.
     * We have already done the crc mismatch check above this code.
     */
    if (machine_initrd) {
        machine_initrd_f = fopen(machine_initrd, "rb");
        if (machine_initrd_f == NULL) {
            error_setg_errno(errp, errno, "Failed to open initrd file %s",
                             machine_initrd);
            goto cleanup;
        }

        machine_initrd_size = get_file_size(machine_initrd_f, errp);
        if (machine_initrd_size == -1) {
            goto cleanup;
        }

        if (!read_eif_ramdisk(machine_initrd_f, initrd_path_f,
                              machine_initrd_size, hash0, hash2, &crc, errp)) {
            goto cleanup;
        }
    }

    if (!get_SHA384_hash(hash0, image_hash, errp)) {
        goto cleanup;
    }
    if (!get_SHA384_hash(hash1, bootstrap_hash, errp)) {
        goto cleanup;
    }
    if (!get_SHA384_hash(hash2, app_hash, errp)) {
        goto cleanup;
    }

    fclose(f);
    fclose(initrd_path_f);
    safe_fclose(machine_initrd_f);
    return true;

 cleanup:
    safe_fclose(f);
    safe_fclose(initrd_path_f);
    safe_fclose(machine_initrd_f);

    safe_unlink(*kernel_path);
    g_free(*kernel_path);
    *kernel_path = NULL;

    safe_unlink(*initrd_path);
    g_free(*initrd_path);
    *initrd_path = NULL;

    g_free(*cmdline);
    *cmdline = NULL;

    return false;
}
