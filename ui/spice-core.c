/*
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include <spice.h>

#include "system/system.h"
#include "system/runstate.h"
#include "ui/qemu-spice.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "qemu/queue.h"
#include "qemu-x509.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "qapi/qapi-events-ui.h"
#include "qemu/notify.h"
#include "qemu/option.h"
#include "crypto/secret_common.h"
#include "migration/misc.h"
#include "hw/pci/pci_bus.h"
#include "ui/spice-display.h"

/* core bits */

static SpiceServer *spice_server;
static NotifierWithReturn migration_state;
static const char *auth = "spice";
static char *auth_passwd;
static time_t auth_expires = TIME_MAX;
static int spice_migration_completed;
static int spice_display_is_running;
static int spice_have_target_host;

struct SpiceTimer {
    QEMUTimer *timer;
};

#define DEFAULT_MAX_REFRESH_RATE 30

static SpiceTimer *timer_add(SpiceTimerFunc func, void *opaque)
{
    SpiceTimer *timer;

    timer = g_malloc0(sizeof(*timer));
    timer->timer = timer_new_ms(QEMU_CLOCK_REALTIME, func, opaque);
    return timer;
}

static void timer_start(SpiceTimer *timer, uint32_t ms)
{
    timer_mod(timer->timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + ms);
}

static void timer_cancel(SpiceTimer *timer)
{
    timer_del(timer->timer);
}

static void timer_remove(SpiceTimer *timer)
{
    timer_free(timer->timer);
    g_free(timer);
}

struct SpiceWatch {
    int fd;
    SpiceWatchFunc func;
    void *opaque;
};

static void watch_read(void *opaque)
{
    SpiceWatch *watch = opaque;
    int fd = watch->fd;

#ifdef WIN32
    fd = _get_osfhandle(fd);
#endif
    watch->func(fd, SPICE_WATCH_EVENT_READ, watch->opaque);
}

static void watch_write(void *opaque)
{
    SpiceWatch *watch = opaque;
    int fd = watch->fd;

#ifdef WIN32
    fd = _get_osfhandle(fd);
#endif
    watch->func(fd, SPICE_WATCH_EVENT_WRITE, watch->opaque);
}

static void watch_update_mask(SpiceWatch *watch, int event_mask)
{
    IOHandler *on_read = NULL;
    IOHandler *on_write = NULL;

    if (event_mask & SPICE_WATCH_EVENT_READ) {
        on_read = watch_read;
    }
    if (event_mask & SPICE_WATCH_EVENT_WRITE) {
        on_write = watch_write;
    }
    qemu_set_fd_handler(watch->fd, on_read, on_write, watch);
}

static SpiceWatch *watch_add(int fd, int event_mask, SpiceWatchFunc func, void *opaque)
{
    SpiceWatch *watch;
#ifdef WIN32
    g_autofree char *msg = NULL;

    fd = _open_osfhandle(fd, _O_BINARY);
    if (fd < 0) {
        msg = g_win32_error_message(WSAGetLastError());
        warn_report("Couldn't associate a FD with the SOCKET: %s", msg);
        return NULL;
    }
#endif

    watch = g_malloc0(sizeof(*watch));
    watch->fd     = fd;
    watch->func   = func;
    watch->opaque = opaque;

    watch_update_mask(watch, event_mask);
    return watch;
}

static void watch_remove(SpiceWatch *watch)
{
    qemu_set_fd_handler(watch->fd, NULL, NULL, NULL);
#ifdef WIN32
    /* SOCKET is owned by spice */
    qemu_close_socket_osfhandle(watch->fd);
#endif
    g_free(watch);
}

typedef struct ChannelList ChannelList;
struct ChannelList {
    SpiceChannelEventInfo *info;
    QTAILQ_ENTRY(ChannelList) link;
};
static QTAILQ_HEAD(, ChannelList) channel_list = QTAILQ_HEAD_INITIALIZER(channel_list);

static void channel_list_add(SpiceChannelEventInfo *info)
{
    ChannelList *item;

    item = g_malloc0(sizeof(*item));
    item->info = info;
    QTAILQ_INSERT_TAIL(&channel_list, item, link);
}

