/*
 * QEMU Crypto block device encryption LUKS format
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/bswap.h"

#include "block-luks.h"

#include "crypto/hash.h"
#include "crypto/afsplit.h"
#include "crypto/pbkdf.h"
#include "crypto/secret.h"
#include "crypto/random.h"
#include "qemu/uuid.h"

#include "qemu/coroutine.h"
#include "qemu/bitmap.h"

/*
 * Reference for the LUKS format implemented here is
 *
 *   docs/on-disk-format.pdf
 *
 * in 'cryptsetup' package source code
 *
 * This file implements the 1.2.1 specification, dated
 * Oct 16, 2011.
 */

typedef struct QCryptoBlockLUKS QCryptoBlockLUKS;
typedef struct QCryptoBlockLUKSHeader QCryptoBlockLUKSHeader;
typedef struct QCryptoBlockLUKSKeySlot QCryptoBlockLUKSKeySlot;


/* The following constants are all defined by the LUKS spec */
#define QCRYPTO_BLOCK_LUKS_VERSION 1

#define QCRYPTO_BLOCK_LUKS_MAGIC_LEN 6
#define QCRYPTO_BLOCK_LUKS_CIPHER_NAME_LEN 32
#define QCRYPTO_BLOCK_LUKS_CIPHER_MODE_LEN 32
#define QCRYPTO_BLOCK_LUKS_HASH_SPEC_LEN 32
#define QCRYPTO_BLOCK_LUKS_DIGEST_LEN 20
#define QCRYPTO_BLOCK_LUKS_SALT_LEN 32
#define QCRYPTO_BLOCK_LUKS_UUID_LEN 40
#define QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS 8
#define QCRYPTO_BLOCK_LUKS_STRIPES 4000
#define QCRYPTO_BLOCK_LUKS_MIN_SLOT_KEY_ITERS 1000
#define QCRYPTO_BLOCK_LUKS_MIN_MASTER_KEY_ITERS 1000
#define QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET 4096

#define QCRYPTO_BLOCK_LUKS_KEY_SLOT_DISABLED 0x0000DEAD
#define QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED 0x00AC71F3

#define QCRYPTO_BLOCK_LUKS_SECTOR_SIZE 512LL

#define QCRYPTO_BLOCK_LUKS_DEFAULT_ITER_TIME_MS 2000
#define QCRYPTO_BLOCK_LUKS_ERASE_ITERATIONS 40

static const char qcrypto_block_luks_magic[QCRYPTO_BLOCK_LUKS_MAGIC_LEN] = {
    'L', 'U', 'K', 'S', 0xBA, 0xBE
};

typedef struct QCryptoBlockLUKSNameMap QCryptoBlockLUKSNameMap;
struct QCryptoBlockLUKSNameMap {
    const char *name;
    int id;
};

typedef struct QCryptoBlockLUKSCipherSizeMap QCryptoBlockLUKSCipherSizeMap;
struct QCryptoBlockLUKSCipherSizeMap {
    uint32_t key_bytes;
    int id;
};
typedef struct QCryptoBlockLUKSCipherNameMap QCryptoBlockLUKSCipherNameMap;
struct QCryptoBlockLUKSCipherNameMap {
    const char *name;
    const QCryptoBlockLUKSCipherSizeMap *sizes;
};


static const QCryptoBlockLUKSCipherSizeMap
qcrypto_block_luks_cipher_size_map_aes[] = {
    { 16, QCRYPTO_CIPHER_ALG_AES_128 },
    { 24, QCRYPTO_CIPHER_ALG_AES_192 },
    { 32, QCRYPTO_CIPHER_ALG_AES_256 },
    { 0, 0 },
};

static const QCryptoBlockLUKSCipherSizeMap
qcrypto_block_luks_cipher_size_map_cast5[] = {
    { 16, QCRYPTO_CIPHER_ALG_CAST5_128 },
    { 0, 0 },
};

static const QCryptoBlockLUKSCipherSizeMap
qcrypto_block_luks_cipher_size_map_serpent[] = {
    { 16, QCRYPTO_CIPHER_ALG_SERPENT_128 },
    { 24, QCRYPTO_CIPHER_ALG_SERPENT_192 },
    { 32, QCRYPTO_CIPHER_ALG_SERPENT_256 },
    { 0, 0 },
};

static const QCryptoBlockLUKSCipherSizeMap
qcrypto_block_luks_cipher_size_map_twofish[] = {
    { 16, QCRYPTO_CIPHER_ALG_TWOFISH_128 },
    { 24, QCRYPTO_CIPHER_ALG_TWOFISH_192 },
    { 32, QCRYPTO_CIPHER_ALG_TWOFISH_256 },
    { 0, 0 },
};

static const QCryptoBlockLUKSCipherNameMap
qcrypto_block_luks_cipher_name_map[] = {
    { "aes", qcrypto_block_luks_cipher_size_map_aes },
    { "cast5", qcrypto_block_luks_cipher_size_map_cast5 },
    { "serpent", qcrypto_block_luks_cipher_size_map_serpent },
    { "twofish", qcrypto_block_luks_cipher_size_map_twofish },
};


/*
 * This struct is written to disk in big-endian format,
 * but operated upon in native-endian format.
 */
struct QCryptoBlockLUKSKeySlot {
    /* state of keyslot, enabled/disable */
    uint32_t active;
    /* iterations for PBKDF2 */
    uint32_t iterations;
    /* salt for PBKDF2 */
    uint8_t salt[QCRYPTO_BLOCK_LUKS_SALT_LEN];
    /* start sector of key material */
    uint32_t key_offset_sector;
    /* number of anti-forensic stripes */
    uint32_t stripes;
};

QEMU_BUILD_BUG_ON(sizeof(struct QCryptoBlockLUKSKeySlot) != 48);


/*
 * This struct is written to disk in big-endian format,
 * but operated upon in native-endian format.
 */
struct QCryptoBlockLUKSHeader {
    /* 'L', 'U', 'K', 'S', '0xBA', '0xBE' */
    char magic[QCRYPTO_BLOCK_LUKS_MAGIC_LEN];

    /* LUKS version, currently 1 */
    uint16_t version;

    /* cipher name specification (aes, etc) */
    char cipher_name[QCRYPTO_BLOCK_LUKS_CIPHER_NAME_LEN];

    /* cipher mode specification (cbc-plain, xts-essiv:sha256, etc) */
    char cipher_mode[QCRYPTO_BLOCK_LUKS_CIPHER_MODE_LEN];

    /* hash specification (sha256, etc) */
    char hash_spec[QCRYPTO_BLOCK_LUKS_HASH_SPEC_LEN];

    /* start offset of the volume data (in 512 byte sectors) */
    uint32_t payload_offset_sector;

    /* Number of key bytes */
    uint32_t master_key_len;

    /* master key checksum after PBKDF2 */
    uint8_t master_key_digest[QCRYPTO_BLOCK_LUKS_DIGEST_LEN];

    /* salt for master key PBKDF2 */
    uint8_t master_key_salt[QCRYPTO_BLOCK_LUKS_SALT_LEN];

    /* iterations for master key PBKDF2 */
    uint32_t master_key_iterations;

    /* UUID of the partition in standard ASCII representation */
    uint8_t uuid[QCRYPTO_BLOCK_LUKS_UUID_LEN];

    /* key slots */
    QCryptoBlockLUKSKeySlot key_slots[QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS];
};

QEMU_BUILD_BUG_ON(sizeof(struct QCryptoBlockLUKSHeader) != 592);


struct QCryptoBlockLUKS {
    QCryptoBlockLUKSHeader header;

    /* Main encryption algorithm used for encryption*/
    QCryptoCipherAlgorithm cipher_alg;

    /* Mode of encryption for the selected encryption algorithm */
    QCryptoCipherMode cipher_mode;

