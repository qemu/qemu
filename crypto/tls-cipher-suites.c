/*
 * QEMU TLS Cipher Suites
 *
 * Copyright (c) 2018-2020 Red Hat, Inc.
 *
 * Author: Philippe Mathieu-Daudé <philmd@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "crypto/tlscreds.h"
#include "crypto/tls-cipher-suites.h"
#include "hw/nvram/fw_cfg.h"
#include "trace.h"

/*
 * IANA registered TLS ciphers:
 * https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-4
 */
typedef struct {
    uint8_t data[2];
} QEMU_PACKED IANA_TLS_CIPHER;

GByteArray *qcrypto_tls_cipher_suites_get_data(QCryptoTLSCipherSuites *obj,
                                               Error **errp)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(obj);
    gnutls_priority_t pcache;
    GByteArray *byte_array;
    const char *err;
    size_t i;
    int ret;

    trace_qcrypto_tls_cipher_suite_priority(creds->priority);
    ret = gnutls_priority_init(&pcache, creds->priority, &err);
    if (ret < 0) {
        error_setg(errp, "Syntax error using priority '%s': %s",
                   creds->priority, gnutls_strerror(ret));
        return NULL;
    }

    byte_array = g_byte_array_new();

    for (i = 0;; i++) {
        int ret;
        unsigned idx;
        const char *name;
        IANA_TLS_CIPHER cipher;
        gnutls_protocol_t protocol;
        const char *version;

        ret = gnutls_priority_get_cipher_suite_index(pcache, i, &idx);
        if (ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
            break;
        }
        if (ret == GNUTLS_E_UNKNOWN_CIPHER_SUITE) {
            continue;
        }

        name = gnutls_cipher_suite_info(idx, (unsigned char *)&cipher,
                                        NULL, NULL, NULL, &protocol);
        if (name == NULL) {
            continue;
        }

        version = gnutls_protocol_get_name(protocol);
        g_byte_array_append(byte_array, cipher.data, 2);
        trace_qcrypto_tls_cipher_suite_info(cipher.data[0],
                                            cipher.data[1],
                                            version, name);
    }
    trace_qcrypto_tls_cipher_suite_count(byte_array->len);
    gnutls_priority_deinit(pcache);

    return byte_array;
}

static void qcrypto_tls_cipher_suites_complete(UserCreatable *uc,
                                               Error **errp)
{
    QCryptoTLSCreds *creds = QCRYPTO_TLS_CREDS(uc);

    if (!creds->priority) {
        error_setg(errp, "'priority' property is not set");
        return;
    }
}

static GByteArray *qcrypto_tls_cipher_suites_fw_cfg_gen_data(Object *obj,
                                                             Error **errp)
{
    return qcrypto_tls_cipher_suites_get_data(QCRYPTO_TLS_CIPHER_SUITES(obj),
                                              errp);
}

static void qcrypto_tls_cipher_suites_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    FWCfgDataGeneratorClass *fwgc = FW_CFG_DATA_GENERATOR_CLASS(oc);

    ucc->complete = qcrypto_tls_cipher_suites_complete;
    fwgc->get_data = qcrypto_tls_cipher_suites_fw_cfg_gen_data;
}

static const TypeInfo qcrypto_tls_cipher_suites_info = {
    .parent = TYPE_QCRYPTO_TLS_CREDS,
    .name = TYPE_QCRYPTO_TLS_CIPHER_SUITES,
    .instance_size = sizeof(QCryptoTLSCipherSuites),
    .class_size = sizeof(QCryptoTLSCredsClass),
    .class_init = qcrypto_tls_cipher_suites_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { TYPE_FW_CFG_DATA_GENERATOR_INTERFACE },
        { }
    }
};

static void qcrypto_tls_cipher_suites_register_types(void)
{
    type_register_static(&qcrypto_tls_cipher_suites_info);
}

type_init(qcrypto_tls_cipher_suites_register_types);
