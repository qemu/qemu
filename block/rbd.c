/*
 * QEMU Block driver for RADOS (Ceph)
 *
 * Copyright (C) 2010-2011 Christian Brunner <chb@muc.de>,
 *                         Josh Durgin <josh.durgin@dreamhost.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"

#include <rbd/librbd.h>
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "block/block_int.h"
#include "block/qdict.h"
#include "crypto/secret.h"
#include "qemu/cutils.h"
#include "sysemu/replay.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qapi-visit-block-core.h"

/*
 * When specifying the image filename use:
 *
 * rbd:poolname/devicename[@snapshotname][:option1=value1[:option2=value2...]]
 *
 * poolname must be the name of an existing rados pool.
 *
 * devicename is the name of the rbd image.
 *
 * Each option given is used to configure rados, and may be any valid
 * Ceph option, "id", or "conf".
 *
 * The "id" option indicates what user we should authenticate as to
 * the Ceph cluster.  If it is excluded we will use the Ceph default
 * (normally 'admin').
 *
 * The "conf" option specifies a Ceph configuration file to read.  If
 * it is not specified, we will read from the default Ceph locations
 * (e.g., /etc/ceph/ceph.conf).  To avoid reading _any_ configuration
 * file, specify conf=/dev/null.
 *
 * Configuration values containing :, @, or = can be escaped with a
 * leading "\".
 */

#define OBJ_MAX_SIZE (1UL << OBJ_DEFAULT_OBJ_ORDER)

#define RBD_MAX_SNAPS 100

#define RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN 8

static const char rbd_luks_header_verification[
        RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN] = {
    'L', 'U', 'K', 'S', 0xBA, 0xBE, 0, 1
};

static const char rbd_luks2_header_verification[
        RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN] = {
    'L', 'U', 'K', 'S', 0xBA, 0xBE, 0, 2
};

typedef enum {
    RBD_AIO_READ,
    RBD_AIO_WRITE,
    RBD_AIO_DISCARD,
    RBD_AIO_FLUSH,
    RBD_AIO_WRITE_ZEROES
} RBDAIOCmd;

typedef struct BDRVRBDState {
    rados_t cluster;
    rados_ioctx_t io_ctx;
    rbd_image_t image;
    char *image_name;
    char *snap;
    char *namespace;
    uint64_t image_size;
    uint64_t object_size;
} BDRVRBDState;

typedef struct RBDTask {
    BlockDriverState *bs;
    Coroutine *co;
    bool complete;
    int64_t ret;
} RBDTask;

typedef struct RBDDiffIterateReq {
    uint64_t offs;
    uint64_t bytes;
    bool exists;
} RBDDiffIterateReq;

static int qemu_rbd_connect(rados_t *cluster, rados_ioctx_t *io_ctx,
                            BlockdevOptionsRbd *opts, bool cache,
                            const char *keypairs, const char *secretid,
                            Error **errp);

static char *qemu_rbd_strchr(char *src, char delim)
{
    char *p;

    for (p = src; *p; ++p) {
        if (*p == delim) {
            return p;
        }
        if (*p == '\\' && p[1] != '\0') {
            ++p;
        }
    }

    return NULL;
}


static char *qemu_rbd_next_tok(char *src, char delim, char **p)
{
    char *end;

    *p = NULL;

    end = qemu_rbd_strchr(src, delim);
    if (end) {
        *p = end + 1;
        *end = '\0';
    }
    return src;
}

static void qemu_rbd_unescape(char *src)
{
    char *p;

    for (p = src; *src; ++src, ++p) {
        if (*src == '\\' && src[1] != '\0') {
            src++;
        }
        *p = *src;
    }
    *p = '\0';
}

static void qemu_rbd_parse_filename(const char *filename, QDict *options,
                                    Error **errp)
{
    const char *start;
    char *p, *buf;
    QList *keypairs = NULL;
    char *found_str, *image_name;

    if (!strstart(filename, "rbd:", &start)) {
        error_setg(errp, "File name must start with 'rbd:'");
        return;
    }

    buf = g_strdup(start);
    p = buf;

    found_str = qemu_rbd_next_tok(p, '/', &p);
    if (!p) {
        error_setg(errp, "Pool name is required");
        goto done;
    }
    qemu_rbd_unescape(found_str);
    qdict_put_str(options, "pool", found_str);

    if (qemu_rbd_strchr(p, '@')) {
        image_name = qemu_rbd_next_tok(p, '@', &p);

        found_str = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(found_str);
        qdict_put_str(options, "snapshot", found_str);
    } else {
        image_name = qemu_rbd_next_tok(p, ':', &p);
    }
    /* Check for namespace in the image_name */
    if (qemu_rbd_strchr(image_name, '/')) {
        found_str = qemu_rbd_next_tok(image_name, '/', &image_name);
        qemu_rbd_unescape(found_str);
        qdict_put_str(options, "namespace", found_str);
    } else {
        qdict_put_str(options, "namespace", "");
    }
    qemu_rbd_unescape(image_name);
    qdict_put_str(options, "image", image_name);
    if (!p) {
        goto done;
    }

    /* The following are essentially all key/value pairs, and we treat
     * 'id' and 'conf' a bit special.  Key/value pairs may be in any order. */
    while (p) {
        char *name, *value;
        name = qemu_rbd_next_tok(p, '=', &p);
        if (!p) {
            error_setg(errp, "conf option %s has no value", name);
            break;
        }

        qemu_rbd_unescape(name);

        value = qemu_rbd_next_tok(p, ':', &p);
        qemu_rbd_unescape(value);

        if (!strcmp(name, "conf")) {
            qdict_put_str(options, "conf", value);
        } else if (!strcmp(name, "id")) {
            qdict_put_str(options, "user", value);
        } else {
            /*
             * We pass these internally to qemu_rbd_set_keypairs(), so
             * we can get away with the simpler list of [ "key1",
             * "value1", "key2", "value2" ] rather than a raw dict
             * { "key1": "value1", "key2": "value2" } where we can't
             * guarantee order, or even a more correct but complex
             * [ { "key1": "value1" }, { "key2": "value2" } ]
             */
            if (!keypairs) {
                keypairs = qlist_new();
            }
            qlist_append_str(keypairs, name);
            qlist_append_str(keypairs, value);
        }
    }

    if (keypairs) {
        qdict_put(options, "=keyvalue-pairs",
                  qstring_from_gstring(qobject_to_json(QOBJECT(keypairs))));
    }

done:
    g_free(buf);
    qobject_unref(keypairs);
    return;
}

