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

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "block/block_int.h"
#include "block/qdict.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/sockets.h"
#include "qemu/uri.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "trace.h"

/*
 * TRACE_LIBSSH=<level> enables tracing in libssh itself.
 * The meaning of <level> is described here:
 * http://api.libssh.org/master/group__libssh__log.html
 */
#define TRACE_LIBSSH  0 /* see: SSH_LOG_* */

typedef struct BDRVSSHState {
    /* Coroutine. */
    CoMutex lock;

    /* SSH connection. */
    int sock;                         /* socket */
    ssh_session session;              /* ssh session */
    sftp_session sftp;                /* sftp session */
    sftp_file sftp_handle;            /* sftp remote file handle */

    /*
     * File attributes at open.  We try to keep the .size field
     * updated if it changes (eg by writing at the end of the file).
     */
    sftp_attributes attrs;

    InetSocketAddress *inet;

    /* Used to warn if 'flush' is not supported. */
    bool unsafe_flush_warning;

    /*
     * Store the user name for ssh_refresh_filename() because the
     * default depends on the system you are on -- therefore, when we
     * generate a filename, it should always contain the user name we
     * are actually using.
     */
    char *user;
} BDRVSSHState;

static void ssh_state_init(BDRVSSHState *s)
{
    memset(s, 0, sizeof *s);
    s->sock = -1;
    qemu_co_mutex_init(&s->lock);
}

