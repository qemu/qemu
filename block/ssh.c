/*
 * Secure Shell (ssh) backend for QEMU.
 *
 * Copyright (C) 2013 Red Hat Inc., Richard W.M. Jones <rjones@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "block/block_int.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qemu/uri.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"

/* DEBUG_SSH=1 enables the DPRINTF (debugging printf) statements in
 * this block driver code.
 *
 * TRACE_LIBSSH2=<bitmask> enables tracing in libssh2 itself.  Note
 * that this requires that libssh2 was specially compiled with the
 * `./configure --enable-debug' option, so most likely you will have
 * to compile it yourself.  The meaning of <bitmask> is described
 * here: http://www.libssh2.org/libssh2_trace.html
 */
#define DEBUG_SSH     0
#define TRACE_LIBSSH2 0 /* or try: LIBSSH2_TRACE_SFTP */

#define DPRINTF(fmt, ...)                           \
    do {                                            \
        if (DEBUG_SSH) {                            \
            fprintf(stderr, "ssh: %-15s " fmt "\n", \
                    __func__, ##__VA_ARGS__);       \
        }                                           \
    } while (0)

typedef struct BDRVSSHState {
    /* Coroutine. */
    CoMutex lock;

    /* SSH connection. */
    int sock;                         /* socket */
    LIBSSH2_SESSION *session;         /* ssh session */
    LIBSSH2_SFTP *sftp;               /* sftp session */
    LIBSSH2_SFTP_HANDLE *sftp_handle; /* sftp remote file handle */

    /* See ssh_seek() function below. */
    int64_t offset;
    bool offset_op_read;

    /* File attributes at open.  We try to keep the .filesize field
     * updated if it changes (eg by writing at the end of the file).
     */
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    InetSocketAddress *inet;

    /* Used to warn if 'flush' is not supported. */
    bool unsafe_flush_warning;
} BDRVSSHState;

static void ssh_state_init(BDRVSSHState *s)
{
    memset(s, 0, sizeof *s);
    s->sock = -1;
    s->offset = -1;
    qemu_co_mutex_init(&s->lock);
}

static void ssh_state_free(BDRVSSHState *s)
{
    if (s->sftp_handle) {
        libssh2_sftp_close(s->sftp_handle);
    }
    if (s->sftp) {
        libssh2_sftp_shutdown(s->sftp);
    }
    if (s->session) {
        libssh2_session_disconnect(s->session,
                                   "from qemu ssh client: "
                                   "user closed the connection");
        libssh2_session_free(s->session);
    }
    if (s->sock >= 0) {
        close(s->sock);
    }
}

static void GCC_FMT_ATTR(3, 4)
session_error_setg(Error **errp, BDRVSSHState *s, const char *fs, ...)
{
    va_list args;
    char *msg;

    va_start(args, fs);
    msg = g_strdup_vprintf(fs, args);
    va_end(args);

    if (s->session) {
        char *ssh_err;
        int ssh_err_code;

        /* This is not an errno.  See <libssh2.h>. */
        ssh_err_code = libssh2_session_last_error(s->session,
                                                  &ssh_err, NULL, 0);
        error_setg(errp, "%s: %s (libssh2 error code: %d)",
                   msg, ssh_err, ssh_err_code);
    } else {
        error_setg(errp, "%s", msg);
    }
    g_free(msg);
}

static void GCC_FMT_ATTR(3, 4)
sftp_error_setg(Error **errp, BDRVSSHState *s, const char *fs, ...)
{
    va_list args;
    char *msg;

    va_start(args, fs);
    msg = g_strdup_vprintf(fs, args);
    va_end(args);

    if (s->sftp) {
        char *ssh_err;
        int ssh_err_code;
        unsigned long sftp_err_code;

        /* This is not an errno.  See <libssh2.h>. */
        ssh_err_code = libssh2_session_last_error(s->session,
                                                  &ssh_err, NULL, 0);
        /* See <libssh2_sftp.h>. */
        sftp_err_code = libssh2_sftp_last_error((s)->sftp);

        error_setg(errp,
                   "%s: %s (libssh2 error code: %d, sftp error code: %lu)",
                   msg, ssh_err, ssh_err_code, sftp_err_code);
    } else {
        error_setg(errp, "%s", msg);
    }
    g_free(msg);
}

static void GCC_FMT_ATTR(2, 3)
sftp_error_report(BDRVSSHState *s, const char *fs, ...)
{
    va_list args;

    va_start(args, fs);
    error_vprintf(fs, args);

    if ((s)->sftp) {
        char *ssh_err;
        int ssh_err_code;
        unsigned long sftp_err_code;

        /* This is not an errno.  See <libssh2.h>. */
        ssh_err_code = libssh2_session_last_error(s->session,
                                                  &ssh_err, NULL, 0);
        /* See <libssh2_sftp.h>. */
        sftp_err_code = libssh2_sftp_last_error((s)->sftp);

        error_printf(": %s (libssh2 error code: %d, sftp error code: %lu)",
                     ssh_err, ssh_err_code, sftp_err_code);
    }

    va_end(args);
    error_printf("\n");
}

static int parse_uri(const char *filename, QDict *options, Error **errp)
{
    URI *uri = NULL;
    QueryParams *qp;
    char *port_str;
    int i;

    uri = uri_parse(filename);
    if (!uri) {
        return -EINVAL;
    }

    if (g_strcmp0(uri->scheme, "ssh") != 0) {
        error_setg(errp, "URI scheme must be 'ssh'");
        goto err;
    }

    if (!uri->server || strcmp(uri->server, "") == 0) {
        error_setg(errp, "missing hostname in URI");
        goto err;
    }

    if (!uri->path || strcmp(uri->path, "") == 0) {
        error_setg(errp, "missing remote path in URI");
        goto err;
    }

    qp = query_params_parse(uri->query);
    if (!qp) {
        error_setg(errp, "could not parse query parameters");
        goto err;
    }

    if(uri->user && strcmp(uri->user, "") != 0) {
        qdict_put_str(options, "user", uri->user);
    }

    qdict_put_str(options, "server.host", uri->server);

    port_str = g_strdup_printf("%d", uri->port ?: 22);
    qdict_put_str(options, "server.port", port_str);
    g_free(port_str);

    qdict_put_str(options, "path", uri->path);

    /* Pick out any query parameters that we understand, and ignore
     * the rest.
     */
    for (i = 0; i < qp->n; ++i) {
        if (strcmp(qp->p[i].name, "host_key_check") == 0) {
            qdict_put_str(options, "host_key_check", qp->p[i].value);
        }
    }

    query_params_free(qp);
    uri_free(uri);
    return 0;

 err:
    if (uri) {
      uri_free(uri);
    }
    return -EINVAL;
}

static bool ssh_has_filename_options_conflict(QDict *options, Error **errp)
{
    const QDictEntry *qe;

    for (qe = qdict_first(options); qe; qe = qdict_next(options, qe)) {
        if (!strcmp(qe->key, "host") ||
            !strcmp(qe->key, "port") ||
            !strcmp(qe->key, "path") ||
            !strcmp(qe->key, "user") ||
            !strcmp(qe->key, "host_key_check") ||
            strstart(qe->key, "server.", NULL))
        {
            error_setg(errp, "Option '%s' cannot be used with a file name",
                       qe->key);
            return true;
        }
    }

    return false;
}

static void ssh_parse_filename(const char *filename, QDict *options,
                               Error **errp)
{
    if (ssh_has_filename_options_conflict(options, errp)) {
        return;
    }

    parse_uri(filename, options, errp);
}

static int check_host_key_knownhosts(BDRVSSHState *s,
                                     const char *host, int port, Error **errp)
{
    const char *home;
    char *knh_file = NULL;
    LIBSSH2_KNOWNHOSTS *knh = NULL;
    struct libssh2_knownhost *found;
    int ret, r;
    const char *hostkey;
    size_t len;
    int type;

    hostkey = libssh2_session_hostkey(s->session, &len, &type);
    if (!hostkey) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to read remote host key");
        goto out;
    }

    knh = libssh2_knownhost_init(s->session);
    if (!knh) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to initialize known hosts support");
        goto out;
    }

    home = getenv("HOME");
    if (home) {
        knh_file = g_strdup_printf("%s/.ssh/known_hosts", home);
    } else {
        knh_file = g_strdup_printf("/root/.ssh/known_hosts");
    }

    /* Read all known hosts from OpenSSH-style known_hosts file. */
    libssh2_knownhost_readfile(knh, knh_file, LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    r = libssh2_knownhost_checkp(knh, host, port, hostkey, len,
                                 LIBSSH2_KNOWNHOST_TYPE_PLAIN|
                                 LIBSSH2_KNOWNHOST_KEYENC_RAW,
                                 &found);
    switch (r) {
    case LIBSSH2_KNOWNHOST_CHECK_MATCH:
        /* OK */
        DPRINTF("host key OK: %s", found->key);
        break;
    case LIBSSH2_KNOWNHOST_CHECK_MISMATCH:
        ret = -EINVAL;
        session_error_setg(errp, s,
                      "host key does not match the one in known_hosts"
                      " (found key %s)", found->key);
        goto out;
    case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND:
        ret = -EINVAL;
        session_error_setg(errp, s, "no host key was found in known_hosts");
        goto out;
    case LIBSSH2_KNOWNHOST_CHECK_FAILURE:
        ret = -EINVAL;
        session_error_setg(errp, s,
                      "failure matching the host key with known_hosts");
        goto out;
    default:
        ret = -EINVAL;
        session_error_setg(errp, s, "unknown error matching the host key"
                      " with known_hosts (%d)", r);
        goto out;
    }

    /* known_hosts checking successful. */
    ret = 0;

 out:
    if (knh != NULL) {
        libssh2_knownhost_free(knh);
    }
    g_free(knh_file);
    return ret;
}