static int qemu_rbd_set_auth(rados_t cluster, BlockdevOptionsRbd *opts,
                             Error **errp)
{
    char *key, *acr;
    int r;
    GString *accu;
    RbdAuthModeList *auth;

    if (opts->key_secret) {
        key = qcrypto_secret_lookup_as_base64(opts->key_secret, errp);
        if (!key) {
            return -EIO;
        }
        r = rados_conf_set(cluster, "key", key);
        g_free(key);
        if (r < 0) {
            error_setg_errno(errp, -r, "Could not set 'key'");
            return r;
        }
    }

    if (opts->has_auth_client_required) {
        accu = g_string_new("");
        for (auth = opts->auth_client_required; auth; auth = auth->next) {
            if (accu->str[0]) {
                g_string_append_c(accu, ';');
            }
            g_string_append(accu, RbdAuthMode_str(auth->value));
        }
        acr = g_string_free(accu, FALSE);
        r = rados_conf_set(cluster, "auth_client_required", acr);
        g_free(acr);
        if (r < 0) {
            error_setg_errno(errp, -r,
                             "Could not set 'auth_client_required'");
            return r;
        }
    }

    return 0;
}

static int qemu_rbd_set_keypairs(rados_t cluster, const char *keypairs_json,
                                 Error **errp)
{
    QList *keypairs;
    QString *name;
    QString *value;
    const char *key;
    size_t remaining;
    int ret = 0;

    if (!keypairs_json) {
        return ret;
    }
    keypairs = qobject_to(QList,
                          qobject_from_json(keypairs_json, &error_abort));
    remaining = qlist_size(keypairs) / 2;
    assert(remaining);

    while (remaining--) {
        name = qobject_to(QString, qlist_pop(keypairs));
        value = qobject_to(QString, qlist_pop(keypairs));
        assert(name && value);
        key = qstring_get_str(name);

        ret = rados_conf_set(cluster, key, qstring_get_str(value));
        qobject_unref(value);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "invalid conf option %s", key);
            qobject_unref(name);
            ret = -EINVAL;
            break;
        }
        qobject_unref(name);
    }

    qobject_unref(keypairs);
    return ret;
}

#ifdef LIBRBD_SUPPORTS_ENCRYPTION
static int qemu_rbd_convert_luks_options(
        RbdEncryptionOptionsLUKSBase *luks_opts,
        char **passphrase,
        size_t *passphrase_len,
        Error **errp)
{
    return qcrypto_secret_lookup(luks_opts->key_secret, (uint8_t **)passphrase,
                                 passphrase_len, errp);
}

static int qemu_rbd_convert_luks_create_options(
        RbdEncryptionCreateOptionsLUKSBase *luks_opts,
        rbd_encryption_algorithm_t *alg,
        char **passphrase,
        size_t *passphrase_len,
        Error **errp)
{
    int r = 0;

    r = qemu_rbd_convert_luks_options(
            qapi_RbdEncryptionCreateOptionsLUKSBase_base(luks_opts),
            passphrase, passphrase_len, errp);
    if (r < 0) {
        return r;
    }

    if (luks_opts->has_cipher_alg) {
        switch (luks_opts->cipher_alg) {
            case QCRYPTO_CIPHER_ALG_AES_128: {
                *alg = RBD_ENCRYPTION_ALGORITHM_AES128;
                break;
            }
            case QCRYPTO_CIPHER_ALG_AES_256: {
                *alg = RBD_ENCRYPTION_ALGORITHM_AES256;
                break;
            }
            default: {
                r = -ENOTSUP;
                error_setg_errno(errp, -r, "unknown encryption algorithm: %u",
                                 luks_opts->cipher_alg);
                return r;
            }
        }
    } else {
        /* default alg */
        *alg = RBD_ENCRYPTION_ALGORITHM_AES256;
    }

    return 0;
}

static int qemu_rbd_encryption_format(rbd_image_t image,
                                      RbdEncryptionCreateOptions *encrypt,
                                      Error **errp)
{
    int r = 0;
    g_autofree char *passphrase = NULL;
    size_t passphrase_len;
    rbd_encryption_format_t format;
    rbd_encryption_options_t opts;
    rbd_encryption_luks1_format_options_t luks_opts;
    rbd_encryption_luks2_format_options_t luks2_opts;
    size_t opts_size;
    uint64_t raw_size, effective_size;

    r = rbd_get_size(image, &raw_size);
    if (r < 0) {
        error_setg_errno(errp, -r, "cannot get raw image size");
        return r;
    }

    switch (encrypt->format) {
        case RBD_IMAGE_ENCRYPTION_FORMAT_LUKS: {
            memset(&luks_opts, 0, sizeof(luks_opts));
            format = RBD_ENCRYPTION_FORMAT_LUKS1;
            opts = &luks_opts;
            opts_size = sizeof(luks_opts);
            r = qemu_rbd_convert_luks_create_options(
                    qapi_RbdEncryptionCreateOptionsLUKS_base(&encrypt->u.luks),
                    &luks_opts.alg, &passphrase, &passphrase_len, errp);
            if (r < 0) {
                return r;
            }
            luks_opts.passphrase = passphrase;
            luks_opts.passphrase_size = passphrase_len;
            break;
        }
        case RBD_IMAGE_ENCRYPTION_FORMAT_LUKS2: {
            memset(&luks2_opts, 0, sizeof(luks2_opts));
            format = RBD_ENCRYPTION_FORMAT_LUKS2;
            opts = &luks2_opts;
            opts_size = sizeof(luks2_opts);
            r = qemu_rbd_convert_luks_create_options(
                    qapi_RbdEncryptionCreateOptionsLUKS2_base(
                            &encrypt->u.luks2),
                    &luks2_opts.alg, &passphrase, &passphrase_len, errp);
            if (r < 0) {
                return r;
            }
            luks2_opts.passphrase = passphrase;
            luks2_opts.passphrase_size = passphrase_len;
            break;
        }
        default: {
            r = -ENOTSUP;
            error_setg_errno(
                    errp, -r, "unknown image encryption format: %u",
                    encrypt->format);
            return r;
        }
    }

    r = rbd_encryption_format(image, format, opts, opts_size);
    if (r < 0) {
        error_setg_errno(errp, -r, "encryption format fail");
        return r;
    }

    r = rbd_get_size(image, &effective_size);
    if (r < 0) {
        error_setg_errno(errp, -r, "cannot get effective image size");
        return r;
    }

    r = rbd_resize(image, raw_size + (raw_size - effective_size));
    if (r < 0) {
        error_setg_errno(errp, -r, "cannot resize image after format");
        return r;
    }

    return 0;
}

