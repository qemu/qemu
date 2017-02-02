#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qemu-common.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "qemu/bitmap.h"
#include "qom/object.h"

/* character device */

typedef enum {
    CHR_EVENT_BREAK, /* serial break char */
    CHR_EVENT_OPENED, /* new connection established */
    CHR_EVENT_MUX_IN, /* mux-focus was set to this terminal */
    CHR_EVENT_MUX_OUT, /* mux-focus will move on */
    CHR_EVENT_CLOSED /* connection closed */
} QEMUChrEvent;

#define CHR_READ_BUF_LEN 4096

#define CHR_IOCTL_SERIAL_SET_PARAMS   1
typedef struct {
    int speed;
    int parity;
    int data_bits;
    int stop_bits;
} QEMUSerialSetParams;

#define CHR_IOCTL_SERIAL_SET_BREAK    2

#define CHR_IOCTL_PP_READ_DATA        3
#define CHR_IOCTL_PP_WRITE_DATA       4
#define CHR_IOCTL_PP_READ_CONTROL     5
#define CHR_IOCTL_PP_WRITE_CONTROL    6
#define CHR_IOCTL_PP_READ_STATUS      7
#define CHR_IOCTL_PP_EPP_READ_ADDR    8
#define CHR_IOCTL_PP_EPP_READ         9
#define CHR_IOCTL_PP_EPP_WRITE_ADDR  10
#define CHR_IOCTL_PP_EPP_WRITE       11
#define CHR_IOCTL_PP_DATA_DIR        12

struct ParallelIOArg {
    void *buffer;
    int count;
};

#define CHR_IOCTL_SERIAL_SET_TIOCM   13
#define CHR_IOCTL_SERIAL_GET_TIOCM   14

#define CHR_TIOCM_CTS	0x020
#define CHR_TIOCM_CAR	0x040
#define CHR_TIOCM_DSR	0x100
#define CHR_TIOCM_RI	0x080
#define CHR_TIOCM_DTR	0x002
#define CHR_TIOCM_RTS	0x004

typedef void IOEventHandler(void *opaque, int event);

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

/* This is the backend as seen by frontend, the actual backend is
 * Chardev */
typedef struct CharBackend {
    Chardev *chr;
    IOEventHandler *chr_event;
    IOCanReadHandler *chr_can_read;
    IOReadHandler *chr_read;
    void *opaque;
    int tag;
    int fe_open;
} CharBackend;

struct Chardev {
    Object parent_obj;

    QemuMutex chr_write_lock;
    CharBackend *be;
    char *label;
    char *filename;
    int logfd;
    int be_open;
    guint fd_in_tag;
    DECLARE_BITMAP(features, QEMU_CHAR_FEATURE_LAST);
    QTAILQ_ENTRY(Chardev) next;
};

/**
 * @qemu_chr_new_from_opts:
 *
 * Create a new character backend from a QemuOpts list.
 *
 * @opts see qemu-config.c for a list of valid options
 *
 * Returns: a new character backend
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
 * @qemu_chr_fe_disconnect:
 *
 * Close a fd accpeted by character backend.
 * Without associated Chardev, do nothing.
 */
void qemu_chr_fe_disconnect(CharBackend *be);

/**
 * @qemu_chr_cleanup:
 *
 * Delete all chardevs (when leaving qemu)
 */
void qemu_chr_cleanup(void);

/**
 * @qemu_chr_fe_wait_connected:
 *
 * Wait for characted backend to be connected, return < 0 on error or
 * if no assicated Chardev.
 */
int qemu_chr_fe_wait_connected(CharBackend *be, Error **errp);

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
 * @qemu_chr_delete:
 *
 * Destroy a character backend and remove it from the list of
 * identified character backends.
 */
void qemu_chr_delete(Chardev *chr);

/**
 * @qemu_chr_fe_set_echo:
 *
 * Ask the backend to override its normal echo setting.  This only really
 * applies to the stdio backend and is used by the QMP server such that you
 * can see what you type if you try to type QMP commands.
 * Without associated Chardev, do nothing.
 *
 * @echo true to enable echo, false to disable echo
 */
void qemu_chr_fe_set_echo(CharBackend *be, bool echo);

/**
 * @qemu_chr_fe_set_open:
 *
 * Set character frontend open status.  This is an indication that the
 * front end is ready (or not) to begin doing I/O.
 * Without associated Chardev, do nothing.
 */
void qemu_chr_fe_set_open(CharBackend *be, int fe_open);

/**
 * @qemu_chr_fe_printf:
 *
 * Write to a character backend using a printf style interface.  This
 * function is thread-safe. It does nothing without associated
 * Chardev.
 *
 * @fmt see #printf
 */
void qemu_chr_fe_printf(CharBackend *be, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);

/**
 * @qemu_chr_fe_add_watch:
 *
 * If the backend is connected, create and add a #GSource that fires
 * when the given condition (typically G_IO_OUT|G_IO_HUP or G_IO_HUP)
 * is active; return the #GSource's tag.  If it is disconnected,
 * or without associated Chardev, return 0.
 *
 * @cond the condition to poll for
 * @func the function to call when the condition happens
 * @user_data the opaque pointer to pass to @func
 *
 * Returns: the source tag
 */
guint qemu_chr_fe_add_watch(CharBackend *be, GIOCondition cond,
                            GIOFunc func, void *user_data);