static void channel_list_del(SpiceChannelEventInfo *info)
{
    ChannelList *item;

    QTAILQ_FOREACH(item, &channel_list, link) {
        if (item->info != info) {
            continue;
        }
        QTAILQ_REMOVE(&channel_list, item, link);
        g_free(item);
        return;
    }
}

static void add_addr_info(SpiceBasicInfo *info, struct sockaddr *addr, int len)
{
    char host[NI_MAXHOST], port[NI_MAXSERV];

    getnameinfo(addr, len, host, sizeof(host), port, sizeof(port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    info->host = g_strdup(host);
    info->port = g_strdup(port);
    info->family = inet_netfamily(addr->sa_family);
}

static void add_channel_info(SpiceChannel *sc, SpiceChannelEventInfo *info)
{
    int tls = info->flags & SPICE_CHANNEL_EVENT_FLAG_TLS;

    sc->connection_id = info->connection_id;
    sc->channel_type = info->type;
    sc->channel_id = info->id;
    sc->tls = !!tls;
}

static void channel_event(int event, SpiceChannelEventInfo *info)
{
    SpiceServerInfo *server = g_malloc0(sizeof(*server));
    SpiceChannel *client = g_malloc0(sizeof(*client));

    /*
     * Spice server might have called us from spice worker thread
     * context (happens on display channel disconnects).  Spice should
     * not do that.  It isn't that easy to fix it in spice and even
     * when it is fixed we still should cover the already released
     * spice versions.  So detect that we've been called from another
     * thread and grab the BQL if so before calling qemu
     * functions.
     */
    bool need_lock = !bql_locked();
    if (need_lock) {
        bql_lock();
    }

    if (info->flags & SPICE_CHANNEL_EVENT_FLAG_ADDR_EXT) {
        add_addr_info(qapi_SpiceChannel_base(client),
                      (struct sockaddr *)&info->paddr_ext,
                      info->plen_ext);
        add_addr_info(qapi_SpiceServerInfo_base(server),
                      (struct sockaddr *)&info->laddr_ext,
                      info->llen_ext);
    } else {
        error_report("spice: %s, extended address is expected",
                     __func__);
    }

    switch (event) {
    case SPICE_CHANNEL_EVENT_CONNECTED:
        qapi_event_send_spice_connected(qapi_SpiceServerInfo_base(server),
                                        qapi_SpiceChannel_base(client));
        break;
    case SPICE_CHANNEL_EVENT_INITIALIZED:
        if (auth) {
            server->auth = g_strdup(auth);
        }
        add_channel_info(client, info);
        channel_list_add(info);
        qapi_event_send_spice_initialized(server, client);
        break;
    case SPICE_CHANNEL_EVENT_DISCONNECTED:
        channel_list_del(info);
        qapi_event_send_spice_disconnected(qapi_SpiceServerInfo_base(server),
                                           qapi_SpiceChannel_base(client));
        break;
    default:
        break;
    }

    if (need_lock) {
        bql_unlock();
    }

    qapi_free_SpiceServerInfo(server);
    qapi_free_SpiceChannel(client);
}

static SpiceCoreInterface core_interface = {
    .base.type          = SPICE_INTERFACE_CORE,
    .base.description   = "qemu core services",
    .base.major_version = SPICE_INTERFACE_CORE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_CORE_MINOR,

    .timer_add          = timer_add,
    .timer_start        = timer_start,
    .timer_cancel       = timer_cancel,
    .timer_remove       = timer_remove,

    .watch_add          = watch_add,
    .watch_update_mask  = watch_update_mask,
    .watch_remove       = watch_remove,

    .channel_event      = channel_event,
};

static void migrate_connect_complete_cb(SpiceMigrateInstance *sin);
static void migrate_end_complete_cb(SpiceMigrateInstance *sin);

static const SpiceMigrateInterface migrate_interface = {
    .base.type = SPICE_INTERFACE_MIGRATION,
    .base.description = "migration",
    .base.major_version = SPICE_INTERFACE_MIGRATION_MAJOR,
    .base.minor_version = SPICE_INTERFACE_MIGRATION_MINOR,
    .migrate_connect_complete = migrate_connect_complete_cb,
    .migrate_end_complete = migrate_end_complete_cb,
};

static SpiceMigrateInstance spice_migrate;

static void migrate_connect_complete_cb(SpiceMigrateInstance *sin)
{
    /* nothing, but libspice-server expects this cb being present. */
}

static void migrate_end_complete_cb(SpiceMigrateInstance *sin)
{
    qapi_event_send_spice_migrate_completed();
    spice_migration_completed = true;
}

/* config string parsing */

static int name2enum(const char *string, const char *table[], int entries)
{
    int i;

    if (string) {
        for (i = 0; i < entries; i++) {
            if (!table[i]) {
                continue;
            }
            if (strcmp(string, table[i]) != 0) {
                continue;
            }
            return i;
        }
    }
    return -1;
}

static int parse_name(const char *string, const char *optname,
                      const char *table[], int entries)
{
    int value = name2enum(string, table, entries);

    if (value != -1) {
        return value;
    }
    error_report("spice: invalid %s: %s", optname, string);
    exit(1);
}

static const char *stream_video_names[] = {
    [ SPICE_STREAM_VIDEO_OFF ]    = "off",
    [ SPICE_STREAM_VIDEO_ALL ]    = "all",
    [ SPICE_STREAM_VIDEO_FILTER ] = "filter",
};
#define parse_stream_video(_name) \
    parse_name(_name, "stream video control", \
               stream_video_names, ARRAY_SIZE(stream_video_names))

static const char *compression_names[] = {
    [ SPICE_IMAGE_COMPRESS_OFF ]      = "off",
    [ SPICE_IMAGE_COMPRESS_AUTO_GLZ ] = "auto_glz",
    [ SPICE_IMAGE_COMPRESS_AUTO_LZ ]  = "auto_lz",
    [ SPICE_IMAGE_COMPRESS_QUIC ]     = "quic",
    [ SPICE_IMAGE_COMPRESS_GLZ ]      = "glz",
    [ SPICE_IMAGE_COMPRESS_LZ ]       = "lz",
};
#define parse_compression(_name)                                        \
    parse_name(_name, "image compression",                              \
               compression_names, ARRAY_SIZE(compression_names))

static const char *wan_compression_names[] = {
    [ SPICE_WAN_COMPRESSION_AUTO   ] = "auto",
    [ SPICE_WAN_COMPRESSION_NEVER  ] = "never",
    [ SPICE_WAN_COMPRESSION_ALWAYS ] = "always",
};
#define parse_wan_compression(_name)                                    \
    parse_name(_name, "wan compression",                                \
               wan_compression_names, ARRAY_SIZE(wan_compression_names))

/* functions for the rest of qemu */

static SpiceChannelList *qmp_query_spice_channels(void)
{
    SpiceChannelList *head = NULL, **tail = &head;
    ChannelList *item;

    QTAILQ_FOREACH(item, &channel_list, link) {
        SpiceChannel *chan;
        char host[NI_MAXHOST], port[NI_MAXSERV];
        struct sockaddr *paddr;
        socklen_t plen;

        assert(item->info->flags & SPICE_CHANNEL_EVENT_FLAG_ADDR_EXT);

        chan = g_malloc0(sizeof(*chan));

        paddr = (struct sockaddr *)&item->info->paddr_ext;
        plen = item->info->plen_ext;
        getnameinfo(paddr, plen,
                    host, sizeof(host), port, sizeof(port),
                    NI_NUMERICHOST | NI_NUMERICSERV);
        chan->host = g_strdup(host);
        chan->port = g_strdup(port);
        chan->family = inet_netfamily(paddr->sa_family);

        chan->connection_id = item->info->connection_id;
        chan->channel_type = item->info->type;
        chan->channel_id = item->info->id;
        chan->tls = item->info->flags & SPICE_CHANNEL_EVENT_FLAG_TLS;

        QAPI_LIST_APPEND(tail, chan);
    }

    return head;
}

static QemuOptsList qemu_spice_opts = {
    .name = "spice",
    .head = QTAILQ_HEAD_INITIALIZER(qemu_spice_opts.head),
    .merge_lists = true,
    .desc = {
        {
            .name = "port",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "tls-port",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "addr",
            .type = QEMU_OPT_STRING,
        },{
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
#ifdef SPICE_ADDR_FLAG_UNIX_ONLY
        },{
            .name = "unix",
            .type = QEMU_OPT_BOOL,
#endif
        },{
            .name = "password-secret",
            .type = QEMU_OPT_STRING,
        },{
            .name = "disable-ticketing",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "disable-copy-paste",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "disable-agent-file-xfer",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "sasl",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "x509-dir",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-key-password",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cert-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-cacert-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "x509-dh-key-file",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-ciphers",
            .type = QEMU_OPT_STRING,
        },{
            .name = "tls-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "plaintext-channel",
            .type = QEMU_OPT_STRING,
        },{
            .name = "image-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "jpeg-wan-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "zlib-glz-wan-compression",
            .type = QEMU_OPT_STRING,
        },{
            .name = "streaming-video",
            .type = QEMU_OPT_STRING,
        },{
            .name = "video-codec",
            .type = QEMU_OPT_STRING,
        },{
            .name = "max-refresh-rate",
            .type = QEMU_OPT_NUMBER,
        },{
            .name = "agent-mouse",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "playback-compression",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "seamless-migration",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "display",
            .type = QEMU_OPT_STRING,
        },{
            .name = "head",
            .type = QEMU_OPT_NUMBER,
#ifdef HAVE_SPICE_GL
        },{
            .name = "gl",
            .type = QEMU_OPT_BOOL,
        },{
            .name = "rendernode",
            .type = QEMU_OPT_STRING,
#endif
        },
        { /* end of list */ }
    },
};

static SpiceInfo *qmp_query_spice_real(Error **errp)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    int port, tls_port;
    const char *addr;
    SpiceInfo *info;
    unsigned int major;
    unsigned int minor;
    unsigned int micro;

    info = g_malloc0(sizeof(*info));

    if (!spice_server || !opts) {
        info->enabled = false;
        return info;
    }

    info->enabled = true;
    info->migrated = spice_migration_completed;

    addr = qemu_opt_get(opts, "addr");
    port = qemu_opt_get_number(opts, "port", 0);
    tls_port = qemu_opt_get_number(opts, "tls-port", 0);

    info->auth = g_strdup(auth);
    info->host = g_strdup(addr ? addr : "*");

    major = (SPICE_SERVER_VERSION & 0xff0000) >> 16;
    minor = (SPICE_SERVER_VERSION & 0xff00) >> 8;
    micro = SPICE_SERVER_VERSION & 0xff;
    info->compiled_version = g_strdup_printf("%d.%d.%d", major, minor, micro);

    if (port) {
        info->has_port = true;
        info->port = port;
    }
    if (tls_port) {
        info->has_tls_port = true;
        info->tls_port = tls_port;
    }

    info->mouse_mode = spice_server_is_server_mouse(spice_server) ?
                       SPICE_QUERY_MOUSE_MODE_SERVER :
                       SPICE_QUERY_MOUSE_MODE_CLIENT;

    /* for compatibility with the original command */
    info->has_channels = true;
    info->channels = qmp_query_spice_channels();

    return info;
}

static int migration_state_notifier(NotifierWithReturn *notifier,
                                    MigrationEvent *e, Error **errp)
{
    if (!spice_have_target_host) {
        return 0;
    }

    if (e->type == MIG_EVENT_PRECOPY_SETUP) {
        spice_server_migrate_start(spice_server);
    } else if (e->type == MIG_EVENT_PRECOPY_DONE) {
        spice_server_migrate_end(spice_server, true);
        spice_have_target_host = false;
    } else if (e->type == MIG_EVENT_PRECOPY_FAILED) {
        spice_server_migrate_end(spice_server, false);
        spice_have_target_host = false;
    }
    return 0;
}

int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject)
{
    int ret;

    ret = spice_server_migrate_connect(spice_server, hostname,
                                       port, tls_port, subject);
    spice_have_target_host = true;
    return ret;
}

