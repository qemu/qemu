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

#include <spice.h>
#include <spice-experimental.h>

#include <netdb.h>

#include "qemu-common.h"
#include "qemu-spice.h"
#include "qemu-timer.h"
#include "qemu-queue.h"
#include "qemu-x509.h"
#include "qemu_socket.h"
#include "qint.h"
#include "qbool.h"
#include "qstring.h"
#include "qjson.h"
#include "notify.h"
#include "migration.h"
#include "monitor.h"
#include "hw/hw.h"

/* core bits */

static SpiceServer *spice_server;
static Notifier migration_state;
static const char *auth = "spice";
static char *auth_passwd;
static time_t auth_expires = TIME_MAX;
int using_spice = 0;

struct SpiceTimer {
    QEMUTimer *timer;
    QTAILQ_ENTRY(SpiceTimer) next;
};
static QTAILQ_HEAD(, SpiceTimer) timers = QTAILQ_HEAD_INITIALIZER(timers);

static SpiceTimer *timer_add(SpiceTimerFunc func, void *opaque)
{
    SpiceTimer *timer;

    timer = qemu_mallocz(sizeof(*timer));
    timer->timer = qemu_new_timer_ms(rt_clock, func, opaque);
    QTAILQ_INSERT_TAIL(&timers, timer, next);
    return timer;
}

static void timer_start(SpiceTimer *timer, uint32_t ms)
{
    qemu_mod_timer(timer->timer, qemu_get_clock_ms(rt_clock) + ms);
}

static void timer_cancel(SpiceTimer *timer)
{
    qemu_del_timer(timer->timer);
}

static void timer_remove(SpiceTimer *timer)
{
    qemu_del_timer(timer->timer);
    qemu_free_timer(timer->timer);
    QTAILQ_REMOVE(&timers, timer, next);
    qemu_free(timer);
}

struct SpiceWatch {
    int fd;
    int event_mask;
    SpiceWatchFunc func;
    void *opaque;
    QTAILQ_ENTRY(SpiceWatch) next;
};
static QTAILQ_HEAD(, SpiceWatch) watches = QTAILQ_HEAD_INITIALIZER(watches);

static void watch_read(void *opaque)
{
    SpiceWatch *watch = opaque;
    watch->func(watch->fd, SPICE_WATCH_EVENT_READ, watch->opaque);
}

static void watch_write(void *opaque)
{
    SpiceWatch *watch = opaque;
    watch->func(watch->fd, SPICE_WATCH_EVENT_WRITE, watch->opaque);
}

static void watch_update_mask(SpiceWatch *watch, int event_mask)
{
    IOHandler *on_read = NULL;
    IOHandler *on_write = NULL;

    watch->event_mask = event_mask;
    if (watch->event_mask & SPICE_WATCH_EVENT_READ) {
        on_read = watch_read;
    }
    if (watch->event_mask & SPICE_WATCH_EVENT_WRITE) {
        on_write = watch_write;
    }
    qemu_set_fd_handler(watch->fd, on_read, on_write, watch);
}

static SpiceWatch *watch_add(int fd, int event_mask, SpiceWatchFunc func, void *opaque)
{
    SpiceWatch *watch;

    watch = qemu_mallocz(sizeof(*watch));
    watch->fd     = fd;
    watch->func   = func;
    watch->opaque = opaque;
    QTAILQ_INSERT_TAIL(&watches, watch, next);

    watch_update_mask(watch, event_mask);
    return watch;
}

static void watch_remove(SpiceWatch *watch)
{
    watch_update_mask(watch, 0);
    QTAILQ_REMOVE(&watches, watch, next);
    qemu_free(watch);
}

#if SPICE_INTERFACE_CORE_MINOR >= 3

typedef struct ChannelList ChannelList;
struct ChannelList {
    SpiceChannelEventInfo *info;
    QTAILQ_ENTRY(ChannelList) link;
};
static QTAILQ_HEAD(, ChannelList) channel_list = QTAILQ_HEAD_INITIALIZER(channel_list);

static void channel_list_add(SpiceChannelEventInfo *info)
{
    ChannelList *item;

    item = qemu_mallocz(sizeof(*item));
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
        qemu_free(item);
        return;
    }
}

