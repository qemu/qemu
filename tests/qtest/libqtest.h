/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *  Andreas Färber    <afaerber@suse.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef LIBQTEST_H
#define LIBQTEST_H

#include "qobject/qobject.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"
#include "libqmp.h"

typedef struct QTestState QTestState;

/**
 * qtest_initf:
 * @fmt: Format for creating other arguments to pass to QEMU, formatted
 * like sprintf().
 *
 * Convenience wrapper around qtest_init().
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_initf(const char *fmt, ...) G_GNUC_PRINTF(1, 2);

/**
 * qtest_vinitf:
 * @fmt: Format for creating other arguments to pass to QEMU, formatted
 * like vsprintf().
 * @ap: Format arguments.
 *
 * Convenience wrapper around qtest_init().
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_vinitf(const char *fmt, va_list ap) G_GNUC_PRINTF(1, 0);

/**
 * qtest_qemu_binary:
 * @var: environment variable name
 *
 * Look up @var and return its value as the qemu binary path.
 * If @var is NULL, look up  the default var name.
 */
const char *qtest_qemu_binary(const char *var);

/**
 * qtest_init_after_exec:
 * @qts: the previous QEMU state
 *
 * Return a test state representing new QEMU after @qts exec's it.
 */
QTestState *qtest_init_after_exec(QTestState *qts);

/**
 * qtest_qemu_args:
 * @extra_args: Other arguments to pass to QEMU.
 *
 * Return the command line used to start QEMU, sans binary.
 */
gchar *qtest_qemu_args(const char *extra_args);

/**
 * qtest_init:
 * @extra_args: other arguments to pass to QEMU.  CAUTION: these
 * arguments are subject to word splitting and shell evaluation.
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_init(const char *extra_args);

/**
 * qtest_init_ext:
 * @var: Environment variable from where to take the QEMU binary
 * @extra_args: Other arguments to pass to QEMU.  CAUTION: these
 * arguments are subject to word splitting and shell evaluation.
 * @capabilities: list of QMP capabilities (strings) to enable
 * @do_connect: connect to qemu monitor and qtest socket.
 *
 * Like qtest_init(), but use a different environment variable for the
 * QEMU binary, allow specify capabilities and skip connecting
 * to QEMU monitor.
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_init_ext(const char *var, const char *extra_args,
                           QList *capabilities, bool do_connect);

/**
 * qtest_init_without_qmp_handshake:
 * @extra_args: other arguments to pass to QEMU.  CAUTION: these
 * arguments are subject to word splitting and shell evaluation.
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_init_without_qmp_handshake(const char *extra_args);

/**
 * qtest_connect
 * @s: #QTestState instance to connect
 * Connect to qemu monitor and qtest socket, after skipping them in
 * qtest_init_ext.  Does not handshake with the monitor.
 */
void qtest_connect(QTestState *s);

/**
 * qtest_qmp_handshake:
 * @s: #QTestState instance to operate on.
 * @capabilities: list of QMP capabilities (strings) to enable
 * Perform handshake after connecting to qemu monitor.
 */
void qtest_qmp_handshake(QTestState *s, QList *capabilities);

/**
 * qtest_init_with_serial:
 * @extra_args: other arguments to pass to QEMU.  CAUTION: these
 * arguments are subject to word splitting and shell evaluation.
 * @sock_fd: pointer to store the socket file descriptor for
 * connection with serial.
 *
 * Returns: #QTestState instance.
 */
QTestState *qtest_init_with_serial(const char *extra_args, int *sock_fd);

/**
 * qtest_system_reset:
 * @s: #QTestState instance to operate on.
 *
 * Send a "system_reset" command to the QEMU under test, and wait for
 * the reset to complete before returning.
 */
void qtest_system_reset(QTestState *s);

/**
 * qtest_system_reset_nowait:
 * @s: #QTestState instance to operate on.
 *
 * Send a "system_reset" command to the QEMU under test, but do not
 * wait for the reset to complete before returning. The caller is
 * responsible for waiting for either the RESET event or some other
 * event of interest to them before proceeding.
 *
 * This function should only be used if you're specifically testing
 * for some other event; in that case you can't use qtest_system_reset()
 * because it will read and discard any other QMP events that arrive
 * before the RESET event.
 */