static int add_channel(void *opaque, const char *name, const char *value,
                       Error **errp)
{
    int security = 0;
    int rc;

    if (strcmp(name, "tls-channel") == 0) {
        int *tls_port = opaque;
        if (!*tls_port) {
            error_setg(errp, "spice: tried to setup tls-channel"
                       " without specifying a TLS port");
            return -1;
        }
        security = SPICE_CHANNEL_SECURITY_SSL;
    }
    if (strcmp(name, "plaintext-channel") == 0) {
        security = SPICE_CHANNEL_SECURITY_NONE;
    }
    if (security == 0) {
        return 0;
    }
    if (strcmp(value, "default") == 0) {
        rc = spice_server_set_channel_security(spice_server, NULL, security);
    } else {
        rc = spice_server_set_channel_security(spice_server, value, security);
    }
    if (rc != 0) {
        error_setg(errp, "spice: failed to set channel security for %s",
                   value);
        return -1;
    }
    return 0;
}

static void vm_change_state_handler(void *opaque, bool running,
                                    RunState state)
{
    if (running) {
        qemu_spice_display_start();
    } else if (state != RUN_STATE_PAUSED) {
        qemu_spice_display_stop();
    }
}

void qemu_spice_display_init_done(void)
{
    if (runstate_is_running()) {
        qemu_spice_display_start();
    }
    qemu_add_vm_change_state_handler(vm_change_state_handler, NULL);
}

