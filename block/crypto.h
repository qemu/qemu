/*
 * QEMU block full disk encryption
 *
 * Copyright (c) 2015-2017 Red Hat, Inc.
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

#ifndef BLOCK_CRYPTO_H
#define BLOCK_CRYPTO_H

#define BLOCK_CRYPTO_OPT_DEF_KEY_SECRET(prefix, helpstr)                \
    {                                                                   \
        .name = prefix BLOCK_CRYPTO_OPT_QCOW_KEY_SECRET,                \
        .type = QEMU_OPT_STRING,                                        \
        .help = helpstr,                                                \
    }

#define BLOCK_CRYPTO_OPT_QCOW_KEY_SECRET "key-secret"

#define BLOCK_CRYPTO_OPT_DEF_QCOW_KEY_SECRET(prefix)                    \
    BLOCK_CRYPTO_OPT_DEF_KEY_SECRET(prefix,                             \
        "ID of the secret that provides the AES encryption key")

#define BLOCK_CRYPTO_OPT_LUKS_KEY_SECRET "key-secret"
#define BLOCK_CRYPTO_OPT_LUKS_CIPHER_ALG "cipher-alg"
#define BLOCK_CRYPTO_OPT_LUKS_CIPHER_MODE "cipher-mode"
#define BLOCK_CRYPTO_OPT_LUKS_IVGEN_ALG "ivgen-alg"
#define BLOCK_CRYPTO_OPT_LUKS_IVGEN_HASH_ALG "ivgen-hash-alg"
#define BLOCK_CRYPTO_OPT_LUKS_HASH_ALG "hash-alg"
#define BLOCK_CRYPTO_OPT_LUKS_ITER_TIME "iter-time"
#define BLOCK_CRYPTO_OPT_LUKS_KEYSLOT "keyslot"
#define BLOCK_CRYPTO_OPT_LUKS_STATE "state"
#define BLOCK_CRYPTO_OPT_LUKS_OLD_SECRET "old-secret"
#define BLOCK_CRYPTO_OPT_LUKS_NEW_SECRET "new-secret"


#define BLOCK_CRYPTO_OPT_DEF_LUKS_KEY_SECRET(prefix)                    \
    BLOCK_CRYPTO_OPT_DEF_KEY_SECRET(prefix,                             \
        "ID of the secret that provides the keyslot passphrase")

#define BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_ALG(prefix)       \
    {                                                      \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_CIPHER_ALG,   \
        .type = QEMU_OPT_STRING,                           \
        .help = "Name of encryption cipher algorithm",     \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_CIPHER_MODE(prefix)      \
    {                                                      \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_CIPHER_MODE,  \
        .type = QEMU_OPT_STRING,                           \
        .help = "Name of encryption cipher mode",          \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_ALG(prefix)     \
    {                                                   \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_IVGEN_ALG, \
        .type = QEMU_OPT_STRING,                        \
        .help = "Name of IV generator algorithm",       \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_IVGEN_HASH_ALG(prefix)        \
    {                                                           \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_IVGEN_HASH_ALG,    \
        .type = QEMU_OPT_STRING,                                \
        .help = "Name of IV generator hash algorithm",          \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_HASH_ALG(prefix)       \
    {                                                    \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_HASH_ALG,   \
        .type = QEMU_OPT_STRING,                         \
        .help = "Name of encryption hash algorithm",     \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_ITER_TIME(prefix)           \
    {                                                         \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_ITER_TIME,       \
        .type = QEMU_OPT_NUMBER,                              \
        .help = "Time to spend in PBKDF in milliseconds",     \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_STATE(prefix)                           \
    {                                                                     \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_STATE,                       \
        .type = QEMU_OPT_STRING,                                          \
        .help = "Select new state of affected keyslots (active/inactive)",\
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_KEYSLOT(prefix)              \
    {                                                          \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_KEYSLOT,          \
        .type = QEMU_OPT_NUMBER,                               \
        .help = "Select a single keyslot to modify explicitly",\
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_OLD_SECRET(prefix)            \
    {                                                           \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_OLD_SECRET,        \
        .type = QEMU_OPT_STRING,                                \
        .help = "Select all keyslots that match this password", \
    }

#define BLOCK_CRYPTO_OPT_DEF_LUKS_NEW_SECRET(prefix)            \
    {                                                           \
        .name = prefix BLOCK_CRYPTO_OPT_LUKS_NEW_SECRET,        \
        .type = QEMU_OPT_STRING,                                \
        .help = "New secret to set in the matching keyslots. "  \
                "Empty string to erase",                        \
    }

QCryptoBlockCreateOptions *
block_crypto_create_opts_init(QDict *opts, Error **errp);

QCryptoBlockAmendOptions *
block_crypto_amend_opts_init(QDict *opts, Error **errp);

QCryptoBlockOpenOptions *
block_crypto_open_opts_init(QDict *opts, Error **errp);

#endif /* BLOCK_CRYPTO_H */
