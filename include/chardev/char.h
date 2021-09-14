#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qapi/qapi-types-char.h"
#include "qemu/bitmap.h"
#include "qemu/thread.h"
#include "qom/object.h"

#define IAC_EOR 239
#define IAC_SE 240
#define IAC_NOP 241
#define IAC_BREAK 243
#define IAC_IP 244
#define IAC_SB 250
#define IAC 255

/* character device */
typedef struct CharBackend CharBackend;

typedef enum {
    CHR_EVENT_BREAK, /* serial break char */
    CHR_EVENT_OPENED, /* new connection established */
    CHR_EVENT_MUX_IN, /* mux-focus was set to this terminal */
    CHR_EVENT_MUX_OUT, /* mux-focus will move on */
    CHR_EVENT_CLOSED /* connection closed.  NOTE: currently this event
                      * is only bound to the read port of the chardev.
                      * Normally the read port and write port of a
                      * chardev should be the same, but it can be
                      * different, e.g., for fd chardevs, when the two
                      * fds are different.  So when we received the
                      * CLOSED event it's still possible that the out
                      * port is still open.  TODO: we should only send
                      * the CLOSED event when both ports are closed.
                      */
} QEMUChrEvent;

#define CHR_READ_BUF_LEN 4096

typedef enum {
    /* Whether the chardev peer is able to close and
     * reopen the data channel, thus requiring support
     * for qemu_chr_wait_connected() to wait for a
     * valid connection */
    QEMU_CHAR_FEATURE_RECONNECTABLE,
    /* Whether it is possible to send/recv file descriptors
     * over the data channel */
    QEMU_CHAR_FEATURE_FD_PASS,
    /* Whether replay or record mode is enabled */
    QEMU_CHAR_FEATURE_REPLAY,
    /* Whether the gcontext can be changed after calling
     * qemu_chr_be_update_read_handlers() */
    QEMU_CHAR_FEATURE_GCONTEXT,

    QEMU_CHAR_FEATURE_LAST,
} ChardevFeature;

#define qemu_chr_replay(chr) qemu_chr_has_feature(chr, QEMU_CHAR_FEATURE_REPLAY)

struct Chardev {
    Object parent_obj;

    QemuMutex chr_write_lock;
    CharBackend *be;
    char *label;
    char *filename;
    int logfd;
    int be_open;
    /* used to coordinate the chardev-change special-case: */
    bool handover_yank_instance;
    GSource *gsource;
    GMainContext *gcontext;
    DECLARE_BITMAP(features, QEMU_CHAR_FEATURE_LAST);
};

/**
 * qemu_chr_new_from_opts:
 * @opts: see qemu-config.c for a list of valid options
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a QemuOpts list.
 *
 * Returns: on success: a new character backend
 *          otherwise:  NULL; @errp specifies the error
 *                            or left untouched in case of help option
 */
Chardev *qemu_chr_new_from_opts(QemuOpts *opts,
                                GMainContext *context,
                                Error **errp);

/**
 * qemu_chr_parse_common:
 * @opts: the options that still need parsing
 * @backend: a new backend
 *
 * Parse the common options available to all character backends.
 */
void qemu_chr_parse_common(QemuOpts *opts, ChardevCommon *backend);

/**
 * qemu_chr_parse_opts:
 *
 * Parse the options to the ChardevBackend struct.
 *
 * Returns: a new backend or NULL on error
 */
ChardevBackend *qemu_chr_parse_opts(QemuOpts *opts,
                                    Error **errp);

/**
 * qemu_chr_new:
 * @label: the name of the backend
 * @filename: the URI
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Do not implicitly initialize a monitor if the chardev is muxed.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new(const char *label, const char *filename,
                      GMainContext *context);

/**
 * qemu_chr_new_mux_mon:
 * @label: the name of the backend
 * @filename: the URI
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Implicitly initialize a monitor if the chardev is muxed.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new_mux_mon(const char *label, const char *filename,
                              GMainContext *context);

/**
* qemu_chr_change:
* @opts: the new backend options
 *
 * Change an existing character backend
 */
void qemu_chr_change(QemuOpts *opts, Error **errp);

/**
 * qemu_chr_cleanup:
 *
 * Delete all chardevs (when leaving qemu)
 */
void qemu_chr_cleanup(void);

/**
 * qemu_chr_new_noreplay:
 * @label: the name of the backend
 * @filename: the URI
 * @permit_mux_mon: if chardev is muxed, initialize a monitor
 * @context: the #GMainContext to be used at initialization time
 *
 * Create a new character backend from a URI.
 * Character device communications are not written
 * into the replay log.
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new_noreplay(const char *label, const char *filename,
                               bool permit_mux_mon, GMainContext *context);

/**
 * qemu_chr_be_can_write:
 *
 * Determine how much data the front end can currently accept.  This function
 * returns the number of bytes the front end can accept.  If it returns 0, the
 * front end cannot receive data at the moment.  The function must be polled
 * to determine when data can be received.
 *
 * Returns: the number of bytes the front end can receive via @qemu_chr_be_write
 */
int qemu_chr_be_can_write(Chardev *s);