static void qemu_spice_init(void)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    char *password = NULL;
    const char *passwordSecret;
    const char *str, *x509_dir, *addr,
        *x509_key_password = NULL,
        *x509_dh_file = NULL,
        *tls_ciphers = NULL;
    char *x509_key_file = NULL,
        *x509_cert_file = NULL,
        *x509_cacert_file = NULL;
    int port, tls_port, addr_flags;
    spice_image_compression_t compression;
    spice_wan_compression_t wan_compr;
    bool seamless_migration;

    if (!opts) {
        return;
    }
    port = qemu_opt_get_number(opts, "port", 0);
    tls_port = qemu_opt_get_number(opts, "tls-port", 0);
    if (port < 0 || port > 65535) {
        error_report("spice port is out of range");
        exit(1);
    }
    if (tls_port < 0 || tls_port > 65535) {
        error_report("spice tls-port is out of range");
        exit(1);
    }
    passwordSecret = qemu_opt_get(opts, "password-secret");
    if (passwordSecret) {
        password = qcrypto_secret_lookup_as_utf8(passwordSecret,
                                                 &error_fatal);
    }

    if (tls_port) {
        x509_dir = qemu_opt_get(opts, "x509-dir");
        if (!x509_dir) {
            x509_dir = ".";
        }

        str = qemu_opt_get(opts, "x509-key-file");
        if (str) {
            x509_key_file = g_strdup(str);
        } else {
            x509_key_file = g_strdup_printf("%s/%s", x509_dir,
                                            X509_SERVER_KEY_FILE);
        }

        str = qemu_opt_get(opts, "x509-cert-file");
        if (str) {
            x509_cert_file = g_strdup(str);
        } else {
            x509_cert_file = g_strdup_printf("%s/%s", x509_dir,
                                             X509_SERVER_CERT_FILE);
        }

        str = qemu_opt_get(opts, "x509-cacert-file");
        if (str) {
            x509_cacert_file = g_strdup(str);
        } else {
            x509_cacert_file = g_strdup_printf("%s/%s", x509_dir,
                                               X509_CA_CERT_FILE);
        }

        x509_key_password = qemu_opt_get(opts, "x509-key-password");
        x509_dh_file = qemu_opt_get(opts, "x509-dh-key-file");
        tls_ciphers = qemu_opt_get(opts, "tls-ciphers");
    }

    addr = qemu_opt_get(opts, "addr");
    addr_flags = 0;
    if (qemu_opt_get_bool(opts, "ipv4", 0)) {
        addr_flags |= SPICE_ADDR_FLAG_IPV4_ONLY;
    } else if (qemu_opt_get_bool(opts, "ipv6", 0)) {
        addr_flags |= SPICE_ADDR_FLAG_IPV6_ONLY;
#ifdef SPICE_ADDR_FLAG_UNIX_ONLY
    } else if (qemu_opt_get_bool(opts, "unix", 0)) {
        addr_flags |= SPICE_ADDR_FLAG_UNIX_ONLY;
#endif
    }

    spice_server = spice_server_new();
    spice_server_set_addr(spice_server, addr ? addr : "", addr_flags);
    if (port) {
        spice_server_set_port(spice_server, port);
    }
    if (tls_port) {
        spice_server_set_tls(spice_server, tls_port,
                             x509_cacert_file,
                             x509_cert_file,
                             x509_key_file,
                             x509_key_password,
                             x509_dh_file,
                             tls_ciphers);
    }
    if (password) {
        qemu_spice.set_passwd(password, false, false);
    }
    if (qemu_opt_get_bool(opts, "sasl", 0)) {
        if (spice_server_set_sasl(spice_server, 1) == -1) {
            error_report("spice: failed to enable sasl");
            exit(1);
        }
        auth = "sasl";
    }
    if (qemu_opt_get_bool(opts, "disable-ticketing", 0)) {
        auth = "none";
        spice_server_set_noauth(spice_server);
    }

    if (qemu_opt_get_bool(opts, "disable-copy-paste", 0)) {
        spice_server_set_agent_copypaste(spice_server, false);
    }

    if (qemu_opt_get_bool(opts, "disable-agent-file-xfer", 0)) {
        spice_server_set_agent_file_xfer(spice_server, false);
    }

    compression = SPICE_IMAGE_COMPRESS_AUTO_GLZ;
    str = qemu_opt_get(opts, "image-compression");
    if (str) {
        compression = parse_compression(str);
    }
    spice_server_set_image_compression(spice_server, compression);

    wan_compr = SPICE_WAN_COMPRESSION_AUTO;
    str = qemu_opt_get(opts, "jpeg-wan-compression");
    if (str) {
        wan_compr = parse_wan_compression(str);
    }
    spice_server_set_jpeg_compression(spice_server, wan_compr);

    wan_compr = SPICE_WAN_COMPRESSION_AUTO;
    str = qemu_opt_get(opts, "zlib-glz-wan-compression");
    if (str) {
        wan_compr = parse_wan_compression(str);
    }
    spice_server_set_zlib_glz_compression(spice_server, wan_compr);

    str = qemu_opt_get(opts, "streaming-video");
    if (str) {
        int streaming_video = parse_stream_video(str);
        spice_server_set_streaming_video(spice_server, streaming_video);
    } else {
        spice_server_set_streaming_video(spice_server, SPICE_STREAM_VIDEO_OFF);
    }

    spice_max_refresh_rate = qemu_opt_get_number(opts, "max-refresh-rate",
                                                 DEFAULT_MAX_REFRESH_RATE);
    if (spice_max_refresh_rate <= 0) {
        error_report("max refresh rate/fps is invalid");
        exit(1);
    }

    spice_server_set_agent_mouse
        (spice_server, qemu_opt_get_bool(opts, "agent-mouse", 1));
    spice_server_set_playback_compression
        (spice_server, qemu_opt_get_bool(opts, "playback-compression", 1));

    qemu_opt_foreach(opts, add_channel, &tls_port, &error_fatal);

    spice_server_set_name(spice_server, qemu_name ?: "QEMU " QEMU_VERSION);
    spice_server_set_uuid(spice_server, (unsigned char *)&qemu_uuid);

    seamless_migration = qemu_opt_get_bool(opts, "seamless-migration", 0);
    spice_server_set_seamless_migration(spice_server, seamless_migration);
    spice_server_set_sasl_appname(spice_server, "qemu");
    if (spice_server_init(spice_server, &core_interface) != 0) {
        error_report("failed to initialize spice server");
        exit(1);
    };
    using_spice = 1;

    migration_add_notifier(&migration_state, migration_state_notifier);
    spice_migrate.base.sif = &migrate_interface.base;
    qemu_spice.add_interface(&spice_migrate.base);

    qemu_spice_input_init();

    qemu_spice_display_stop();

    g_free(x509_key_file);
    g_free(x509_cert_file);
    g_free(x509_cacert_file);
    g_free(password);

