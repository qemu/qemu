/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * uefi vars device - pkcs7 verification
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/dma.h"

#include <gnutls/gnutls.h>
#include <gnutls/pkcs7.h>
#include <gnutls/crypto.h>

#include "hw/uefi/var-service.h"

#define AUTHVAR_DIGEST_ALGO GNUTLS_DIG_SHA256
#define AUTHVAR_DIGEST_SIZE 32

/*
 * Replicate the signed data for signature verification.
 */
static gnutls_datum_t *build_signed_data(mm_variable_access *va, void *data)
{
    variable_auth_2 *auth = data;
    uint64_t data_offset = sizeof(efi_time) + auth->hdr_length;
    uint16_t *name = (void *)va + sizeof(mm_variable_access);
    gnutls_datum_t *sdata;
    uint64_t pos = 0;

    sdata = g_new(gnutls_datum_t, 1);
    sdata->size = (va->name_size - 2
                   + sizeof(QemuUUID)
                   + sizeof(va->attributes)
                   + sizeof(auth->timestamp)
                   + va->data_size - data_offset);
    sdata->data = g_malloc(sdata->size);

    /* Variable Name (without terminating \0) */
    memcpy(sdata->data + pos, name, va->name_size - 2);
    pos += va->name_size - 2;

    /* Variable Namespace Guid */
    memcpy(sdata->data + pos, &va->guid, sizeof(va->guid));
    pos += sizeof(va->guid);

    /* Attributes */
    memcpy(sdata->data + pos, &va->attributes, sizeof(va->attributes));
    pos += sizeof(va->attributes);

    /* TimeStamp */
    memcpy(sdata->data + pos, &auth->timestamp, sizeof(auth->timestamp));
    pos += sizeof(auth->timestamp);

    /* Variable Content */
    memcpy(sdata->data + pos, data + data_offset, va->data_size - data_offset);
    pos += va->data_size - data_offset;

    assert(pos == sdata->size);
    return sdata;
}

/*
 * See WrapPkcs7Data() in edk2.
 *
 * UEFI spec allows pkcs7 signatures being used without the envelope which
 * identifies them as pkcs7 signatures.  openssl and gnutls will not parse them
 * without the envelope though.  So add it if needed.
 */
static void wrap_pkcs7(gnutls_datum_t *pkcs7)
{
    static uint8_t signed_data_oid[9] = {
        0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02
    };
    gnutls_datum_t wrap;

    if (pkcs7->data[4] == 0x06 &&
        pkcs7->data[5] == 0x09 &&
        memcmp(pkcs7->data + 6, signed_data_oid, sizeof(signed_data_oid)) == 0 &&
        pkcs7->data[15] == 0x0a &&
        pkcs7->data[16] == 0x82) {
        return;
    }

    wrap.size = pkcs7->size + 19;
    wrap.data = g_malloc(wrap.size);

    wrap.data[0] = 0x30;
    wrap.data[1] = 0x82;
    wrap.data[2] = (wrap.size - 4) >> 8;
    wrap.data[3] = (wrap.size - 4) & 0xff;
    wrap.data[4] = 0x06;
    wrap.data[5] = 0x09;
    memcpy(wrap.data + 6, signed_data_oid, sizeof(signed_data_oid));

    wrap.data[15] = 0xa0;
    wrap.data[16] = 0x82;
    wrap.data[17] = pkcs7->size >> 8;
    wrap.data[18] = pkcs7->size & 0xff;
    memcpy(wrap.data + 19, pkcs7->data, pkcs7->size);

    g_free(pkcs7->data);
    *pkcs7 = wrap;
}

static gnutls_datum_t *build_pkcs7(void *data)
{
    variable_auth_2 *auth = data;
    gnutls_datum_t *pkcs7;

    pkcs7 = g_new(gnutls_datum_t, 1);
    pkcs7->size = auth->hdr_length - 24;
    pkcs7->data = g_malloc(pkcs7->size);
    memcpy(pkcs7->data, data + 16 + 24, pkcs7->size);

    wrap_pkcs7(pkcs7);

    return pkcs7;
}

/*
 * Read UEFI signature database, store x509 all certificates found in
 * gnutls_x509_trust_list_t.
 */
