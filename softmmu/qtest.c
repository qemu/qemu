/*
 * Test Server
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/qtest.h"
#include "sysemu/runstate.h"
#include "chardev/char-fe.h"
#include "exec/ioport.h"
#include "exec/memory.h"
#include "exec/tswap.h"
#include "hw/qdev-core.h"
#include "hw/irq.h"
#include "qemu/accel.h"
#include "sysemu/cpu-timers.h"
#include "qemu/config-file.h"
#include "qemu/option.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"

#define MAX_IRQ 256

#define TYPE_QTEST "qtest"

OBJECT_DECLARE_SIMPLE_TYPE(QTest, QTEST)

struct QTest {
    Object parent;

    bool has_machine_link;
    char *chr_name;
    Chardev *chr;
    CharBackend qtest_chr;
    char *log;
};

bool qtest_allowed;

static DeviceState *irq_intercept_dev;
static FILE *qtest_log_fp;
static QTest *qtest;
static GString *inbuf;
static int irq_levels[MAX_IRQ];
static GTimer *timer;
static bool qtest_opened;
static void (*qtest_server_send)(void*, const char*);
static void *qtest_server_send_opaque;

#define FMT_timeval "%.06f"

/**
 * DOC: QTest Protocol
 *
 * Line based protocol, request/response based.  Server can send async messages
 * so clients should always handle many async messages before the response
 * comes in.
 *
 * Valid requests
 * ^^^^^^^^^^^^^^
 *
 * Clock management:
 * """""""""""""""""
 *
 * The qtest client is completely in charge of the QEMU_CLOCK_VIRTUAL.  qtest commands
 * let you adjust the value of the clock (monotonically).  All the commands
 * return the current value of the clock in nanoseconds.
 *
 * .. code-block:: none
 *
 *  > clock_step
 *  < OK VALUE
 *
 * Advance the clock to the next deadline.  Useful when waiting for
 * asynchronous events.
 *
 * .. code-block:: none
 *
 *  > clock_step NS
 *  < OK VALUE
 *
 * Advance the clock by NS nanoseconds.
 *
 * .. code-block:: none
 *
 *  > clock_set NS
 *  < OK VALUE
 *
 * Advance the clock to NS nanoseconds (do nothing if it's already past).
 *
 * PIO and memory access:
 * """"""""""""""""""""""
 *
 * .. code-block:: none
 *
 *  > outb ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > outw ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > outl ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > inb ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > inw ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > inl ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > writeb ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > writew ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > writel ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > writeq ADDR VALUE
 *  < OK
 *
 * .. code-block:: none
 *
 *  > readb ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > readw ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > readl ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > readq ADDR
 *  < OK VALUE
 *
 * .. code-block:: none
 *
 *  > read ADDR SIZE
 *  < OK DATA
 *
 * .. code-block:: none
 *
 *  > write ADDR SIZE DATA
 *  < OK
 *
 * .. code-block:: none
 *
 *  > b64read ADDR SIZE
 *  < OK B64_DATA
 *
 * .. code-block:: none
 *
 *  > b64write ADDR SIZE B64_DATA
 *  < OK
 *
 * .. code-block:: none
 *
 *  > memset ADDR SIZE VALUE
 *  < OK
 *
 * ADDR, SIZE, VALUE are all integers parsed with strtoul() with a base of 0.
 * For 'memset' a zero size is permitted and does nothing.
 *
 * DATA is an arbitrarily long hex number prefixed with '0x'.  If it's smaller
 * than the expected size, the value will be zero filled at the end of the data
 * sequence.
 *
 * B64_DATA is an arbitrarily long base64 encoded string.
 * If the sizes do not match, the data will be truncated.
 *
 * IRQ management:
 * """""""""""""""
 *
 * .. code-block:: none
 *
 *  > irq_intercept_in QOM-PATH
 *  < OK
 *
 * .. code-block:: none
 *
 *  > irq_intercept_out QOM-PATH
 *  < OK
 *
 * Attach to the gpio-in (resp. gpio-out) pins exported by the device at
 * QOM-PATH.  When the pin is triggered, one of the following async messages
 * will be printed to the qtest stream::
 *
 *  IRQ raise NUM
 *  IRQ lower NUM
 *
 * where NUM is an IRQ number.  For the PC, interrupts can be intercepted
 * simply with "irq_intercept_in ioapic" (note that IRQ0 comes out with
 * NUM=0 even though it is remapped to GSI 2).
 *
 * Setting interrupt level:
 * """"""""""""""""""""""""
 *
 * .. code-block:: none
 *
 *  > set_irq_in QOM-PATH NAME NUM LEVEL
 *  < OK
 *
 * where NAME is the name of the irq/gpio list, NUM is an IRQ number and
 * LEVEL is an signed integer IRQ level.
 *
 * Forcibly set the given interrupt pin to the given level.
 *
 */