#ifdef HAVE_SPICE_GL
    if (qemu_opt_get_bool(opts, "gl", 0)) {
        if ((port != 0) || (tls_port != 0)) {
#if SPICE_SERVER_VERSION >= 0x000f03 /* release 0.15.3 */
            const char *video_codec = NULL;
            g_autofree char *enc_codec = NULL;

            spice_remote_client = 1;

            video_codec = qemu_opt_get(opts, "video-codec");
            if (video_codec) {
                enc_codec = g_strconcat("gstreamer:", video_codec, NULL);
            }
            if (spice_server_set_video_codecs(spice_server,
                                              enc_codec ?: "gstreamer:h264")) {
                error_report("invalid video codec");
                exit(1);
            }
#else
            error_report("SPICE GL support is local-only for now and "
                         "incompatible with -spice port/tls-port");
            exit(1);
#endif
        }
        egl_init(qemu_opt_get(opts, "rendernode"), DISPLAY_GL_MODE_ON, &error_fatal);
        spice_opengl = 1;
    }
#endif
}

static int qemu_spice_add_interface(SpiceBaseInstance *sin)
{
    if (!spice_server) {
        if (QTAILQ_FIRST(&qemu_spice_opts.head) != NULL) {
            error_report("Oops: spice configured but not active");
            exit(1);
        }
        /*
         * Create a spice server instance.
         * It does *not* listen on the network.
         * It handles QXL local rendering only.
         *
         * With a command line like '-vnc :0 -vga qxl' you'll end up here.
         */
        spice_server = spice_server_new();
        spice_server_set_sasl_appname(spice_server, "qemu");
        spice_server_init(spice_server, &core_interface);
        qemu_add_vm_change_state_handler(vm_change_state_handler, NULL);
    }

    return spice_server_add_interface(spice_server, sin);
}