static unsigned hex2decimal(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (ch - '0');
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }

    return -1;
}

/* Compare the binary fingerprint (hash of host key) with the
 * host_key_check parameter.
 */
static int compare_fingerprint(const unsigned char *fingerprint, size_t len,
                               const char *host_key_check)
{
    unsigned c;

    while (len > 0) {
        while (*host_key_check == ':')
            host_key_check++;
        if (!qemu_isxdigit(host_key_check[0]) ||
            !qemu_isxdigit(host_key_check[1]))
            return 1;
        c = hex2decimal(host_key_check[0]) * 16 +
            hex2decimal(host_key_check[1]);
        if (c - *fingerprint != 0)
            return c - *fingerprint;
        fingerprint++;
        len--;
        host_key_check += 2;
    }
    return *host_key_check - '\0';
}

static int
check_host_key_hash(BDRVSSHState *s, const char *hash,
                    int hash_type, size_t fingerprint_len, Error **errp)
{
    const char *fingerprint;

    fingerprint = libssh2_hostkey_hash(s->session, hash_type);
    if (!fingerprint) {
        session_error_setg(errp, s, "failed to read remote host key");
        return -EINVAL;
    }

    if(compare_fingerprint((unsigned char *) fingerprint, fingerprint_len,
                           hash) != 0) {
        error_setg(errp, "remote host key does not match host_key_check '%s'",
                   hash);
        return -EPERM;
    }

    return 0;
}