static int qemu_rbd_encryption_load(rbd_image_t image,
                                    RbdEncryptionOptions *encrypt,
                                    Error **errp)
{
    int r = 0;
    g_autofree char *passphrase = NULL;
    size_t passphrase_len;
    rbd_encryption_luks1_format_options_t luks_opts;
    rbd_encryption_luks2_format_options_t luks2_opts;
    rbd_encryption_format_t format;
    rbd_encryption_options_t opts;
    size_t opts_size;

    switch (encrypt->format) {
        case RBD_IMAGE_ENCRYPTION_FORMAT_LUKS: {
            memset(&luks_opts, 0, sizeof(luks_opts));
            format = RBD_ENCRYPTION_FORMAT_LUKS1;
            opts = &luks_opts;
            opts_size = sizeof(luks_opts);
            r = qemu_rbd_convert_luks_options(
                    qapi_RbdEncryptionOptionsLUKS_base(&encrypt->u.luks),
                    &passphrase, &passphrase_len, errp);
            if (r < 0) {
                return r;
            }
            luks_opts.passphrase = passphrase;
            luks_opts.passphrase_size = passphrase_len;
            break;
        }
        case RBD_IMAGE_ENCRYPTION_FORMAT_LUKS2: {
            memset(&luks2_opts, 0, sizeof(luks2_opts));
            format = RBD_ENCRYPTION_FORMAT_LUKS2;
            opts = &luks2_opts;
            opts_size = sizeof(luks2_opts);
            r = qemu_rbd_convert_luks_options(
                    qapi_RbdEncryptionOptionsLUKS2_base(&encrypt->u.luks2),
                    &passphrase, &passphrase_len, errp);
            if (r < 0) {
                return r;
            }
            luks2_opts.passphrase = passphrase;
            luks2_opts.passphrase_size = passphrase_len;
            break;
        }
        default: {
            r = -ENOTSUP;
            error_setg_errno(
                    errp, -r, "unknown image encryption format: %u",
                    encrypt->format);
            return r;
        }
    }

    r = rbd_encryption_load(image, format, opts, opts_size);
    if (r < 0) {
        error_setg_errno(errp, -r, "encryption load fail");
        return r;
    }

    return 0;
}
#endif

/* FIXME Deprecate and remove keypairs or make it available in QMP. */
static int qemu_rbd_do_create(BlockdevCreateOptions *options,
                              const char *keypairs, const char *password_secret,
                              Error **errp)
{
    BlockdevCreateOptionsRbd *opts = &options->u.rbd;
    rados_t cluster;
    rados_ioctx_t io_ctx;
    int obj_order = 0;
    int ret;

    assert(options->driver == BLOCKDEV_DRIVER_RBD);
    if (opts->location->has_snapshot) {
        error_setg(errp, "Can't use snapshot name for image creation");
        return -EINVAL;
    }

#ifndef LIBRBD_SUPPORTS_ENCRYPTION
    if (opts->has_encrypt) {
        error_setg(errp, "RBD library does not support image encryption");
        return -ENOTSUP;
    }
#endif

    if (opts->has_cluster_size) {
        int64_t objsize = opts->cluster_size;
        if ((objsize - 1) & objsize) {    /* not a power of 2? */
            error_setg(errp, "obj size needs to be power of 2");
            return -EINVAL;
        }
        if (objsize < 4096) {
            error_setg(errp, "obj size too small");
            return -EINVAL;
        }
        obj_order = ctz32(objsize);
    }

    ret = qemu_rbd_connect(&cluster, &io_ctx, opts->location, false, keypairs,
                           password_secret, errp);
    if (ret < 0) {
        return ret;
    }

    ret = rbd_create(io_ctx, opts->location->image, opts->size, &obj_order);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "error rbd create");
        goto out;
    }

#ifdef LIBRBD_SUPPORTS_ENCRYPTION
    if (opts->has_encrypt) {
        rbd_image_t image;

        ret = rbd_open(io_ctx, opts->location->image, &image, NULL);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "error opening image '%s' for encryption format",
                             opts->location->image);
            goto out;
        }

        ret = qemu_rbd_encryption_format(image, opts->encrypt, errp);
        rbd_close(image);
        if (ret < 0) {
            /* encryption format fail, try removing the image */
            rbd_remove(io_ctx, opts->location->image);
            goto out;
        }
    }
#endif

    ret = 0;
out:
    rados_ioctx_destroy(io_ctx);
    rados_shutdown(cluster);
    return ret;
}

static int qemu_rbd_co_create(BlockdevCreateOptions *options, Error **errp)
{
    return qemu_rbd_do_create(options, NULL, NULL, errp);
}

static int qemu_rbd_extract_encryption_create_options(
        QemuOpts *opts,
        RbdEncryptionCreateOptions **spec,
        Error **errp)
{
    QDict *opts_qdict;
    QDict *encrypt_qdict;
    Visitor *v;
    int ret = 0;

    opts_qdict = qemu_opts_to_qdict(opts, NULL);
    qdict_extract_subqdict(opts_qdict, &encrypt_qdict, "encrypt.");
    qobject_unref(opts_qdict);
    if (!qdict_size(encrypt_qdict)) {
        *spec = NULL;
        goto exit;
    }

    /* Convert options into a QAPI object */
    v = qobject_input_visitor_new_flat_confused(encrypt_qdict, errp);
    if (!v) {
        ret = -EINVAL;
        goto exit;
    }

    visit_type_RbdEncryptionCreateOptions(v, NULL, spec, errp);
    visit_free(v);
    if (!*spec) {
        ret = -EINVAL;
        goto exit;
    }

exit:
    qobject_unref(encrypt_qdict);
    return ret;
}