static void add_addr_info(QDict *dict, struct sockaddr *addr, int len)
{
    char host[NI_MAXHOST], port[NI_MAXSERV];
    const char *family;

    getnameinfo(addr, len, host, sizeof(host), port, sizeof(port),
                NI_NUMERICHOST | NI_NUMERICSERV);
    family = inet_strfamily(addr->sa_family);

    qdict_put(dict, "host", qstring_from_str(host));
    qdict_put(dict, "port", qstring_from_str(port));
    qdict_put(dict, "family", qstring_from_str(family));
}

static void add_channel_info(QDict *dict, SpiceChannelEventInfo *info)
{
    int tls = info->flags & SPICE_CHANNEL_EVENT_FLAG_TLS;

    qdict_put(dict, "connection-id", qint_from_int(info->connection_id));
    qdict_put(dict, "channel-type", qint_from_int(info->type));
    qdict_put(dict, "channel-id", qint_from_int(info->id));
    qdict_put(dict, "tls", qbool_from_int(tls));
}

static QList *channel_list_get(void)
{
    ChannelList *item;
    QList *list;
    QDict *dict;

    list = qlist_new();
    QTAILQ_FOREACH(item, &channel_list, link) {
        dict = qdict_new();
        add_addr_info(dict, &item->info->paddr, item->info->plen);
        add_channel_info(dict, item->info);
        qlist_append(list, dict);
    }
    return list;
}

static void channel_event(int event, SpiceChannelEventInfo *info)
{
    static const int qevent[] = {
        [ SPICE_CHANNEL_EVENT_CONNECTED    ] = QEVENT_SPICE_CONNECTED,
        [ SPICE_CHANNEL_EVENT_INITIALIZED  ] = QEVENT_SPICE_INITIALIZED,
        [ SPICE_CHANNEL_EVENT_DISCONNECTED ] = QEVENT_SPICE_DISCONNECTED,
    };
    QDict *server, *client;
    QObject *data;

    client = qdict_new();
    add_addr_info(client, &info->paddr, info->plen);

    server = qdict_new();
    add_addr_info(server, &info->laddr, info->llen);

    if (event == SPICE_CHANNEL_EVENT_INITIALIZED) {
        qdict_put(server, "auth", qstring_from_str(auth));
        add_channel_info(client, info);
        channel_list_add(info);
    }
    if (event == SPICE_CHANNEL_EVENT_DISCONNECTED) {
        channel_list_del(info);
    }

    data = qobject_from_jsonf("{ 'client': %p, 'server': %p }",
                              QOBJECT(client), QOBJECT(server));
    monitor_protocol_event(qevent[event], data);
    qobject_decref(data);
}

#else /* SPICE_INTERFACE_CORE_MINOR >= 3 */

static QList *channel_list_get(void)
{
    return NULL;
}

#endif /* SPICE_INTERFACE_CORE_MINOR >= 3 */

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

#if SPICE_INTERFACE_CORE_MINOR >= 3
    .channel_event      = channel_event,
#endif
};

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
    fprintf(stderr, "spice: invalid %s: %s\n", optname, string);
    exit(1);
}

#if SPICE_SERVER_VERSION >= 0x000600 /* 0.6.0 */

static const char *stream_video_names[] = {
    [ SPICE_STREAM_VIDEO_OFF ]    = "off",
    [ SPICE_STREAM_VIDEO_ALL ]    = "all",
    [ SPICE_STREAM_VIDEO_FILTER ] = "filter",
};
#define parse_stream_video(_name) \
    name2enum(_name, stream_video_names, ARRAY_SIZE(stream_video_names))

#endif /* >= 0.6.0 */

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

static void info_spice_iter(QObject *obj, void *opaque)
{
    QDict *client;
    Monitor *mon = opaque;

    client = qobject_to_qdict(obj);
    monitor_printf(mon, "Channel:\n");
    monitor_printf(mon, "     address: %s:%s%s\n",
                   qdict_get_str(client, "host"),
                   qdict_get_str(client, "port"),
                   qdict_get_bool(client, "tls") ? " [tls]" : "");
    monitor_printf(mon, "     session: %" PRId64 "\n",
                   qdict_get_int(client, "connection-id"));
    monitor_printf(mon, "     channel: %d:%d\n",
                   (int)qdict_get_int(client, "channel-type"),
                   (int)qdict_get_int(client, "channel-id"));
}