    /* Initialization vector generation algorithm */
    QCryptoIVGenAlgorithm ivgen_alg;

    /* Hash algorithm used for IV generation*/
    QCryptoHashAlgorithm ivgen_hash_alg;

    /*
     * Encryption algorithm used for IV generation.
     * Usually the same as main encryption algorithm
     */
    QCryptoCipherAlgorithm ivgen_cipher_alg;

    /* Hash algorithm used in pbkdf2 function */
    QCryptoHashAlgorithm hash_alg;

    /* Name of the secret that was used to open the image */
    char *secret;
};


static int qcrypto_block_luks_cipher_name_lookup(const char *name,
                                                 QCryptoCipherMode mode,
                                                 uint32_t key_bytes,
                                                 Error **errp)
{
    const QCryptoBlockLUKSCipherNameMap *map =
        qcrypto_block_luks_cipher_name_map;
    size_t maplen = G_N_ELEMENTS(qcrypto_block_luks_cipher_name_map);
    size_t i, j;

    if (mode == QCRYPTO_CIPHER_MODE_XTS) {
        key_bytes /= 2;
    }

    for (i = 0; i < maplen; i++) {
        if (!g_str_equal(map[i].name, name)) {
            continue;
        }
        for (j = 0; j < map[i].sizes[j].key_bytes; j++) {
            if (map[i].sizes[j].key_bytes == key_bytes) {
                return map[i].sizes[j].id;
            }
        }
    }

    error_setg(errp, "Algorithm %s with key size %d bytes not supported",
               name, key_bytes);
    return 0;
}

static const char *
qcrypto_block_luks_cipher_alg_lookup(QCryptoCipherAlgorithm alg,
                                     Error **errp)
{
    const QCryptoBlockLUKSCipherNameMap *map =
        qcrypto_block_luks_cipher_name_map;
    size_t maplen = G_N_ELEMENTS(qcrypto_block_luks_cipher_name_map);
    size_t i, j;
    for (i = 0; i < maplen; i++) {
        for (j = 0; j < map[i].sizes[j].key_bytes; j++) {
            if (map[i].sizes[j].id == alg) {
                return map[i].name;
            }
        }
    }

    error_setg(errp, "Algorithm '%s' not supported",
               QCryptoCipherAlgorithm_str(alg));
    return NULL;
}

/* XXX replace with qapi_enum_parse() in future, when we can
 * make that function emit a more friendly error message */
static int qcrypto_block_luks_name_lookup(const char *name,
                                          const QEnumLookup *map,
                                          const char *type,
                                          Error **errp)
{
    int ret = qapi_enum_parse(map, name, -1, NULL);

    if (ret < 0) {
        error_setg(errp, "%s %s not supported", type, name);
        return 0;
    }
    return ret;
}

#define qcrypto_block_luks_cipher_mode_lookup(name, errp)               \
    qcrypto_block_luks_name_lookup(name,                                \
                                   &QCryptoCipherMode_lookup,           \
                                   "Cipher mode",                       \
                                   errp)

#define qcrypto_block_luks_hash_name_lookup(name, errp)                 \
    qcrypto_block_luks_name_lookup(name,                                \
                                   &QCryptoHashAlgorithm_lookup,        \
                                   "Hash algorithm",                    \
                                   errp)

#define qcrypto_block_luks_ivgen_name_lookup(name, errp)                \
    qcrypto_block_luks_name_lookup(name,                                \
                                   &QCryptoIVGenAlgorithm_lookup,       \
                                   "IV generator",                      \
                                   errp)


static bool
qcrypto_block_luks_has_format(const uint8_t *buf,
                              size_t buf_size)
{
    const QCryptoBlockLUKSHeader *luks_header = (const void *)buf;

    if (buf_size >= offsetof(QCryptoBlockLUKSHeader, cipher_name) &&
        memcmp(luks_header->magic, qcrypto_block_luks_magic,
               QCRYPTO_BLOCK_LUKS_MAGIC_LEN) == 0 &&
        be16_to_cpu(luks_header->version) == QCRYPTO_BLOCK_LUKS_VERSION) {
        return true;
    } else {
        return false;
    }
}


/**
 * Deal with a quirk of dm-crypt usage of ESSIV.
 *
 * When calculating ESSIV IVs, the cipher length used by ESSIV
 * may be different from the cipher length used for the block
 * encryption, becauses dm-crypt uses the hash digest length
 * as the key size. ie, if you have AES 128 as the block cipher
 * and SHA 256 as ESSIV hash, then ESSIV will use AES 256 as
 * the cipher since that gets a key length matching the digest
 * size, not AES 128 with truncated digest as might be imagined
 */
static QCryptoCipherAlgorithm
qcrypto_block_luks_essiv_cipher(QCryptoCipherAlgorithm cipher,
                                QCryptoHashAlgorithm hash,
                                Error **errp)
{
    size_t digestlen = qcrypto_hash_digest_len(hash);
    size_t keylen = qcrypto_cipher_get_key_len(cipher);
    if (digestlen == keylen) {
        return cipher;
    }

    switch (cipher) {
    case QCRYPTO_CIPHER_ALG_AES_128:
    case QCRYPTO_CIPHER_ALG_AES_192:
    case QCRYPTO_CIPHER_ALG_AES_256:
        if (digestlen == qcrypto_cipher_get_key_len(
                QCRYPTO_CIPHER_ALG_AES_128)) {
            return QCRYPTO_CIPHER_ALG_AES_128;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_AES_192)) {
            return QCRYPTO_CIPHER_ALG_AES_192;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_AES_256)) {
            return QCRYPTO_CIPHER_ALG_AES_256;
        } else {
            error_setg(errp, "No AES cipher with key size %zu available",
                       digestlen);
            return 0;
        }
        break;
    case QCRYPTO_CIPHER_ALG_SERPENT_128:
    case QCRYPTO_CIPHER_ALG_SERPENT_192:
    case QCRYPTO_CIPHER_ALG_SERPENT_256:
        if (digestlen == qcrypto_cipher_get_key_len(
                QCRYPTO_CIPHER_ALG_SERPENT_128)) {
            return QCRYPTO_CIPHER_ALG_SERPENT_128;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_SERPENT_192)) {
            return QCRYPTO_CIPHER_ALG_SERPENT_192;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_SERPENT_256)) {
            return QCRYPTO_CIPHER_ALG_SERPENT_256;
        } else {
            error_setg(errp, "No Serpent cipher with key size %zu available",
                       digestlen);
            return 0;
        }
        break;
    case QCRYPTO_CIPHER_ALG_TWOFISH_128:
    case QCRYPTO_CIPHER_ALG_TWOFISH_192:
    case QCRYPTO_CIPHER_ALG_TWOFISH_256:
        if (digestlen == qcrypto_cipher_get_key_len(
                QCRYPTO_CIPHER_ALG_TWOFISH_128)) {
            return QCRYPTO_CIPHER_ALG_TWOFISH_128;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_TWOFISH_192)) {
            return QCRYPTO_CIPHER_ALG_TWOFISH_192;
        } else if (digestlen == qcrypto_cipher_get_key_len(
                       QCRYPTO_CIPHER_ALG_TWOFISH_256)) {
            return QCRYPTO_CIPHER_ALG_TWOFISH_256;
        } else {
            error_setg(errp, "No Twofish cipher with key size %zu available",
                       digestlen);
            return 0;
        }
        break;
    default:
        error_setg(errp, "Cipher %s not supported with essiv",
                   QCryptoCipherAlgorithm_str(cipher));
        return 0;
    }
}

/*
 * Returns number of sectors needed to store the key material
 * given number of anti forensic stripes
 */