static int check_host_key(BDRVSSHState *s, const char *host, int port,
                          SshHostKeyCheck *hkc, Error **errp)
{
    SshHostKeyCheckMode mode;

    if (hkc) {
        mode = hkc->mode;
    } else {
        mode = SSH_HOST_KEY_CHECK_MODE_KNOWN_HOSTS;
    }

    switch (mode) {
    case SSH_HOST_KEY_CHECK_MODE_NONE:
        return 0;
    case SSH_HOST_KEY_CHECK_MODE_HASH:
        if (hkc->u.hash.type == SSH_HOST_KEY_CHECK_HASH_TYPE_MD5) {
            return check_host_key_hash(s, hkc->u.hash.hash,
                                       LIBSSH2_HOSTKEY_HASH_MD5, 16, errp);
        } else if (hkc->u.hash.type == SSH_HOST_KEY_CHECK_HASH_TYPE_SHA1) {
            return check_host_key_hash(s, hkc->u.hash.hash,
                                       LIBSSH2_HOSTKEY_HASH_SHA1, 20, errp);
        }
        g_assert_not_reached();
        break;
    case SSH_HOST_KEY_CHECK_MODE_KNOWN_HOSTS:
        return check_host_key_knownhosts(s, host, port, errp);
    default:
        g_assert_not_reached();
    }

    return -EINVAL;
}

static int authenticate(BDRVSSHState *s, const char *user, Error **errp)
{
    int r, ret;
    const char *userauthlist;
    LIBSSH2_AGENT *agent = NULL;
    struct libssh2_agent_publickey *identity;
    struct libssh2_agent_publickey *prev_identity = NULL;

    userauthlist = libssh2_userauth_list(s->session, user, strlen(user));
    if (strstr(userauthlist, "publickey") == NULL) {
        ret = -EPERM;
        error_setg(errp,
                "remote server does not support \"publickey\" authentication");
        goto out;
    }

    /* Connect to ssh-agent and try each identity in turn. */
    agent = libssh2_agent_init(s->session);
    if (!agent) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to initialize ssh-agent support");
        goto out;
    }
    if (libssh2_agent_connect(agent)) {
        ret = -ECONNREFUSED;
        session_error_setg(errp, s, "failed to connect to ssh-agent");
        goto out;
    }
    if (libssh2_agent_list_identities(agent)) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed requesting identities from ssh-agent");
        goto out;
    }

    for(;;) {
        r = libssh2_agent_get_identity(agent, &identity, prev_identity);
        if (r == 1) {           /* end of list */
            break;
        }
        if (r < 0) {
            ret = -EINVAL;
            session_error_setg(errp, s,
                               "failed to obtain identity from ssh-agent");
            goto out;
        }
        r = libssh2_agent_userauth(agent, user, identity);
        if (r == 0) {
            /* Authenticated! */
            ret = 0;
            goto out;
        }
        /* Failed to authenticate with this identity, try the next one. */
        prev_identity = identity;
    }

    ret = -EPERM;
    error_setg(errp, "failed to authenticate using publickey authentication "
               "and the identities held by your ssh-agent");

 out:
    if (agent != NULL) {
        /* Note: libssh2 implementation implicitly calls
         * libssh2_agent_disconnect if necessary.
         */
        libssh2_agent_free(agent);
    }

    return ret;
}

