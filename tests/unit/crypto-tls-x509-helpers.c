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

#include "qemu/osdep.h"

#include "crypto-tls-x509-helpers.h"
#include "crypto/init.h"
#include "qemu/sockets.h"

/*
 * This stores some static data that is needed when
 * encoding extensions in the x509 certs
 */
asn1_node pkix_asn1;

/*
 * To avoid consuming random entropy to generate keys,
 * here's one we prepared earlier :-)
 */
gnutls_x509_privkey_t privkey;
# define PRIVATE_KEY \
    "-----BEGIN RSA PRIVATE KEY-----\n" \
    "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\n" \
    "-----END RSA PRIVATE KEY-----\n"

/*
 * This loads the private key we defined earlier
 */
static gnutls_x509_privkey_t test_tls_load_key(void)
{
    gnutls_x509_privkey_t key;
    const gnutls_datum_t data = { (unsigned char *)PRIVATE_KEY,
                                  strlen(PRIVATE_KEY) };
    int err;

    err = gnutls_x509_privkey_init(&key);
    if (err < 0) {
        g_critical("Failed to init key %s", gnutls_strerror(err));
        abort();
    }

    err = gnutls_x509_privkey_import(key, &data,
                                     GNUTLS_X509_FMT_PEM);
    if (err < 0) {
        if (err != GNUTLS_E_BASE64_UNEXPECTED_HEADER_ERROR &&
            err != GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) {
            g_critical("Failed to import key %s", gnutls_strerror(err));
            abort();
        }

        err = gnutls_x509_privkey_import_pkcs8(
            key, &data, GNUTLS_X509_FMT_PEM, NULL, 0);
        if (err < 0) {
            g_critical("Failed to import PKCS8 key %s", gnutls_strerror(err));
            abort();
        }
    }

    return key;
}


void test_tls_init(const char *keyfile)
{
    qcrypto_init(&error_abort);

    if (asn1_array2tree(pkix_asn1_tab, &pkix_asn1, NULL) != ASN1_SUCCESS) {
        abort();
    }

    privkey = test_tls_load_key();
    if (!g_file_set_contents(keyfile, PRIVATE_KEY, -1, NULL)) {
        abort();
    }
}


void test_tls_cleanup(const char *keyfile)
{
    asn1_delete_structure(&pkix_asn1);
    unlink(keyfile);
}

/*
 * Turns an ASN1 object into a DER encoded byte array
 */
static void test_tls_der_encode(asn1_node src,
                                const char *src_name,
                                gnutls_datum_t *res)
{
  int size;
  char *data = NULL;

  size = 0;
  asn1_der_coding(src, src_name, NULL, &size, NULL);

  data = g_new0(char, size);

  asn1_der_coding(src, src_name, data, &size, NULL);

  res->data = (unsigned char *)data;
  res->size = size;
}


static void
test_tls_get_ipaddr(const char *addrstr,
                    char **data,
                    int *datalen)
{
    struct addrinfo *res;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    g_assert(getaddrinfo(addrstr, NULL, &hints, &res) == 0);

    *datalen = res->ai_addrlen;
    *data = g_new(char, *datalen);
    memcpy(*data, res->ai_addr, *datalen);
    freeaddrinfo(res);
}

/*
 * This is a fairly lame x509 certificate generator.
 *
 * Do not copy/use this code for generating real certificates
 * since it leaves out many things that you would want in
 * certificates for real world usage.
 *
 * This is good enough only for doing tests of the QEMU
 * TLS certificate code
 */