static int
qcrypto_block_luks_splitkeylen_sectors(const QCryptoBlockLUKS *luks,
                                       unsigned int header_sectors,
                                       unsigned int stripes)
{
    /*
     * This calculation doesn't match that shown in the spec,
     * but instead follows the cryptsetup implementation.
     */

    size_t splitkeylen = luks->header.master_key_len * stripes;

    /* First align the key material size to block size*/
    size_t splitkeylen_sectors =
        DIV_ROUND_UP(splitkeylen, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE);

    /* Then also align the key material size to the size of the header */
    return ROUND_UP(splitkeylen_sectors, header_sectors);
}

/*
 * Stores the main LUKS header, taking care of endianess
 */
static int
qcrypto_block_luks_store_header(QCryptoBlock *block,
                                QCryptoBlockWriteFunc writefunc,
                                void *opaque,
                                Error **errp)
{
    const QCryptoBlockLUKS *luks = block->opaque;
    Error *local_err = NULL;
    size_t i;
    g_autofree QCryptoBlockLUKSHeader *hdr_copy = NULL;

    /* Create a copy of the header */
    hdr_copy = g_new0(QCryptoBlockLUKSHeader, 1);
    memcpy(hdr_copy, &luks->header, sizeof(QCryptoBlockLUKSHeader));

    /*
     * Everything on disk uses Big Endian (tm), so flip header fields
     * before writing them
     */
    cpu_to_be16s(&hdr_copy->version);
    cpu_to_be32s(&hdr_copy->payload_offset_sector);
    cpu_to_be32s(&hdr_copy->master_key_len);
    cpu_to_be32s(&hdr_copy->master_key_iterations);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        cpu_to_be32s(&hdr_copy->key_slots[i].active);
        cpu_to_be32s(&hdr_copy->key_slots[i].iterations);
        cpu_to_be32s(&hdr_copy->key_slots[i].key_offset_sector);
        cpu_to_be32s(&hdr_copy->key_slots[i].stripes);
    }

    /* Write out the partition header and key slot headers */
    writefunc(block, 0, (const uint8_t *)hdr_copy, sizeof(*hdr_copy),
              opaque, &local_err);

    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }
    return 0;
}

/*
 * Loads the main LUKS header,and byteswaps it to native endianess
 * And run basic sanity checks on it
 */
static int
qcrypto_block_luks_load_header(QCryptoBlock *block,
                                QCryptoBlockReadFunc readfunc,
                                void *opaque,
                                Error **errp)
{
    ssize_t rv;
    size_t i;
    QCryptoBlockLUKS *luks = block->opaque;

    /*
     * Read the entire LUKS header, minus the key material from
     * the underlying device
     */
    rv = readfunc(block, 0,
                  (uint8_t *)&luks->header,
                  sizeof(luks->header),
                  opaque,
                  errp);
    if (rv < 0) {
        return rv;
    }

    /*
     * The header is always stored in big-endian format, so
     * convert everything to native
     */
    be16_to_cpus(&luks->header.version);
    be32_to_cpus(&luks->header.payload_offset_sector);
    be32_to_cpus(&luks->header.master_key_len);
    be32_to_cpus(&luks->header.master_key_iterations);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        be32_to_cpus(&luks->header.key_slots[i].active);
        be32_to_cpus(&luks->header.key_slots[i].iterations);
        be32_to_cpus(&luks->header.key_slots[i].key_offset_sector);
        be32_to_cpus(&luks->header.key_slots[i].stripes);
    }

    return 0;
}

/*
 * Does basic sanity checks on the LUKS header
 */
static int
qcrypto_block_luks_check_header(const QCryptoBlockLUKS *luks, Error **errp)
{
    size_t i, j;

    unsigned int header_sectors = QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;

    if (memcmp(luks->header.magic, qcrypto_block_luks_magic,
               QCRYPTO_BLOCK_LUKS_MAGIC_LEN) != 0) {
        error_setg(errp, "Volume is not in LUKS format");
        return -1;
    }

    if (luks->header.version != QCRYPTO_BLOCK_LUKS_VERSION) {
        error_setg(errp, "LUKS version %" PRIu32 " is not supported",
                   luks->header.version);
        return -1;
    }

    /* Check all keyslots for corruption  */
    for (i = 0 ; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS ; i++) {

        const QCryptoBlockLUKSKeySlot *slot1 = &luks->header.key_slots[i];
        unsigned int start1 = slot1->key_offset_sector;
        unsigned int len1 =
            qcrypto_block_luks_splitkeylen_sectors(luks,
                                                   header_sectors,
                                                   slot1->stripes);

        if (slot1->stripes == 0) {
            error_setg(errp, "Keyslot %zu is corrupted (stripes == 0)", i);
            return -1;
        }

        if (slot1->active != QCRYPTO_BLOCK_LUKS_KEY_SLOT_DISABLED &&
            slot1->active != QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED) {
            error_setg(errp,
                       "Keyslot %zu state (active/disable) is corrupted", i);
            return -1;
        }

        if (start1 + len1 > luks->header.payload_offset_sector) {
            error_setg(errp,
                       "Keyslot %zu is overlapping with the encrypted payload",
                       i);
            return -1;
        }

        for (j = i + 1 ; j < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS ; j++) {
            const QCryptoBlockLUKSKeySlot *slot2 = &luks->header.key_slots[j];
            unsigned int start2 = slot2->key_offset_sector;
            unsigned int len2 =
                qcrypto_block_luks_splitkeylen_sectors(luks,
                                                       header_sectors,
                                                       slot2->stripes);

            if (start1 + len1 > start2 && start2 + len2 > start1) {
                error_setg(errp,
                           "Keyslots %zu and %zu are overlapping in the header",
                           i, j);
                return -1;
            }
        }

    }
    return 0;
}

/*
 * Parses the crypto parameters that are stored in the LUKS header
 */