static QemuOptsList ssh_runtime_opts = {
    .name = "ssh",
    .head = QTAILQ_HEAD_INITIALIZER(ssh_runtime_opts.head),
    .desc = {
        {
            .name = "host",
            .type = QEMU_OPT_STRING,
            .help = "Host to connect to",
        },
        {
            .name = "port",
            .type = QEMU_OPT_NUMBER,
            .help = "Port to connect to",
        },
        {
            .name = "host_key_check",
            .type = QEMU_OPT_STRING,
            .help = "Defines how and what to check the host key against",
        },
        { /* end of list */ }
    },
};

static bool ssh_process_legacy_options(QDict *output_opts,
                                       QemuOpts *legacy_opts,
                                       Error **errp)
{
    const char *host = qemu_opt_get(legacy_opts, "host");
    const char *port = qemu_opt_get(legacy_opts, "port");
    const char *host_key_check = qemu_opt_get(legacy_opts, "host_key_check");

    if (!host && port) {
        error_setg(errp, "port may not be used without host");
        return false;
    }

    if (host) {
        qdict_put_str(output_opts, "server.host", host);
        qdict_put_str(output_opts, "server.port", port ?: stringify(22));
    }

    if (host_key_check) {
        if (strcmp(host_key_check, "no") == 0) {
            qdict_put_str(output_opts, "host-key-check.mode", "none");
        } else if (strncmp(host_key_check, "md5:", 4) == 0) {
            qdict_put_str(output_opts, "host-key-check.mode", "hash");
            qdict_put_str(output_opts, "host-key-check.type", "md5");
            qdict_put_str(output_opts, "host-key-check.hash",
                          &host_key_check[4]);
        } else if (strncmp(host_key_check, "sha1:", 5) == 0) {
            qdict_put_str(output_opts, "host-key-check.mode", "hash");
            qdict_put_str(output_opts, "host-key-check.type", "sha1");
            qdict_put_str(output_opts, "host-key-check.hash",
                          &host_key_check[5]);
        } else if (strcmp(host_key_check, "yes") == 0) {
            qdict_put_str(output_opts, "host-key-check.mode", "known_hosts");
        } else {
            error_setg(errp, "unknown host_key_check setting (%s)",
                       host_key_check);
            return false;
        }
    }

    return true;
}

static BlockdevOptionsSsh *ssh_parse_options(QDict *options, Error **errp)
{
    BlockdevOptionsSsh *result = NULL;
    QemuOpts *opts = NULL;
    Error *local_err = NULL;
    QObject *crumpled;
    const QDictEntry *e;
    Visitor *v;

    /* Translate legacy options */
    opts = qemu_opts_create(&ssh_runtime_opts, NULL, 0, &error_abort);
    qemu_opts_absorb_qdict(opts, options, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }

    if (!ssh_process_legacy_options(options, opts, errp)) {
        goto fail;
    }

    /* Create the QAPI object */
    crumpled = qdict_crumple(options, errp);
    if (crumpled == NULL) {
        goto fail;
    }

    /*
     * FIXME .numeric, .to, .ipv4 or .ipv6 don't work with -drive.
     * .to doesn't matter, it's ignored anyway.
     * That's because when @options come from -blockdev or
     * blockdev_add, members are typed according to the QAPI schema,
     * but when they come from -drive, they're all QString.  The
     * visitor expects the former.
     */
    v = qobject_input_visitor_new(crumpled);
    visit_type_BlockdevOptionsSsh(v, NULL, &result, &local_err);
    visit_free(v);
    qobject_unref(crumpled);

    if (local_err) {
        error_propagate(errp, local_err);
        goto fail;
    }

    /* Remove the processed options from the QDict (the visitor processes
     * _all_ options in the QDict) */
    while ((e = qdict_first(options))) {
        qdict_del(options, e->key);
    }

fail:
    qemu_opts_del(opts);
    return result;
}