void do_info_spice_print(Monitor *mon, const QObject *data)
{
    QDict *server;
    QList *channels;
    const char *host;
    int port;

    server = qobject_to_qdict(data);
    if (qdict_get_bool(server, "enabled") == 0) {
        monitor_printf(mon, "Server: disabled\n");
        return;
    }

    monitor_printf(mon, "Server:\n");
    host = qdict_get_str(server, "host");
    port = qdict_get_try_int(server, "port", -1);
    if (port != -1) {
        monitor_printf(mon, "     address: %s:%d\n", host, port);
    }
    port = qdict_get_try_int(server, "tls-port", -1);
    if (port != -1) {
        monitor_printf(mon, "     address: %s:%d [tls]\n", host, port);
    }
    monitor_printf(mon, "        auth: %s\n", qdict_get_str(server, "auth"));

    channels = qdict_get_qlist(server, "channels");
    if (qlist_empty(channels)) {
        monitor_printf(mon, "Channels: none\n");
    } else {
        qlist_iter(channels, info_spice_iter, mon);
    }
}

void do_info_spice(Monitor *mon, QObject **ret_data)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    QDict *server;
    QList *clist;
    const char *addr;
    int port, tls_port;

    if (!spice_server) {
        *ret_data = qobject_from_jsonf("{ 'enabled': false }");
        return;
    }

    addr = qemu_opt_get(opts, "addr");
    port = qemu_opt_get_number(opts, "port", 0);
    tls_port = qemu_opt_get_number(opts, "tls-port", 0);
    clist = channel_list_get();

    server = qdict_new();
    qdict_put(server, "enabled", qbool_from_int(true));
    qdict_put(server, "auth", qstring_from_str(auth));
    qdict_put(server, "host", qstring_from_str(addr ? addr : "0.0.0.0"));
    if (port) {
        qdict_put(server, "port", qint_from_int(port));
    }
    if (tls_port) {
        qdict_put(server, "tls-port", qint_from_int(tls_port));
    }
    if (clist) {
        qdict_put(server, "channels", clist);
    }

    *ret_data = QOBJECT(server);
}

static void migration_state_notifier(Notifier *notifier)
{
    int state = get_migration_state();

    if (state == MIG_STATE_COMPLETED) {
#if SPICE_SERVER_VERSION >= 0x000701 /* 0.7.1 */
        spice_server_migrate_switch(spice_server);
#endif
    }
}

int qemu_spice_migrate_info(const char *hostname, int port, int tls_port,
                            const char *subject)
{
    return spice_server_migrate_info(spice_server, hostname,
                                     port, tls_port, subject);
}

static int add_channel(const char *name, const char *value, void *opaque)
{
    int security = 0;
    int rc;

    if (strcmp(name, "tls-channel") == 0) {
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
        fprintf(stderr, "spice: failed to set channel security for %s\n", value);
        exit(1);
    }
    return 0;
}