static int
qcrypto_block_luks_parse_header(QCryptoBlockLUKS *luks, Error **errp)
{
    g_autofree char *cipher_mode = g_strdup(luks->header.cipher_mode);
    char *ivgen_name, *ivhash_name;
    Error *local_err = NULL;

    /*
     * The cipher_mode header contains a string that we have
     * to further parse, of the format
     *
     *    <cipher-mode>-<iv-generator>[:<iv-hash>]
     *
     * eg  cbc-essiv:sha256, cbc-plain64
     */
    ivgen_name = strchr(cipher_mode, '-');
    if (!ivgen_name) {
        error_setg(errp, "Unexpected cipher mode string format %s",
                   luks->header.cipher_mode);
        return -1;
    }
    *ivgen_name = '\0';
    ivgen_name++;

    ivhash_name = strchr(ivgen_name, ':');
    if (!ivhash_name) {
        luks->ivgen_hash_alg = 0;
    } else {
        *ivhash_name = '\0';
        ivhash_name++;

        luks->ivgen_hash_alg = qcrypto_block_luks_hash_name_lookup(ivhash_name,
                                                                   &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -1;
        }
    }

    luks->cipher_mode = qcrypto_block_luks_cipher_mode_lookup(cipher_mode,
                                                              &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    luks->cipher_alg =
            qcrypto_block_luks_cipher_name_lookup(luks->header.cipher_name,
                                                  luks->cipher_mode,
                                                  luks->header.master_key_len,
                                                  &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    luks->hash_alg =
            qcrypto_block_luks_hash_name_lookup(luks->header.hash_spec,
                                                &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    luks->ivgen_alg = qcrypto_block_luks_ivgen_name_lookup(ivgen_name,
                                                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return -1;
    }

    if (luks->ivgen_alg == QCRYPTO_IVGEN_ALG_ESSIV) {
        if (!ivhash_name) {
            error_setg(errp, "Missing IV generator hash specification");
            return -1;
        }
        luks->ivgen_cipher_alg =
                qcrypto_block_luks_essiv_cipher(luks->cipher_alg,
                                                luks->ivgen_hash_alg,
                                                &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return -1;
        }
    } else {

        /*
         * Note we parsed the ivhash_name earlier in the cipher_mode
         * spec string even with plain/plain64 ivgens, but we
         * will ignore it, since it is irrelevant for these ivgens.
         * This is for compat with dm-crypt which will silently
         * ignore hash names with these ivgens rather than report
         * an error about the invalid usage
         */
        luks->ivgen_cipher_alg = luks->cipher_alg;
    }
    return 0;
}

/*
 * Given a key slot,  user password, and the master key,
 * will store the encrypted master key there, and update the
 * in-memory header. User must then write the in-memory header
 *
 * Returns:
 *    0 if the keyslot was written successfully
 *      with the provided password
 *   -1 if a fatal error occurred while storing the key
 */
static int
qcrypto_block_luks_store_key(QCryptoBlock *block,
                             unsigned int slot_idx,
                             const char *password,
                             uint8_t *masterkey,
                             uint64_t iter_time,
                             QCryptoBlockWriteFunc writefunc,
                             void *opaque,
                             Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    QCryptoBlockLUKSKeySlot *slot;
    g_autofree uint8_t *splitkey = NULL;
    size_t splitkeylen;
    g_autofree uint8_t *slotkey = NULL;
    g_autoptr(QCryptoCipher) cipher = NULL;
    g_autoptr(QCryptoIVGen) ivgen = NULL;
    Error *local_err = NULL;
    uint64_t iters;
    int ret = -1;

    assert(slot_idx < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);
    slot = &luks->header.key_slots[slot_idx];
    if (qcrypto_random_bytes(slot->salt,
                             QCRYPTO_BLOCK_LUKS_SALT_LEN,
                             errp) < 0) {
        goto cleanup;
    }

    splitkeylen = luks->header.master_key_len * slot->stripes;

    /*
     * Determine how many iterations are required to
     * hash the user password while consuming 1 second of compute
     * time
     */
    iters = qcrypto_pbkdf2_count_iters(luks->hash_alg,
                                       (uint8_t *)password, strlen(password),
                                       slot->salt,
                                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                                       luks->header.master_key_len,
                                       &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto cleanup;
    }

    if (iters > (ULLONG_MAX / iter_time)) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu too large to scale",
                         (unsigned long long)iters);
        goto cleanup;
    }

    /* iter_time was in millis, but count_iters reported for secs */
    iters = iters * iter_time / 1000;

    if (iters > UINT32_MAX) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu larger than %u",
                         (unsigned long long)iters, UINT32_MAX);
        goto cleanup;
    }

    slot->iterations =
        MAX(iters, QCRYPTO_BLOCK_LUKS_MIN_SLOT_KEY_ITERS);


    /*
     * Generate a key that we'll use to encrypt the master
     * key, from the user's password
     */
    slotkey = g_new0(uint8_t, luks->header.master_key_len);
    if (qcrypto_pbkdf2(luks->hash_alg,
                       (uint8_t *)password, strlen(password),
                       slot->salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       slot->iterations,
                       slotkey, luks->header.master_key_len,
                       errp) < 0) {
        goto cleanup;
    }


    /*
     * Setup the encryption objects needed to encrypt the
     * master key material
     */
    cipher = qcrypto_cipher_new(luks->cipher_alg,
                                luks->cipher_mode,
                                slotkey, luks->header.master_key_len,
                                errp);
    if (!cipher) {
        goto cleanup;
    }

    ivgen = qcrypto_ivgen_new(luks->ivgen_alg,
                              luks->ivgen_cipher_alg,
                              luks->ivgen_hash_alg,
                              slotkey, luks->header.master_key_len,
                              errp);
    if (!ivgen) {
        goto cleanup;
    }

    /*
     * Before storing the master key, we need to vastly
     * increase its size, as protection against forensic
     * disk data recovery
     */
    splitkey = g_new0(uint8_t, splitkeylen);

    if (qcrypto_afsplit_encode(luks->hash_alg,
                               luks->header.master_key_len,
                               slot->stripes,
                               masterkey,
                               splitkey,
                               errp) < 0) {
        goto cleanup;
    }

    /*
     * Now we encrypt the split master key with the key generated
     * from the user's password, before storing it
     */
    if (qcrypto_block_cipher_encrypt_helper(cipher, block->niv, ivgen,
                                            QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                            0,
                                            splitkey,
                                            splitkeylen,
                                            errp) < 0) {
        goto cleanup;
    }

    /* Write out the slot's master key material. */
    if (writefunc(block,
                  slot->key_offset_sector *
                  QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                  splitkey, splitkeylen,
                  opaque,
                  errp) != splitkeylen) {
        goto cleanup;
    }

    slot->active = QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED;

    if (qcrypto_block_luks_store_header(block,  writefunc, opaque, errp) < 0) {
        goto cleanup;
    }

    ret = 0;

cleanup:
    if (slotkey) {
        memset(slotkey, 0, luks->header.master_key_len);
    }
    if (splitkey) {
        memset(splitkey, 0, splitkeylen);
    }
    return ret;
}

/*
 * Given a key slot, and user password, this will attempt to unlock
 * the master encryption key from the key slot.
 *
 * Returns:
 *    0 if the key slot is disabled, or key could not be decrypted
 *      with the provided password
 *    1 if the key slot is enabled, and key decrypted successfully
 *      with the provided password
 *   -1 if a fatal error occurred loading the key
 */
static int
qcrypto_block_luks_load_key(QCryptoBlock *block,
                            size_t slot_idx,
                            const char *password,
                            uint8_t *masterkey,
                            QCryptoBlockReadFunc readfunc,
                            void *opaque,
                            Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    const QCryptoBlockLUKSKeySlot *slot;
    g_autofree uint8_t *splitkey = NULL;
    size_t splitkeylen;
    g_autofree uint8_t *possiblekey = NULL;
    ssize_t rv;
    g_autoptr(QCryptoCipher) cipher = NULL;
    uint8_t keydigest[QCRYPTO_BLOCK_LUKS_DIGEST_LEN];
    g_autoptr(QCryptoIVGen) ivgen = NULL;
    size_t niv;

    assert(slot_idx < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);
    slot = &luks->header.key_slots[slot_idx];
    if (slot->active != QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED) {
        return 0;
    }

    splitkeylen = luks->header.master_key_len * slot->stripes;
    splitkey = g_new0(uint8_t, splitkeylen);
    possiblekey = g_new0(uint8_t, luks->header.master_key_len);

    /*
     * The user password is used to generate a (possible)
     * decryption key. This may or may not successfully
     * decrypt the master key - we just blindly assume
     * the key is correct and validate the results of
     * decryption later.
     */
    if (qcrypto_pbkdf2(luks->hash_alg,
                       (const uint8_t *)password, strlen(password),
                       slot->salt, QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       slot->iterations,
                       possiblekey, luks->header.master_key_len,
                       errp) < 0) {
        return -1;
    }

    /*
     * We need to read the master key material from the
     * LUKS key material header. What we're reading is
     * not the raw master key, but rather the data after
     * it has been passed through AFSplit and the result
     * then encrypted.
     */
    rv = readfunc(block,
                  slot->key_offset_sector * QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                  splitkey, splitkeylen,
                  opaque,
                  errp);
    if (rv < 0) {
        return -1;
    }


    /* Setup the cipher/ivgen that we'll use to try to decrypt
     * the split master key material */
    cipher = qcrypto_cipher_new(luks->cipher_alg,
                                luks->cipher_mode,
                                possiblekey,
                                luks->header.master_key_len,
                                errp);
    if (!cipher) {
        return -1;
    }

    niv = qcrypto_cipher_get_iv_len(luks->cipher_alg,
                                    luks->cipher_mode);

    ivgen = qcrypto_ivgen_new(luks->ivgen_alg,
                              luks->ivgen_cipher_alg,
                              luks->ivgen_hash_alg,
                              possiblekey,
                              luks->header.master_key_len,
                              errp);
    if (!ivgen) {
        return -1;
    }


    /*
     * The master key needs to be decrypted in the same
     * way that the block device payload will be decrypted
     * later. In particular we'll be using the IV generator
     * to reset the encryption cipher every time the master
     * key crosses a sector boundary.
     */
    if (qcrypto_block_cipher_decrypt_helper(cipher,
                                            niv,
                                            ivgen,
                                            QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                            0,
                                            splitkey,
                                            splitkeylen,
                                            errp) < 0) {
        return -1;
    }

    /*
     * Now we've decrypted the split master key, join
     * it back together to get the actual master key.
     */
    if (qcrypto_afsplit_decode(luks->hash_alg,
                               luks->header.master_key_len,
                               slot->stripes,
                               splitkey,
                               masterkey,
                               errp) < 0) {
        return -1;
    }


    /*
     * We still don't know that the masterkey we got is valid,
     * because we just blindly assumed the user's password
     * was correct. This is where we now verify it. We are
     * creating a hash of the master key using PBKDF and
     * then comparing that to the hash stored in the key slot
     * header
     */
    if (qcrypto_pbkdf2(luks->hash_alg,
                       masterkey,
                       luks->header.master_key_len,
                       luks->header.master_key_salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       luks->header.master_key_iterations,
                       keydigest,
                       G_N_ELEMENTS(keydigest),
                       errp) < 0) {
        return -1;
    }

    if (memcmp(keydigest, luks->header.master_key_digest,
               QCRYPTO_BLOCK_LUKS_DIGEST_LEN) == 0) {
        /* Success, we got the right master key */
        return 1;
    }

    /* Fail, user's password was not valid for this key slot,
     * tell caller to try another slot */
    return 0;
}