static int coroutine_fn qemu_rbd_co_create_opts(BlockDriver *drv,
                                                const char *filename,
                                                QemuOpts *opts,
                                                Error **errp)
{
    BlockdevCreateOptions *create_options;
    BlockdevCreateOptionsRbd *rbd_opts;
    BlockdevOptionsRbd *loc;
    RbdEncryptionCreateOptions *encrypt = NULL;
    Error *local_err = NULL;
    const char *keypairs, *password_secret;
    QDict *options = NULL;
    int ret = 0;

    create_options = g_new0(BlockdevCreateOptions, 1);
    create_options->driver = BLOCKDEV_DRIVER_RBD;
    rbd_opts = &create_options->u.rbd;

    rbd_opts->location = g_new0(BlockdevOptionsRbd, 1);

    password_secret = qemu_opt_get(opts, "password-secret");

    /* Read out options */
    rbd_opts->size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);
    rbd_opts->cluster_size = qemu_opt_get_size_del(opts,
                                                   BLOCK_OPT_CLUSTER_SIZE, 0);
    rbd_opts->has_cluster_size = (rbd_opts->cluster_size != 0);

    options = qdict_new();
    qemu_rbd_parse_filename(filename, options, &local_err);
    if (local_err) {
        ret = -EINVAL;
        error_propagate(errp, local_err);
        goto exit;
    }

    ret = qemu_rbd_extract_encryption_create_options(opts, &encrypt, errp);
    if (ret < 0) {
        goto exit;
    }
    rbd_opts->encrypt     = encrypt;
    rbd_opts->has_encrypt = !!encrypt;

    /*
     * Caution: while qdict_get_try_str() is fine, getting non-string
     * types would require more care.  When @options come from -blockdev
     * or blockdev_add, its members are typed according to the QAPI
     * schema, but when they come from -drive, they're all QString.
     */
    loc = rbd_opts->location;
    loc->pool        = g_strdup(qdict_get_try_str(options, "pool"));
    loc->conf        = g_strdup(qdict_get_try_str(options, "conf"));
    loc->has_conf    = !!loc->conf;
    loc->user        = g_strdup(qdict_get_try_str(options, "user"));
    loc->has_user    = !!loc->user;
    loc->q_namespace = g_strdup(qdict_get_try_str(options, "namespace"));
    loc->has_q_namespace = !!loc->q_namespace;
    loc->image       = g_strdup(qdict_get_try_str(options, "image"));
    keypairs         = qdict_get_try_str(options, "=keyvalue-pairs");

    ret = qemu_rbd_do_create(create_options, keypairs, password_secret, errp);
    if (ret < 0) {
        goto exit;
    }

exit:
    qobject_unref(options);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static char *qemu_rbd_mon_host(BlockdevOptionsRbd *opts, Error **errp)
{
    const char **vals;
    const char *host, *port;
    char *rados_str;
    InetSocketAddressBaseList *p;
    int i, cnt;

    if (!opts->has_server) {
        return NULL;
    }

    for (cnt = 0, p = opts->server; p; p = p->next) {
        cnt++;
    }

    vals = g_new(const char *, cnt + 1);

    for (i = 0, p = opts->server; p; p = p->next, i++) {
        host = p->value->host;
        port = p->value->port;

        if (strchr(host, ':')) {
            vals[i] = g_strdup_printf("[%s]:%s", host, port);
        } else {
            vals[i] = g_strdup_printf("%s:%s", host, port);
        }
    }
    vals[i] = NULL;

    rados_str = i ? g_strjoinv(";", (char **)vals) : NULL;
    g_strfreev((char **)vals);
    return rados_str;
}

static int qemu_rbd_connect(rados_t *cluster, rados_ioctx_t *io_ctx,
                            BlockdevOptionsRbd *opts, bool cache,
                            const char *keypairs, const char *secretid,
                            Error **errp)
{
    char *mon_host = NULL;
    Error *local_err = NULL;
    int r;

    if (secretid) {
        if (opts->key_secret) {
            error_setg(errp,
                       "Legacy 'password-secret' clashes with 'key-secret'");
            return -EINVAL;
        }
        opts->key_secret = g_strdup(secretid);
        opts->has_key_secret = true;
    }

    mon_host = qemu_rbd_mon_host(opts, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        r = -EINVAL;
        goto out;
    }

    r = rados_create(cluster, opts->user);
    if (r < 0) {
        error_setg_errno(errp, -r, "error initializing");
        goto out;
    }

    /* try default location when conf=NULL, but ignore failure */
    r = rados_conf_read_file(*cluster, opts->conf);
    if (opts->has_conf && r < 0) {
        error_setg_errno(errp, -r, "error reading conf file %s", opts->conf);
        goto failed_shutdown;
    }

    r = qemu_rbd_set_keypairs(*cluster, keypairs, errp);
    if (r < 0) {
        goto failed_shutdown;
    }

    if (mon_host) {
        r = rados_conf_set(*cluster, "mon_host", mon_host);
        if (r < 0) {
            goto failed_shutdown;
        }
    }

    r = qemu_rbd_set_auth(*cluster, opts, errp);
    if (r < 0) {
        goto failed_shutdown;
    }

    /*
     * Fallback to more conservative semantics if setting cache
     * options fails. Ignore errors from setting rbd_cache because the
     * only possible error is that the option does not exist, and
     * librbd defaults to no caching. If write through caching cannot
     * be set up, fall back to no caching.
     */
    if (cache) {
        rados_conf_set(*cluster, "rbd_cache", "true");
    } else {
        rados_conf_set(*cluster, "rbd_cache", "false");
    }

    r = rados_connect(*cluster);
    if (r < 0) {
        error_setg_errno(errp, -r, "error connecting");
        goto failed_shutdown;
    }

    r = rados_ioctx_create(*cluster, opts->pool, io_ctx);
    if (r < 0) {
        error_setg_errno(errp, -r, "error opening pool %s", opts->pool);
        goto failed_shutdown;
    }

#ifdef HAVE_RBD_NAMESPACE_EXISTS
    if (opts->has_q_namespace && strlen(opts->q_namespace) > 0) {
        bool exists;

        r = rbd_namespace_exists(*io_ctx, opts->q_namespace, &exists);
        if (r < 0) {
            error_setg_errno(errp, -r, "error checking namespace");
            goto failed_ioctx_destroy;
        }

        if (!exists) {
            error_setg(errp, "namespace '%s' does not exist",
                       opts->q_namespace);
            r = -ENOENT;
            goto failed_ioctx_destroy;
        }
    }
#endif

    /*
     * Set the namespace after opening the io context on the pool,
     * if nspace == NULL or if nspace == "", it is just as we did nothing
     */
    rados_ioctx_set_namespace(*io_ctx, opts->q_namespace);

    r = 0;
    goto out;

#ifdef HAVE_RBD_NAMESPACE_EXISTS
failed_ioctx_destroy:
    rados_ioctx_destroy(*io_ctx);
#endif
failed_shutdown:
    rados_shutdown(*cluster);
out:
    g_free(mon_host);
    return r;
}