static gnutls_x509_trust_list_t build_trust_list_sb(uefi_variable *var)
{
    gnutls_x509_trust_list_t tlist;
    gnutls_datum_t cert_data;
    gnutls_x509_crt_t cert;
    uefi_vars_siglist siglist;
    uefi_vars_cert *c;
    int rc;

    rc = gnutls_x509_trust_list_init(&tlist, 0);
    if (rc < 0) {
        warn_report("gnutls_x509_trust_list_init error: %s",
                     gnutls_strerror(rc));
        return NULL;
    }

    uefi_vars_siglist_init(&siglist);
    uefi_vars_siglist_parse(&siglist, var->data, var->data_size);

    QTAILQ_FOREACH(c, &siglist.x509, next) {
        cert_data.size = c->size;
        cert_data.data = c->data;

        rc = gnutls_x509_crt_init(&cert);
        if (rc < 0) {
            warn_report("gnutls_x509_crt_init error: %s", gnutls_strerror(rc));
            break;
        }
        rc = gnutls_x509_crt_import(cert, &cert_data, GNUTLS_X509_FMT_DER);
        if (rc < 0) {
            warn_report("gnutls_x509_crt_import error: %s",
                        gnutls_strerror(rc));
            gnutls_x509_crt_deinit(cert);
            break;
        }
        rc = gnutls_x509_trust_list_add_cas(tlist, &cert, 1, 0);
        if (rc < 0) {
            warn_report("gnutls_x509_crt_import error: %s",
                        gnutls_strerror(rc));
            gnutls_x509_crt_deinit(cert);
            break;
        }
    }

    uefi_vars_siglist_free(&siglist);

    return tlist;
}

static int build_digest_authvar(gnutls_x509_crt_t signer,
                                gnutls_x509_crt_t root,
                                uint8_t *hash_digest)
{
    char *cn;
    size_t cn_size = 0;
    uint8_t fp[AUTHVAR_DIGEST_SIZE];
    size_t fp_size = sizeof(fp);
    gnutls_hash_hd_t hash;
    int rc;

    /* get signer CN */
    rc = gnutls_x509_crt_get_dn_by_oid(signer, GNUTLS_OID_X520_COMMON_NAME,
                                       0, 0, NULL, &cn_size);
    if (rc != GNUTLS_E_SHORT_MEMORY_BUFFER) {
        warn_report("gnutls_x509_crt_get_dn_by_oid error #1: %s",
                    gnutls_strerror(rc));
        return rc;
    }

    cn = g_malloc(cn_size);
    rc = gnutls_x509_crt_get_dn_by_oid(signer, GNUTLS_OID_X520_COMMON_NAME,
                                       0, 0, cn, &cn_size);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_get_dn_by_oid error #2: %s",
                    gnutls_strerror(rc));
        goto err;
    }

    /* get root certificate fingerprint */
    rc = gnutls_x509_crt_get_fingerprint(root, AUTHVAR_DIGEST_ALGO,
                                         fp, &fp_size);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_get_fingerprint error: %s",
                    gnutls_strerror(rc));
        goto err;
    }

    /* digest both items */
    rc = gnutls_hash_init(&hash, AUTHVAR_DIGEST_ALGO);
    if (rc < 0) {
        warn_report("gnutls_hash_init error: %s",
                    gnutls_strerror(rc));
        goto err;
    }
    rc = gnutls_hash(hash, cn, cn_size);
    if (rc < 0) {
        warn_report("gnutls_hash error: %s",
                    gnutls_strerror(rc));
        goto err;
    }
    rc = gnutls_hash(hash, fp, fp_size);
    if (rc < 0) {
        warn_report("gnutls_hash error: %s",
                    gnutls_strerror(rc));
        goto err;
    }
    gnutls_hash_deinit(hash, hash_digest);

    return 0;

err:
    g_free(cn);
    return rc;
}

/*
 * uefi spec 2.9, section 8.2.2
 *
 * For EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS variables which are
 * NOT secure boot variables we should track the root certificate of the trust
 * chain, and the subject CN of the signer certificate.
 *
 * So we'll go store a digest of these two items so we can verify this.  Also
 * create a gnutls_x509_trust_list_t with the root certificate, so
 * gnutls_pkcs7_verify() will pass (assuming the signature is otherwise
 * correct).
 */