static void ssh_state_free(BDRVSSHState *s)
{
    g_free(s->user);

    if (s->attrs) {
        sftp_attributes_free(s->attrs);
    }
    if (s->sftp_handle) {
        sftp_close(s->sftp_handle);
    }
    if (s->sftp) {
        sftp_free(s->sftp);
    }
    if (s->session) {
        ssh_disconnect(s->session);
        ssh_free(s->session); /* This frees s->sock */
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
        const char *ssh_err;
        int ssh_err_code;

        /* This is not an errno.  See <libssh/libssh.h>. */
        ssh_err = ssh_get_error(s->session);
        ssh_err_code = ssh_get_error_code(s->session);
        error_setg(errp, "%s: %s (libssh error code: %d)",
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
        const char *ssh_err;
        int ssh_err_code;
        int sftp_err_code;

        /* This is not an errno.  See <libssh/libssh.h>. */
        ssh_err = ssh_get_error(s->session);
        ssh_err_code = ssh_get_error_code(s->session);
        /* See <libssh/sftp.h>. */
        sftp_err_code = sftp_get_error(s->sftp);

        error_setg(errp,
                   "%s: %s (libssh error code: %d, sftp error code: %d)",
                   msg, ssh_err, ssh_err_code, sftp_err_code);
    } else {
        error_setg(errp, "%s", msg);
    }
    g_free(msg);
}

static void sftp_error_trace(BDRVSSHState *s, const char *op)
{
    const char *ssh_err;
    int ssh_err_code;
    int sftp_err_code;

    /* This is not an errno.  See <libssh/libssh.h>. */
    ssh_err = ssh_get_error(s->session);
    ssh_err_code = ssh_get_error_code(s->session);
    /* See <libssh/sftp.h>. */
    sftp_err_code = sftp_get_error(s->sftp);

    trace_sftp_error(op, ssh_err, ssh_err_code, sftp_err_code);
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

static int check_host_key_knownhosts(BDRVSSHState *s, Error **errp)
{
    int ret;
#ifdef HAVE_LIBSSH_0_8
    enum ssh_known_hosts_e state;
    int r;
    ssh_key pubkey;
    enum ssh_keytypes_e pubkey_type;
    unsigned char *server_hash = NULL;
    size_t server_hash_len;
    char *fingerprint = NULL;

    state = ssh_session_is_known_server(s->session);
    trace_ssh_server_status(state);

    switch (state) {
    case SSH_KNOWN_HOSTS_OK:
        /* OK */
        trace_ssh_check_host_key_knownhosts();
        break;
    case SSH_KNOWN_HOSTS_CHANGED:
        ret = -EINVAL;
        r = ssh_get_server_publickey(s->session, &pubkey);
        if (r == 0) {
            r = ssh_get_publickey_hash(pubkey, SSH_PUBLICKEY_HASH_SHA256,
                                       &server_hash, &server_hash_len);
            pubkey_type = ssh_key_type(pubkey);
            ssh_key_free(pubkey);
        }
        if (r == 0) {
            fingerprint = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256,
                                                   server_hash,
                                                   server_hash_len);
            ssh_clean_pubkey_hash(&server_hash);
        }
        if (fingerprint) {
            error_setg(errp,
                       "host key (%s key with fingerprint %s) does not match "
                       "the one in known_hosts; this may be a possible attack",
                       ssh_key_type_to_char(pubkey_type), fingerprint);
            ssh_string_free_char(fingerprint);
        } else  {
            error_setg(errp,
                       "host key does not match the one in known_hosts; this "
                       "may be a possible attack");
        }
        goto out;
    case SSH_KNOWN_HOSTS_OTHER:
        ret = -EINVAL;
        error_setg(errp,
                   "host key for this server not found, another type exists");
        goto out;
    case SSH_KNOWN_HOSTS_UNKNOWN:
        ret = -EINVAL;
        error_setg(errp, "no host key was found in known_hosts");
        goto out;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
        ret = -ENOENT;
        error_setg(errp, "known_hosts file not found");
        goto out;
    case SSH_KNOWN_HOSTS_ERROR:
        ret = -EINVAL;
        error_setg(errp, "error while checking the host");
        goto out;
    default:
        ret = -EINVAL;
        error_setg(errp, "error while checking for known server (%d)", state);
        goto out;
    }
#else /* !HAVE_LIBSSH_0_8 */
    int state;

    state = ssh_is_server_known(s->session);
    trace_ssh_server_status(state);

    switch (state) {
    case SSH_SERVER_KNOWN_OK:
        /* OK */
        trace_ssh_check_host_key_knownhosts();
        break;
    case SSH_SERVER_KNOWN_CHANGED:
        ret = -EINVAL;
        error_setg(errp,
                   "host key does not match the one in known_hosts; this "
                   "may be a possible attack");
        goto out;
    case SSH_SERVER_FOUND_OTHER:
        ret = -EINVAL;
        error_setg(errp,
                   "host key for this server not found, another type exists");
        goto out;
    case SSH_SERVER_FILE_NOT_FOUND:
        ret = -ENOENT;
        error_setg(errp, "known_hosts file not found");
        goto out;
    case SSH_SERVER_NOT_KNOWN:
        ret = -EINVAL;
        error_setg(errp, "no host key was found in known_hosts");
        goto out;
    case SSH_SERVER_ERROR:
        ret = -EINVAL;
        error_setg(errp, "server error");
        goto out;
    default:
        ret = -EINVAL;
        error_setg(errp, "error while checking for known server (%d)", state);
        goto out;
    }
#endif /* !HAVE_LIBSSH_0_8 */

    /* known_hosts checking successful. */
    ret = 0;

 out:
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
                    enum ssh_publickey_hash_type type, Error **errp)
{
    int r;
    ssh_key pubkey;
    unsigned char *server_hash;
    size_t server_hash_len;

#ifdef HAVE_LIBSSH_0_8
    r = ssh_get_server_publickey(s->session, &pubkey);
#else
    r = ssh_get_publickey(s->session, &pubkey);
#endif
    if (r != SSH_OK) {
        session_error_setg(errp, s, "failed to read remote host key");
        return -EINVAL;
    }

    r = ssh_get_publickey_hash(pubkey, type, &server_hash, &server_hash_len);
    ssh_key_free(pubkey);
    if (r != 0) {
        session_error_setg(errp, s,
                           "failed reading the hash of the server SSH key");
        return -EINVAL;
    }

    r = compare_fingerprint(server_hash, server_hash_len, hash);
    ssh_clean_pubkey_hash(&server_hash);
    if (r != 0) {
        error_setg(errp, "remote host key does not match host_key_check '%s'",
                   hash);
        return -EPERM;
    }

    return 0;
}

static int check_host_key(BDRVSSHState *s, SshHostKeyCheck *hkc, Error **errp)
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
                                       SSH_PUBLICKEY_HASH_MD5, errp);
        } else if (hkc->u.hash.type == SSH_HOST_KEY_CHECK_HASH_TYPE_SHA1) {
            return check_host_key_hash(s, hkc->u.hash.hash,
                                       SSH_PUBLICKEY_HASH_SHA1, errp);
        }
        g_assert_not_reached();
        break;
    case SSH_HOST_KEY_CHECK_MODE_KNOWN_HOSTS:
        return check_host_key_knownhosts(s, errp);
    default:
        g_assert_not_reached();
    }

    return -EINVAL;
}

