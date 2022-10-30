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


void
qcrypto_block_luks_to_disk_endian(QCryptoBlockLUKSHeader *hdr);
void
qcrypto_block_luks_from_disk_endian(QCryptoBlockLUKSHeader *hdr);