static int hex2nib(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    } else if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    } else if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    } else {
        return -1;
    }
}

void qtest_send_prefix(CharBackend *chr)
{
    if (!qtest_log_fp || !qtest_opened) {
        return;
    }

    fprintf(qtest_log_fp, "[S +" FMT_timeval "] ", g_timer_elapsed(timer, NULL));
}

static void G_GNUC_PRINTF(1, 2) qtest_log_send(const char *fmt, ...)
{
    va_list ap;

    if (!qtest_log_fp || !qtest_opened) {
        return;
    }

    qtest_send_prefix(NULL);

    va_start(ap, fmt);
    vfprintf(qtest_log_fp, fmt, ap);
    va_end(ap);
}

static void qtest_server_char_be_send(void *opaque, const char *str)
{
    size_t len = strlen(str);
    CharBackend* chr = (CharBackend *)opaque;
    qemu_chr_fe_write_all(chr, (uint8_t *)str, len);
    if (qtest_log_fp && qtest_opened) {
        fprintf(qtest_log_fp, "%s", str);
    }
}

static void qtest_send(CharBackend *chr, const char *str)
{
    qtest_server_send(qtest_server_send_opaque, str);
}

void qtest_sendf(CharBackend *chr, const char *fmt, ...)
{
    va_list ap;
    gchar *buffer;

    va_start(ap, fmt);
    buffer = g_strdup_vprintf(fmt, ap);
    qtest_send(chr, buffer);
    g_free(buffer);
    va_end(ap);
}

static void qtest_irq_handler(void *opaque, int n, int level)
{
    qemu_irq old_irq = *(qemu_irq *)opaque;
    qemu_set_irq(old_irq, level);

    if (irq_levels[n] != level) {
        CharBackend *chr = &qtest->qtest_chr;
        irq_levels[n] = level;
        qtest_send_prefix(chr);
        qtest_sendf(chr, "IRQ %s %d\n",
                    level ? "raise" : "lower", n);
    }
}

static int64_t qtest_clock_counter;

int64_t qtest_get_virtual_clock(void)
{
    return qatomic_read_i64(&qtest_clock_counter);
}

static void qtest_set_virtual_clock(int64_t count)
{
    qatomic_set_i64(&qtest_clock_counter, count);
}

static void qtest_clock_warp(int64_t dest)
{
    int64_t clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    AioContext *aio_context;
    assert(qtest_enabled());
    aio_context = qemu_get_aio_context();
    while (clock < dest) {
        int64_t deadline = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                                      QEMU_TIMER_ATTR_ALL);
        int64_t warp = qemu_soonest_timeout(dest - clock, deadline);

        qtest_set_virtual_clock(qtest_get_virtual_clock() + warp);

        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
        timerlist_run_timers(aio_context->tlg.tl[QEMU_CLOCK_VIRTUAL]);
        clock = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    qemu_clock_notify(QEMU_CLOCK_VIRTUAL);
}

static bool (*process_command_cb)(CharBackend *chr, gchar **words);

void qtest_set_command_cb(bool (*pc_cb)(CharBackend *chr, gchar **words))
{
    assert(!process_command_cb);  /* Switch to a list if we need more than one */

    process_command_cb = pc_cb;
}

