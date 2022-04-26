/*
 * Copyright (C) 2015 Red Hat, Inc.
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#ifndef TESTS_CRYPTO_TLS_X509_HELPERS_H
#define TESTS_CRYPTO_TLS_X509_HELPERS_H

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <libtasn1.h>


#define QCRYPTO_TLS_TEST_CLIENT_NAME "ACME QEMU Client"
#define QCRYPTO_TLS_TEST_CLIENT_HOSTILE_NAME "ACME Hostile Client"

/*
 * This contains parameter about how to generate
 * certificates.
 */
typedef struct QCryptoTLSTestCertReq QCryptoTLSTestCertReq;
struct QCryptoTLSTestCertReq {
    gnutls_x509_crt_t crt;

    const char *filename;

    /* Identifying information */
    const char *country;
    const char *cn;
    const char *altname1;
    const char *altname2;
    const char *ipaddr1;
    const char *ipaddr2;

    /* Basic constraints */
    bool basicConstraintsEnable;
    bool basicConstraintsCritical;
    bool basicConstraintsIsCA;

    /* Key usage */
    bool keyUsageEnable;
    bool keyUsageCritical;
    int keyUsageValue;

    /* Key purpose (aka Extended key usage) */
    bool keyPurposeEnable;
    bool keyPurposeCritical;
    const char *keyPurposeOID1;
    const char *keyPurposeOID2;

    /* zero for current time, or non-zero for hours from now */
    int start_offset;
    /* zero for 24 hours from now, or non-zero for hours from now */
    int expire_offset;
};

void test_tls_generate_cert(QCryptoTLSTestCertReq *req,
                            gnutls_x509_crt_t ca);
void test_tls_write_cert_chain(const char *filename,
                               gnutls_x509_crt_t *certs,
                               size_t ncerts);
void test_tls_discard_cert(QCryptoTLSTestCertReq *req);

void test_tls_init(const char *keyfile);
void test_tls_cleanup(const char *keyfile);

# define TLS_CERT_REQ(varname, cavarname,                               \
                      country, commonname,                              \
                      altname1, altname2,                               \
                      ipaddr1, ipaddr2,                                 \
                      basicconsenable, basicconscritical, basicconsca,  \
                      keyusageenable, keyusagecritical, keyusagevalue,  \
                      keypurposeenable, keypurposecritical,             \
                      keypurposeoid1, keypurposeoid2,                   \
                      startoffset, endoffset)                           \
    static QCryptoTLSTestCertReq varname = {                            \
        NULL, WORKDIR #varname "-ctx.pem",                              \
        country, commonname, altname1, altname2,                        \
        ipaddr1, ipaddr2,                                               \
        basicconsenable, basicconscritical, basicconsca,                \
        keyusageenable, keyusagecritical, keyusagevalue,                \
        keypurposeenable, keypurposecritical,                           \
        keypurposeoid1, keypurposeoid2,                                 \
        startoffset, endoffset                                          \
    };                                                                  \
    test_tls_generate_cert(&varname, cavarname.crt)

# define TLS_ROOT_REQ(varname,                                          \
                      country, commonname,                              \
                      altname1, altname2,                               \
                      ipaddr1, ipaddr2,                                 \
                      basicconsenable, basicconscritical, basicconsca,  \
                      keyusageenable, keyusagecritical, keyusagevalue,  \
                      keypurposeenable, keypurposecritical,             \
                      keypurposeoid1, keypurposeoid2,                   \
                      startoffset, endoffset)                           \
    static QCryptoTLSTestCertReq varname = {                            \
        NULL, WORKDIR #varname "-ctx.pem",                              \
        country, commonname, altname1, altname2,                        \
        ipaddr1, ipaddr2,                                               \
        basicconsenable, basicconscritical, basicconsca,                \
        keyusageenable, keyusagecritical, keyusagevalue,                \
        keypurposeenable, keypurposecritical,                           \
        keypurposeoid1, keypurposeoid2,                                 \
        startoffset, endoffset                                          \
    };                                                                  \
    test_tls_generate_cert(&varname, NULL)

# define TLS_ROOT_REQ_SIMPLE(varname, fname)                            \
    QCryptoTLSTestCertReq varname = {                                   \
        .filename = fname,                                              \
        .cn = "qemu-CA",                                                \
        .basicConstraintsEnable = true,                                 \
        .basicConstraintsCritical = true,                               \
        .basicConstraintsIsCA = true,                                   \
        .keyUsageEnable = true,                                         \
        .keyUsageCritical = true,                                       \
        .keyUsageValue = GNUTLS_KEY_KEY_CERT_SIGN,                      \
    };                                                                  \
    test_tls_generate_cert(&varname, NULL)

# define TLS_CERT_REQ_SIMPLE_CLIENT(varname, cavarname, cname, fname)   \
    QCryptoTLSTestCertReq varname = {                                   \
        .filename = fname,                                              \
        .cn = cname,                                                    \
        .basicConstraintsEnable = true,                                 \
        .basicConstraintsCritical = true,                               \
        .basicConstraintsIsCA = false,                                  \
        .keyUsageEnable = true,                                         \
        .keyUsageCritical = true,                                       \
        .keyUsageValue =                                                \
        GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,     \
        .keyPurposeEnable = true,                                       \
        .keyPurposeCritical = true,                                     \
        .keyPurposeOID1 = GNUTLS_KP_TLS_WWW_CLIENT,                     \
    };                                                                  \
    test_tls_generate_cert(&varname, cavarname.crt)

# define TLS_CERT_REQ_SIMPLE_SERVER(varname, cavarname, fname,          \
                                    hostname, ipaddr)                   \
    QCryptoTLSTestCertReq varname = {                                   \
        .filename = fname,                                              \
        .cn = hostname ? hostname : ipaddr,                             \
        .altname1 = hostname,                                           \
        .ipaddr1 = ipaddr,                                              \
        .basicConstraintsEnable = true,                                 \
        .basicConstraintsCritical = true,                               \
        .basicConstraintsIsCA = false,                                  \
        .keyUsageEnable = true,                                         \
        .keyUsageCritical = true,                                       \
        .keyUsageValue =                                                \
        GNUTLS_KEY_DIGITAL_SIGNATURE | GNUTLS_KEY_KEY_ENCIPHERMENT,     \
        .keyPurposeEnable = true,                                       \
        .keyPurposeCritical = true,                                     \
        .keyPurposeOID1 = GNUTLS_KP_TLS_WWW_SERVER,                     \
    };                                                                  \
    test_tls_generate_cert(&varname, cavarname.crt)

extern const asn1_static_node pkix_asn1_tab[];

#endif