static int connect_to_ssh(BDRVSSHState *s, BlockdevOptionsSsh *opts,
                          int ssh_flags, int creat_mode, Error **errp)
{
    int r, ret;
    const char *user;
    long port = 0;

    if (opts->has_user) {
        user = opts->user;
    } else {
        user = g_get_user_name();
        if (!user) {
            error_setg_errno(errp, errno, "Can't get user name");
            ret = -errno;
            goto err;
        }
    }

    /* Pop the config into our state object, Exit if invalid */
    s->inet = opts->server;
    opts->server = NULL;

    if (qemu_strtol(s->inet->port, NULL, 10, &port) < 0) {
        error_setg(errp, "Use only numeric port value");
        ret = -EINVAL;
        goto err;
    }

    /* Open the socket and connect. */
    s->sock = inet_connect_saddr(s->inet, errp);
    if (s->sock < 0) {
        ret = -EIO;
        goto err;
    }

    /* Create SSH session. */
    s->session = libssh2_session_init();
    if (!s->session) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to initialize libssh2 session");
        goto err;
    }

#if TRACE_LIBSSH2 != 0
    libssh2_trace(s->session, TRACE_LIBSSH2);
#endif

    r = libssh2_session_handshake(s->session, s->sock);
    if (r != 0) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to establish SSH session");
        goto err;
    }

    /* Check the remote host's key against known_hosts. */
    ret = check_host_key(s, s->inet->host, port, opts->host_key_check, errp);
    if (ret < 0) {
        goto err;
    }

    /* Authenticate. */
    ret = authenticate(s, user, errp);
    if (ret < 0) {
        goto err;
    }

    /* Start SFTP. */
    s->sftp = libssh2_sftp_init(s->session);
    if (!s->sftp) {
        session_error_setg(errp, s, "failed to initialize sftp handle");
        ret = -EINVAL;
        goto err;
    }

    /* Open the remote file. */
    DPRINTF("opening file %s flags=0x%x creat_mode=0%o",
            opts->path, ssh_flags, creat_mode);
    s->sftp_handle = libssh2_sftp_open(s->sftp, opts->path, ssh_flags,
                                       creat_mode);
    if (!s->sftp_handle) {
        session_error_setg(errp, s, "failed to open remote file '%s'",
                           opts->path);
        ret = -EINVAL;
        goto err;
    }

    r = libssh2_sftp_fstat(s->sftp_handle, &s->attrs);
    if (r < 0) {
        sftp_error_setg(errp, s, "failed to read file attributes");
        return -EINVAL;
    }

    return 0;

 err:
    if (s->sftp_handle) {
        libssh2_sftp_close(s->sftp_handle);
    }
    s->sftp_handle = NULL;
    if (s->sftp) {
        libssh2_sftp_shutdown(s->sftp);
    }
    s->sftp = NULL;
    if (s->session) {
        libssh2_session_disconnect(s->session,
                                   "from qemu ssh client: "
                                   "error opening connection");
        libssh2_session_free(s->session);
    }
    s->session = NULL;

    return ret;
}

static int ssh_file_open(BlockDriverState *bs, QDict *options, int bdrv_flags,
                         Error **errp)
{
    BDRVSSHState *s = bs->opaque;
    BlockdevOptionsSsh *opts;
    int ret;
    int ssh_flags;

    ssh_state_init(s);

    ssh_flags = LIBSSH2_FXF_READ;
    if (bdrv_flags & BDRV_O_RDWR) {
        ssh_flags |= LIBSSH2_FXF_WRITE;
    }

    opts = ssh_parse_options(options, errp);
    if (opts == NULL) {
        return -EINVAL;
    }

    /* Start up SSH. */
    ret = connect_to_ssh(s, opts, ssh_flags, 0, errp);
    if (ret < 0) {
        goto err;
    }

    /* Go non-blocking. */
    libssh2_session_set_blocking(s->session, 0);

    qapi_free_BlockdevOptionsSsh(opts);

    return 0;

 err:
    if (s->sock >= 0) {
        close(s->sock);
    }
    s->sock = -1;

    qapi_free_BlockdevOptionsSsh(opts);

    return ret;
}

/* Note: This is a blocking operation */
static int ssh_grow_file(BDRVSSHState *s, int64_t offset, Error **errp)
{
    ssize_t ret;
    char c[1] = { '\0' };
    int was_blocking = libssh2_session_get_blocking(s->session);

    /* offset must be strictly greater than the current size so we do
     * not overwrite anything */
    assert(offset > 0 && offset > s->attrs.filesize);

    libssh2_session_set_blocking(s->session, 1);

    libssh2_sftp_seek64(s->sftp_handle, offset - 1);
    ret = libssh2_sftp_write(s->sftp_handle, c, 1);

    libssh2_session_set_blocking(s->session, was_blocking);

    if (ret < 0) {
        sftp_error_setg(errp, s, "Failed to grow file");
        return -EIO;
    }

    s->attrs.filesize = offset;
    return 0;
}