static void qtest_process_command(CharBackend *chr, gchar **words)
{
    const gchar *command;

    g_assert(words);

    command = words[0];

    if (qtest_log_fp) {
        int i;

        fprintf(qtest_log_fp, "[R +" FMT_timeval "]", g_timer_elapsed(timer, NULL));
        for (i = 0; words[i]; i++) {
            fprintf(qtest_log_fp, " %s", words[i]);
        }
        fprintf(qtest_log_fp, "\n");
    }

    g_assert(command);
    if (strcmp(words[0], "irq_intercept_out") == 0
        || strcmp(words[0], "irq_intercept_in") == 0) {
        DeviceState *dev;
        NamedGPIOList *ngl;

        g_assert(words[1]);
        dev = DEVICE(object_resolve_path(words[1], NULL));
        if (!dev) {
            qtest_send_prefix(chr);
            qtest_send(chr, "FAIL Unknown device\n");
            return;
        }

        if (irq_intercept_dev) {
            qtest_send_prefix(chr);
            if (irq_intercept_dev != dev) {
                qtest_send(chr, "FAIL IRQ intercept already enabled\n");
            } else {
                qtest_send(chr, "OK\n");
            }
            return;
        }

        QLIST_FOREACH(ngl, &dev->gpios, node) {
            /* We don't support intercept of named GPIOs yet */
            if (ngl->name) {
                continue;
            }
            if (words[0][14] == 'o') {
                int i;
                for (i = 0; i < ngl->num_out; ++i) {
                    qemu_irq *disconnected = g_new0(qemu_irq, 1);
                    qemu_irq icpt = qemu_allocate_irq(qtest_irq_handler,
                                                      disconnected, i);

                    *disconnected = qdev_intercept_gpio_out(dev, icpt,
                                                            ngl->name, i);
                }
            } else {
                qemu_irq_intercept_in(ngl->in, qtest_irq_handler,
                                      ngl->num_in);
            }
        }
        irq_intercept_dev = dev;
        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "set_irq_in") == 0) {
        DeviceState *dev;
        qemu_irq irq;
        char *name;
        int ret;
        int num;
        int level;

        g_assert(words[1] && words[2] && words[3] && words[4]);

        dev = DEVICE(object_resolve_path(words[1], NULL));
        if (!dev) {
            qtest_send_prefix(chr);
            qtest_send(chr, "FAIL Unknown device\n");
            return;
        }

        if (strcmp(words[2], "unnamed-gpio-in") == 0) {
            name = NULL;
        } else {
            name = words[2];
        }

        ret = qemu_strtoi(words[3], NULL, 0, &num);
        g_assert(!ret);
        ret = qemu_strtoi(words[4], NULL, 0, &level);
        g_assert(!ret);

        irq = qdev_get_gpio_in_named(dev, name, num);

        qemu_set_irq(irq, level);
        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "outb") == 0 ||
               strcmp(words[0], "outw") == 0 ||
               strcmp(words[0], "outl") == 0) {
        unsigned long addr;
        unsigned long value;
        int ret;

        g_assert(words[1] && words[2]);
        ret = qemu_strtoul(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtoul(words[2], NULL, 0, &value);
        g_assert(ret == 0);
        g_assert(addr <= 0xffff);

        if (words[0][3] == 'b') {
            cpu_outb(addr, value);
        } else if (words[0][3] == 'w') {
            cpu_outw(addr, value);
        } else if (words[0][3] == 'l') {
            cpu_outl(addr, value);
        }
        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "inb") == 0 ||
        strcmp(words[0], "inw") == 0 ||
        strcmp(words[0], "inl") == 0) {
        unsigned long addr;
        uint32_t value = -1U;
        int ret;

        g_assert(words[1]);
        ret = qemu_strtoul(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        g_assert(addr <= 0xffff);

        if (words[0][2] == 'b') {
            value = cpu_inb(addr);
        } else if (words[0][2] == 'w') {
            value = cpu_inw(addr);
        } else if (words[0][2] == 'l') {
            value = cpu_inl(addr);
        }
        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK 0x%04x\n", value);
    } else if (strcmp(words[0], "writeb") == 0 ||
               strcmp(words[0], "writew") == 0 ||
               strcmp(words[0], "writel") == 0 ||
               strcmp(words[0], "writeq") == 0) {
        uint64_t addr;
        uint64_t value;
        int ret;

        g_assert(words[1] && words[2]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &value);
        g_assert(ret == 0);

        if (words[0][5] == 'b') {
            uint8_t data = value;
            address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                                &data, 1);
        } else if (words[0][5] == 'w') {
            uint16_t data = value;
            tswap16s(&data);
            address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                                &data, 2);
        } else if (words[0][5] == 'l') {
            uint32_t data = value;
            tswap32s(&data);
            address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                                &data, 4);
        } else if (words[0][5] == 'q') {
            uint64_t data = value;
            tswap64s(&data);
            address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                                &data, 8);
        }
        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "readb") == 0 ||
               strcmp(words[0], "readw") == 0 ||
               strcmp(words[0], "readl") == 0 ||
               strcmp(words[0], "readq") == 0) {
        uint64_t addr;
        uint64_t value = UINT64_C(-1);
        int ret;

        g_assert(words[1]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);

        if (words[0][4] == 'b') {
            uint8_t data;
            address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                               &data, 1);
            value = data;
        } else if (words[0][4] == 'w') {
            uint16_t data;
            address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                               &data, 2);
            value = tswap16(data);
        } else if (words[0][4] == 'l') {
            uint32_t data;
            address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                               &data, 4);
            value = tswap32(data);
        } else if (words[0][4] == 'q') {
            address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                               &value, 8);
            tswap64s(&value);
        }
        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK 0x%016" PRIx64 "\n", value);
    } else if (strcmp(words[0], "read") == 0) {
        uint64_t addr, len, i;
        uint8_t *data;
        char *enc;
        int ret;

        g_assert(words[1] && words[2]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &len);
        g_assert(ret == 0);
        /* We'd send garbage to libqtest if len is 0 */
        g_assert(len);

        data = g_malloc(len);
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                           len);

        enc = g_malloc(2 * len + 1);
        for (i = 0; i < len; i++) {
            sprintf(&enc[i * 2], "%02x", data[i]);
        }

        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK 0x%s\n", enc);

        g_free(data);
        g_free(enc);
    } else if (strcmp(words[0], "b64read") == 0) {
        uint64_t addr, len;
        uint8_t *data;
        gchar *b64_data;
        int ret;

        g_assert(words[1] && words[2]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &len);
        g_assert(ret == 0);

        data = g_malloc(len);
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                           len);
        b64_data = g_base64_encode(data, len);
        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK %s\n", b64_data);

        g_free(data);
        g_free(b64_data);
    } else if (strcmp(words[0], "write") == 0) {
        uint64_t addr, len, i;
        uint8_t *data;
        size_t data_len;
        int ret;

        g_assert(words[1] && words[2] && words[3]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &len);
        g_assert(ret == 0);

        data_len = strlen(words[3]);
        if (data_len < 3) {
            qtest_send(chr, "ERR invalid argument size\n");
            return;
        }

        data = g_malloc(len);
        for (i = 0; i < len; i++) {
            if ((i * 2 + 4) <= data_len) {
                data[i] = hex2nib(words[3][i * 2 + 2]) << 4;
                data[i] |= hex2nib(words[3][i * 2 + 3]);
            } else {
                data[i] = 0;
            }
        }
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                            len);
        g_free(data);

        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "memset") == 0) {
        uint64_t addr, len;
        uint8_t *data;
        unsigned long pattern;
        int ret;

        g_assert(words[1] && words[2] && words[3]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &len);
        g_assert(ret == 0);
        ret = qemu_strtoul(words[3], NULL, 0, &pattern);
        g_assert(ret == 0);

        if (len) {
            data = g_malloc(len);
            memset(data, pattern, len);
            address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                                data, len);
            g_free(data);
        }

        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    }  else if (strcmp(words[0], "b64write") == 0) {
        uint64_t addr, len;
        uint8_t *data;
        size_t data_len;
        gsize out_len;
        int ret;

        g_assert(words[1] && words[2] && words[3]);
        ret = qemu_strtou64(words[1], NULL, 0, &addr);
        g_assert(ret == 0);
        ret = qemu_strtou64(words[2], NULL, 0, &len);
        g_assert(ret == 0);

        data_len = strlen(words[3]);
        if (data_len < 3) {
            qtest_send(chr, "ERR invalid argument size\n");
            return;
        }

        data = g_base64_decode_inplace(words[3], &out_len);
        if (out_len != len) {
            qtest_log_send("b64write: data length mismatch (told %"PRIu64", "
                           "found %zu)\n",
                           len, out_len);
            out_len = MIN(out_len, len);
        }

        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                            len);

        qtest_send_prefix(chr);
        qtest_send(chr, "OK\n");
    } else if (strcmp(words[0], "endianness") == 0) {
        qtest_send_prefix(chr);
        if (target_words_bigendian()) {
            qtest_sendf(chr, "OK big\n");
        } else {
            qtest_sendf(chr, "OK little\n");
        }
    } else if (qtest_enabled() && strcmp(words[0], "clock_step") == 0) {
        int64_t ns;

        if (words[1]) {
            int ret = qemu_strtoi64(words[1], NULL, 0, &ns);
            g_assert(ret == 0);
        } else {
            ns = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL,
                                            QEMU_TIMER_ATTR_ALL);
        }
        qtest_clock_warp(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + ns);
        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK %"PRIi64"\n",
                    (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    } else if (strcmp(words[0], "module_load") == 0) {
        Error *local_err = NULL;
        int rv;
        g_assert(words[1] && words[2]);

        qtest_send_prefix(chr);
        rv = module_load(words[1], words[2], &local_err);
        if (rv > 0) {
            qtest_sendf(chr, "OK\n");
        } else {
            if (rv < 0) {
                error_report_err(local_err);
            }
            qtest_sendf(chr, "FAIL\n");
        }
    } else if (qtest_enabled() && strcmp(words[0], "clock_set") == 0) {
        int64_t ns;
        int ret;

        g_assert(words[1]);
        ret = qemu_strtoi64(words[1], NULL, 0, &ns);
        g_assert(ret == 0);
        qtest_clock_warp(ns);
        qtest_send_prefix(chr);
        qtest_sendf(chr, "OK %"PRIi64"\n",
                    (int64_t)qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    } else if (process_command_cb && process_command_cb(chr, words)) {
        /* Command got consumed by the callback handler */
    } else {
        qtest_send_prefix(chr);
        qtest_sendf(chr, "FAIL Unknown command '%s'\n", words[0]);
    }
}