static gnutls_x509_trust_list_t build_trust_list_authvar(gnutls_pkcs7_t pkcs7,
                                                         uint8_t *hash_digest)
{
    gnutls_datum_t signer_data = { 0 };
    gnutls_datum_t root_data = { 0 };
    gnutls_x509_crt_t signer = NULL;
    gnutls_x509_crt_t root = NULL;
    gnutls_x509_trust_list_t tlist = NULL;
    int n, rc;

    n = gnutls_pkcs7_get_crt_count(pkcs7);

    /* first is signer certificate */
    rc = gnutls_pkcs7_get_crt_raw2(pkcs7, 0, &signer_data);
    if (rc < 0) {
        warn_report("gnutls_pkcs7_get_crt_raw2(0) error: %s",
                    gnutls_strerror(rc));
        goto done;
    }
    rc = gnutls_x509_crt_init(&signer);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_init error: %s", gnutls_strerror(rc));
        goto done;
    }
    rc = gnutls_x509_crt_import(signer, &signer_data, GNUTLS_X509_FMT_DER);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_import error: %s",
                    gnutls_strerror(rc));
        gnutls_x509_crt_deinit(signer);
        goto done;
    }

    /* last is root-of-trust certificate (can be identical to signer) */
    rc = gnutls_pkcs7_get_crt_raw2(pkcs7, n - 1, &root_data);
    if (rc < 0) {
        warn_report("gnutls_pkcs7_get_crt_raw2(%d) error: %s",
                    n - 1, gnutls_strerror(rc));
        goto done;
    }
    rc = gnutls_x509_crt_init(&root);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_init error: %s", gnutls_strerror(rc));
        goto done;
    }
    rc = gnutls_x509_crt_import(root, &root_data, GNUTLS_X509_FMT_DER);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_import error: %s",
                    gnutls_strerror(rc));
        goto done;
    }

    /* calc digest for signer CN + root cert */
    rc = build_digest_authvar(signer, root, hash_digest);
    if (rc < 0) {
        goto done;
    }

    /* add root to trust list */
    rc = gnutls_x509_trust_list_init(&tlist, 0);
    if (rc < 0) {
        warn_report("gnutls_x509_trust_list_init error: %s",
                    gnutls_strerror(rc));
        goto done;
    }
    rc = gnutls_x509_trust_list_add_cas(tlist, &root, 1, 0);
    if (rc < 0) {
        warn_report("gnutls_x509_crt_import error: %s",
                    gnutls_strerror(rc));
        gnutls_x509_trust_list_deinit(tlist, 1);
        tlist = NULL;
        goto done;
    } else {
        /* ownership passed to tlist */
        root = NULL;
    }

done:
    if (signer_data.data) {
        gnutls_free(signer_data.data);
    }
    if (root_data.data) {
        gnutls_free(root_data.data);
    }
    if (signer) {
        gnutls_x509_crt_deinit(signer);
    }
    if (root) {
        gnutls_x509_crt_deinit(root);
    }
    return tlist;
}

static void free_datum(gnutls_datum_t *ptr)
{
    if (!ptr) {
        return;
    }
    g_free(ptr->data);
    g_free(ptr);
}

static void gnutls_log_stderr(int level, const char *msg)
{
    if (strncmp(msg, "ASSERT:", 7) == 0) {
        return;
    }
    fprintf(stderr, "    %d: %s", level, msg);
}

/*
 * pkcs7 signature verification (EFI_VARIABLE_AUTHENTICATION_2).
 */
efi_status uefi_vars_check_pkcs7_2(uefi_variable *siglist,
                                   void **digest, uint32_t *digest_size,
                                   mm_variable_access *va, void *data)
{
    gnutls_x509_trust_list_t tlist = NULL;
    gnutls_datum_t *signed_data = NULL;
    gnutls_datum_t *pkcs7_data = NULL;
    gnutls_pkcs7_t pkcs7 = NULL;
    efi_status status = EFI_SECURITY_VIOLATION;
    int rc;

    if (0) {
        /* gnutls debug logging */
        static bool first = true;

        if (first) {
            first = false;
            gnutls_global_set_log_function(gnutls_log_stderr);
            gnutls_global_set_log_level(99);
        }
    }

    signed_data = build_signed_data(va, data);
    pkcs7_data = build_pkcs7(data);

    rc = gnutls_pkcs7_init(&pkcs7);
    if (rc < 0) {
        warn_report("gnutls_pkcs7_init error: %s", gnutls_strerror(rc));
        goto out;
    }

    rc = gnutls_pkcs7_import(pkcs7, pkcs7_data, GNUTLS_X509_FMT_DER);
    if (rc < 0) {
        warn_report("gnutls_pkcs7_import error: %s", gnutls_strerror(rc));
        goto out;
    }

    if (siglist) {
        /* secure boot variables */
        tlist = build_trust_list_sb(siglist);
    } else if (digest && digest_size) {
        /* other authenticated variables */
        *digest_size = AUTHVAR_DIGEST_SIZE;
        *digest = g_malloc(*digest_size);
        tlist = build_trust_list_authvar(pkcs7, *digest);
    } else {
        /* should not happen */
        goto out;
    }

    rc = gnutls_pkcs7_verify(pkcs7, tlist,
                             NULL, 0,
                             0, signed_data,
                             GNUTLS_VERIFY_DISABLE_TIME_CHECKS |
                             GNUTLS_VERIFY_DISABLE_TRUSTED_TIME_CHECKS);
    if (rc < 0) {
        warn_report("gnutls_pkcs7_verify error: %s", gnutls_strerror(rc));
        goto out;
    }

    /* check passed */
    status = EFI_SUCCESS;

out:
    free_datum(signed_data);
    free_datum(pkcs7_data);
    if (tlist) {
        gnutls_x509_trust_list_deinit(tlist, 1);
    }
    if (pkcs7) {
        gnutls_pkcs7_deinit(pkcs7);
    }
    return status;
}