static int authenticate(BDRVSSHState *s, Error **errp)
{
    int r, ret;
    int method;

    /* Try to authenticate with the "none" method. */
    r = ssh_userauth_none(s->session, NULL);
    if (r == SSH_AUTH_ERROR) {
        ret = -EPERM;
        session_error_setg(errp, s, "failed to authenticate using none "
                                    "authentication");
        goto out;
    } else if (r == SSH_AUTH_SUCCESS) {
        /* Authenticated! */
        ret = 0;
        goto out;
    }

    method = ssh_userauth_list(s->session, NULL);
    trace_ssh_auth_methods(method);

    /*
     * Try to authenticate with publickey, using the ssh-agent
     * if available.
     */
    if (method & SSH_AUTH_METHOD_PUBLICKEY) {
        r = ssh_userauth_publickey_auto(s->session, NULL, NULL);
        if (r == SSH_AUTH_ERROR) {
            ret = -EINVAL;
            session_error_setg(errp, s, "failed to authenticate using "
                                        "publickey authentication");
            goto out;
        } else if (r == SSH_AUTH_SUCCESS) {
            /* Authenticated! */
            ret = 0;
            goto out;
        }
    }

    ret = -EPERM;
    error_setg(errp, "failed to authenticate using publickey authentication "
               "and the identities held by your ssh-agent");

 out:
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
    const QDictEntry *e;
    Visitor *v;

    /* Translate legacy options */
    opts = qemu_opts_create(&ssh_runtime_opts, NULL, 0, &error_abort);
    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        goto fail;
    }

    if (!ssh_process_legacy_options(options, opts, errp)) {
        goto fail;
    }

    /* Create the QAPI object */
    v = qobject_input_visitor_new_flat_confused(options, errp);
    if (!v) {
        goto fail;
    }

    visit_type_BlockdevOptionsSsh(v, NULL, &result, errp);
    visit_free(v);
    if (!result) {
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
    unsigned int port = 0;
    int new_sock = -1;

    if (opts->has_user) {
        s->user = g_strdup(opts->user);
    } else {
        s->user = g_strdup(g_get_user_name());
        if (!s->user) {
            error_setg_errno(errp, errno, "Can't get user name");
            ret = -errno;
            goto err;
        }
    }

    /* Pop the config into our state object, Exit if invalid */
    s->inet = opts->server;
    opts->server = NULL;

    if (qemu_strtoui(s->inet->port, NULL, 10, &port) < 0) {
        error_setg(errp, "Use only numeric port value");
        ret = -EINVAL;
        goto err;
    }

    /* Open the socket and connect. */
    new_sock = inet_connect_saddr(s->inet, errp);
    if (new_sock < 0) {
        ret = -EIO;
        goto err;
    }

    /*
     * Try to disable the Nagle algorithm on TCP sockets to reduce latency,
     * but do not fail if it cannot be disabled.
     */
    r = socket_set_nodelay(new_sock);
    if (r < 0) {
        warn_report("can't set TCP_NODELAY for the ssh server %s: %s",
                    s->inet->host, strerror(errno));
    }

    /* Create SSH session. */
    s->session = ssh_new();
    if (!s->session) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to initialize libssh session");
        goto err;
    }

    /*
     * Make sure we are in blocking mode during the connection and
     * authentication phases.
     */
    ssh_set_blocking(s->session, 1);

    r = ssh_options_set(s->session, SSH_OPTIONS_USER, s->user);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to set the user in the libssh session");
        goto err;
    }

    r = ssh_options_set(s->session, SSH_OPTIONS_HOST, s->inet->host);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to set the host in the libssh session");
        goto err;
    }

    if (port > 0) {
        r = ssh_options_set(s->session, SSH_OPTIONS_PORT, &port);
        if (r < 0) {
            ret = -EINVAL;
            session_error_setg(errp, s,
                               "failed to set the port in the libssh session");
            goto err;
        }
    }

    r = ssh_options_set(s->session, SSH_OPTIONS_COMPRESSION, "none");
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to disable the compression in the libssh "
                           "session");
        goto err;
    }

    /* Read ~/.ssh/config. */
    r = ssh_options_parse_config(s->session, NULL);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to parse ~/.ssh/config");
        goto err;
    }

    r = ssh_options_set(s->session, SSH_OPTIONS_FD, &new_sock);
    if (r < 0) {
        ret = -EINVAL;
        session_error_setg(errp, s,
                           "failed to set the socket in the libssh session");
        goto err;
    }
    /* libssh took ownership of the socket. */
    s->sock = new_sock;
    new_sock = -1;

    /* Connect. */
    r = ssh_connect(s->session);
    if (r != SSH_OK) {
        ret = -EINVAL;
        session_error_setg(errp, s, "failed to establish SSH session");
        goto err;
    }

    /* Check the remote host's key against known_hosts. */
    ret = check_host_key(s, opts->host_key_check, errp);
    if (ret < 0) {
        goto err;
    }

    /* Authenticate. */
    ret = authenticate(s, errp);
    if (ret < 0) {
        goto err;
    }

    /* Start SFTP. */
    s->sftp = sftp_new(s->session);
    if (!s->sftp) {
        session_error_setg(errp, s, "failed to create sftp handle");
        ret = -EINVAL;
        goto err;
    }

    r = sftp_init(s->sftp);
    if (r < 0) {
        sftp_error_setg(errp, s, "failed to initialize sftp handle");
        ret = -EINVAL;
        goto err;
    }

    /* Open the remote file. */
    trace_ssh_connect_to_ssh(opts->path, ssh_flags, creat_mode);
    s->sftp_handle = sftp_open(s->sftp, opts->path, ssh_flags, creat_mode);
    if (!s->sftp_handle) {
        sftp_error_setg(errp, s, "failed to open remote file '%s'",
                        opts->path);
        ret = -EINVAL;
        goto err;
    }

    /* Make sure the SFTP file is handled in blocking mode. */
    sftp_file_set_blocking(s->sftp_handle);

    s->attrs = sftp_fstat(s->sftp_handle);
    if (!s->attrs) {
        sftp_error_setg(errp, s, "failed to read file attributes");
        return -EINVAL;
    }

    return 0;

 err:
    if (s->attrs) {
        sftp_attributes_free(s->attrs);
    }
    s->attrs = NULL;
    if (s->sftp_handle) {
        sftp_close(s->sftp_handle);
    }
    s->sftp_handle = NULL;
    if (s->sftp) {
        sftp_free(s->sftp);
    }
    s->sftp = NULL;
    if (s->session) {
        ssh_disconnect(s->session);
        ssh_free(s->session);
    }
    s->session = NULL;
    s->sock = -1;
    if (new_sock >= 0) {
        close(new_sock);
    }

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

    ssh_flags = 0;
    if (bdrv_flags & BDRV_O_RDWR) {
        ssh_flags |= O_RDWR;
    } else {
        ssh_flags |= O_RDONLY;
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
    ssh_set_blocking(s->session, 0);

    if (s->attrs->type == SSH_FILEXFER_TYPE_REGULAR) {
        bs->supported_truncate_flags = BDRV_REQ_ZERO_WRITE;
    }

    qapi_free_BlockdevOptionsSsh(opts);

    return 0;

 err:
    qapi_free_BlockdevOptionsSsh(opts);

    return ret;
}