static int qemu_rbd_convert_options(QDict *options, BlockdevOptionsRbd **opts,
                                    Error **errp)
{
    Visitor *v;

    /* Convert the remaining options into a QAPI object */
    v = qobject_input_visitor_new_flat_confused(options, errp);
    if (!v) {
        return -EINVAL;
    }

    visit_type_BlockdevOptionsRbd(v, NULL, opts, errp);
    visit_free(v);
    if (!opts) {
        return -EINVAL;
    }

    return 0;
}

static int qemu_rbd_attempt_legacy_options(QDict *options,
                                           BlockdevOptionsRbd **opts,
                                           char **keypairs)
{
    char *filename;
    int r;

    filename = g_strdup(qdict_get_try_str(options, "filename"));
    if (!filename) {
        return -EINVAL;
    }
    qdict_del(options, "filename");

    qemu_rbd_parse_filename(filename, options, NULL);

    /* keypairs freed by caller */
    *keypairs = g_strdup(qdict_get_try_str(options, "=keyvalue-pairs"));
    if (*keypairs) {
        qdict_del(options, "=keyvalue-pairs");
    }

    r = qemu_rbd_convert_options(options, opts, NULL);

    g_free(filename);
    return r;
}

static int qemu_rbd_open(BlockDriverState *bs, QDict *options, int flags,
                         Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    BlockdevOptionsRbd *opts = NULL;
    const QDictEntry *e;
    Error *local_err = NULL;
    char *keypairs, *secretid;
    rbd_image_info_t info;
    int r;

    keypairs = g_strdup(qdict_get_try_str(options, "=keyvalue-pairs"));
    if (keypairs) {
        qdict_del(options, "=keyvalue-pairs");
    }

    secretid = g_strdup(qdict_get_try_str(options, "password-secret"));
    if (secretid) {
        qdict_del(options, "password-secret");
    }

    r = qemu_rbd_convert_options(options, &opts, &local_err);
    if (local_err) {
        /* If keypairs are present, that means some options are present in
         * the modern option format.  Don't attempt to parse legacy option
         * formats, as we won't support mixed usage. */
        if (keypairs) {
            error_propagate(errp, local_err);
            goto out;
        }

        /* If the initial attempt to convert and process the options failed,
         * we may be attempting to open an image file that has the rbd options
         * specified in the older format consisting of all key/value pairs
         * encoded in the filename.  Go ahead and attempt to parse the
         * filename, and see if we can pull out the required options. */
        r = qemu_rbd_attempt_legacy_options(options, &opts, &keypairs);
        if (r < 0) {
            /* Propagate the original error, not the legacy parsing fallback
             * error, as the latter was just a best-effort attempt. */
            error_propagate(errp, local_err);
            goto out;
        }
        /* Take care whenever deciding to actually deprecate; once this ability
         * is removed, we will not be able to open any images with legacy-styled
         * backing image strings. */
        warn_report("RBD options encoded in the filename as keyvalue pairs "
                    "is deprecated");
    }

    /* Remove the processed options from the QDict (the visitor processes
     * _all_ options in the QDict) */
    while ((e = qdict_first(options))) {
        qdict_del(options, e->key);
    }

    r = qemu_rbd_connect(&s->cluster, &s->io_ctx, opts,
                         !(flags & BDRV_O_NOCACHE), keypairs, secretid, errp);
    if (r < 0) {
        goto out;
    }

    s->snap = g_strdup(opts->snapshot);
    s->image_name = g_strdup(opts->image);

    /* rbd_open is always r/w */
    r = rbd_open(s->io_ctx, s->image_name, &s->image, s->snap);
    if (r < 0) {
        error_setg_errno(errp, -r, "error reading header from %s",
                         s->image_name);
        goto failed_open;
    }

    if (opts->has_encrypt) {
#ifdef LIBRBD_SUPPORTS_ENCRYPTION
        r = qemu_rbd_encryption_load(s->image, opts->encrypt, errp);
        if (r < 0) {
            goto failed_post_open;
        }
#else
        r = -ENOTSUP;
        error_setg(errp, "RBD library does not support image encryption");
        goto failed_post_open;
#endif
    }

    r = rbd_stat(s->image, &info, sizeof(info));
    if (r < 0) {
        error_setg_errno(errp, -r, "error getting image info from %s",
                         s->image_name);
        goto failed_post_open;
    }
    s->image_size = info.size;
    s->object_size = info.obj_size;

    /* If we are using an rbd snapshot, we must be r/o, otherwise
     * leave as-is */
    if (s->snap != NULL) {
        r = bdrv_apply_auto_read_only(bs, "rbd snapshots are read-only", errp);
        if (r < 0) {
            goto failed_post_open;
        }
    }

#ifdef LIBRBD_SUPPORTS_WRITE_ZEROES
    bs->supported_zero_flags = BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK;
#endif

    /* When extending regular files, we get zeros from the OS */
    bs->supported_truncate_flags = BDRV_REQ_ZERO_WRITE;

    r = 0;
    goto out;

failed_post_open:
    rbd_close(s->image);
failed_open:
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    g_free(s->image_name);
    rados_shutdown(s->cluster);
out:
    qapi_free_BlockdevOptionsRbd(opts);
    g_free(keypairs);
    g_free(secretid);
    return r;
}


/* Since RBD is currently always opened R/W via the API,
 * we just need to check if we are using a snapshot or not, in
 * order to determine if we will allow it to be R/W */
static int qemu_rbd_reopen_prepare(BDRVReopenState *state,
                                   BlockReopenQueue *queue, Error **errp)
{
    BDRVRBDState *s = state->bs->opaque;
    int ret = 0;

    if (s->snap && state->flags & BDRV_O_RDWR) {
        error_setg(errp,
                   "Cannot change node '%s' to r/w when using RBD snapshot",
                   bdrv_get_device_or_node_name(state->bs));
        ret = -EINVAL;
    }

    return ret;
}

static void qemu_rbd_close(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;

    rbd_close(s->image);
    rados_ioctx_destroy(s->io_ctx);
    g_free(s->snap);
    g_free(s->image_name);
    rados_shutdown(s->cluster);
}