void
test_tls_generate_cert(QCryptoTLSTestCertReq *req,
                       gnutls_x509_crt_t ca)
{
    gnutls_x509_crt_t crt;
    int err;
    static char buffer[1024 * 1024];
    size_t size = sizeof(buffer);
    char serial[5] = { 1, 2, 3, 4, 0 };
    gnutls_datum_t der;
    time_t start = time(NULL) + (60 * 60 * req->start_offset);
    time_t expire = time(NULL) + (60 * 60 * (req->expire_offset
                                             ? req->expire_offset : 24));

    /*
     * Prepare our new certificate object
     */
    err = gnutls_x509_crt_init(&crt);
    if (err < 0) {
        g_critical("Failed to initialize certificate %s", gnutls_strerror(err));
        abort();
    }
    err = gnutls_x509_crt_set_key(crt, privkey);
    if (err < 0) {
        g_critical("Failed to set certificate key %s", gnutls_strerror(err));
        abort();
    }

    /*
     * A v3 certificate is required in order to be able
     * set any of the basic constraints, key purpose and
     * key usage data
     */
    gnutls_x509_crt_set_version(crt, 3);

    if (req->country) {
        err = gnutls_x509_crt_set_dn_by_oid(
            crt, GNUTLS_OID_X520_COUNTRY_NAME, 0,
            req->country, strlen(req->country));
        if (err < 0) {
            g_critical("Failed to set certificate country name %s",
                       gnutls_strerror(err));
            abort();
        }
    }
    if (req->cn) {
        err = gnutls_x509_crt_set_dn_by_oid(
            crt, GNUTLS_OID_X520_COMMON_NAME, 0,
            req->cn, strlen(req->cn));
        if (err < 0) {
            g_critical("Failed to set certificate common name %s",
                       gnutls_strerror(err));
            abort();
        }
    }

    /*
     * Setup the subject altnames, which are used
     * for hostname checks in live sessions
     */
    if (req->altname1) {
        err = gnutls_x509_crt_set_subject_alt_name(
            crt, GNUTLS_SAN_DNSNAME,
            req->altname1,
            strlen(req->altname1),
            GNUTLS_FSAN_APPEND);
        if (err < 0) {
            g_critical("Failed to set certificate alt name %s",
                       gnutls_strerror(err));
            abort();
        }
    }
    if (req->altname2) {
        err = gnutls_x509_crt_set_subject_alt_name(
            crt, GNUTLS_SAN_DNSNAME,
            req->altname2,
            strlen(req->altname2),
            GNUTLS_FSAN_APPEND);
        if (err < 0) {
            g_critical("Failed to set certificate %s alt name",
                       gnutls_strerror(err));
            abort();
        }
    }

    /*
     * IP address need to be put into the cert in their
     * raw byte form, not strings, hence this is a little
     * more complicated
     */
    if (req->ipaddr1) {
        char *data;
        int len;

        test_tls_get_ipaddr(req->ipaddr1, &data, &len);

        err = gnutls_x509_crt_set_subject_alt_name(
            crt, GNUTLS_SAN_IPADDRESS,
            data, len, GNUTLS_FSAN_APPEND);
        if (err < 0) {
            g_critical("Failed to set certificate alt name %s",
                       gnutls_strerror(err));
            abort();
        }
        g_free(data);
    }
    if (req->ipaddr2) {
        char *data;
        int len;

        test_tls_get_ipaddr(req->ipaddr2, &data, &len);

        err = gnutls_x509_crt_set_subject_alt_name(
            crt, GNUTLS_SAN_IPADDRESS,
            data, len, GNUTLS_FSAN_APPEND);
        if (err < 0) {
            g_critical("Failed to set certificate alt name %s",
                       gnutls_strerror(err));
            abort();
        }
        g_free(data);
    }


    /*
     * Basic constraints are used to decide if the cert
     * is for a CA or not. We can't use the convenient
     * gnutls API for setting this, since it hardcodes
     * the 'critical' field which we want control over
     */
    if (req->basicConstraintsEnable) {
        asn1_node ext = NULL;

        asn1_create_element(pkix_asn1, "PKIX1.BasicConstraints", &ext);
        asn1_write_value(ext, "cA",
                         req->basicConstraintsIsCA ? "TRUE" : "FALSE", 1);
        asn1_write_value(ext, "pathLenConstraint", NULL, 0);
        test_tls_der_encode(ext, "", &der);
        err = gnutls_x509_crt_set_extension_by_oid(
            crt, "2.5.29.19",
            der.data, der.size,
            req->basicConstraintsCritical);
        if (err < 0) {
            g_critical("Failed to set certificate basic constraints %s",
                       gnutls_strerror(err));
            g_free(der.data);
            abort();
        }
        asn1_delete_structure(&ext);
        g_free(der.data);
    }

    /*
     * Next up the key usage extension. Again we can't
     * use the gnutls API since it hardcodes the extension
     * to be 'critical'
     */
    if (req->keyUsageEnable) {
        asn1_node ext = NULL;
        char str[2];

        str[0] = req->keyUsageValue & 0xff;
        str[1] = (req->keyUsageValue >> 8) & 0xff;

        asn1_create_element(pkix_asn1, "PKIX1.KeyUsage", &ext);
        asn1_write_value(ext, "", str, 9);
        test_tls_der_encode(ext, "", &der);
        err = gnutls_x509_crt_set_extension_by_oid(
            crt, "2.5.29.15",
            der.data, der.size,
            req->keyUsageCritical);
        if (err < 0) {
            g_critical("Failed to set certificate key usage %s",
                       gnutls_strerror(err));
            g_free(der.data);
            abort();
        }
        asn1_delete_structure(&ext);
        g_free(der.data);
    }

    /*
     * Finally the key purpose extension. This time
     * gnutls has the opposite problem, always hardcoding
     * it to be non-critical. So once again we have to
     * set this the hard way building up ASN1 data ourselves
     */
    if (req->keyPurposeEnable) {
        asn1_node ext = NULL;

        asn1_create_element(pkix_asn1, "PKIX1.ExtKeyUsageSyntax", &ext);
        if (req->keyPurposeOID1) {
            asn1_write_value(ext, "", "NEW", 1);
            asn1_write_value(ext, "?LAST", req->keyPurposeOID1, 1);
        }
        if (req->keyPurposeOID2) {
            asn1_write_value(ext, "", "NEW", 1);
            asn1_write_value(ext, "?LAST", req->keyPurposeOID2, 1);
        }
        test_tls_der_encode(ext, "", &der);
        err = gnutls_x509_crt_set_extension_by_oid(
            crt, "2.5.29.37",
            der.data, der.size,
            req->keyPurposeCritical);
        if (err < 0) {
            g_critical("Failed to set certificate key purpose %s",
                       gnutls_strerror(err));
            g_free(der.data);
            abort();
        }
        asn1_delete_structure(&ext);
        g_free(der.data);
    }

    /*
     * Any old serial number will do, so lets pick 5
     */
    err = gnutls_x509_crt_set_serial(crt, serial, 5);
    if (err < 0) {
        g_critical("Failed to set certificate serial %s",
                   gnutls_strerror(err));
        abort();
    }

    err = gnutls_x509_crt_set_activation_time(crt, start);
    if (err < 0) {
        g_critical("Failed to set certificate activation %s",
                   gnutls_strerror(err));
        abort();
    }
    err = gnutls_x509_crt_set_expiration_time(crt, expire);
    if (err < 0) {
        g_critical("Failed to set certificate expiration %s",
                   gnutls_strerror(err));
        abort();
    }


    /*
     * If no 'ca' is set then we are self signing
     * the cert. This is done for the root CA certs
     */
    err = gnutls_x509_crt_sign2(crt, ca ? ca : crt, privkey,
                                GNUTLS_DIG_SHA256, 0);
    if (err < 0) {
        g_critical("Failed to sign certificate %s",
                   gnutls_strerror(err));
        abort();
    }

    /*
     * Finally write the new cert out to disk
     */
    err = gnutls_x509_crt_export(
        crt, GNUTLS_X509_FMT_PEM, buffer, &size);
    if (err < 0) {
        g_critical("Failed to export certificate %s: %d",
                   gnutls_strerror(err), err);
        abort();
    }

    if (!g_file_set_contents(req->filename, buffer, -1, NULL)) {
        g_critical("Failed to write certificate %s",
                   req->filename);
        abort();
    }

    req->crt = crt;
}