/**
 * @qemu_chr_fe_write:
 *
 * Write data to a character backend from the front end.  This function
 * will send data from the front end to the back end.  This function
 * is thread-safe.
 *
 * @buf the data
 * @len the number of bytes to send
 *
 * Returns: the number of bytes consumed (0 if no assicated Chardev)
 */
int qemu_chr_fe_write(CharBackend *be, const uint8_t *buf, int len);

/**
 * @qemu_chr_fe_write_all:
 *
 * Write data to a character backend from the front end.  This function will
 * send data from the front end to the back end.  Unlike @qemu_chr_fe_write,
 * this function will block if the back end cannot consume all of the data
 * attempted to be written.  This function is thread-safe.
 *
 * @buf the data
 * @len the number of bytes to send
 *
 * Returns: the number of bytes consumed (0 if no assicated Chardev)
 */
int qemu_chr_fe_write_all(CharBackend *be, const uint8_t *buf, int len);

/**
 * @qemu_chr_fe_read_all:
 *
 * Read data to a buffer from the back end.
 *
 * @buf the data buffer
 * @len the number of bytes to read
 *
 * Returns: the number of bytes read (0 if no assicated Chardev)
 */
int qemu_chr_fe_read_all(CharBackend *be, uint8_t *buf, int len);

/**
 * @qemu_chr_fe_ioctl:
 *
 * Issue a device specific ioctl to a backend.  This function is thread-safe.
 *
 * @cmd see CHR_IOCTL_*
 * @arg the data associated with @cmd
 *
 * Returns: if @cmd is not supported by the backend or there is no
 *          associated Chardev, -ENOTSUP, otherwise the return
 *          value depends on the semantics of @cmd
 */
int qemu_chr_fe_ioctl(CharBackend *be, int cmd, void *arg);

/**
 * @qemu_chr_fe_get_msgfd:
 *
 * For backends capable of fd passing, return the latest file descriptor passed
 * by a client.
 *
 * Returns: -1 if fd passing isn't supported or there is no pending file
 *          descriptor.  If a file descriptor is returned, subsequent calls to
 *          this function will return -1 until a client sends a new file
 *          descriptor.
 */
int qemu_chr_fe_get_msgfd(CharBackend *be);

/**
 * @qemu_chr_fe_get_msgfds:
 *
 * For backends capable of fd passing, return the number of file received
 * descriptors and fills the fds array up to num elements
 *
 * Returns: -1 if fd passing isn't supported or there are no pending file
 *          descriptors.  If file descriptors are returned, subsequent calls to
 *          this function will return -1 until a client sends a new set of file
 *          descriptors.
 */
int qemu_chr_fe_get_msgfds(CharBackend *be, int *fds, int num);

/**
 * @qemu_chr_fe_set_msgfds:
 *
 * For backends capable of fd passing, set an array of fds to be passed with
 * the next send operation.
 * A subsequent call to this function before calling a write function will
 * result in overwriting the fd array with the new value without being send.
 * Upon writing the message the fd array is freed.
 *
 * Returns: -1 if fd passing isn't supported or no associated Chardev.
 */
int qemu_chr_fe_set_msgfds(CharBackend *be, int *fds, int num);

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

/**
 * @qemu_chr_fe_init:
 *
 * Initializes a front end for the given CharBackend and
 * Chardev. Call qemu_chr_fe_deinit() to remove the association and
 * release the driver.
 *
 * Returns: false on error.
 */
bool qemu_chr_fe_init(CharBackend *b, Chardev *s, Error **errp);

/**
 * @qemu_chr_fe_get_driver:
 *
 * Returns the driver associated with a CharBackend or NULL if no
 * associated Chardev.
 */
Chardev *qemu_chr_fe_get_driver(CharBackend *be);

/**
 * @qemu_chr_fe_deinit:
 *
 * Dissociate the CharBackend from the Chardev.
 *
 * Safe to call without associated Chardev.
 */
void qemu_chr_fe_deinit(CharBackend *b);

/**
 * @qemu_chr_fe_set_handlers:
 * @b: a CharBackend
 * @fd_can_read: callback to get the amount of data the frontend may
 *               receive
 * @fd_read: callback to receive data from char
 * @fd_event: event callback
 * @opaque: an opaque pointer for the callbacks
 * @context: a main loop context or NULL for the default
 * @set_open: whether to call qemu_chr_fe_set_open() implicitely when
 * any of the handler is non-NULL
 *
 * Set the front end char handlers. The front end takes the focus if
 * any of the handler is non-NULL.
 *
 * Without associated Chardev, nothing is changed.
 */
void qemu_chr_fe_set_handlers(CharBackend *b,
                              IOCanReadHandler *fd_can_read,
                              IOReadHandler *fd_read,
                              IOEventHandler *fd_event,
                              void *opaque,
                              GMainContext *context,
                              bool set_open);

/**
 * @qemu_chr_fe_take_focus:
 *
 * Take the focus (if the front end is muxed).
 *
 * Without associated Chardev, nothing is changed.
 */
void qemu_chr_fe_take_focus(CharBackend *b);

void qemu_chr_be_generic_open(Chardev *s);
void qemu_chr_fe_accept_input(CharBackend *be);
int qemu_chr_add_client(Chardev *s, int fd);
Chardev *qemu_chr_find(const char *name);

bool qemu_chr_has_feature(Chardev *chr,
                          ChardevFeature feature);
void qemu_chr_set_feature(Chardev *chr,
                          ChardevFeature feature);
QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename);
int qemu_chr_write_all(Chardev *s, const uint8_t *buf, int len);
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