/* Resize the RBD image and update the 'image_size' with the current size */
static int qemu_rbd_resize(BlockDriverState *bs, uint64_t size)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    r = rbd_resize(s->image, size);
    if (r < 0) {
        return r;
    }

    s->image_size = size;

    return 0;
}

static void qemu_rbd_finish_bh(void *opaque)
{
    RBDTask *task = opaque;
    task->complete = true;
    aio_co_wake(task->co);
}

/*
 * This is the completion callback function for all rbd aio calls
 * started from qemu_rbd_start_co().
 *
 * Note: this function is being called from a non qemu thread so
 * we need to be careful about what we do here. Generally we only
 * schedule a BH, and do the rest of the io completion handling
 * from qemu_rbd_finish_bh() which runs in a qemu context.
 */
static void qemu_rbd_completion_cb(rbd_completion_t c, RBDTask *task)
{
    task->ret = rbd_aio_get_return_value(c);
    rbd_aio_release(c);
    aio_bh_schedule_oneshot(bdrv_get_aio_context(task->bs),
                            qemu_rbd_finish_bh, task);
}

static int coroutine_fn qemu_rbd_start_co(BlockDriverState *bs,
                                          uint64_t offset,
                                          uint64_t bytes,
                                          QEMUIOVector *qiov,
                                          int flags,
                                          RBDAIOCmd cmd)
{
    BDRVRBDState *s = bs->opaque;
    RBDTask task = { .bs = bs, .co = qemu_coroutine_self() };
    rbd_completion_t c;
    int r;

    assert(!qiov || qiov->size == bytes);

    if (cmd == RBD_AIO_WRITE || cmd == RBD_AIO_WRITE_ZEROES) {
        /*
         * RBD APIs don't allow us to write more than actual size, so in order
         * to support growing images, we resize the image before write
         * operations that exceed the current size.
         */
        if (offset + bytes > s->image_size) {
            int r = qemu_rbd_resize(bs, offset + bytes);
            if (r < 0) {
                return r;
            }
        }
    }

    r = rbd_aio_create_completion(&task,
                                  (rbd_callback_t) qemu_rbd_completion_cb, &c);
    if (r < 0) {
        return r;
    }

    switch (cmd) {
    case RBD_AIO_READ:
        r = rbd_aio_readv(s->image, qiov->iov, qiov->niov, offset, c);
        break;
    case RBD_AIO_WRITE:
        r = rbd_aio_writev(s->image, qiov->iov, qiov->niov, offset, c);
        break;
    case RBD_AIO_DISCARD:
        r = rbd_aio_discard(s->image, offset, bytes, c);
        break;
    case RBD_AIO_FLUSH:
        r = rbd_aio_flush(s->image, c);
        break;
#ifdef LIBRBD_SUPPORTS_WRITE_ZEROES
    case RBD_AIO_WRITE_ZEROES: {
        int zero_flags = 0;
#ifdef RBD_WRITE_ZEROES_FLAG_THICK_PROVISION
        if (!(flags & BDRV_REQ_MAY_UNMAP)) {
            zero_flags = RBD_WRITE_ZEROES_FLAG_THICK_PROVISION;
        }
#endif
        r = rbd_aio_write_zeroes(s->image, offset, bytes, c, zero_flags, 0);
        break;
    }
#endif
    default:
        r = -EINVAL;
    }

    if (r < 0) {
        error_report("rbd request failed early: cmd %d offset %" PRIu64
                     " bytes %" PRIu64 " flags %d r %d (%s)", cmd, offset,
                     bytes, flags, r, strerror(-r));
        rbd_aio_release(c);
        return r;
    }

    while (!task.complete) {
        qemu_coroutine_yield();
    }

    if (task.ret < 0) {
        error_report("rbd request failed: cmd %d offset %" PRIu64 " bytes %"
                     PRIu64 " flags %d task.ret %" PRIi64 " (%s)", cmd, offset,
                     bytes, flags, task.ret, strerror(-task.ret));
        return task.ret;
    }

    /* zero pad short reads */
    if (cmd == RBD_AIO_READ && task.ret < qiov->size) {
        qemu_iovec_memset(qiov, task.ret, 0, qiov->size - task.ret);
    }

    return 0;
}

static int
coroutine_fn qemu_rbd_co_preadv(BlockDriverState *bs, int64_t offset,
                                int64_t bytes, QEMUIOVector *qiov,
                                BdrvRequestFlags flags)
{
    return qemu_rbd_start_co(bs, offset, bytes, qiov, flags, RBD_AIO_READ);
}

static int
coroutine_fn qemu_rbd_co_pwritev(BlockDriverState *bs, int64_t offset,
                                 int64_t bytes, QEMUIOVector *qiov,
                                 BdrvRequestFlags flags)
{
    return qemu_rbd_start_co(bs, offset, bytes, qiov, flags, RBD_AIO_WRITE);
}

static int coroutine_fn qemu_rbd_co_flush(BlockDriverState *bs)
{
    return qemu_rbd_start_co(bs, 0, 0, NULL, 0, RBD_AIO_FLUSH);
}

static int coroutine_fn qemu_rbd_co_pdiscard(BlockDriverState *bs,
                                             int64_t offset, int64_t bytes)
{
    return qemu_rbd_start_co(bs, offset, bytes, NULL, 0, RBD_AIO_DISCARD);
}

#ifdef LIBRBD_SUPPORTS_WRITE_ZEROES
static int
coroutine_fn qemu_rbd_co_pwrite_zeroes(BlockDriverState *bs, int64_t offset,
                                       int64_t bytes, BdrvRequestFlags flags)
{
    return qemu_rbd_start_co(bs, offset, bytes, NULL, flags,
                             RBD_AIO_WRITE_ZEROES);
}
#endif

static int qemu_rbd_getinfo(BlockDriverState *bs, BlockDriverInfo *bdi)
{
    BDRVRBDState *s = bs->opaque;
    bdi->cluster_size = s->object_size;
    return 0;
}

