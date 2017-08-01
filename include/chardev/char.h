#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qemu-common.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
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
    CHR_EVENT_CLOSED /* connection closed */
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
    GSource *gsource;
    DECLARE_BITMAP(features, QEMU_CHAR_FEATURE_LAST);
};

/**
 * @qemu_chr_new_from_opts:
 *
 * Create a new character backend from a QemuOpts list.
 *
 * @opts see qemu-config.c for a list of valid options
 *
 * Returns: on success: a new character backend
 *          otherwise:  NULL; @errp specifies the error
 *                            or left untouched in case of help option
 */
Chardev *qemu_chr_new_from_opts(QemuOpts *opts,
                                Error **errp);

/**
 * @qemu_chr_parse_common:
 *
 * Parse the common options available to all character backends.
 *
 * @opts the options that still need parsing
 * @backend a new backend
 */
void qemu_chr_parse_common(QemuOpts *opts, ChardevCommon *backend);

/**
 * @qemu_chr_parse_opts:
 *
 * Parse the options to the ChardevBackend struct.
 *
 * Returns: a new backend or NULL on error
 */
ChardevBackend *qemu_chr_parse_opts(QemuOpts *opts,
                                    Error **errp);

/**
 * @qemu_chr_new:
 *
 * Create a new character backend from a URI.
 *
 * @label the name of the backend
 * @filename the URI
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new(const char *label, const char *filename);

/**
 * @qemu_chr_change:
 *
 * Change an existing character backend
 *
 * @opts the new backend options
 */
void qemu_chr_change(QemuOpts *opts, Error **errp);

/**
 * @qemu_chr_cleanup:
 *
 * Delete all chardevs (when leaving qemu)
 */
void qemu_chr_cleanup(void);

/**
 * @qemu_chr_new_noreplay:
 *
 * Create a new character backend from a URI.
 * Character device communications are not written
 * into the replay log.
 *
 * @label the name of the backend
 * @filename the URI
 *
 * Returns: a new character backend
 */
Chardev *qemu_chr_new_noreplay(const char *label, const char *filename);

/**
 * @qemu_chr_be_can_write:
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
 * @qemu_chr_be_write:
 *
 * Write data from the back end to the front end.  Before issuing this call,
 * the caller should call @qemu_chr_be_can_write to determine how much data
 * the front end can currently accept.
 *
 * @buf a buffer to receive data from the front end
 * @len the number of bytes to receive from the front end
 */
void qemu_chr_be_write(Chardev *s, uint8_t *buf, int len);

/**
 * @qemu_chr_be_write_impl:
 *
 * Implementation of back end writing. Used by replay module.
 *
 * @buf a buffer to receive data from the front end
 * @len the number of bytes to receive from the front end
 */
void qemu_chr_be_write_impl(Chardev *s, uint8_t *buf, int len);

/**
 * @qemu_chr_be_event:
 *
 * Send an event from the back end to the front end.
 *
 * @event the event to send
 */
void qemu_chr_be_event(Chardev *s, int event);

int qemu_chr_add_client(Chardev *s, int fd);
Chardev *qemu_chr_find(const char *name);

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature);
void qemu_chr_set_feature(Chardev *chr,
                          ChardevFeature feature);
QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename);
int qemu_chr_write(Chardev *s, const uint8_t *buf, int len, bool write_all);
#define qemu_chr_write_all(s, buf, len) qemu_chr_write(s, buf, len, true)
int qemu_chr_wait_connected(Chardev *chr, Error **errp);

#define TYPE_CHARDEV "chardev"
#define CHARDEV(obj) OBJECT_CHECK(Chardev, (obj), TYPE_CHARDEV)
#define CHARDEV_CLASS(klass) \
    OBJECT_CLASS_CHECK(ChardevClass, (klass), TYPE_CHARDEV)
#define CHARDEV_GET_CLASS(obj) \
    OBJECT_GET_CLASS(ChardevClass, (obj), TYPE_CHARDEV)

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

typedef struct ChardevClass {
    ObjectClass parent_class;

    bool internal; /* TODO: eventually use TYPE_USER_CREATABLE */
    void (*parse)(QemuOpts *opts, ChardevBackend *backend, Error **errp);

    void (*open)(Chardev *chr, ChardevBackend *backend,
                 bool *be_opened, Error **errp);

    int (*chr_write)(Chardev *s, const uint8_t *buf, int len);
    int (*chr_sync_read)(Chardev *s, const uint8_t *buf, int len);
    GSource *(*chr_add_watch)(Chardev *s, GIOCondition cond);
    void (*chr_update_read_handler)(Chardev *s, GMainContext *context);
    int (*chr_ioctl)(Chardev *s, int cmd, void *arg);
    int (*get_msgfds)(Chardev *s, int* fds, int num);
    int (*set_msgfds)(Chardev *s, int *fds, int num);
    int (*chr_add_client)(Chardev *chr, int fd);
    int (*chr_wait_connected)(Chardev *chr, Error **errp);
    void (*chr_disconnect)(Chardev *chr);
    void (*chr_accept_input)(Chardev *chr);
    void (*chr_set_echo)(Chardev *chr, bool echo);
    void (*chr_set_fe_open)(Chardev *chr, int fe_open);
} ChardevClass;

Chardev *qemu_chardev_new(const char *id, const char *typename,
                          ChardevBackend *backend, Error **errp);

extern int term_escape_char;

/* console.c */
void qemu_chr_parse_vc(QemuOpts *opts, ChardevBackend *backend, Error **errp);

#endif
