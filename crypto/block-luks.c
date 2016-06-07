/*
 * QEMU Crypto block device encryption LUKS format
 *
 * Copyright (c) 2015-2016 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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

#include "crypto/block-luks.h"

#include "crypto/hash.h"
#include "crypto/afsplit.h"
#include "crypto/pbkdf.h"
#include "crypto/secret.h"
#include "crypto/random.h"

#ifdef CONFIG_UUID
#include <uuid/uuid.h>
#endif

#include "qemu/coroutine.h"

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
    uint32_t key_offset;
    /* number of anti-forensic stripes */
    uint32_t stripes;
} QEMU_PACKED;

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
    uint32_t payload_offset;

    /* Number of key bytes */
    uint32_t key_bytes;

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
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct QCryptoBlockLUKSHeader) != 592);


struct QCryptoBlockLUKS {
    QCryptoBlockLUKSHeader header;
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
               QCryptoCipherAlgorithm_lookup[alg]);
    return NULL;
}

/* XXX replace with qapi_enum_parse() in future, when we can
 * make that function emit a more friendly error message */
static int qcrypto_block_luks_name_lookup(const char *name,
                                          const char *const *map,
                                          size_t maplen,
                                          const char *type,
                                          Error **errp)
{
    size_t i;
    for (i = 0; i < maplen; i++) {
        if (g_str_equal(map[i], name)) {
            return i;
        }
    }

    error_setg(errp, "%s %s not supported", type, name);
    return 0;
}

#define qcrypto_block_luks_cipher_mode_lookup(name, errp)               \
    qcrypto_block_luks_name_lookup(name,                                \
                                   QCryptoCipherMode_lookup,            \
                                   QCRYPTO_CIPHER_MODE__MAX,            \
                                   "Cipher mode",                       \
                                   errp)

#define qcrypto_block_luks_hash_name_lookup(name, errp)                 \
    qcrypto_block_luks_name_lookup(name,                                \
                                   QCryptoHashAlgorithm_lookup,         \
                                   QCRYPTO_HASH_ALG__MAX,               \
                                   "Hash algorithm",                    \
                                   errp)

