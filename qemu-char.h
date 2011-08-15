#ifndef QEMU_CHAR_H
#define QEMU_CHAR_H

#include "qemu-common.h"
#include "qemu-queue.h"
#include "qemu-option.h"
#include "qemu-config.h"
#include "qobject.h"
#include "qstring.h"

/* character device */

#define CHR_EVENT_BREAK   0 /* serial break char */
#define CHR_EVENT_FOCUS   1 /* focus to this terminal (modal input needed) */
#define CHR_EVENT_OPENED  2 /* new connection established */
#define CHR_EVENT_MUX_IN  3 /* mux-focus was set to this terminal */
#define CHR_EVENT_MUX_OUT 4 /* mux-focus will move on */
#define CHR_EVENT_CLOSED  5 /* connection closed */


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

#define CHR_IOCTL_SERIAL_SET_TIOCM   13
#define CHR_IOCTL_SERIAL_GET_TIOCM   14

#define CHR_TIOCM_CTS	0x020
#define CHR_TIOCM_CAR	0x040
#define CHR_TIOCM_DSR	0x100
#define CHR_TIOCM_RI	0x080
#define CHR_TIOCM_DTR	0x002
#define CHR_TIOCM_RTS	0x004

typedef void IOEventHandler(void *opaque, int event);

struct CharDriverState {
    void (*init)(struct CharDriverState *s);
    int (*chr_write)(struct CharDriverState *s, const uint8_t *buf, int len);
    void (*chr_update_read_handler)(struct CharDriverState *s);
    int (*chr_ioctl)(struct CharDriverState *s, int cmd, void *arg);
    int (*get_msgfd)(struct CharDriverState *s);
    int (*chr_add_client)(struct CharDriverState *chr, int fd);
    IOEventHandler *chr_event;
    IOCanReadHandler *chr_can_read;
    IOReadHandler *chr_read;
    void *handler_opaque;
    void (*chr_send_event)(struct CharDriverState *chr, int event);
    void (*chr_close)(struct CharDriverState *chr);
    void (*chr_accept_input)(struct CharDriverState *chr);
    void (*chr_set_echo)(struct CharDriverState *chr, bool echo);
    void (*chr_guest_open)(struct CharDriverState *chr);
    void (*chr_guest_close)(struct CharDriverState *chr);
    void *opaque;
    QEMUBH *bh;
    char *label;
    char *filename;
    int opened;
    int avail_connections;
    QTAILQ_ENTRY(CharDriverState) next;
};

QemuOpts *qemu_chr_parse_compat(const char *label, const char *filename);
CharDriverState *qemu_chr_new_from_opts(QemuOpts *opts,
                                    void (*init)(struct CharDriverState *s));
CharDriverState *qemu_chr_new(const char *label, const char *filename, void (*init)(struct CharDriverState *s));
void qemu_chr_fe_set_echo(struct CharDriverState *chr, bool echo);
void qemu_chr_fe_open(struct CharDriverState *chr);
void qemu_chr_fe_close(struct CharDriverState *chr);
void qemu_chr_delete(CharDriverState *chr);
void qemu_chr_fe_printf(CharDriverState *s, const char *fmt, ...)
    GCC_FMT_ATTR(2, 3);
int qemu_chr_fe_write(CharDriverState *s, const uint8_t *buf, int len);
void qemu_chr_send_event(CharDriverState *s, int event);
void qemu_chr_add_handlers(CharDriverState *s,
                           IOCanReadHandler *fd_can_read,
                           IOReadHandler *fd_read,
                           IOEventHandler *fd_event,
                           void *opaque);
int qemu_chr_fe_ioctl(CharDriverState *s, int cmd, void *arg);
void qemu_chr_generic_open(CharDriverState *s);
int qemu_chr_be_can_write(CharDriverState *s);
void qemu_chr_be_write(CharDriverState *s, uint8_t *buf, int len);
int qemu_chr_get_msgfd(CharDriverState *s);
void qemu_chr_accept_input(CharDriverState *s);
int qemu_chr_add_client(CharDriverState *s, int fd);
void qemu_chr_info_print(Monitor *mon, const QObject *ret_data);
void qemu_chr_info(Monitor *mon, QObject **ret_data);
CharDriverState *qemu_chr_find(const char *name);

/* add an eventfd to the qemu devices that are polled */
CharDriverState *qemu_chr_open_eventfd(int eventfd);

extern int term_escape_char;

/* memory chardev */
void qemu_chr_init_mem(CharDriverState *chr);
void qemu_chr_close_mem(CharDriverState *chr);
QString *qemu_chr_mem_to_qs(CharDriverState *chr);
size_t qemu_chr_mem_osize(const CharDriverState *chr);

/* async I/O support */

int qemu_set_fd_handler2(int fd,
                         IOCanReadHandler *fd_read_poll,
                         IOHandler *fd_read,
                         IOHandler *fd_write,
                         void *opaque);
int qemu_set_fd_handler(int fd,
                        IOHandler *fd_read,
                        IOHandler *fd_write,
                        void *opaque);
#endif