static GSList *spice_consoles;

bool qemu_spice_have_display_interface(QemuConsole *con)
{
    if (g_slist_find(spice_consoles, con)) {
        return true;
    }
    return false;
}

int qemu_spice_add_display_interface(QXLInstance *qxlin, QemuConsole *con)
{
    if (g_slist_find(spice_consoles, con)) {
        return -1;
    }
    qxlin->id = qemu_console_get_index(con);
    spice_consoles = g_slist_append(spice_consoles, con);
    return qemu_spice_add_interface(&qxlin->base);
}

static int qemu_spice_set_ticket(bool fail_if_conn, bool disconnect_if_conn)
{
    time_t lifetime, now = time(NULL);
    char *passwd;

    if (now < auth_expires) {
        passwd = auth_passwd;
        lifetime = (auth_expires - now);
        if (lifetime > INT_MAX) {
            lifetime = INT_MAX;
        }
    } else {
        passwd = NULL;
        lifetime = 1;
    }
    return spice_server_set_ticket(spice_server, passwd, lifetime,
                                   fail_if_conn, disconnect_if_conn);
}

static int qemu_spice_set_passwd(const char *passwd,
                                 bool fail_if_conn, bool disconnect_if_conn)
{
    if (strcmp(auth, "spice") != 0) {
        return -1;
    }

    g_free(auth_passwd);
    auth_passwd = g_strdup(passwd);
    return qemu_spice_set_ticket(fail_if_conn, disconnect_if_conn);
}