#define qcrypto_block_luks_ivgen_name_lookup(name, errp)                \
    qcrypto_block_luks_name_lookup(name,                                \
                                   QCryptoIVGenAlgorithm_lookup,        \
                                   QCRYPTO_IVGEN_ALG__MAX,              \
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
                   QCryptoCipherAlgorithm_lookup[cipher]);
        return 0;
    }
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
                            QCryptoBlockLUKSKeySlot *slot,
                            const char *password,
                            QCryptoCipherAlgorithm cipheralg,
                            QCryptoCipherMode ciphermode,
                            QCryptoHashAlgorithm hash,
                            QCryptoIVGenAlgorithm ivalg,
                            QCryptoCipherAlgorithm ivcipheralg,
                            QCryptoHashAlgorithm ivhash,
                            uint8_t *masterkey,
                            size_t masterkeylen,
                            QCryptoBlockReadFunc readfunc,
                            void *opaque,
                            Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    uint8_t *splitkey;
    size_t splitkeylen;
    uint8_t *possiblekey;
    int ret = -1;
    ssize_t rv;
    QCryptoCipher *cipher = NULL;
    uint8_t keydigest[QCRYPTO_BLOCK_LUKS_DIGEST_LEN];
    QCryptoIVGen *ivgen = NULL;
    size_t niv;

    if (slot->active != QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED) {
        return 0;
    }

    splitkeylen = masterkeylen * slot->stripes;
    splitkey = g_new0(uint8_t, splitkeylen);
    possiblekey = g_new0(uint8_t, masterkeylen);

    /*
     * The user password is used to generate a (possible)
     * decryption key. This may or may not successfully
     * decrypt the master key - we just blindly assume
     * the key is correct and validate the results of
     * decryption later.
     */
    if (qcrypto_pbkdf2(hash,
                       (const uint8_t *)password, strlen(password),
                       slot->salt, QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       slot->iterations,
                       possiblekey, masterkeylen,
                       errp) < 0) {
        goto cleanup;
    }

    /*
     * We need to read the master key material from the
     * LUKS key material header. What we're reading is
     * not the raw master key, but rather the data after
     * it has been passed through AFSplit and the result
     * then encrypted.
     */
    rv = readfunc(block,
                  slot->key_offset * QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                  splitkey, splitkeylen,
                  errp,
                  opaque);
    if (rv < 0) {
        goto cleanup;
    }


    /* Setup the cipher/ivgen that we'll use to try to decrypt
     * the split master key material */
    cipher = qcrypto_cipher_new(cipheralg, ciphermode,
                                possiblekey, masterkeylen,
                                errp);
    if (!cipher) {
        goto cleanup;
    }

    niv = qcrypto_cipher_get_iv_len(cipheralg,
                                    ciphermode);
    ivgen = qcrypto_ivgen_new(ivalg,
                              ivcipheralg,
                              ivhash,
                              possiblekey, masterkeylen,
                              errp);
    if (!ivgen) {
        goto cleanup;
    }


    /*
     * The master key needs to be decrypted in the same
     * way that the block device payload will be decrypted
     * later. In particular we'll be using the IV generator
     * to reset the encryption cipher every time the master
     * key crosses a sector boundary.
     */
    if (qcrypto_block_decrypt_helper(cipher,
                                     niv,
                                     ivgen,
                                     QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                     0,
                                     splitkey,
                                     splitkeylen,
                                     errp) < 0) {
        goto cleanup;
    }

    /*
     * Now we've decrypted the split master key, join
     * it back together to get the actual master key.
     */
    if (qcrypto_afsplit_decode(hash,
                               masterkeylen,
                               slot->stripes,
                               splitkey,
                               masterkey,
                               errp) < 0) {
        goto cleanup;
    }


    /*
     * We still don't know that the masterkey we got is valid,
     * because we just blindly assumed the user's password
     * was correct. This is where we now verify it. We are
     * creating a hash of the master key using PBKDF and
     * then comparing that to the hash stored in the key slot
     * header
     */
    if (qcrypto_pbkdf2(hash,
                       masterkey, masterkeylen,
                       luks->header.master_key_salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       luks->header.master_key_iterations,
                       keydigest, G_N_ELEMENTS(keydigest),
                       errp) < 0) {
        goto cleanup;
    }

    if (memcmp(keydigest, luks->header.master_key_digest,
               QCRYPTO_BLOCK_LUKS_DIGEST_LEN) == 0) {
        /* Success, we got the right master key */
        ret = 1;
        goto cleanup;
    }

    /* Fail, user's password was not valid for this key slot,
     * tell caller to try another slot */
    ret = 0;

 cleanup:
    qcrypto_ivgen_free(ivgen);
    qcrypto_cipher_free(cipher);
    g_free(splitkey);
    g_free(possiblekey);
    return ret;
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
                            QCryptoCipherAlgorithm cipheralg,
                            QCryptoCipherMode ciphermode,
                            QCryptoHashAlgorithm hash,
                            QCryptoIVGenAlgorithm ivalg,
                            QCryptoCipherAlgorithm ivcipheralg,
                            QCryptoHashAlgorithm ivhash,
                            uint8_t **masterkey,
                            size_t *masterkeylen,
                            QCryptoBlockReadFunc readfunc,
                            void *opaque,
                            Error **errp)
{
    QCryptoBlockLUKS *luks = block->opaque;
    size_t i;
    int rv;

    *masterkey = g_new0(uint8_t, luks->header.key_bytes);
    *masterkeylen = luks->header.key_bytes;

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        rv = qcrypto_block_luks_load_key(block,
                                         &luks->header.key_slots[i],
                                         password,
                                         cipheralg,
                                         ciphermode,
                                         hash,
                                         ivalg,
                                         ivcipheralg,
                                         ivhash,
                                         *masterkey,
                                         *masterkeylen,
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
    g_free(*masterkey);
    *masterkey = NULL;
    *masterkeylen = 0;
    return -1;
}


static int
qcrypto_block_luks_open(QCryptoBlock *block,
                        QCryptoBlockOpenOptions *options,
                        QCryptoBlockReadFunc readfunc,
                        void *opaque,
                        unsigned int flags,
                        Error **errp)
{
    QCryptoBlockLUKS *luks;
    Error *local_err = NULL;
    int ret = 0;
    size_t i;
    ssize_t rv;
    uint8_t *masterkey = NULL;
    size_t masterkeylen;
    char *ivgen_name, *ivhash_name;
    QCryptoCipherMode ciphermode;
    QCryptoCipherAlgorithm cipheralg;
    QCryptoIVGenAlgorithm ivalg;
    QCryptoCipherAlgorithm ivcipheralg;
    QCryptoHashAlgorithm hash;
    QCryptoHashAlgorithm ivhash;
    char *password = NULL;

    if (!(flags & QCRYPTO_BLOCK_OPEN_NO_IO)) {
        if (!options->u.luks.key_secret) {
            error_setg(errp, "Parameter 'key-secret' is required for cipher");
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

    /* Read the entire LUKS header, minus the key material from
     * the underlying device */
    rv = readfunc(block, 0,
                  (uint8_t *)&luks->header,
                  sizeof(luks->header),
                  errp,
                  opaque);
    if (rv < 0) {
        ret = rv;
        goto fail;
    }

    /* The header is always stored in big-endian format, so
     * convert everything to native */
    be16_to_cpus(&luks->header.version);
    be32_to_cpus(&luks->header.payload_offset);
    be32_to_cpus(&luks->header.key_bytes);
    be32_to_cpus(&luks->header.master_key_iterations);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        be32_to_cpus(&luks->header.key_slots[i].active);
        be32_to_cpus(&luks->header.key_slots[i].iterations);
        be32_to_cpus(&luks->header.key_slots[i].key_offset);
        be32_to_cpus(&luks->header.key_slots[i].stripes);
    }

    if (memcmp(luks->header.magic, qcrypto_block_luks_magic,
               QCRYPTO_BLOCK_LUKS_MAGIC_LEN) != 0) {
        error_setg(errp, "Volume is not in LUKS format");
        ret = -EINVAL;
        goto fail;
    }
    if (luks->header.version != QCRYPTO_BLOCK_LUKS_VERSION) {
        error_setg(errp, "LUKS version %" PRIu32 " is not supported",
                   luks->header.version);
        ret = -ENOTSUP;
        goto fail;
    }

    /*
     * The cipher_mode header contains a string that we have
     * to further parse, of the format
     *
     *    <cipher-mode>-<iv-generator>[:<iv-hash>]
     *
     * eg  cbc-essiv:sha256, cbc-plain64
     */
    ivgen_name = strchr(luks->header.cipher_mode, '-');
    if (!ivgen_name) {
        ret = -EINVAL;
        error_setg(errp, "Unexpected cipher mode string format %s",
                   luks->header.cipher_mode);
        goto fail;
    }
    *ivgen_name = '\0';
    ivgen_name++;

    ivhash_name = strchr(ivgen_name, ':');
    if (!ivhash_name) {
        ivhash = 0;
    } else {
        *ivhash_name = '\0';
        ivhash_name++;

        ivhash = qcrypto_block_luks_hash_name_lookup(ivhash_name,
                                                     &local_err);
        if (local_err) {
            ret = -ENOTSUP;
            error_propagate(errp, local_err);
            goto fail;
        }
    }

    ciphermode = qcrypto_block_luks_cipher_mode_lookup(luks->header.cipher_mode,
                                                       &local_err);
    if (local_err) {
        ret = -ENOTSUP;
        error_propagate(errp, local_err);
        goto fail;
    }

    cipheralg = qcrypto_block_luks_cipher_name_lookup(luks->header.cipher_name,
                                                      ciphermode,
                                                      luks->header.key_bytes,
                                                      &local_err);
    if (local_err) {
        ret = -ENOTSUP;
        error_propagate(errp, local_err);
        goto fail;
    }

    hash = qcrypto_block_luks_hash_name_lookup(luks->header.hash_spec,
                                               &local_err);
    if (local_err) {
        ret = -ENOTSUP;
        error_propagate(errp, local_err);
        goto fail;
    }

    ivalg = qcrypto_block_luks_ivgen_name_lookup(ivgen_name,
                                                 &local_err);
    if (local_err) {
        ret = -ENOTSUP;
        error_propagate(errp, local_err);
        goto fail;
    }

    if (ivalg == QCRYPTO_IVGEN_ALG_ESSIV) {
        ivcipheralg = qcrypto_block_luks_essiv_cipher(cipheralg,
                                                      ivhash,
                                                      &local_err);
        if (local_err) {
            ret = -ENOTSUP;
            error_propagate(errp, local_err);
            goto fail;
        }
    } else {
        ivcipheralg = cipheralg;
    }

    if (!(flags & QCRYPTO_BLOCK_OPEN_NO_IO)) {
        /* Try to find which key slot our password is valid for
         * and unlock the master key from that slot.
         */
        if (qcrypto_block_luks_find_key(block,
                                        password,
                                        cipheralg, ciphermode,
                                        hash,
                                        ivalg,
                                        ivcipheralg,
                                        ivhash,
                                        &masterkey, &masterkeylen,
                                        readfunc, opaque,
                                        errp) < 0) {
            ret = -EACCES;
            goto fail;
        }

        /* We have a valid master key now, so can setup the
         * block device payload decryption objects
         */
        block->kdfhash = hash;
        block->niv = qcrypto_cipher_get_iv_len(cipheralg,
                                               ciphermode);
        block->ivgen = qcrypto_ivgen_new(ivalg,
                                         ivcipheralg,
                                         ivhash,
                                         masterkey, masterkeylen,
                                         errp);
        if (!block->ivgen) {
            ret = -ENOTSUP;
            goto fail;
        }

        block->cipher = qcrypto_cipher_new(cipheralg,
                                           ciphermode,
                                           masterkey, masterkeylen,
                                           errp);
        if (!block->cipher) {
            ret = -ENOTSUP;
            goto fail;
        }
    }

    block->payload_offset = luks->header.payload_offset *
        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;

    g_free(masterkey);
    g_free(password);

    return 0;

 fail:
    g_free(masterkey);
    qcrypto_cipher_free(block->cipher);
    qcrypto_ivgen_free(block->ivgen);
    g_free(luks);
    g_free(password);
    return ret;
}


static int
qcrypto_block_luks_uuid_gen(uint8_t *uuidstr, Error **errp)
{
#ifdef CONFIG_UUID
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, (char *)uuidstr);
    return 0;
#else
    error_setg(errp, "Unable to generate uuids on this platform");
    return -1;
#endif
}

static int
qcrypto_block_luks_create(QCryptoBlock *block,
                          QCryptoBlockCreateOptions *options,
                          QCryptoBlockInitFunc initfunc,
                          QCryptoBlockWriteFunc writefunc,
                          void *opaque,
                          Error **errp)
{
    QCryptoBlockLUKS *luks;
    QCryptoBlockCreateOptionsLUKS luks_opts;
    Error *local_err = NULL;
    uint8_t *masterkey = NULL;
    uint8_t *slotkey = NULL;
    uint8_t *splitkey = NULL;
    size_t splitkeylen = 0;
    size_t i;
    QCryptoCipher *cipher = NULL;
    QCryptoIVGen *ivgen = NULL;
    char *password;
    const char *cipher_alg;
    const char *cipher_mode;
    const char *ivgen_alg;
    const char *ivgen_hash_alg = NULL;
    const char *hash_alg;
    char *cipher_mode_spec = NULL;
    QCryptoCipherAlgorithm ivcipheralg = 0;

    memcpy(&luks_opts, &options->u.luks, sizeof(luks_opts));
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

    if (!options->u.luks.key_secret) {
        error_setg(errp, "Parameter 'key-secret' is required for cipher");
        return -1;
    }
    password = qcrypto_secret_lookup_as_utf8(luks_opts.key_secret, errp);
    if (!password) {
        return -1;
    }

    luks = g_new0(QCryptoBlockLUKS, 1);
    block->opaque = luks;

    memcpy(luks->header.magic, qcrypto_block_luks_magic,
           QCRYPTO_BLOCK_LUKS_MAGIC_LEN);

    /* We populate the header in native endianness initially and
     * then convert everything to big endian just before writing
     * it out to disk
     */
    luks->header.version = QCRYPTO_BLOCK_LUKS_VERSION;
    if (qcrypto_block_luks_uuid_gen(luks->header.uuid,
                                    errp) < 0) {
        goto error;
    }

    cipher_alg = qcrypto_block_luks_cipher_alg_lookup(luks_opts.cipher_alg,
                                                      errp);
    if (!cipher_alg) {
        goto error;
    }

    cipher_mode = QCryptoCipherMode_lookup[luks_opts.cipher_mode];
    ivgen_alg = QCryptoIVGenAlgorithm_lookup[luks_opts.ivgen_alg];
    if (luks_opts.has_ivgen_hash_alg) {
        ivgen_hash_alg = QCryptoHashAlgorithm_lookup[luks_opts.ivgen_hash_alg];
        cipher_mode_spec = g_strdup_printf("%s-%s:%s", cipher_mode, ivgen_alg,
                                           ivgen_hash_alg);
    } else {
        cipher_mode_spec = g_strdup_printf("%s-%s", cipher_mode, ivgen_alg);
    }
    hash_alg = QCryptoHashAlgorithm_lookup[luks_opts.hash_alg];


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
        ivcipheralg = qcrypto_block_luks_essiv_cipher(luks_opts.cipher_alg,
                                                      luks_opts.ivgen_hash_alg,
                                                      &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            goto error;
        }
    } else {
        ivcipheralg = luks_opts.cipher_alg;
    }

    strcpy(luks->header.cipher_name, cipher_alg);
    strcpy(luks->header.cipher_mode, cipher_mode_spec);
    strcpy(luks->header.hash_spec, hash_alg);

    luks->header.key_bytes = qcrypto_cipher_get_key_len(luks_opts.cipher_alg);
    if (luks_opts.cipher_mode == QCRYPTO_CIPHER_MODE_XTS) {
        luks->header.key_bytes *= 2;
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
    masterkey = g_new0(uint8_t, luks->header.key_bytes);
    if (qcrypto_random_bytes(masterkey,
                             luks->header.key_bytes, errp) < 0) {
        goto error;
    }


    /* Setup the block device payload encryption objects */
    block->cipher = qcrypto_cipher_new(luks_opts.cipher_alg,
                                       luks_opts.cipher_mode,
                                       masterkey, luks->header.key_bytes,
                                       errp);
    if (!block->cipher) {
        goto error;
    }

    block->kdfhash = luks_opts.hash_alg;
    block->niv = qcrypto_cipher_get_iv_len(luks_opts.cipher_alg,
                                           luks_opts.cipher_mode);
    block->ivgen = qcrypto_ivgen_new(luks_opts.ivgen_alg,
                                     ivcipheralg,
                                     luks_opts.ivgen_hash_alg,
                                     masterkey, luks->header.key_bytes,
                                     errp);

    if (!block->ivgen) {
        goto error;
    }


    /* Determine how many iterations we need to hash the master
     * key, in order to have 1 second of compute time used
     */
    luks->header.master_key_iterations =
        qcrypto_pbkdf2_count_iters(luks_opts.hash_alg,
                                   masterkey, luks->header.key_bytes,
                                   luks->header.master_key_salt,
                                   QCRYPTO_BLOCK_LUKS_SALT_LEN,
                                   &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    /* Why /= 8 ?  That matches cryptsetup, but there's no
     * explanation why they chose /= 8... Probably so that
     * if all 8 keyslots are active we only spend 1 second
     * in total time to check all keys */
    luks->header.master_key_iterations /= 8;
    luks->header.master_key_iterations = MAX(
        luks->header.master_key_iterations,
        QCRYPTO_BLOCK_LUKS_MIN_MASTER_KEY_ITERS);


    /* Hash the master key, saving the result in the LUKS
     * header. This hash is used when opening the encrypted
     * device to verify that the user password unlocked a
     * valid master key
     */
    if (qcrypto_pbkdf2(luks_opts.hash_alg,
                       masterkey, luks->header.key_bytes,
                       luks->header.master_key_salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       luks->header.master_key_iterations,
                       luks->header.master_key_digest,
                       QCRYPTO_BLOCK_LUKS_DIGEST_LEN,
                       errp) < 0) {
        goto error;
    }


    /* Although LUKS has multiple key slots, we're just going
     * to use the first key slot */
    splitkeylen = luks->header.key_bytes * QCRYPTO_BLOCK_LUKS_STRIPES;
    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        luks->header.key_slots[i].active = i == 0 ?
            QCRYPTO_BLOCK_LUKS_KEY_SLOT_ENABLED :
            QCRYPTO_BLOCK_LUKS_KEY_SLOT_DISABLED;
        luks->header.key_slots[i].stripes = QCRYPTO_BLOCK_LUKS_STRIPES;

        /* This calculation doesn't match that shown in the spec,
         * but instead follows the cryptsetup implementation.
         */
        luks->header.key_slots[i].key_offset =
            (QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
             QCRYPTO_BLOCK_LUKS_SECTOR_SIZE) +
            (ROUND_UP(DIV_ROUND_UP(splitkeylen, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE),
                      (QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
                       QCRYPTO_BLOCK_LUKS_SECTOR_SIZE)) * i);
    }

    if (qcrypto_random_bytes(luks->header.key_slots[0].salt,
                             QCRYPTO_BLOCK_LUKS_SALT_LEN,
                             errp) < 0) {
        goto error;
    }

    /* Again we determine how many iterations are required to
     * hash the user password while consuming 1 second of compute
     * time */
    luks->header.key_slots[0].iterations =
        qcrypto_pbkdf2_count_iters(luks_opts.hash_alg,
                                   (uint8_t *)password, strlen(password),
                                   luks->header.key_slots[0].salt,
                                   QCRYPTO_BLOCK_LUKS_SALT_LEN,
                                   &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }
    /* Why /= 2 ?  That matches cryptsetup, but there's no
     * explanation why they chose /= 2... */
    luks->header.key_slots[0].iterations /= 2;
    luks->header.key_slots[0].iterations = MAX(
        luks->header.key_slots[0].iterations,
        QCRYPTO_BLOCK_LUKS_MIN_SLOT_KEY_ITERS);


    /* Generate a key that we'll use to encrypt the master
     * key, from the user's password
     */
    slotkey = g_new0(uint8_t, luks->header.key_bytes);
    if (qcrypto_pbkdf2(luks_opts.hash_alg,
                       (uint8_t *)password, strlen(password),
                       luks->header.key_slots[0].salt,
                       QCRYPTO_BLOCK_LUKS_SALT_LEN,
                       luks->header.key_slots[0].iterations,
                       slotkey, luks->header.key_bytes,
                       errp) < 0) {
        goto error;
    }


    /* Setup the encryption objects needed to encrypt the
     * master key material
     */
    cipher = qcrypto_cipher_new(luks_opts.cipher_alg,
                                luks_opts.cipher_mode,
                                slotkey, luks->header.key_bytes,
                                errp);
    if (!cipher) {
        goto error;
    }

    ivgen = qcrypto_ivgen_new(luks_opts.ivgen_alg,
                              ivcipheralg,
                              luks_opts.ivgen_hash_alg,
                              slotkey, luks->header.key_bytes,
                              errp);
    if (!ivgen) {
        goto error;
    }

    /* Before storing the master key, we need to vastly
     * increase its size, as protection against forensic
     * disk data recovery */
    splitkey = g_new0(uint8_t, splitkeylen);

    if (qcrypto_afsplit_encode(luks_opts.hash_alg,
                               luks->header.key_bytes,
                               luks->header.key_slots[0].stripes,
                               masterkey,
                               splitkey,
                               errp) < 0) {
        goto error;
    }

    /* Now we encrypt the split master key with the key generated
     * from the user's password, before storing it */
    if (qcrypto_block_encrypt_helper(cipher, block->niv, ivgen,
                                     QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                     0,
                                     splitkey,
                                     splitkeylen,
                                     errp) < 0) {
        goto error;
    }


    /* The total size of the LUKS headers is the partition header + key
     * slot headers, rounded up to the nearest sector, combined with
     * the size of each master key material region, also rounded up
     * to the nearest sector */
    luks->header.payload_offset =
        (QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
         QCRYPTO_BLOCK_LUKS_SECTOR_SIZE) +
        (ROUND_UP(DIV_ROUND_UP(splitkeylen, QCRYPTO_BLOCK_LUKS_SECTOR_SIZE),
                  (QCRYPTO_BLOCK_LUKS_KEY_SLOT_OFFSET /
                   QCRYPTO_BLOCK_LUKS_SECTOR_SIZE)) *
         QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS);

    block->payload_offset = luks->header.payload_offset *
        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE;

    /* Reserve header space to match payload offset */
    initfunc(block, block->payload_offset, &local_err, opaque);
    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    /* Everything on disk uses Big Endian, so flip header fields
     * before writing them */
    cpu_to_be16s(&luks->header.version);
    cpu_to_be32s(&luks->header.payload_offset);
    cpu_to_be32s(&luks->header.key_bytes);
    cpu_to_be32s(&luks->header.master_key_iterations);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        cpu_to_be32s(&luks->header.key_slots[i].active);
        cpu_to_be32s(&luks->header.key_slots[i].iterations);
        cpu_to_be32s(&luks->header.key_slots[i].key_offset);
        cpu_to_be32s(&luks->header.key_slots[i].stripes);
    }


    /* Write out the partition header and key slot headers */
    writefunc(block, 0,
              (const uint8_t *)&luks->header,
              sizeof(luks->header),
              &local_err,
              opaque);

    /* Delay checking local_err until we've byte-swapped */

    /* Byte swap the header back to native, in case we need
     * to read it again later */
    be16_to_cpus(&luks->header.version);
    be32_to_cpus(&luks->header.payload_offset);
    be32_to_cpus(&luks->header.key_bytes);
    be32_to_cpus(&luks->header.master_key_iterations);

    for (i = 0; i < QCRYPTO_BLOCK_LUKS_NUM_KEY_SLOTS; i++) {
        be32_to_cpus(&luks->header.key_slots[i].active);
        be32_to_cpus(&luks->header.key_slots[i].iterations);
        be32_to_cpus(&luks->header.key_slots[i].key_offset);
        be32_to_cpus(&luks->header.key_slots[i].stripes);
    }

    if (local_err) {
        error_propagate(errp, local_err);
        goto error;
    }

    /* Write out the master key material, starting at the
     * sector immediately following the partition header. */
    if (writefunc(block,
                  luks->header.key_slots[0].key_offset *
                  QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                  splitkey, splitkeylen,
                  errp,
                  opaque) != splitkeylen) {
        goto error;
    }

    memset(masterkey, 0, luks->header.key_bytes);
    g_free(masterkey);
    memset(slotkey, 0, luks->header.key_bytes);
    g_free(slotkey);
    g_free(splitkey);
    g_free(password);
    g_free(cipher_mode_spec);

    qcrypto_ivgen_free(ivgen);
    qcrypto_cipher_free(cipher);

    return 0;

 error:
    if (masterkey) {
        memset(masterkey, 0, luks->header.key_bytes);
    }
    g_free(masterkey);
    if (slotkey) {
        memset(slotkey, 0, luks->header.key_bytes);
    }
    g_free(slotkey);
    g_free(splitkey);
    g_free(password);
    g_free(cipher_mode_spec);

    qcrypto_ivgen_free(ivgen);
    qcrypto_cipher_free(cipher);

    g_free(luks);
    return -1;
}


static void qcrypto_block_luks_cleanup(QCryptoBlock *block)
{
    g_free(block->opaque);
}


static int
qcrypto_block_luks_decrypt(QCryptoBlock *block,
                           uint64_t startsector,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    return qcrypto_block_decrypt_helper(block->cipher,
                                        block->niv, block->ivgen,
                                        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                        startsector, buf, len, errp);
}


static int
qcrypto_block_luks_encrypt(QCryptoBlock *block,
                           uint64_t startsector,
                           uint8_t *buf,
                           size_t len,
                           Error **errp)
{
    return qcrypto_block_encrypt_helper(block->cipher,
                                        block->niv, block->ivgen,
                                        QCRYPTO_BLOCK_LUKS_SECTOR_SIZE,
                                        startsector, buf, len, errp);
}


const QCryptoBlockDriver qcrypto_block_driver_luks = {
    .open = qcrypto_block_luks_open,
    .create = qcrypto_block_luks_create,
    .cleanup = qcrypto_block_luks_cleanup,
    .decrypt = qcrypto_block_luks_decrypt,
    .encrypt = qcrypto_block_luks_encrypt,
    .has_format = qcrypto_block_luks_has_format,
};