void test_tls_write_cert_chain(const char *filename,
                               gnutls_x509_crt_t *certs,
                               size_t ncerts)
{
    size_t i;
    size_t capacity = 1024, offset = 0;
    char *buffer = g_new0(char, capacity);
    int err;

    for (i = 0; i < ncerts; i++) {
        size_t len = capacity - offset;
    retry:
        err = gnutls_x509_crt_export(certs[i], GNUTLS_X509_FMT_PEM,
                                     buffer + offset, &len);
        if (err < 0) {
            if (err == GNUTLS_E_SHORT_MEMORY_BUFFER) {
                buffer = g_renew(char, buffer, offset + len);
                capacity = offset + len;
                goto retry;
            }
            g_critical("Failed to export certificate chain %s: %d",
                       gnutls_strerror(err), err);
            abort();
        }
        offset += len;
    }

    if (!g_file_set_contents(filename, buffer, offset, NULL)) {
        abort();
    }
    g_free(buffer);
}


void test_tls_discard_cert(QCryptoTLSTestCertReq *req)
{
    if (!req->crt) {
        return;
    }

    gnutls_x509_crt_deinit(req->crt);
    req->crt = NULL;

    if (getenv("QEMU_TEST_DEBUG_CERTS") == NULL) {
        unlink(req->filename);
    }
}