/* Note: This is a blocking operation */
static int ssh_grow_file(BDRVSSHState *s, int64_t offset, Error **errp)
{
    ssize_t ret;
    char c[1] = { '\0' };
    int was_blocking = ssh_is_blocking(s->session);

    /* offset must be strictly greater than the current size so we do
     * not overwrite anything */
    assert(offset > 0 && offset > s->attrs->size);

    ssh_set_blocking(s->session, 1);

    sftp_seek64(s->sftp_handle, offset - 1);
    ret = sftp_write(s->sftp_handle, c, 1);

    ssh_set_blocking(s->session, was_blocking);

    if (ret < 0) {
        sftp_error_setg(errp, s, "Failed to grow file");
        return -EIO;
    }

    s->attrs->size = offset;
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
                         O_RDWR | O_CREAT | O_TRUNC,
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

static int coroutine_fn ssh_co_create_opts(BlockDriver *drv,
                                           const char *filename,
                                           QemuOpts *opts,
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
    trace_ssh_co_create_opts(ssh_opts->size);

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

    if (s->attrs->type == SSH_FILEXFER_TYPE_REGULAR) {
        has_zero_init = 1;
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

    trace_ssh_restart_coroutine(restart->co);
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

    r = ssh_get_poll_flags(s->session);

    if (r & SSH_READ_PENDING) {
        rd_handler = restart_coroutine;
    }
    if (r & SSH_WRITE_PENDING) {
        wr_handler = restart_coroutine;
    }

    trace_ssh_co_yield(s->sock, rd_handler, wr_handler);

    aio_set_fd_handler(bdrv_get_aio_context(bs), s->sock,
                       false, rd_handler, wr_handler, NULL, &restart);
    qemu_coroutine_yield();
    trace_ssh_co_yield_back(s->sock);
}

static coroutine_fn int ssh_read(BDRVSSHState *s, BlockDriverState *bs,
                                 int64_t offset, size_t size,
                                 QEMUIOVector *qiov)
{
    ssize_t r;
    size_t got;
    char *buf, *end_of_vec;
    struct iovec *i;

    trace_ssh_read(offset, size);

    trace_ssh_seek(offset);
    sftp_seek64(s->sftp_handle, offset);

    /* This keeps track of the current iovec element ('i'), where we
     * will write to next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    for (got = 0; got < size; ) {
        size_t request_read_size;
    again:
        /*
         * The size of SFTP packets is limited to 32K bytes, so limit
         * the amount of data requested to 16K, as libssh currently
         * does not handle multiple requests on its own.
         */
        request_read_size = MIN(end_of_vec - buf, 16384);
        trace_ssh_read_buf(buf, end_of_vec - buf, request_read_size);
        r = sftp_read(s->sftp_handle, buf, request_read_size);
        trace_ssh_read_return(r, sftp_get_error(s->sftp));

        if (r == SSH_AGAIN) {
            co_yield(s, bs);
            goto again;
        }
        if (r == SSH_EOF || (r == 0 && sftp_get_error(s->sftp) == SSH_FX_EOF)) {
            /* EOF: Short read so pad the buffer with zeroes and return it. */
            qemu_iovec_memset(qiov, got, 0, size - got);
            return 0;
        }
        if (r <= 0) {
            sftp_error_trace(s, "read");
            return -EIO;
        }

        got += r;
        buf += r;
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

    trace_ssh_write(offset, size);

    trace_ssh_seek(offset);
    sftp_seek64(s->sftp_handle, offset);

    /* This keeps track of the current iovec element ('i'), where we
     * will read from next ('buf'), and the end of the current iovec
     * ('end_of_vec').
     */
    i = &qiov->iov[0];
    buf = i->iov_base;
    end_of_vec = i->iov_base + i->iov_len;

    for (written = 0; written < size; ) {
        size_t request_write_size;
    again:
        /*
         * Avoid too large data packets, as libssh currently does not
         * handle multiple requests on its own.
         */
        request_write_size = MIN(end_of_vec - buf, 131072);
        trace_ssh_write_buf(buf, end_of_vec - buf, request_write_size);
        r = sftp_write(s->sftp_handle, buf, request_write_size);
        trace_ssh_write_return(r, sftp_get_error(s->sftp));

        if (r == SSH_AGAIN) {
            co_yield(s, bs);
            goto again;
        }
        if (r < 0) {
            sftp_error_trace(s, "write");
            return -EIO;
        }

        written += r;
        buf += r;
        if (buf >= end_of_vec && written < size) {
            i++;
            buf = i->iov_base;
            end_of_vec = i->iov_base + i->iov_len;
        }

        if (offset + written > s->attrs->size) {
            s->attrs->size = offset + written;
        }
    }

    return 0;
}

static coroutine_fn int ssh_co_writev(BlockDriverState *bs,
                                      int64_t sector_num,
                                      int nb_sectors, QEMUIOVector *qiov,
                                      int flags)
{
    BDRVSSHState *s = bs->opaque;
    int ret;

    assert(!flags);
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

#ifdef HAVE_LIBSSH_0_8

static coroutine_fn int ssh_flush(BDRVSSHState *s, BlockDriverState *bs)
{
    int r;

    trace_ssh_flush();

    if (!sftp_extension_supported(s->sftp, "fsync@openssh.com", "1")) {
        unsafe_flush_warning(s, "OpenSSH >= 6.3");
        return 0;
    }
 again:
    r = sftp_fsync(s->sftp_handle);
    if (r == SSH_AGAIN) {
        co_yield(s, bs);
        goto again;
    }
    if (r < 0) {
        sftp_error_trace(s, "fsync");
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

#else /* !HAVE_LIBSSH_0_8 */

static coroutine_fn int ssh_co_flush(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;

    unsafe_flush_warning(s, "libssh >= 0.8.0");
    return 0;
}

#endif /* !HAVE_LIBSSH_0_8 */

static int64_t ssh_getlength(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    int64_t length;

    /* Note we cannot make a libssh call here. */
    length = (int64_t) s->attrs->size;
    trace_ssh_getlength(length);

    return length;
}

static int coroutine_fn ssh_co_truncate(BlockDriverState *bs, int64_t offset,
                                        bool exact, PreallocMode prealloc,
                                        BdrvRequestFlags flags, Error **errp)
{
    BDRVSSHState *s = bs->opaque;

    if (prealloc != PREALLOC_MODE_OFF) {
        error_setg(errp, "Unsupported preallocation mode '%s'",
                   PreallocMode_str(prealloc));
        return -ENOTSUP;
    }

    if (offset < s->attrs->size) {
        error_setg(errp, "ssh driver does not support shrinking files");
        return -ENOTSUP;
    }

    if (offset == s->attrs->size) {
        return 0;
    }

    return ssh_grow_file(s, offset, errp);
}

static void ssh_refresh_filename(BlockDriverState *bs)
{
    BDRVSSHState *s = bs->opaque;
    const char *path, *host_key_check;
    int ret;

    /*
     * None of these options can be represented in a plain "host:port"
     * format, so if any was given, we have to abort.
     */
    if (s->inet->has_ipv4 || s->inet->has_ipv6 || s->inet->has_to ||
        s->inet->has_numeric)
    {
        return;
    }

    path = qdict_get_try_str(bs->full_open_options, "path");
    assert(path); /* mandatory option */

    host_key_check = qdict_get_try_str(bs->full_open_options, "host_key_check");

    ret = snprintf(bs->exact_filename, sizeof(bs->exact_filename),
                   "ssh://%s@%s:%s%s%s%s",
                   s->user, s->inet->host, s->inet->port, path,
                   host_key_check ? "?host_key_check=" : "",
                   host_key_check ?: "");
    if (ret >= sizeof(bs->exact_filename)) {
        /* An overflow makes the filename unusable, so do not report any */
        bs->exact_filename[0] = '\0';
    }
}

static char *ssh_bdrv_dirname(BlockDriverState *bs, Error **errp)
{
    if (qdict_haskey(bs->full_open_options, "host_key_check")) {
        /*
         * We cannot generate a simple prefix if we would have to
         * append a query string.
         */
        error_setg(errp,
                   "Cannot generate a base directory with host_key_check set");
        return NULL;
    }

    if (bs->exact_filename[0] == '\0') {
        error_setg(errp, "Cannot generate a base directory for this ssh node");
        return NULL;
    }

    return path_combine(bs->exact_filename, "");
}

static const char *const ssh_strong_runtime_opts[] = {
    "host",
    "port",
    "path",
    "user",
    "host_key_check",
    "server.",

    NULL
};

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
    .bdrv_co_truncate             = ssh_co_truncate,
    .bdrv_co_flush_to_disk        = ssh_co_flush,
    .bdrv_refresh_filename        = ssh_refresh_filename,
    .bdrv_dirname                 = ssh_bdrv_dirname,
    .create_opts                  = &ssh_create_opts,
    .strong_runtime_opts          = ssh_strong_runtime_opts,
};

static void bdrv_ssh_init(void)
{
    int r;

    r = ssh_init();
    if (r != 0) {
        fprintf(stderr, "libssh initialization failed, %d\n", r);
        exit(EXIT_FAILURE);
    }

#if TRACE_LIBSSH != 0
    ssh_set_log_level(TRACE_LIBSSH);
#endif

    bdrv_register(&bdrv_ssh);
}

block_init(bdrv_ssh_init);