void qtest_system_reset_nowait(QTestState *s);

/**
 * qtest_wait_qemu:
 * @s: #QTestState instance to operate on.
 *
 * Wait for the QEMU process to terminate. It is safe to call this function
 * multiple times.
 */
void qtest_wait_qemu(QTestState *s);

/**
 * qtest_kill_qemu:
 * @s: #QTestState instance to operate on.
 *
 * Kill the QEMU process and wait for it to terminate. It is safe to call this
 * function multiple times. Normally qtest_quit() is used instead because it
 * also frees QTestState. Use qtest_kill_qemu() when you just want to kill QEMU
 * and qtest_quit() will be called later.
 */
void qtest_kill_qemu(QTestState *s);

/**
 * qtest_quit:
 * @s: #QTestState instance to operate on.
 *
 * Shut down the QEMU process associated to @s.
 */
void qtest_quit(QTestState *s);

#ifndef _WIN32
/**
 * qtest_qmp_fds:
 * @s: #QTestState instance to operate on.
 * @fds: array of file descriptors
 * @fds_num: number of elements in @fds
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU with fds and returns the response.
 */
QDict *qtest_qmp_fds(QTestState *s, int *fds, size_t fds_num,
                     const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);
#endif /* _WIN32 */

/**
 * qtest_qmp:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_qmp(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * qtest_qmp_send:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_qmp_send(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * qtest_qmp_send_raw:
 * @s: #QTestState instance to operate on.
 * @fmt: text to send, formatted like sprintf()
 *
 * Sends text to the QMP monitor verbatim.  Need not be valid JSON;
 * this is useful for negative tests.
 */