static ImageInfoSpecific *qemu_rbd_get_specific_info(BlockDriverState *bs,
                                                     Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    ImageInfoSpecific *spec_info;
    char buf[RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN] = {0};
    int r;

    if (s->image_size >= RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN) {
        r = rbd_read(s->image, 0,
                     RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN, buf);
        if (r < 0) {
            error_setg_errno(errp, -r, "cannot read image start for probe");
            return NULL;
        }
    }

    spec_info = g_new(ImageInfoSpecific, 1);
    *spec_info = (ImageInfoSpecific){
        .type  = IMAGE_INFO_SPECIFIC_KIND_RBD,
        .u.rbd.data = g_new0(ImageInfoSpecificRbd, 1),
    };

    if (memcmp(buf, rbd_luks_header_verification,
               RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN) == 0) {
        spec_info->u.rbd.data->encryption_format =
                RBD_IMAGE_ENCRYPTION_FORMAT_LUKS;
        spec_info->u.rbd.data->has_encryption_format = true;
    } else if (memcmp(buf, rbd_luks2_header_verification,
               RBD_ENCRYPTION_LUKS_HEADER_VERIFICATION_LEN) == 0) {
        spec_info->u.rbd.data->encryption_format =
                RBD_IMAGE_ENCRYPTION_FORMAT_LUKS2;
        spec_info->u.rbd.data->has_encryption_format = true;
    } else {
        spec_info->u.rbd.data->has_encryption_format = false;
    }

    return spec_info;
}

/*
 * rbd_diff_iterate2 allows to interrupt the exection by returning a negative
 * value in the callback routine. Choose a value that does not conflict with
 * an existing exitcode and return it if we want to prematurely stop the
 * execution because we detected a change in the allocation status.
 */
#define QEMU_RBD_EXIT_DIFF_ITERATE2 -9000

static int qemu_rbd_diff_iterate_cb(uint64_t offs, size_t len,
                                    int exists, void *opaque)
{
    RBDDiffIterateReq *req = opaque;

    assert(req->offs + req->bytes <= offs);

    /* treat a hole like an unallocated area and bail out */
    if (!exists) {
        return 0;
    }

    if (!req->exists && offs > req->offs) {
        /*
         * we started in an unallocated area and hit the first allocated
         * block. req->bytes must be set to the length of the unallocated area
         * before the allocated area. stop further processing.
         */
        req->bytes = offs - req->offs;
        return QEMU_RBD_EXIT_DIFF_ITERATE2;
    }

    if (req->exists && offs > req->offs + req->bytes) {
        /*
         * we started in an allocated area and jumped over an unallocated area,
         * req->bytes contains the length of the allocated area before the
         * unallocated area. stop further processing.
         */
        return QEMU_RBD_EXIT_DIFF_ITERATE2;
    }

    req->bytes += len;
    req->exists = true;

    return 0;
}

static int coroutine_fn qemu_rbd_co_block_status(BlockDriverState *bs,
                                                 bool want_zero, int64_t offset,
                                                 int64_t bytes, int64_t *pnum,
                                                 int64_t *map,
                                                 BlockDriverState **file)
{
    BDRVRBDState *s = bs->opaque;
    int status, r;
    RBDDiffIterateReq req = { .offs = offset };
    uint64_t features, flags;
    uint64_t head = 0;

    assert(offset + bytes <= s->image_size);

    /* default to all sectors allocated */
    status = BDRV_BLOCK_DATA | BDRV_BLOCK_OFFSET_VALID;
    *map = offset;
    *file = bs;
    *pnum = bytes;

    /* check if RBD image supports fast-diff */
    r = rbd_get_features(s->image, &features);
    if (r < 0) {
        return status;
    }
    if (!(features & RBD_FEATURE_FAST_DIFF)) {
        return status;
    }

    /* check if RBD fast-diff result is valid */
    r = rbd_get_flags(s->image, &flags);
    if (r < 0) {
        return status;
    }
    if (flags & RBD_FLAG_FAST_DIFF_INVALID) {
        return status;
    }

#if LIBRBD_VERSION_CODE < LIBRBD_VERSION(1, 17, 0)
    /*
     * librbd had a bug until early 2022 that affected all versions of ceph that
     * supported fast-diff. This bug results in reporting of incorrect offsets
     * if the offset parameter to rbd_diff_iterate2 is not object aligned.
     * Work around this bug by rounding down the offset to object boundaries.
     * This is OK because we call rbd_diff_iterate2 with whole_object = true.
     * However, this workaround only works for non cloned images with default
     * striping.
     *
     * See: https://tracker.ceph.com/issues/53784
     */

    /* check if RBD image has non-default striping enabled */
    if (features & RBD_FEATURE_STRIPINGV2) {
        return status;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    /*
     * check if RBD image is a clone (= has a parent).
     *
     * rbd_get_parent_info is deprecated from Nautilus onwards, but the
     * replacement rbd_get_parent is not present in Luminous and Mimic.
     */
    if (rbd_get_parent_info(s->image, NULL, 0, NULL, 0, NULL, 0) != -ENOENT) {
        return status;
    }
#pragma GCC diagnostic pop

    head = req.offs & (s->object_size - 1);
    req.offs -= head;
    bytes += head;
#endif

    r = rbd_diff_iterate2(s->image, NULL, req.offs, bytes, true, true,
                          qemu_rbd_diff_iterate_cb, &req);
    if (r < 0 && r != QEMU_RBD_EXIT_DIFF_ITERATE2) {
        return status;
    }
    assert(req.bytes <= bytes);
    if (!req.exists) {
        if (r == 0) {
            /*
             * rbd_diff_iterate2 does not invoke callbacks for unallocated
             * areas. This here catches the case where no callback was
             * invoked at all (req.bytes == 0).
             */
            assert(req.bytes == 0);
            req.bytes = bytes;
        }
        status = BDRV_BLOCK_ZERO | BDRV_BLOCK_OFFSET_VALID;
    }

    assert(req.bytes > head);
    *pnum = req.bytes - head;
    return status;
}

static int64_t qemu_rbd_getlength(BlockDriverState *bs)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    r = rbd_get_size(s->image, &s->image_size);
    if (r < 0) {
        return r;
    }

    return s->image_size;
}

static int coroutine_fn qemu_rbd_co_truncate(BlockDriverState *bs,
                                             int64_t offset,
                                             bool exact,
                                             PreallocMode prealloc,
                                             BdrvRequestFlags flags,
                                             Error **errp)
{
    int r;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    r = qemu_rbd_resize(bs, offset);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to resize file");
        return r;
    }

    return 0;
}