static QemuOptsList ssh_create_opts = {
    .name = "ssh-create-opts",
    .head = QTAILQ_HEAD_INITIALIZER(ssh_create_opts.head),
    .desc = {
        {
            .name = BLOCK_OPT_SIZE,
            .type = QEMU_OPT_SIZE,
            .help = "Virtual disk size"
        },
        { /* end of list */ }
    }
};

static int ssh_co_create(BlockdevCreateOptions *options, Error **errp)
{
    BlockdevCreateOptionsSsh *opts = &options->u.ssh;
    BDRVSSHState s;
    int ret;

    assert(options->driver == BLOCKDEV_DRIVER_SSH);

    ssh_state_init(&s);

    ret = connect_to_ssh(&s, opts->location,
                         LIBSSH2_FXF_READ|LIBSSH2_FXF_WRITE|
                         LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
                         0644, errp);
    if (ret < 0) {
        goto fail;
    }

    if (opts->size > 0) {
        ret = ssh_grow_file(&s, opts->size, errp);
        if (ret < 0) {
            goto fail;
        }
    }

    ret = 0;
fail:
    ssh_state_free(&s);
    return ret;
}

static int coroutine_fn ssh_co_create_opts(const char *filename, QemuOpts *opts,
                                           Error **errp)
{
    BlockdevCreateOptions *create_options;
    BlockdevCreateOptionsSsh *ssh_opts;
    int ret;
    QDict *uri_options = NULL;

    create_options = g_new0(BlockdevCreateOptions, 1);
    create_options->driver = BLOCKDEV_DRIVER_SSH;
    ssh_opts = &create_options->u.ssh;

    /* Get desired file size. */
    ssh_opts->size = ROUND_UP(qemu_opt_get_size_del(opts, BLOCK_OPT_SIZE, 0),
                              BDRV_SECTOR_SIZE);
    DPRINTF("total_size=%" PRIi64, ssh_opts->size);

    uri_options = qdict_new();
    ret = parse_uri(filename, uri_options, errp);
    if (ret < 0) {
        goto out;
    }

    ssh_opts->location = ssh_parse_options(uri_options, errp);
    if (ssh_opts->location == NULL) {
        ret = -EINVAL;
        goto out;
    }

    ret = ssh_co_create(create_options, errp);

 out:
    qobject_unref(uri_options);
    qapi_free_BlockdevCreateOptions(create_options);
    return ret;
}

static void ssh_close(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;

    ssh_state_free(s);
}

static int ssh_has_zero_init(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    /* Assume false, unless we can positively prove it's true. */
    int has_zero_init = 0;

    if (s->attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        if (s->attrs.permissions & LIBSSH2_SFTP_S_IFREG) {
            has_zero_init = 1;
        }
    }

    return has_zero_init;
}

typedef struct BDRVSSHRestart {
    BlockDriverState *bs;
    Coroutine *co;
} BDRVSSHRestart;

static void restart_coroutine(void *opaque)
{
    BDRVSSHRestart *restart = opaque;
    BlockDriverState *bs = restart->bs;
    BDRVSSHState *s = bs->opaque;
    AioContext *ctx = bdrv_get_aio_context(bs);

    DPRINTF("co=%p", restart->co);
    aio_set_fd_handler(ctx, s->sock, false, NULL, NULL, NULL, NULL);

    aio_co_wake(restart->co);
}

/* A non-blocking call returned EAGAIN, so yield, ensuring the
 * handlers are set up so that we'll be rescheduled when there is an
 * interesting event on the socket.
 */
static coroutine_fn void co_yield(BDRVSSHState *s, BlockDriverState *bs)
{
    int r;
    IOHandler *rd_handler = NULL, *wr_handler = NULL;
    BDRVSSHRestart restart = {
        .bs = bs,
        .co = qemu_coroutine_self()
    };

    r = libssh2_session_block_directions(s->session);

    if (r & LIBSSH2_SESSION_BLOCK_INBOUND) {
        rd_handler = restart_coroutine;
    }
    if (r & LIBSSH2_SESSION_BLOCK_OUTBOUND) {
        wr_handler = restart_coroutine;
    }

    DPRINTF("s->sock=%d rd_handler=%p wr_handler=%p", s->sock,
            rd_handler, wr_handler);

    aio_set_fd_handler(bdrv_get_aio_context(bs), s->sock,
                       false, rd_handler, wr_handler, NULL, &restart);
    qemu_coroutine_yield();
    DPRINTF("s->sock=%d - back", s->sock);
}