static void qtest_process_inbuf(CharBackend *chr, GString *inbuf)
{
    char *end;

    while ((end = strchr(inbuf->str, '\n')) != NULL) {
        size_t offset;
        GString *cmd;
        gchar **words;

        offset = end - inbuf->str;

        cmd = g_string_new_len(inbuf->str, offset);
        g_string_erase(inbuf, 0, offset + 1);

        words = g_strsplit(cmd->str, " ", 0);
        qtest_process_command(chr, words);
        g_strfreev(words);

        g_string_free(cmd, TRUE);
    }
}

static void qtest_read(void *opaque, const uint8_t *buf, int size)
{
    CharBackend *chr = opaque;

    g_string_append_len(inbuf, (const gchar *)buf, size);
    qtest_process_inbuf(chr, inbuf);
}

static int qtest_can_read(void *opaque)
{
    return 1024;
}

static void qtest_event(void *opaque, QEMUChrEvent event)
{
    int i;

    switch (event) {
    case CHR_EVENT_OPENED:
        /*
         * We used to call qemu_system_reset() here, hoping we could
         * use the same process for multiple tests that way.  Never
         * used.  Injects an extra reset even when it's not used, and
         * that can mess up tests, e.g. -boot once.
         */
        for (i = 0; i < ARRAY_SIZE(irq_levels); i++) {
            irq_levels[i] = 0;
        }

        g_clear_pointer(&timer, g_timer_destroy);
        timer = g_timer_new();
        qtest_opened = true;
        if (qtest_log_fp) {
            fprintf(qtest_log_fp, "[I " FMT_timeval "] OPENED\n", g_timer_elapsed(timer, NULL));
        }
        break;
    case CHR_EVENT_CLOSED:
        qtest_opened = false;
        if (qtest_log_fp) {
            fprintf(qtest_log_fp, "[I +" FMT_timeval "] CLOSED\n", g_timer_elapsed(timer, NULL));
        }
        g_clear_pointer(&timer, g_timer_destroy);
        break;
    default:
        break;
    }
}