/*
 * Given a user password, this will iterate over all key
 * slots and try to unlock each active key slot using the
 * password until it successfully obtains a master key.
 *
 * Returns 0 if a key was loaded, -1 if no keys could be loaded
 */
static int
qcrypto_block_luks_find_key(QCryptoBlock *block,
                            const char *password,
                            uint8_t *masterkey,
                            QCryptoBlockReadFunc readfunc,
                            void *opaque,
                            Error **errp)
{
    size_t i;
    int rv;

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        rv = qcrypto_block_luks_load_key(block,
                                         i,
                                         password,
                                         masterkey,
                                         readfunc,
                                         opaque,
                                         errp);
        if (rv < 0) {
            goto error;
        }
        if (rv == 1) {
            return 0;
        }
    }

    error_setg(errp, "Invalid password, cannot unlock any keyslot");
 error:
    return -1;
}

/*
 * Returns true if a slot i is marked as active
 * (contains encrypted copy of the master key)
 */
static bool
qcrypto_block_luks_slot_active(const QCryptoBlockLUKS *luks,
                               unsigned int slot_idx)
{
    uint32_t val;

    assert(slot_idx < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);
    val = luks->header.key_slots[slot_idx].active;
    return val == QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED;
}

/*
 * Returns the number of slots that are marked as active
 * (slots that contain encrypted copy of the master key)
 */
static unsigned int
qcrypto_block_luks_count_active_slots(const QCryptoBlockLUKS *luks)
{
    size_t i = 0;
    unsigned int ret = 0;

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        if (qcrypto_block_luks_slot_active(luks, i)) {
            ret++;
        }
    }
    return ret;
}

/*
 * Finds first key slot which is not active
 * Returns the key slot index, or -1 if it doesn't exist
 */
static int
qcrypto_block_luks_find_free_keyslot(const QCryptoBlockLUKS *luks)
{
    size_t i;

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        if (!qcrypto_block_luks_slot_active(luks, i)) {
            return i;
        }
    }
    return -1;
}

/*
 * Erases an keyslot given its index
 * Returns:
 *    0 if the keyslot was erased successfully
 *   -1 if a error occurred while erasing the keyslot
 *
 */
static int
qcrypto_block_luks_erase_key(QCryptoBlock *block,
                             unsigned int slot_idx,
                             QCryptoBlockWriteFunc writefunc,
                             void *opaque,
                             Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    QCryptoBlockLUKSKeySlot *slot;
    g_autofree uint8_t *garbagesplitkey = NULL;
    size_t splitkeylen;
    size_t i;
    Error *local_err = NULL;
    int ret;

    assert(slot_idx < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);
    slot = &luks->header.key_slots[slot_idx];

    splitkeylen = luks->header.master_key_len * slot->stripes;
    assert(splitkeylen > 0);

    garbagesplitkey = g_new0(uint8_t, splitkeylen);

    /* Reset the key slot header */
    memset(slot->salt, 0, QCRYPTO_BLOCK_LUKS_SALT_LEN);
    slot->iterations = 0;
    slot->active = QCRYPTO_BLOCK_LUKS_KEY_SLOT_DISABLED;

    ret = qcrypto_block_luks_store_header(block, writefunc,
                                          opaque, &local_err);

    if (ret < 0) {
        error_propagate(errp, local_err);
    }
    /*
     * Now try to erase the key material, even if the header
     * update failed
     */
    for (i = 0; i < QCRYPTO_BLOCK_LUKS_ERASE_ITERATIONS; i++) {
        if (qcrypto_random_bytes(garbagesplitkey,
                                 splitkeylen, &local_err) < 0) {
            /*
             * If we failed to get the random data, still write
             * at least zeros to the key slot at least once
             */
            error_propagate(errp, local_err);

            if (i > 0) {
                return -1;
            }
        }
        if (writefunc(block,
                      slot->key_offset_sector * QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                      garbagesplitkey,
                      splitkeylen,
                      opaque,
                      &local_err) != splitkeylen) {
            error_propagate(errp, local_err);
            return -1;
        }
    }
    return ret;
}

static int
qcrypto_block_luks_open(QCryptoBlock *block,
                        QCryptoBlockOpenOptions *options,
                        const char *optprefix,
                        QCryptoBlockReadFunc readfunc,
                        void *opaque,
                        unsigned int flags,
                        size_t n_threads,
                        Error **errp)
{
    QCryptoBlockLUKS *luks = NULL;
    g_autofree uint8_t *masterkey = NULL;
    g_autofree char *password = NULL;

    if (!(flags & QCRYPTO_BLOCK_OPEN_NO_IO)) {
        if (!options->u.luks.key_secret) {
            error_setg(errp, "Parameter '%skey-secret' is required for cipher",
                       optprefix ? optprefix : "");
            return -1;
        }
        password = qcrypto_secret_lookup_as_utf8(
            options->u.luks.key_secret, errp);
        if (!password) {
            return -1;
        }
    }

    luks = g_new0(QCryptoBlockLUKS, 1);
    block->opaque = luks;
    luks->secret = g_strdup(options->u.luks.key_secret);

    if (qcrypto_block_luks_load_header(block, readfunc, opaque, errp) < 0) {
        goto fail;
    }

    if (qcrypto_block_luks_check_header(luks, errp) < 0) {
        goto fail;
    }

    if (qcrypto_block_luks_parse_header(luks, errp) < 0) {
        goto fail;
    }

    if (!(flags & QCRYPTO_BLOCK_OPEN_NO_IO)) {
        /* Try to find which key slot our password is valid for
         * and unlock the master key from that slot.
         */

        masterkey = g_new0(uint8_t, luks->header.master_key_len);

        if (qcrypto_block_luks_find_key(block,
                                        password,
                                        masterkey,
                                        readfunc, opaque,
                                        errp) < 0) {
            goto fail;
        }

        /* We have a valid master key now, so can setup the
         * block device payload decryption objects
         */
        block->kdfhash = luks->hash_alg;
        block->niv = qcrypto_cipher_get_iv_len(luks->cipher_alg,
                                               luks->cipher_mode);

        block->ivgen = qcrypto_ivgen_new(luks->ivgen_alg,
                                         luks->ivgen_cipher_alg,
                                         luks->ivgen_hash_alg,
                                         masterkey,
                                         luks->header.master_key_len,
                                         errp);
        if (!block->ivgen) {
            goto fail;
        }

        if (qcrypto_block_init_cipher(block,
                                      luks->cipher_alg,
                                      luks->cipher_mode,
                                      masterkey,
                                      luks->header.master_key_len,
                                      n_threads,
                                      errp) < 0) {
            goto fail;
        }
    }

    block->sector_size = QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;
    block->payload_offset = luks->header.payload_offset_sector *
        block->sector_size;

    return 0;

 fail:
    qcrypto_block_free_cipher(block);
    qcrypto_ivgen_free(block->ivgen);
    g_free(luks->secret);
    g_free(luks);
    return -1;
}