void qemu_spice_init(void)
{
    QemuOpts *opts = QTAILQ_FIRST(&qemu_spice_opts.head);
    const char *password, *str, *x509_dir, *addr,
        *x509_key_password = NULL,
        *x509_dh_file = NULL,
        *tls_ciphers = NULL;
    char *x509_key_file = NULL,
        *x509_cert_file = NULL,
        *x509_cacert_file = NULL;
    int port, tls_port, len, addr_flags;
    spice_image_compression_t compression;
    spice_wan_compression_t wan_compr;

    if (!opts) {
        return;
    }
    port = qemu_opt_get_number(opts, "port", 0);
    tls_port = qemu_opt_get_number(opts, "tls-port", 0);
    if (!port && !tls_port) {
        return;
    }
    password = qemu_opt_get(opts, "password");

    if (tls_port) {
        x509_dir = qemu_opt_get(opts, "x509-dir");
        if (NULL == x509_dir) {
            x509_dir = ".";
        }
        len = strlen(x509_dir) + 32;

        str = qemu_opt_get(opts, "x509-key-file");
        if (str) {
            x509_key_file = qemu_strdup(str);
        } else {
            x509_key_file = qemu_malloc(len);
            snprintf(x509_key_file, len, "%s/%s", x509_dir, X509_SERVER_KEY_FILE);
        }

        str = qemu_opt_get(opts, "x509-cert-file");
        if (str) {
            x509_cert_file = qemu_strdup(str);
        } else {
            x509_cert_file = qemu_malloc(len);
            snprintf(x509_cert_file, len, "%s/%s", x509_dir, X509_SERVER_CERT_FILE);
        }

        str = qemu_opt_get(opts, "x509-cacert-file");
        if (str) {
            x509_cacert_file = qemu_strdup(str);
        } else {
            x509_cacert_file = qemu_malloc(len);
            snprintf(x509_cacert_file, len, "%s/%s", x509_dir, X509_CA_CERT_FILE);
        }

        x509_key_password = qemu_opt_get(opts, "x509-key-password");
        x509_dh_file = qemu_opt_get(opts, "x509-dh-file");
        tls_ciphers = qemu_opt_get(opts, "tls-ciphers");
    }

    addr = qemu_opt_get(opts, "addr");
    addr_flags = 0;
    if (qemu_opt_get_bool(opts, "ipv4", 0)) {
        addr_flags |= SPICE_ADDR_FLAG_IPV4_ONLY;
    } else if (qemu_opt_get_bool(opts, "ipv6", 0)) {
        addr_flags |= SPICE_ADDR_FLAG_IPV6_ONLY;
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
        spice_server_set_ticket(spice_server, password, 0, 0, 0);
    }
    if (qemu_opt_get_bool(opts, "disable-ticketing", 0)) {
        auth = "none";
        spice_server_set_noauth(spice_server);
    }

#if SPICE_SERVER_VERSION >= 0x000801
    if (qemu_opt_get_bool(opts, "disable-copy-paste", 0)) {
        spice_server_set_agent_copypaste(spice_server, false);
    }
#endif

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

#if SPICE_SERVER_VERSION >= 0x000600 /* 0.6.0 */

    str = qemu_opt_get(opts, "streaming-video");
    if (str) {
        int streaming_video = parse_stream_video(str);
        spice_server_set_streaming_video(spice_server, streaming_video);
    }

    spice_server_set_agent_mouse
        (spice_server, qemu_opt_get_bool(opts, "agent-mouse", 1));
    spice_server_set_playback_compression
        (spice_server, qemu_opt_get_bool(opts, "playback-compression", 1));

#endif /* >= 0.6.0 */

    qemu_opt_foreach(opts, add_channel, NULL, 0);

    spice_server_init(spice_server, &core_interface);
    using_spice = 1;

    migration_state.notify = migration_state_notifier;
    add_migration_state_change_notifier(&migration_state);

    qemu_spice_input_init();
    qemu_spice_audio_init();

    qemu_free(x509_key_file);
    qemu_free(x509_cert_file);
    qemu_free(x509_cacert_file);
}

int qemu_spice_add_interface(SpiceBaseInstance *sin)
{
    if (!spice_server) {
        if (QTAILQ_FIRST(&qemu_spice_opts.head) != NULL) {
            fprintf(stderr, "Oops: spice configured but not active\n");
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
        spice_server_init(spice_server, &core_interface);
    }
    return spice_server_add_interface(spice_server, sin);
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

int qemu_spice_set_passwd(const char *passwd,
                          bool fail_if_conn, bool disconnect_if_conn)
{
    free(auth_passwd);
    auth_passwd = strdup(passwd);
    return qemu_spice_set_ticket(fail_if_conn, disconnect_if_conn);
}

int qemu_spice_set_pw_expire(time_t expires)
{
    auth_expires = expires;
    return qemu_spice_set_ticket(false, false);
}

static void spice_register_config(void)
{
    qemu_add_opts(&qemu_spice_opts);
}
machine_init(spice_register_config);

static void spice_initialize(void)
{
    qemu_spice_init();
}
device_init(spice_initialize);