void qtest_server_init(const char *qtest_chrdev, const char *qtest_log, Error **errp)
{
    ERRP_GUARD();
    Chardev *chr;
    Object *qtest;

    chr = qemu_chr_new("qtest", qtest_chrdev, NULL);
    if (chr == NULL) {
        error_setg(errp, "Failed to initialize device for qtest: \"%s\"",
                   qtest_chrdev);
        return;
    }

    qtest = object_new(TYPE_QTEST);
    object_property_set_str(qtest, "chardev", chr->label, &error_abort);
    if (qtest_log) {
        object_property_set_str(qtest, "log", qtest_log, &error_abort);
    }
    object_property_add_child(qdev_get_machine(), "qtest", qtest);
    user_creatable_complete(USER_CREATABLE(qtest), errp);
    if (*errp) {
        object_unparent(qtest);
    }
    object_unref(OBJECT(chr));
    object_unref(qtest);
}

static bool qtest_server_start(QTest *q, Error **errp)
{
    Chardev *chr = q->chr;
    const char *qtest_log = q->log;

    if (qtest_log) {
        if (strcmp(qtest_log, "none") != 0) {
            qtest_log_fp = fopen(qtest_log, "w+");
        }
    } else {
        qtest_log_fp = stderr;
    }

    if (!qemu_chr_fe_init(&q->qtest_chr, chr, errp)) {
        return false;
    }
    qemu_chr_fe_set_handlers(&q->qtest_chr, qtest_can_read, qtest_read,
                             qtest_event, NULL, &q->qtest_chr, NULL, true);
    qemu_chr_fe_set_echo(&q->qtest_chr, true);

    inbuf = g_string_new("");

    if (!qtest_server_send) {
        qtest_server_set_send_handler(qtest_server_char_be_send, &q->qtest_chr);
    }
    qtest = q;
    return true;
}