static void
qcrypto_block_luks_uuid_gen(uint8_t *uuidstr)
{
    QemuUUID uuid;
    qemu_uuid_generate(&uuid);
    qemu_uuid_unparse(&uuid, (char *)uuidstr);
}

static int
qcrypto_block_luks_create(QCryptoBlock *block,
                          QCryptoBlockCreateOptions *options,
                          const char *optprefix,
                          QCryptoBlockInitFunc initfunc,
                          QCryptoBlockWriteFunc writefunc,
                          void *opaque,
                          Error **errp)
{
    QCryptoBlockLUKS *luks;
    QCryptoBlockCreateOptionsLUKS luks_opts;
    Error *local_err = NULL;
    g_autofree uint8_t *masterkey = NULL;
    size_t header_sectors;
    size_t split_key_sectors;
    size_t i;
    g_autofree char *password = NULL;
    const char *cipher_alg;
    const char *cipher_mode;
    const char *ivgen_alg;
    const char *ivgen_hash_alg = NULL;
    const char *hash_alg;
    g_autofree char *cipher_mode_spec = NULL;
    uint64_t iters;

    memcpy(&luks_opts, &options->u.luks, sizeof(luks_opts));
    if (!luks_opts.has_iter_time) {
        luks_opts.iter_time = QCRYPTO_BLOCK_LUKS_DEFAULT_ITER_TIME_MS;
    }
    if (!luks_opts.has_cipher_alg) {
        luks_opts.cipher_alg = QCRYPTO_CIPHER_ALG_AES_256;
    }
    if (!luks_opts.has_cipher_mode) {
        luks_opts.cipher_mode = QCRYPTO_CIPHER_MODE_XTS;
    }
    if (!luks_opts.has_ivgen_alg) {
        luks_opts.ivgen_alg = QCRYPTO_IVGEN_ALG_PLAIN64;
    }
    if (!luks_opts.has_hash_alg) {
        luks_opts.hash_alg = QCRYPTO_HASH_ALG_SHA256;
    }
    if (luks_opts.ivgen_alg == QCRYPTO_IVGEN_ALG_ESSIV) {
        if (!luks_opts.has_ivgen_hash_alg) {
            luks_opts.ivgen_hash_alg = QCRYPTO_HASH_ALG_SHA256;
            luks_opts.has_ivgen_hash_alg = true;
        }
    }

    luks = g_new0(QCryptoBlockLUKS, 1);
    block->opaque = luks;

    luks->cipher_alg = luks_opts.cipher_alg;
    luks->cipher_mode = luks_opts.cipher_mode;
    luks->ivgen_alg = luks_opts.ivgen_alg;
    luks->ivgen_hash_alg = luks_opts.ivgen_hash_alg;
    luks->hash_alg = luks_opts.hash_alg;


    /* Note we're allowing ivgen_hash_alg to be set even for
     * non-essiv iv generators that don't need a hash. It will
     * be silently ignored, for compatibility with dm-crypt */

    if (!options->u.luks.key_secret) {
        error_setg(errp, "Parameter '%skey-secret' is required for cipher",
                   optprefix ? optprefix : "");
        goto error;
    }
    luks->secret = g_strdup(options->u.luks.key_secret);

    password = qcrypto_secret_lookup_as_utf8(luks_opts.key_secret, errp);
    if (!password) {
        goto error;
    }


    memcpy(luks->header.magic, qcrypto_block_luks_magic,
           QCRYPTO_BLOCK_LUKS_MAGIC_LEN);

    /* We populate the header in native endianness initially and
     * then convert everything to big endian just before writing
     * it out to disk
     */
    luks->header.version = QCRYPTO_BLOCK_LUKS_VERSION;
    qcrypto_block_luks_uuid_gen(luks->header.uuid);

    cipher_alg = qcrypto_block_luks_cipher_alg_lookup(luks_opts.cipher_alg,
                                                      errp);
    if (!cipher_alg) {
        goto error;
    }

    cipher_mode = QCryptoCipherMode_str(luks_opts.cipher_mode);
    ivgen_alg = QCryptoIVGenAlgorithm_str(luks_opts.ivgen_alg);
    if (luks_opts.has_ivgen_hash_alg) {
        ivgen_hash_alg = QCryptoHashAlgorithm_str(luks_opts.ivgen_hash_alg);
        cipher_mode_spec = g_strdup_printf("%s-%s:%s", cipher_mode, ivgen_alg,
                                           ivgen_hash_alg);
    } else {
        cipher_mode_spec = g_strdup_printf("%s-%s", cipher_mode, ivgen_alg);
    }
    hash_alg = QCryptoHashAlgorithm_str(luks_opts.hash_alg);


    if (strlen(cipher_alg) >= QCRYPTO_BLOCK_LUKS_CIPHER_NAME_LEN) {
        error_setg(errp, "Cipher name '%s' is too long for LUKS header",
                   cipher_alg);
        goto error;
    }
    if (strlen(cipher_mode_spec) >= QCRYPTO_BLOCK_LUKS_CIPHER_MODE_LEN) {
        error_setg(errp, "Cipher mode '%s' is too long for LUKS header",
                   cipher_mode_spec);
        goto error;
    }
    if (strlen(hash_alg) >= QCRYPTO_BLOCK_LUKS_HASH_SPEC_LEN) {
        error_setg(errp, "Hash name '%s' is too long for LUKS header",
                   hash_alg);
        goto error;
    }

    if (luks_opts.ivgen_alg == QCRYPTO_IVGEN_ALG_ESSIV) {
        luks->ivgen_cipher_alg =
                qcrypto_block_luks_essiv_cipher(luks_opts.cipher_alg,
                                                luks_opts.ivgen_hash_alg,
                                                &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto error;
        }
    } else {
        luks->ivgen_cipher_alg = luks_opts.cipher_alg;
    }

    strcpy(luks->header.cipher_name, cipher_alg);
    strcpy(luks->header.cipher_mode, cipher_mode_spec);
    strcpy(luks->header.hash_spec, hash_alg);

    luks->header.master_key_len =
        qcrypto_cipher_get_key_len(luks_opts.cipher_alg);

    if (luks_opts.cipher_mode == QCRYPTO_CIPHER_MODE_XTS) {
        luks->header.master_key_len *= 2;
    }

    /* Generate the salt used for hashing the master key
     * with PBKDF later
     */
    if (qcrypto_random_bytes(luks->header.master_key_salt,
                             QCRYPTO_BLOCK_LUKS_SALT_LEN,
                             errp) < 0) {
        goto error;
    }

    /* Generate random master key */
    masterkey = g_new0(uint8_t, luks->header.master_key_len);
    if (qcrypto_random_bytes(masterkey,
                             luks->header.master_key_len, errp) < 0) {
        goto error;
    }


    /* Setup the block device payload encryption objects */
    if (qcrypto_block_init_cipher(block, luks_opts.cipher_alg,
                                  luks_opts.cipher_mode, masterkey,
                                  luks->header.master_key_len, 1, errp) < 0) {
        goto error;
    }

    block->kdfhash = luks_opts.hash_alg;
    block->niv = qcrypto_cipher_get_iv_len(luks_opts.cipher_alg,
                                           luks_opts.cipher_mode);
    block->ivgen = qcrypto_ivgen_new(luks_opts.ivgen_alg,
                                     luks->ivgen_cipher_alg,
                                     luks_opts.ivgen_hash_alg,
                                     masterkey, luks->header.master_key_len,
                                     errp);

    if (!block->ivgen) {
        goto error;
    }


    /* Determine how many iterations we need to hash the master
     * key, in order to have 1 second of compute time used
     */
    iters = qcrypto_pbkdf2_count_iters(luks_opts.hash_alg,
                                       masterkey, luks->header.master_key_len,
                                       luks->header.master_key_salt,
                                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                                       QCRYPTO_BLOCK_LUKS_DIGEST_LEN,
                                       &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    if (iters > (ULLONG_MAX / luks_opts.iter_time)) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu too large to scale",
                         (unsigned long long)iters);
        goto error;
    }

    /* iter_time was in millis, but count_iters reported for secs */
    iters = iters * luks_opts.iter_time / 1000;

    /* Why /= 8 ?  That matches cryptsetup, but there's no
     * explanation why they chose /= 8... Probably so that
     * if all 8 keyslots are active we only spend 1 second
     * in total time to check all keys */
    iters /= 8;
    if (iters > UINT32_MAX) {
        error_setg_errno(errp, ERANGE,
                         "PBKDF iterations %llu larger than %u",
                         (unsigned long long)iters, UINT32_MAX);
        goto error;
    }
    iters = MAX(iters, QCRYPTO_BLOCK_LUKS_MIN_MASTER_KEY_ITERS);
    luks->header.master_key_iterations = iters;

    /* Hash the master key, saving the result in the LUKS
     * header. This hash is used when opening the encrypted
     * device to verify that the user password unlocked a
     * valid master key
     */
    if (qcrypto_pbkdf2(luks_opts.hash_alg,
                       masterkey, luks->header.master_key_len,
                       luks->header.master_key_salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       luks->header.master_key_iterations,
                       luks->header.master_key_digest,
                       QCRYPTO_BLOCK_LUKS_DIGEST_LEN,
                       errp) < 0) {
        goto error;
    }

    /* start with the sector that follows the header*/
    header_sectors = QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;

    split_key_sectors =
        qcrypto_block_luks_splitkeylen_sectors(luks,
                                               header_sectors,
                                               QCRYPTO_BLOCK_LUKS_STRIPES);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        QCryptoBlockLUKSKeySlot *slot = &luks->header.key_slots[i];
        slot->active = QCRYPTO_BLOCK_LUKS_KEY_SLOT_DISABLED;

        slot->key_offset_sector = header_sectors + i * split_key_sectors;
        slot->stripes = QCRYPTO_BLOCK_LUKS_STRIPES;
    }

    /* The total size of the LUKS headers is the partition header + key
     * slot headers, rounded up to the nearest sector, combined with
     * the size of each master key material region, also rounded up
     * to the nearest sector */
    luks->header.payload_offset_sector = header_sectors +
            QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS * split_key_sectors;

    block->sector_size = QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;
    block->payload_offset = luks->header.payload_offset_sector *
        block->sector_size;

    /* Reserve header space to match payload offset */
    initfunc(block, block->payload_offset, opaque, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }


    /* populate the slot 0 with the password encrypted master key*/
    /* This will also store the header */
    if (qcrypto_block_luks_store_key(block,
                                     0,
                                     password,
                                     masterkey,
                                     luks_opts.iter_time,
                                     writefunc,
                                     opaque,
                                     errp) < 0) {
        goto error;
    }

    memset(masterkey, 0, luks->header.master_key_len);

    return 0;

 error:
    if (masterkey) {
        memset(masterkey, 0, luks->header.master_key_len);
    }

    qcrypto_block_free_cipher(block);
    qcrypto_ivgen_free(block->ivgen);

    g_free(luks->secret);
    g_free(luks);
    return -1;
}