/**
 * qemu_chr_be_write:
 * @buf: a buffer to receive data from the front end
 * @len: the number of bytes to receive from the front end
 *
 * Write data from the back end to the front end.  Before issuing this call,
 * the caller should call @qemu_chr_be_can_write to determine how much data
 * the front end can currently accept.
 */
void qemu_chr_be_write(Chardev *s, uint8_t *buf, int len);

/**
 * qemu_chr_be_write_impl:
 * @buf: a buffer to receive data from the front end
 * @len: the number of bytes to receive from the front end
 *
 * Implementation of back end writing. Used by replay module.
 */
void qemu_chr_be_write_impl(Chardev *s, uint8_t *buf, int len);

/**
 * qemu_chr_be_update_read_handlers:
 * @context: the gcontext that will be used to attach the watch sources
 *
 * Invoked when frontend read handlers are setup
 */
void qemu_chr_be_update_read_handlers(Chardev *s,
                                      GMainContext *context);

/**
 * qemu_chr_be_event:
 * @event: the event to send
 *
 * Send an event from the back end to the front end.
 */
void qemu_chr_be_event(Chardev *s, QEMUChrEvent event);

int qemu_chr_add_client(Chardev *s, int fd);
Chardev *qemu_chr_find(const char *name);

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature);
void qemu_chr_set_feature(Chardev *chr,
                          ChardevFeature feature);
QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename,
                                bool permit_mux_mon);
int qemu_chr_write(Chardev *s, const uint8_t *buf, int len, bool write_all);
#define qemu_chr_write_all(s, buf, len) qemu_chr_write(s, buf, len, true)
int qemu_chr_wait_connected(Chardev *chr, Error **errp);

#define TYPE_CHARDEV "chardev"
OBJECT_DECLARE_TYPE(Chardev, ChardevClass, CHARDEV)

#define TYPE_CHARDEV_NULL "chardev-null"
#define TYPE_CHARDEV_MUX "chardev-mux"
#define TYPE_CHARDEV_RINGBUF "chardev-ringbuf"
#define TYPE_CHARDEV_PTY "chardev-pty"
#define TYPE_CHARDEV_CONSOLE "chardev-console"
#define TYPE_CHARDEV_STDIO "chardev-stdio"
#define TYPE_CHARDEV_PIPE "chardev-pipe"
#define TYPE_CHARDEV_MEMORY "chardev-memory"
#define TYPE_CHARDEV_PARALLEL "chardev-parallel"
#define TYPE_CHARDEV_FILE "chardev-file"
#define TYPE_CHARDEV_SERIAL "chardev-serial"
#define TYPE_CHARDEV_SOCKET "chardev-socket"
#define TYPE_CHARDEV_UDP "chardev-udp"

#define CHARDEV_IS_RINGBUF(chr) \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_RINGBUF)
#define CHARDEV_IS_PTY(chr) \
    object_dynamic_cast(OBJECT(chr), TYPE_CHARDEV_PTY)

struct ChardevClass {
    ObjectClass parent_class;

    bool internal; /* TODO: eventually use TYPE_USER_CREATABLE */
    bool supports_yank;

    /* parse command line options and populate QAPI @backend */
    void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp);

    /* called after construction, open/starts the backend */
    void (*open)(Chardev *chr, ChardevBackend *backend,
                 bool *be_opened, Error **errp);

    /* write buf to the backend */
    int (*chr_write)(Chardev *s, const uint8_t *buf, int len);

    /*
     * Read from the backend (blocking). A typical front-end will instead rely
     * on chr_can_read/chr_read being called when polling/looping.
     */
    int (*chr_sync_read)(Chardev *s, const uint8_t *buf, int len);

    /* create a watch on the backend */
    GSource *(*chr_add_watch)(Chardev *s, GIOCondition cond);

    /* update the backend internal sources */
    void (*chr_update_read_handler)(Chardev *s);

    /* send an ioctl to the backend */
    int (*chr_ioctl)(Chardev *s, int cmd, void *arg);

    /* get ancillary-received fds during last read */
    int (*get_msgfds)(Chardev *s, int* fds, int num);

    /* set ancillary fds to be sent with next write */
    int (*set_msgfds)(Chardev *s, int *fds, int num);

    /* accept the given fd */
    int (*chr_add_client)(Chardev *chr, int fd);

    /* wait for a connection */
    int (*chr_wait_connected)(Chardev *chr, Error **errp);

    /* disconnect a connection */
    void (*chr_disconnect)(Chardev *chr);

    /* called by frontend when it can read */
    void (*chr_accept_input)(Chardev *chr);

    /* set terminal echo */
    void (*chr_set_echo)(Chardev *chr, bool echo);

    /* notify the backend of frontend open state */
    void (*chr_set_fe_open)(Chardev *chr, int fe_open);

    /* handle various events */
    void (*chr_be_event)(Chardev *s, QEMUChrEvent event);
};

Chardev *qemu_chardev_new(const char *id, const char *typename,
                          ChardevBackend *backend, GMainContext *context,
                          Error **errp);

extern int term_escape_char;

GSource *qemu_chr_timeout_add_ms(Chardev *chr, guint ms,
                                 GSourceFunc func, void *private);

void suspend_mux_open(void);
void resume_mux_open(void);

/* console.c */
void qemu_chr_parse_vc(QemuOpts *opts, ChardevBackend *backend, Error **errp);

#endif