void qtest_qmp_send_raw(QTestState *s, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * qtest_socket_server:
 * @socket_path: the UNIX domain socket path
 *
 * Create and return a listen socket file descriptor, or abort on failure.
 */
int qtest_socket_server(const char *socket_path);

#ifndef _WIN32
/**
 * qtest_vqmp_fds:
 * @s: #QTestState instance to operate on.
 * @fds: array of file descriptors
 * @fds_num: number of elements in @fds
 * @fmt: QMP message to send to QEMU, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU with fds and returns the response.
 */
QDict *qtest_vqmp_fds(QTestState *s, int *fds, size_t fds_num,
                      const char *fmt, va_list ap)
    G_GNUC_PRINTF(4, 0);
#endif /* _WIN32 */

/**
 * qtest_vqmp:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and returns the response.
 */
QDict *qtest_vqmp(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

#ifndef _WIN32
/**
 * qtest_qmp_vsend_fds:
 * @s: #QTestState instance to operate on.
 * @fds: array of file descriptors
 * @fds_num: number of elements in @fds
 * @fmt: QMP message to send to QEMU, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_qmp_vsend_fds(QTestState *s, int *fds, size_t fds_num,
                         const char *fmt, va_list ap)
    G_GNUC_PRINTF(4, 0);
#endif /* _WIN32 */

/**
 * qtest_qmp_vsend:
 * @s: #QTestState instance to operate on.
 * @fmt: QMP message to send to QEMU, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @ap: QMP message arguments
 *
 * Sends a QMP message to QEMU and leaves the response in the stream.
 */
void qtest_qmp_vsend(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

/**
 * qtest_qmp_receive_dict:
 * @s: #QTestState instance to operate on.
 *
 * Reads a QMP message from QEMU and returns the response.
 */
QDict *qtest_qmp_receive_dict(QTestState *s);

/**
 * qtest_qmp_receive:
 * @s: #QTestState instance to operate on.
 *
 * Reads a QMP message from QEMU and returns the response.
 *
 * If a callback is registered with qtest_qmp_set_event_callback,
 * it will be invoked for every event seen, otherwise events
 * will be buffered until a call to one of the qtest_qmp_eventwait
 * family of functions.
 */
QDict *qtest_qmp_receive(QTestState *s);

/*
 * QTestQMPEventCallback:
 * @s: #QTestState instance event was received on
 * @name: name of the event type
 * @event: #QDict for the event details
 * @opaque: opaque data from time of callback registration
 *
 * This callback will be invoked whenever an event is received.
 * If the callback returns true the event will be consumed,
 * otherwise it will be put on the list of pending events.
 * Pending events can be later handled by calling either
 * qtest_qmp_eventwait or qtest_qmp_eventwait_ref.
 *
 * Return: true to consume the event, false to let it be queued
 */
typedef bool (*QTestQMPEventCallback)(QTestState *s, const char *name,
                                      QDict *event, void *opaque);

/**
 * qtest_qmp_set_event_callback:
 * @s: #QTestSTate instance to operate on
 * @cb: callback to invoke for events
 * @opaque: data to pass to @cb
 *
 * Register a callback to be invoked whenever an event arrives
 */
void qtest_qmp_set_event_callback(QTestState *s,
                                  QTestQMPEventCallback cb, void *opaque);

/**
 * qtest_qmp_eventwait:
 * @s: #QTestState instance to operate on.
 * @event: event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 *
 * Any callback registered with qtest_qmp_set_event_callback will
 * be invoked for every event seen.
 */
void qtest_qmp_eventwait(QTestState *s, const char *event);

/**
 * qtest_qmp_eventwait_ref:
 * @s: #QTestState instance to operate on.
 * @event: event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 *
 * Any callback registered with qtest_qmp_set_event_callback will
 * be invoked for every event seen.
 *
 * Returns a copy of the event for further investigation.
 */
QDict *qtest_qmp_eventwait_ref(QTestState *s, const char *event);

/**
 * qtest_qmp_event_ref:
 * @s: #QTestState instance to operate on.
 * @event: event to return.
 *
 * Removes non-matching events from the buffer that was set by
 * qtest_qmp_receive, until an event bearing the given name is found,
 * and returns it.
 * If no event matches, clears the buffer and returns NULL.
 *
 */
QDict *qtest_qmp_event_ref(QTestState *s, const char *event);

/**
 * qtest_hmp:
 * @s: #QTestState instance to operate on.
 * @fmt: HMP command to send to QEMU, formats arguments like sprintf().
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_hmp(QTestState *s, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

/**
 * qtest_vhmp:
 * @s: #QTestState instance to operate on.
 * @fmt: HMP command to send to QEMU, formats arguments like vsprintf().
 * @ap: HMP command arguments
 *
 * Send HMP command to QEMU via QMP's human-monitor-command.
 * QMP events are discarded.
 *
 * Returns: the command's output.  The caller should g_free() it.
 */
char *qtest_vhmp(QTestState *s, const char *fmt, va_list ap)
    G_GNUC_PRINTF(2, 0);

void qtest_module_load(QTestState *s, const char *prefix, const char *libname);

/**
 * qtest_get_irq:
 * @s: #QTestState instance to operate on.
 * @num: Interrupt to observe.
 *
 * Returns: The level of the @num interrupt.
 */
bool qtest_get_irq(QTestState *s, int num);

/**
 * qtest_irq_intercept_in:
 * @s: #QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-in pins of the device
 * whose path is specified by @string.
 */
void qtest_irq_intercept_in(QTestState *s, const char *string);

/**
 * qtest_irq_intercept_out:
 * @s: #QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-out pins of the device
 * whose path is specified by @string.
 */
void qtest_irq_intercept_out(QTestState *s, const char *string);

/**
 * qtest_irq_intercept_out_named:
 * @s: #QTestState instance to operate on.
 * @qom_path: QOM path of a device.
 * @name: Name of the GPIO out pin
 *
 * Associate a qtest irq with the named GPIO-out pin of the device
 * whose path is specified by @string and whose name is @name.
 */
void qtest_irq_intercept_out_named(QTestState *s, const char *qom_path, const char *name);

/**
 * qtest_set_irq_in:
 * @s: QTestState instance to operate on.
 * @string: QOM path of a device
 * @name: IRQ name
 * @irq: IRQ number
 * @level: IRQ level
 *
 * Force given device/irq GPIO-in pin to the given level.
 */
void qtest_set_irq_in(QTestState *s, const char *string, const char *name,
                      int irq, int level);

/**
 * qtest_outb:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port.
 */
void qtest_outb(QTestState *s, uint16_t addr, uint8_t value);

/**
 * qtest_outw:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port.
 */
void qtest_outw(QTestState *s, uint16_t addr, uint16_t value);

/**
 * qtest_outl:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port.
 */
void qtest_outl(QTestState *s, uint16_t addr, uint32_t value);

/**
 * qtest_inb:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns an 8-bit value from an I/O port.
 */
uint8_t qtest_inb(QTestState *s, uint16_t addr);

/**
 * qtest_inw:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns a 16-bit value from an I/O port.
 */
uint16_t qtest_inw(QTestState *s, uint16_t addr);

/**
 * qtest_inl:
 * @s: #QTestState instance to operate on.
 * @addr: I/O port to read from.
 *
 * Returns a 32-bit value from an I/O port.
 */
uint32_t qtest_inl(QTestState *s, uint16_t addr);

/**
 * qtest_writeb:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes an 8-bit value to memory.
 */
void qtest_writeb(QTestState *s, uint64_t addr, uint8_t value);

/**
 * qtest_writew:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 16-bit value to memory.
 */
void qtest_writew(QTestState *s, uint64_t addr, uint16_t value);

/**
 * qtest_writel:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 32-bit value to memory.
 */
void qtest_writel(QTestState *s, uint64_t addr, uint32_t value);

/**
 * qtest_writeq:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 64-bit value to memory.
 */
void qtest_writeq(QTestState *s, uint64_t addr, uint64_t value);

/**
 * qtest_readb:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads an 8-bit value from memory.
 *
 * Returns: Value read.
 */
uint8_t qtest_readb(QTestState *s, uint64_t addr);

/**
 * qtest_readw:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 16-bit value from memory.
 *
 * Returns: Value read.
 */
uint16_t qtest_readw(QTestState *s, uint64_t addr);

/**
 * qtest_readl:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 32-bit value from memory.
 *
 * Returns: Value read.
 */
uint32_t qtest_readl(QTestState *s, uint64_t addr);

/**
 * qtest_readq:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 *
 * Reads a 64-bit value from memory.
 *
 * Returns: Value read.
 */
uint64_t qtest_readq(QTestState *s, uint64_t addr);

/**
 * qtest_memread:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer.
 */
void qtest_memread(QTestState *s, uint64_t addr, void *data, size_t size);

/**
 * qtest_rtas_call:
 * @s: #QTestState instance to operate on.
 * @name: name of the command to call.
 * @nargs: Number of args.
 * @args: Guest address to read args from.
 * @nret: Number of return value.
 * @ret: Guest address to write return values to.
 *
 * Call an RTAS function
 */
uint64_t qtest_rtas_call(QTestState *s, const char *name,
                         uint32_t nargs, uint64_t args,
                         uint32_t nret, uint64_t ret);

/**
 * qtest_csr_call:
 * @s: #QTestState instance to operate on.
 * @name: name of the command to call.
 * @cpu: hart number.
 * @csr: CSR number.
 * @val: Value for reading/writing.
 *
 * Call an RISC-V CSR read/write function
 */
uint64_t qtest_csr_call(QTestState *s, const char *name,
                         uint64_t cpu, int csr,
                         uint64_t *val);

/**
 * qtest_bufread:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer and receive using a base64 encoding.
 */
void qtest_bufread(QTestState *s, uint64_t addr, void *data, size_t size);

/**
 * qtest_memwrite:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory.
 */
void qtest_memwrite(QTestState *s, uint64_t addr, const void *data, size_t size);

/**
 * qtest_bufwrite:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory and transmit using a base64 encoding.
 */
void qtest_bufwrite(QTestState *s, uint64_t addr,
                    const void *data, size_t size);

/**
 * qtest_memset:
 * @s: #QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @patt: Byte pattern to fill the guest memory region with.
 * @size: Number of bytes to write.
 *
 * Write a pattern to guest memory.
 */
void qtest_memset(QTestState *s, uint64_t addr, uint8_t patt, size_t size);

/**
 * qtest_clock_step_next:
 * @s: #QTestState instance to operate on.
 *
 * Advance the QEMU_CLOCK_VIRTUAL to the next deadline.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t qtest_clock_step_next(QTestState *s);

/**
 * qtest_clock_step:
 * @s: QTestState instance to operate on.
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the QEMU_CLOCK_VIRTUAL by @step nanoseconds.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t qtest_clock_step(QTestState *s, int64_t step);

/**
 * qtest_clock_set:
 * @s: QTestState instance to operate on.
 * @val: Nanoseconds value to advance the clock to.
 *
 * Advance the QEMU_CLOCK_VIRTUAL to @val nanoseconds since the VM was launched.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
int64_t qtest_clock_set(QTestState *s, int64_t val);

/**
 * qtest_big_endian:
 * @s: QTestState instance to operate on.
 *
 * Returns: True if the architecture under test has a big endian configuration.
 */
bool qtest_big_endian(QTestState *s);

/**
 * qtest_get_arch:
 *
 * Returns: The architecture for the QEMU executable under test.
 */
const char *qtest_get_arch(void);

/**
 * qtest_has_accel:
 * @accel_name: Accelerator name to check for.
 *
 * Returns: true if the accelerator is built in.
 */
bool qtest_has_accel(const char *accel_name);

/**
 * qtest_add_func:
 * @str: Test case path.
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_func(const char *str, void (*fn)(void));

/**
 * qtest_add_data_func:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
void qtest_add_data_func(const char *str, const void *data,
                         void (*fn)(const void *));

/**
 * qtest_add_data_func_full:
 * @str: Test case path.
 * @data: Test case data
 * @fn: Test case function
 * @data_free_func: GDestroyNotify for data
 *
 * Add a GTester testcase with the given name, data and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 *
 * @data is passed to @data_free_func() on test completion.
 */
void qtest_add_data_func_full(const char *str, void *data,
                              void (*fn)(const void *),
                              GDestroyNotify data_free_func);

/**
 * qtest_add:
 * @testpath: Test case path
 * @Fixture: Fixture type
 * @tdata: Test case data
 * @fsetup: Test case setup function
 * @ftest: Test case function
 * @fteardown: Test case teardown function
 *
 * Add a GTester testcase with the given name, data and functions.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch().
 */
#define qtest_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    do { \
        char *path = g_strdup_printf("/%s/%s", qtest_get_arch(), testpath); \
        g_test_add(path, Fixture, tdata, fsetup, ftest, fteardown); \
        g_free(path); \
    } while (0)

/**
 * qtest_add_abrt_handler:
 * @fn: Handler function
 * @data: Argument that is passed to the handler
 *
 * Add a handler function that is invoked on SIGABRT. This can be used to
 * terminate processes and perform other cleanup. The handler can be removed
 * with qtest_remove_abrt_handler().
 */
void qtest_add_abrt_handler(GHookFunc fn, const void *data);

/**
 * qtest_remove_abrt_handler:
 * @data: Argument previously passed to qtest_add_abrt_handler()
 *
 * Remove an abrt handler that was previously added with
 * qtest_add_abrt_handler().
 */
void qtest_remove_abrt_handler(void *data);

/**
 * qtest_vqmp_assert_success_ref:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @args: variable arguments for @fmt
 *
 * Sends a QMP message to QEMU, asserts that a 'return' key is present in
 * the response, and returns the response.
 */
QDict *qtest_vqmp_assert_success_ref(QTestState *qts,
                                     const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

/**
 * qtest_vqmp_assert_success:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @args: variable arguments for @fmt
 *
 * Sends a QMP message to QEMU and asserts that a 'return' key is present in
 * the response.
 */
void qtest_vqmp_assert_success(QTestState *qts,
                               const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

#ifndef _WIN32
/**
 * qtest_vqmp_fds_assert_success_ref:
 * @qts: QTestState instance to operate on
 * @fds: the file descriptors to send
 * @nfds: number of @fds to send
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @args: variable arguments for @fmt
 *
 * Sends a QMP message with file descriptors to QEMU,
 * asserts that a 'return' key is present in the response,
 * and returns the response.
 */
QDict *qtest_vqmp_fds_assert_success_ref(QTestState *qts, int *fds, size_t nfds,
                                         const char *fmt, va_list args)
    G_GNUC_PRINTF(4, 0);

/**
 * qtest_vqmp_fds_assert_success:
 * @qts: QTestState instance to operate on
 * @fds: the file descriptors to send
 * @nfds: number of @fds to send
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @args: variable arguments for @fmt
 *
 * Sends a QMP message with file descriptors to QEMU and
 * asserts that a 'return' key is present in the response.
 */
void qtest_vqmp_fds_assert_success(QTestState *qts, int *fds, size_t nfds,
                                   const char *fmt, va_list args)
    G_GNUC_PRINTF(4, 0);
#endif /* !_WIN32 */

/**
 * qtest_qmp_assert_failure_ref:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU, asserts that an 'error' key is present in
 * the response, and returns the response.
 */
QDict *qtest_qmp_assert_failure_ref(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * qtest_vqmp_assert_failure_ref:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 * @args: variable arguments for @fmt
 *
 * Sends a QMP message to QEMU, asserts that an 'error' key is present in
 * the response, and returns the response.
 */
QDict *qtest_vqmp_assert_failure_ref(QTestState *qts,
                                     const char *fmt, va_list args)
    G_GNUC_PRINTF(2, 0);

/**
 * qtest_qmp_assert_success_ref:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU, asserts that a 'return' key is present in
 * the response, and returns the response.
 */
QDict *qtest_qmp_assert_success_ref(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

/**
 * qtest_qmp_assert_success:
 * @qts: QTestState instance to operate on
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU and asserts that a 'return' key is present in
 * the response.
 */
void qtest_qmp_assert_success(QTestState *qts, const char *fmt, ...)
    G_GNUC_PRINTF(2, 3);

#ifndef _WIN32
/**
 * qtest_qmp_fds_assert_success_ref:
 * @qts: QTestState instance to operate on
 * @fds: the file descriptors to send
 * @nfds: number of @fds to send
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message with file descriptors to QEMU,
 * asserts that a 'return' key is present in the response,
 * and returns the response.
 */
QDict *qtest_qmp_fds_assert_success_ref(QTestState *qts, int *fds, size_t nfds,
                                        const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);

/**
 * qtest_qmp_fds_assert_success:
 * @qts: QTestState instance to operate on
 * @fds: the file descriptors to send
 * @nfds: number of @fds to send
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Sends a QMP message with file descriptors to QEMU and
 * asserts that a 'return' key is present in the response.
 */
void qtest_qmp_fds_assert_success(QTestState *qts, int *fds, size_t nfds,
                                  const char *fmt, ...)
    G_GNUC_PRINTF(4, 5);
#endif /* !_WIN32 */

/**
 * qtest_cb_for_every_machine:
 * @cb: Pointer to the callback function
 * @skip_old_versioned: true if versioned old machine types should be skipped
 *
 * Call a callback function for every name of all available machines.
 */
void qtest_cb_for_every_machine(void (*cb)(const char *machine),
                                bool skip_old_versioned);

/**
 * qtest_resolve_machine_alias:
 * @var: Environment variable from where to take the QEMU binary
 * @alias: The alias to resolve
 *
 * Returns: the machine type corresponding to the alias if any,
 * otherwise NULL.
 */
char *qtest_resolve_machine_alias(const char *var, const char *alias);

/**
 * qtest_has_machine:
 * @machine: The machine to look for
 *
 * Returns: true if the machine is available in the target binary.
 */
bool qtest_has_machine(const char *machine);

/**
 * qtest_has_machine_with_env:
 * @var: Environment variable from where to take the QEMU binary
 * @machine: The machine to look for
 *
 * Returns: true if the machine is available in the specified binary.
 */
bool qtest_has_machine_with_env(const char *var, const char *machine);

/**
 * qtest_has_cpu_model:
 * @cpu: The cpu to look for
 *
 * Returns: true if the cpu is available in the target binary.
 */
bool qtest_has_cpu_model(const char *cpu);

/**
 * qtest_has_device:
 * @device: The device to look for
 *
 * Returns: true if the device is available in the target binary.
 */
bool qtest_has_device(const char *device);

/**
 * qtest_qmp_device_add_qdict:
 * @qts: QTestState instance to operate on
 * @drv: Name of the device that should be added
 * @arguments: QDict with properties for the device to initialize
 *
 * Generic hot-plugging test via the device_add QMP command with properties
 * supplied in form of QDict. Use NULL for empty properties list.
 */
void qtest_qmp_device_add_qdict(QTestState *qts, const char *drv,
                                const QDict *arguments);

/**
 * qtest_qmp_device_add:
 * @qts: QTestState instance to operate on
 * @driver: Name of the device that should be added
 * @id: Identification string
 * @fmt: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_interpolation() for what's
 * supported after '%'.
 *
 * Generic hot-plugging test via the device_add QMP command.
 */
void qtest_qmp_device_add(QTestState *qts, const char *driver, const char *id,
                          const char *fmt, ...) G_GNUC_PRINTF(4, 5);

/**
 * qtest_qmp_add_client:
 * @qts: QTestState instance to operate on
 * @protocol: the protocol to add to
 * @fd: the client file-descriptor
 *
 * Call QMP ``getfd`` (on Windows ``get-win32-socket``) followed by
 * ``add_client`` with the given @fd.
 */
void qtest_qmp_add_client(QTestState *qts, const char *protocol, int fd);

/**
 * qtest_qmp_device_del_send:
 * @qts: QTestState instance to operate on
 * @id: Identification string
 *
 * Generic hot-unplugging test via the device_del QMP command.
 */
void qtest_qmp_device_del_send(QTestState *qts, const char *id);

/**
 * qtest_qmp_device_del:
 * @qts: QTestState instance to operate on
 * @id: Identification string
 *
 * Generic hot-unplugging test via the device_del QMP command.
 * Waiting for command completion event.
 */
void qtest_qmp_device_del(QTestState *qts, const char *id);

/**
 * qtest_probe_child:
 * @s: QTestState instance to operate on.
 *
 * Returns: true if the child is still alive.
 */
bool qtest_probe_child(QTestState *s);

/**
 * qtest_set_expected_status:
 * @s: QTestState instance to operate on.
 * @status: an expected exit status.
 *
 * Set expected exit status of the child.
 */
void qtest_set_expected_status(QTestState *s, int status);

QTestState *qtest_inproc_init(QTestState **s, bool log, const char* arch,
                    void (*send)(void*, const char*));

void qtest_client_inproc_recv(void *opaque, const char *str);

/**
 * qtest_qom_set_bool:
 * @s: QTestState instance to operate on.
 * @path: Path to the property being set.
 * @property: Property being set.
 * @value: Value to set the property.
 *
 * Set the property with passed in value.
 */
void qtest_qom_set_bool(QTestState *s, const char *path, const char *property,
                         bool value);

/**
 * qtest_qom_get_bool:
 * @s: QTestState instance to operate on.
 * @path: Path to the property being retrieved.
 * @property: Property from where the value is being retrieved.
 *
 * Returns: Value retrieved from property.
 */
bool qtest_qom_get_bool(QTestState *s, const char *path, const char *property);

/**
 * qtest_pid:
 * @s: QTestState instance to operate on.
 *
 * Returns: the PID of the QEMU process, or <= 0
 */
pid_t qtest_pid(QTestState *s);

/**
 * have_qemu_img:
 *
 * Returns: true if "qemu-img" is available.
 */
bool have_qemu_img(void);

/**
 * mkimg:
 * @file: File name of the image that should be created
 * @fmt: Format, e.g. "qcow2" or "raw"
 * @size_mb: Size of the image in megabytes
 *
 * Create a disk image with qemu-img. Note that the QTEST_QEMU_IMG
 * environment variable must point to the qemu-img file.
 *
 * Returns: true if the image has been created successfully.
 */
bool mkimg(const char *file, const char *fmt, unsigned size_mb);

#endif