static int
qcrypto_block_luks_amend_add_keyslot(QCryptoBlock *block,
                                     QCryptoBlockReadFunc readfunc,
                                     QCryptoBlockWriteFunc writefunc,
                                     void *opaque,
                                     QCryptoBlockAmendOptionsLUKS *opts_luks,
                                     bool force,
                                     Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    uint64_t iter_time = opts_luks->has_iter_time ?
                         opts_luks->iter_time :
                         QCRYPTO_BLOCK_LUKS_DEFAULT_ITER_TIME_MS;
    int keyslot;
    g_autofree char *old_password = NULL;
    g_autofree char *new_password = NULL;
    g_autofree uint8_t *master_key = NULL;

    char *secret = opts_luks->has_secret ? opts_luks->secret : luks->secret;

    if (!opts_luks->has_new_secret) {
        error_setg(errp, "'new-secret' is required to activate a keyslot");
        return -1;
    }
    if (opts_luks->has_old_secret) {
        error_setg(errp,
                   "'old-secret' must not be given when activating keyslots");
        return -1;
    }

    if (opts_luks->has_keyslot) {
        keyslot = opts_luks->keyslot;
        if (keyslot < 0 || keyslot >= QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS) {
            error_setg(errp,
                       "Invalid keyslot %u specified, must be between 0 and %u",
                       keyslot, QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS - 1);
            return -1;
        }
    } else {
        keyslot = qcrypto_block_luks_find_free_keyslot(luks);
        if (keyslot == -1) {
            error_setg(errp,
                       "Can't add a keyslot - all keyslots are in use");
            return -1;
        }
    }

    if (!force && qcrypto_block_luks_slot_active(luks, keyslot)) {
        error_setg(errp,
                   "Refusing to overwrite active keyslot %i - "
                   "please erase it first",
                   keyslot);
        return -1;
    }

    /* Locate the password that will be used to retrieve the master key */
    old_password = qcrypto_secret_lookup_as_utf8(secret, errp);
    if (!old_password) {
        return -1;
    }

    /* Retrieve the master key */
    master_key = g_new0(uint8_t, luks->header.master_key_len);

    if (qcrypto_block_luks_find_key(block, old_password, master_key,
                                    readfunc, opaque, errp) < 0) {
        error_append_hint(errp, "Failed to retrieve the master key");
        return -1;
    }

    /* Locate the new password*/
    new_password = qcrypto_secret_lookup_as_utf8(opts_luks->new_secret, errp);
    if (!new_password) {
        return -1;
    }

    /* Now set the new keyslots */
    if (qcrypto_block_luks_store_key(block, keyslot, new_password, master_key,
                                     iter_time, writefunc, opaque, errp)) {
        error_append_hint(errp, "Failed to write to keyslot %i", keyslot);
        return -1;
    }
    return 0;
}