static int qemu_rbd_snap_create(BlockDriverState *bs,
                                QEMUSnapshotInfo *sn_info)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    if (sn_info->name[0] == '\0') {
        return -EINVAL; /* we need a name for rbd snapshots */
    }

    /*
     * rbd snapshots are using the name as the user controlled unique identifier
     * we can't use the rbd snapid for that purpose, as it can't be set
     */
    if (sn_info->id_str[0] != '\0' &&
        strcmp(sn_info->id_str, sn_info->name) != 0) {
        return -EINVAL;
    }

    if (strlen(sn_info->name) >= sizeof(sn_info->id_str)) {
        return -ERANGE;
    }

    r = rbd_snap_create(s->image, sn_info->name);
    if (r < 0) {
        error_report("failed to create snap: %s", strerror(-r));
        return r;
    }

    return 0;
}

static int qemu_rbd_snap_remove(BlockDriverState *bs,
                                const char *snapshot_id,
                                const char *snapshot_name,
                                Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    int r;

    if (!snapshot_name) {
        error_setg(errp, "rbd need a valid snapshot name");
        return -EINVAL;
    }

    /* If snapshot_id is specified, it must be equal to name, see
       qemu_rbd_snap_list() */
    if (snapshot_id && strcmp(snapshot_id, snapshot_name)) {
        error_setg(errp,
                   "rbd do not support snapshot id, it should be NULL or "
                   "equal to snapshot name");
        return -EINVAL;
    }

    r = rbd_snap_remove(s->image, snapshot_name);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to remove the snapshot");
    }
    return r;
}

static int qemu_rbd_snap_rollback(BlockDriverState *bs,
                                  const char *snapshot_name)
{
    BDRVRBDState *s = bs->opaque;

    return rbd_snap_rollback(s->image, snapshot_name);
}

static int qemu_rbd_snap_list(BlockDriverState *bs,
                              QEMUSnapshotInfo **psn_tab)
{
    BDRVRBDState *s = bs->opaque;
    QEMUSnapshotInfo *sn_info, *sn_tab = NULL;
    int i, snap_count;
    rbd_snap_info_t *snaps;
    int max_snaps = RBD_MAX_SNAPS;

    do {
        snaps = g_new(rbd_snap_info_t, max_snaps);
        snap_count = rbd_snap_list(s->image, snaps, &max_snaps);
        if (snap_count <= 0) {
            g_free(snaps);
        }
    } while (snap_count == -ERANGE);

    if (snap_count <= 0) {
        goto done;
    }

    sn_tab = g_new0(QEMUSnapshotInfo, snap_count);

    for (i = 0; i < snap_count; i++) {
        const char *snap_name = snaps[i].name;

        sn_info = sn_tab + i;
        pstrcpy(sn_info->id_str, sizeof(sn_info->id_str), snap_name);
        pstrcpy(sn_info->name, sizeof(sn_info->name), snap_name);

        sn_info->vm_state_size = snaps[i].size;
        sn_info->date_sec = 0;
        sn_info->date_nsec = 0;
        sn_info->vm_clock_nsec = 0;
    }
    rbd_snap_list_end(snaps);
    g_free(snaps);

 done:
    *psn_tab = sn_tab;
    return snap_count;
}

static void coroutine_fn qemu_rbd_co_invalidate_cache(BlockDriverState *bs,
                                                      Error **errp)
{
    BDRVRBDState *s = bs->opaque;
    int r = rbd_invalidate_cache(s->image);
    if (r < 0) {
        error_setg_errno(errp, -r, "Failed to invalidate the cache");
    }
}

static QemuOptsList qemu_rbd_create_opts = {
    .name = "rbd-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_rbd_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        {
            .name = BLOCK_OPT_CLUSTER_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "RBD object size"
        },
        {
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
            .help = "ID of secret providing the password",
        },
        {
            .name = "encrypt.format",
            .type = QEMU_OPT_STRING,
            .help = "Encrypt the image, format choices: 'luks', 'luks2'",
        },
        {
            .name = "encrypt.cipher-alg",
            .type = QEMU_OPT_STRING,
            .help = "Name of encryption cipher algorithm"
                    " (allowed values: aes-128, aes-256)",
        },
        {
            .name = "encrypt.key-secret",
            .type = QEMU_OPT_STRING,
            .help = "ID of secret providing LUKS passphrase",
        },
        { /* end of list */ }
    }
};

static const char *const qemu_rbd_strong_runtime_opts[] = {
    "pool",
    "namespace",
    "image",
    "conf",
    "snapshot",
    "user",
    "server.",
    "password-secret",

    NULL
};

static BlockDriver bdrv_rbd = {
    .format_name            = "rbd",
    .instance_size          = sizeof(BDRVRBDState),
    .bdrv_parse_filename    = qemu_rbd_parse_filename,
    .bdrv_file_open         = qemu_rbd_open,
    .bdrv_close             = qemu_rbd_close,
    .bdrv_reopen_prepare    = qemu_rbd_reopen_prepare,
    .bdrv_co_create         = qemu_rbd_co_create,
    .bdrv_co_create_opts    = qemu_rbd_co_create_opts,
    .bdrv_has_zero_init     = bdrv_has_zero_init_1,
    .bdrv_get_info          = qemu_rbd_getinfo,
    .bdrv_get_specific_info = qemu_rbd_get_specific_info,
    .create_opts            = &qemu_rbd_create_opts,
    .bdrv_getlength         = qemu_rbd_getlength,
    .bdrv_co_truncate       = qemu_rbd_co_truncate,
    .protocol_name          = "rbd",

    .bdrv_co_preadv         = qemu_rbd_co_preadv,
    .bdrv_co_pwritev        = qemu_rbd_co_pwritev,
    .bdrv_co_flush_to_disk  = qemu_rbd_co_flush,
    .bdrv_co_pdiscard       = qemu_rbd_co_pdiscard,
#ifdef LIBRBD_SUPPORTS_WRITE_ZEROES
    .bdrv_co_pwrite_zeroes  = qemu_rbd_co_pwrite_zeroes,
#endif
    .bdrv_co_block_status   = qemu_rbd_co_block_status,

    .bdrv_snapshot_create   = qemu_rbd_snap_create,
    .bdrv_snapshot_delete   = qemu_rbd_snap_remove,
    .bdrv_snapshot_list     = qemu_rbd_snap_list,
    .bdrv_snapshot_goto     = qemu_rbd_snap_rollback,
    .bdrv_co_invalidate_cache = qemu_rbd_co_invalidate_cache,

    .strong_runtime_opts    = qemu_rbd_strong_runtime_opts,
};

static void bdrv_rbd_init(void)
{
    bdrv_register(&bdrv_rbd);
}

block_init(bdrv_rbd_init);