void qtest_server_set_send_handler(void (*send)(void*, const char*),
                                   void *opaque)
{
    qtest_server_send = send;
    qtest_server_send_opaque = opaque;
}

bool qtest_driver(void)
{
    return qtest && qtest->qtest_chr.chr != NULL;
}

void qtest_server_inproc_recv(void *dummy, const char *buf)
{
    static GString *gstr;
    if (!gstr) {
        gstr = g_string_new(NULL);
    }
    g_string_append(gstr, buf);
    if (gstr->str[gstr->len - 1] == '\n') {
        qtest_process_inbuf(NULL, gstr);
        g_string_truncate(gstr, 0);
    }
}

static void qtest_complete(UserCreatable *uc, Error **errp)
{
    QTest *q = QTEST(uc);
    if (qtest) {
        error_setg(errp, "Only one instance of qtest can be created");
        return;
    }
    if (!q->chr_name) {
        error_setg(errp, "No backend specified");
        return;
    }

    if (OBJECT(uc)->parent != qdev_get_machine()) {
        q->has_machine_link = true;
        object_property_add_const_link(qdev_get_machine(), "qtest", OBJECT(uc));
    } else {
        /* -qtest was used.  */
    }

    qtest_server_start(q, errp);
}

static void qtest_unparent(Object *obj)
{
    QTest *q = QTEST(obj);

    if (qtest == q) {
        qemu_chr_fe_disconnect(&q->qtest_chr);
        assert(!qtest_opened);
        qemu_chr_fe_deinit(&q->qtest_chr, false);
        if (qtest_log_fp) {
            fclose(qtest_log_fp);
            qtest_log_fp = NULL;
        }
        qtest = NULL;
    }

    if (q->has_machine_link) {
        object_property_del(qdev_get_machine(), "qtest");
        q->has_machine_link = false;
    }
}

static void qtest_set_log(Object *obj, const char *value, Error **errp)
{
    QTest *q = QTEST(obj);

    if (qtest == q) {
        error_setg(errp, "Property 'log' can not be set now");
    } else {
        g_free(q->log);
        q->log = g_strdup(value);
    }
}

static char *qtest_get_log(Object *obj, Error **errp)
{
    QTest *q = QTEST(obj);

    return g_strdup(q->log);
}

static void qtest_set_chardev(Object *obj, const char *value, Error **errp)
{
    QTest *q = QTEST(obj);
    Chardev *chr;

    if (qtest == q) {
        error_setg(errp, "Property 'chardev' can not be set now");
        return;
    }

    chr = qemu_chr_find(value);
    if (!chr) {
        error_setg(errp, "Cannot find character device '%s'", value);
        return;
    }

    g_free(q->chr_name);
    q->chr_name = g_strdup(value);

    if (q->chr) {
        object_unref(q->chr);
    }
    q->chr = chr;
    object_ref(chr);
}

static char *qtest_get_chardev(Object *obj, Error **errp)
{
    QTest *q = QTEST(obj);

    return g_strdup(q->chr_name);
}

static void qtest_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    oc->unparent = qtest_unparent;
    ucc->complete = qtest_complete;

    object_class_property_add_str(oc, "chardev",
                                  qtest_get_chardev, qtest_set_chardev);
    object_class_property_add_str(oc, "log",
                                  qtest_get_log, qtest_set_log);
}

static const TypeInfo qtest_info = {
    .name = TYPE_QTEST,
    .parent = TYPE_OBJECT,
    .class_init = qtest_class_init,
    .instance_size = sizeof(QTest),
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&qtest_info);
}

type_init(register_types);