/* SFTP has a function `libssh2_sftp_seek64' which seeks to a position
 * in the remote file.  Notice that it just updates a field in the
 * sftp_handle structure, so there is no network traffic and it cannot
 * fail.
 *
 * However, `libssh2_sftp_seek64' does have a catastrophic effect on
 * performance since it causes the handle to throw away all in-flight
 * reads and buffered readahead data.  Therefore this function tries
 * to be intelligent about when to call the underlying libssh2 function.
 */
#define SSH_SEEK_WRITE 0
#define SSH_SEEK_READ  1
#define SSH_SEEK_FORCE 2

static void ssh_seek(BDRVSSHState *s, int64_t offset, int flags)
{
    bool op_read = (flags & SSH_SEEK_READ) != 0;
    bool force = (flags & SSH_SEEK_FORCE) != 0;

    if (force || op_read != s->offset_op_read || offset != s->offset) {
        DPRINTF("seeking to offset=%" PRIi64, offset);
        libssh2_sftp_seek64(s->sftp_handle, offset);
        s->offset = offset;
        s->offset_op_read = op_read;
    }
}

static coroutine_fn int ssh_read(BDRVSSHState *s, BlockDriverState *bs,
                                 int64_t offset, size_t size,
                                 QEMUIOVector *qiov)
{
    ssize_t r;
    size_t got;
    char *buf, *end_of_vec;
    struct iovec *i;

    DPRINTF("offset=%" PRIi64 " size=%zu", offset, size);

    ssh_seek(s, offset, SSH_SEEK_READ);

    /* This keeps track of the current iovec element ('i'), where we
     * will write to next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    /* libssh2 has a hard-coded limit of 2000 bytes per request,
     * although it will also do readahead behind our backs.  Therefore
     * we may have to do repeated reads here until we have read 'size'
     * bytes.
     */
    for (got = 0; got < size; ) {
    again:
        DPRINTF("sftp_read buf=%p size=%zu", buf, end_of_vec - buf);
        r = libssh2_sftp_read(s->sftp_handle, buf, end_of_vec - buf);
        DPRINTF("sftp_read returned %zd", r);

        if (r == LIBSSH2_ERROR_EAGAIN || r == LIBSSH2_ERROR_TIMEOUT) {
            co_yield(s, bs);
            goto again;
        }
        if (r < 0) {
            sftp_error_report(s, "read failed");
            s->offset = -1;
            return -EIO;
        }
        if (r == 0) {
            /* EOF: Short read so pad the buffer with zeroes and return it. */
            qemu_iovec_memset(qiov, got, 0, size - got);
            return 0;
        }

        got += r;
        buf += r;
        s->offset += r;
        if (buf >= end_of_vec && got < size) {
            i++;
            buf = i->iov_base;
            end_of_vec = i->iov_base + i->iov_len;
        }
    }

    return 0;
}

static coroutine_fn int ssh_co_readv(BlockDriverState *bs,
                                     int64_t sector_num,
                                     int nb_sectors, QEMUIOVector *qiov)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_read(s, bs, sector_num * BDRV_SECTOR_SIZE,
                   nb_sectors * BDRV_SECTOR_SIZE, qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static int ssh_write(BDRVSSHState *s, BlockDriverState *bs,
                     int64_t offset, size_t size,
                     QEMUIOVector *qiov)
{
    ssize_t r;
    size_t written;
    char *buf, *end_of_vec;
    struct iovec *i;

    DPRINTF("offset=%" PRIi64 " size=%zu", offset, size);

    ssh_seek(s, offset, SSH_SEEK_WRITE);

    /* This keeps track of the current iovec element ('i'), where we
     * will read from next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    for (written = 0; written < size; ) {
    again:
        DPRINTF("sftp_write buf=%p size=%zu", buf, end_of_vec - buf);
        r = libssh2_sftp_write(s->sftp_handle, buf, end_of_vec - buf);
        DPRINTF("sftp_write returned %zd", r);

        if (r == LIBSSH2_ERROR_EAGAIN || r == LIBSSH2_ERROR_TIMEOUT) {
            co_yield(s, bs);
            goto again;
        }
        if (r < 0) {
            sftp_error_report(s, "write failed");
            s->offset = -1;
            return -EIO;
        }
        /* The libssh2 API is very unclear about this.  A comment in
         * the code says "nothing was acked, and no EAGAIN was
         * received!" which apparently means that no data got sent
         * out, and the underlying channel didn't return any EAGAIN
         * indication.  I think this is a bug in either libssh2 or
         * OpenSSH (server-side).  In any case, forcing a seek (to
         * discard libssh2 internal buffers), and then trying again
         * works for me.
         */
        if (r == 0) {
            ssh_seek(s, offset + written, SSH_SEEK_WRITE|SSH_SEEK_FORCE);
            co_yield(s, bs);
            goto again;
        }

        written += r;
        buf += r;
        s->offset += r;
        if (buf >= end_of_vec && written < size) {
            i++;
            buf = i->iov_base;
            end_of_vec = i->iov_base + i->iov_len;
        }

        if (offset + written > s->attrs.filesize)
            s->attrs.filesize = offset + written;
    }

    return 0;
}

