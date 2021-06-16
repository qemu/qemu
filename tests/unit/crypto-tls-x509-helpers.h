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

extern const asn1_static_node pkix_asn1_tab[];

#endif