static int
qcrypto_block_luks_amend_erase_keyslots(QCryptoBlock *block,
                                        QCryptoBlockReadFunc readfunc,
                                        QCryptoBlockWriteFunc writefunc,
                                        void *opaque,
                                        QCryptoBlockAmendOptionsLUKS *opts_luks,
                                        bool force,
                                        Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    g_autofree uint8_t *tmpkey = NULL;
    g_autofree char *old_password = NULL;

    if (opts_luks->has_new_secret) {
        error_setg(errp,
                   "'new-secret' must not be given when erasing keyslots");
        return -1;
    }
    if (opts_luks->has_iter_time) {
        error_setg(errp,
                   "'iter-time' must not be given when erasing keyslots");
        return -1;
    }
    if (opts_luks->has_secret) {
        error_setg(errp,
                   "'secret' must not be given when erasing keyslots");
        return -1;
    }

    /* Load the old password if given */
    if (opts_luks->has_old_secret) {
        old_password = qcrypto_secret_lookup_as_utf8(opts_luks->old_secret,
                                                     errp);
        if (!old_password) {
            return -1;
        }

        /*
         * Allocate a temporary key buffer that we will need when
         * checking if slot matches the given old password
         */
        tmpkey = g_new0(uint8_t, luks->header.master_key_len);
    }

    /* Erase an explicitly given keyslot */
    if (opts_luks->has_keyslot) {
        int keyslot = opts_luks->keyslot;

        if (keyslot < 0 || keyslot >= QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS) {
            error_setg(errp,
                       "Invalid keyslot %i specified, must be between 0 and %i",
                       keyslot, QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS - 1);
            return -1;
        }

        if (opts_luks->has_old_secret) {
            int rv = qcrypto_block_luks_load_key(block,
                                                 keyslot,
                                                 old_password,
                                                 tmpkey,
                                                 readfunc,
                                                 opaque,
                                                 errp);
            if (rv == -1) {
                return -1;
            } else if (rv == 0) {
                error_setg(errp,
                           "Given keyslot %i doesn't contain the given "
                           "old password for erase operation",
                           keyslot);
                return -1;
            }
        }

        if (!force && !qcrypto_block_luks_slot_active(luks, keyslot)) {
            error_setg(errp,
                       "Given keyslot %i is already erased (inactive) ",
                       keyslot);
            return -1;
        }

        if (!force && qcrypto_block_luks_count_active_slots(luks) == 1) {
            error_setg(errp,
                       "Attempt to erase the only active keyslot %i "
                       "which will erase all the data in the image "
                       "irreversibly - refusing operation",
                       keyslot);
            return -1;
        }

        if (qcrypto_block_luks_erase_key(block, keyslot,
                                         writefunc, opaque, errp)) {
            error_append_hint(errp, "Failed to erase keyslot %i", keyslot);
            return -1;
        }

    /* Erase all keyslots that match the given old password */
    } else if (opts_luks->has_old_secret) {

        unsigned long slots_to_erase_bitmap = 0;
        size_t i;
        int slot_count;

        assert(QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS <=
               sizeof(slots_to_erase_bitmap) * 8);

        for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
            int rv = qcrypto_block_luks_load_key(block,
                                                 i,
                                                 old_password,
                                                 tmpkey,
                                                 readfunc,
                                                 opaque,
                                                 errp);
            if (rv == -1) {
                return -1;
            } else if (rv == 1) {
                bitmap_set(&slots_to_erase_bitmap, i, 1);
            }
        }

        slot_count = bitmap_count_one(&slots_to_erase_bitmap,
                                      QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);
        if (slot_count == 0) {
            error_setg(errp,
                       "No keyslots match given (old) password for erase operation");
            return -1;
        }

        if (!force &&
            slot_count == qcrypto_block_luks_count_active_slots(luks)) {
            error_setg(errp,
                       "All the active keyslots match the (old) password that "
                       "was given and erasing them will erase all the data in "
                       "the image irreversibly - refusing operation");
            return -1;
        }

        /* Now apply the update */
        for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
            if (!test_bit(i, &slots_to_erase_bitmap)) {
                continue;
            }
            if (qcrypto_block_luks_erase_key(block, i, writefunc,
                opaque, errp)) {
                error_append_hint(errp, "Failed to erase keyslot %zu", i);
                return -1;
            }
        }
    } else {
        error_setg(errp,
                   "To erase keyslot(s), either explicit keyslot index "
                   "or the password currently contained in them must be given");
        return -1;
    }
    return 0;
}

static int
qcrypto_block_luks_amend_options(QCryptoBlock *block,
                                 QCryptoBlockReadFunc readfunc,
                                 QCryptoBlockWriteFunc writefunc,
                                 void *opaque,
                                 QCryptoBlockAmendOptions *options,
                                 bool force,
                                 Error **errp)
{
    QCryptoBlockAmendOptionsLUKS *opts_luks = &options->u.luks;

    switch (opts_luks->state) {
    case Q_CRYPTO_BLOCKLUKS_KEYSLOT_STATE_ACTIVE:
        return qcrypto_block_luks_amend_add_keyslot(block, readfunc,
                                                    writefunc, opaque,
                                                    opts_luks, force, errp);
    case Q_CRYPTO_BLOCKLUKS_KEYSLOT_STATE_INACTIVE:
        return qcrypto_block_luks_amend_erase_keyslots(block, readfunc,
                                                       writefunc, opaque,
                                                       opts_luks, force, errp);
    default:
        g_assert_not_reached();
    }
}

static int qcrypto_block_luks_get_info(QCryptoBlock *block,
                                       QCryptoBlockInfo *info,
                                       Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    QCryptoBlockInfoLUKSSlot *slot;
    QCryptoBlockInfoLUKSSlotList *slots = NULL, **prev = &info->u.luks.slots;
    size_t i;

    info->u.luks.cipher_alg = luks->cipher_alg;
    info->u.luks.cipher_mode = luks->cipher_mode;
    info->u.luks.ivgen_alg = luks->ivgen_alg;
    if (info->u.luks.ivgen_alg == QCRYPTO_IVGEN_ALG_ESSIV) {
        info->u.luks.has_ivgen_hash_alg = true;
        info->u.luks.ivgen_hash_alg = luks->ivgen_hash_alg;
    }
    info->u.luks.hash_alg = luks->hash_alg;
    info->u.luks.payload_offset = block->payload_offset;
    info->u.luks.master_key_iters = luks->header.master_key_iterations;
    info->u.luks.uuid = g_strndup((const char *)luks->header.uuid,
                                  sizeof(luks->header.uuid));

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        slots = g_new0(QCryptoBlockInfoLUKSSlotList, 1);
        *prev = slots;

        slots->value = slot = g_new0(QCryptoBlockInfoLUKSSlot, 1);
        slot->active = luks->header.key_slots[i].active ==
            QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED;
        slot->key_offset = luks->header.key_slots[i].key_offset_sector
             * QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;
        if (slot->active) {
            slot->has_iters = true;
            slot->iters = luks->header.key_slots[i].iterations;
            slot->has_stripes = true;
            slot->stripes = luks->header.key_slots[i].stripes;
        }

        prev = &slots->next;
    }

    return 0;
}


static void qcrypto_block_luks_cleanup(QCryptoBlock *block)
{
    QCryptoBlockLUKS *luks = block->opaque;
    if (luks) {
        g_free(luks->secret);
        g_free(luks);
    }
}


static int
qcrypto_block_luks_decrypt(QCryptoBlock *block,
                           uint64_t offset,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    assert(QEMU_IS_ALIGNED(offset, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(len, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE));
    return qcrypto_block_decrypt_helper(block,
                                        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                        offset, buf, len, errp);
}


static int
qcrypto_block_luks_encrypt(QCryptoBlock *block,
                           uint64_t offset,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    assert(QEMU_IS_ALIGNED(offset, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE));
    assert(QEMU_IS_ALIGNED(len, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE));
    return qcrypto_block_encrypt_helper(block,
                                        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                        offset, buf, len, errp);
}


const QCryptoBlockDriver qcrypto_block_driver_luks = {
    .open = qcrypto_block_luks_open,
    .create = qcrypto_block_luks_create,
    .amend = qcrypto_block_luks_amend_options,
    .get_info = qcrypto_block_luks_get_info,
    .cleanup = qcrypto_block_luks_cleanup,
    .decrypt = qcrypto_block_luks_decrypt,
    .encrypt = qcrypto_block_luks_encrypt,
    .has_format = qcrypto_block_luks_has_format,
};