static int qemu_spice_set_pw_expire(time_t expires)
{
    auth_expires = expires;
    return qemu_spice_set_ticket(false, false);
}

static int qemu_spice_display_add_client(int csock, int skipauth, int tls)
{
#ifdef WIN32
    csock = qemu_close_socket_osfhandle(csock);
#endif
    if (tls) {
        return spice_server_add_ssl_client(spice_server, csock, skipauth);
    } else {
        return spice_server_add_client(spice_server, csock, skipauth);
    }
}

void qemu_spice_display_start(void)
{
    if (spice_display_is_running) {
        return;
    }

    spice_display_is_running = true;
    spice_server_vm_start(spice_server);
}

void qemu_spice_display_stop(void)
{
    if (!spice_display_is_running) {
        return;
    }

    spice_server_vm_stop(spice_server);
    spice_display_is_running = false;
}

int qemu_spice_display_is_running(SimpleSpiceDisplay *ssd)
{
    return spice_display_is_running;
}

static struct QemuSpiceOps real_spice_ops = {
    .init         = qemu_spice_init,
    .display_init = qemu_spice_display_init,
    .migrate_info = qemu_spice_migrate_info,
    .set_passwd   = qemu_spice_set_passwd,
    .set_pw_expire = qemu_spice_set_pw_expire,
    .display_add_client = qemu_spice_display_add_client,
    .add_interface = qemu_spice_add_interface,
    .qmp_query = qmp_query_spice_real,
};

static void spice_register_config(void)
{
    qemu_spice = real_spice_ops;
    qemu_add_opts(&qemu_spice_opts);
}
opts_init(spice_register_config);
module_opts("spice");

#ifdef HAVE_SPICE_GL
module_dep("ui-opengl");
#endif