static coroutine_fn int ssh_co_writev(BlockDriverState *bs,
                                      int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *qiov)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_write(s, bs, sector_num * BDRV_SECTOR_SIZE,
                    nb_sectors * BDRV_SECTOR_SIZE, qiov);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

static void unsafe_flush_warning(BDRVSSHState *s, const char *what)
{
    if (!s->unsafe_flush_warning) {
        warn_report("ssh server %s does not support fsync",
                    s->inet->host);
        if (what) {
            error_report("to support fsync, you need %s", what);
        }
        s->unsafe_flush_warning = true;
    }
}

#ifdef HAS_LIBSSH2_SFTP_FSYNC

static coroutine_fn int ssh_flush(BDRVSSHState *s, BlockDriverState *bs)
{
    int r;

    DPRINTF("fsync");
 again:
    r = libssh2_sftp_fsync(s->sftp_handle);
    if (r == LIBSSH2_ERROR_EAGAIN || r == LIBSSH2_ERROR_TIMEOUT) {
        co_yield(s, bs);
        goto again;
    }
    if (r == LIBSSH2_ERROR_SFTP_PROTOCOL &&
        libssh2_sftp_last_error(s->sftp) == LIBSSH2_FX_OP_UNSUPPORTED) {
        unsafe_flush_warning(s, "OpenSSH >= 6.3");
        return 0;
    }
    if (r < 0) {
        sftp_error_report(s, "fsync failed");
        return -EIO;
    }

    return 0;
}

static coroutine_fn int ssh_co_flush(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    qemu_co_mutex_lock(&s->lock);
    ret = ssh_flush(s, bs);
    qemu_co_mutex_unlock(&s->lock);

    return ret;
}

#else /* !HAS_LIBSSH2_SFTP_FSYNC */

static coroutine_fn int ssh_co_flush(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;

    unsafe_flush_warning(s, "libssh2 >= 1.4.4");
    return 0;
}

#endif /* !HAS_LIBSSH2_SFTP_FSYNC */

static int64_t ssh_getlength(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    int64_t length;

    /* Note we cannot make a libssh2 call here. */
    length = (int64_t) s->attrs.filesize;
    DPRINTF("length=%" PRIi64, length);

    return length;
}

static int ssh_truncate(BlockDriverState *bs, int64_t offset,
                        PreallocMode prealloc, Error **errp)
{
    BDRVSSHState *s = bs->opaque;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    if (offset < s->attrs.filesize) {
        error_setg(errp, "ssh driver does not support shrinking files");
        return -ENOTSUP;
    }

    if (offset == s->attrs.filesize) {
        return 0;
    }

    return ssh_grow_file(s, offset, errp);
}

static BlockDriver bdrv_ssh = {
    .format_name                  = "ssh",
    .protocol_name                = "ssh",
    .instance_size                = sizeof(BDRVSSHState),
    .bdrv_parse_filename          = ssh_parse_filename,
    .bdrv_file_open               = ssh_file_open,
    .bdrv_co_create               = ssh_co_create,
    .bdrv_co_create_opts          = ssh_co_create_opts,
    .bdrv_close                   = ssh_close,
    .bdrv_has_zero_init           = ssh_has_zero_init,
    .bdrv_co_readv                = ssh_co_readv,
    .bdrv_co_writev               = ssh_co_writev,
    .bdrv_getlength               = ssh_getlength,
    .bdrv_truncate                = ssh_truncate,
    .bdrv_co_flush_to_disk        = ssh_co_flush,
    .create_opts                  = &ssh_create_opts,
};

static void bdrv_ssh_init(void)
{
    int r;

    r = libssh2_init(0);
    if (r != 0) {
        fprintf(stderr, "libssh2 initialization failed, %d\n", r);
        exit(EXIT_FAILURE);
    }

    bdrv_register(&bdrv_ssh);
}

block_init(bdrv_ssh_init);
