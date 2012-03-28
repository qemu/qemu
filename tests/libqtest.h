/*
 * QTest
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Paolo Bonzini     <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef LIBQTEST_H
#define LIBQTEST_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

typedef struct QTestState QTestState;

extern QTestState *global_qtest;

/**
 * qtest_init:
 * @extra_args: other arguments to pass to QEMU.
 */
QTestState *qtest_init(const char *extra_args);

/**
 * qtest_quit:
 * @s: QTestState instance to operate on.
 *
 * Shut down the QEMU process associated to @s.
 */
void qtest_quit(QTestState *s);

/**
 * qtest_get_irq:
 * @s: QTestState instance to operate on.
 * @num: Interrupt to observe.
 *
 * Return the level of the @num interrupt.
 */
bool qtest_get_irq(QTestState *s, int num);

/**
 * qtest_irq_intercept_in:
 * @s: QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-in pins of the device
 * whose path is specified by @string.
 */
void qtest_irq_intercept_in(QTestState *s, const char *string);

/**
 * qtest_irq_intercept_out:
 * @s: QTestState instance to operate on.
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-out pins of the device
 * whose path is specified by @string.
 */
void qtest_irq_intercept_out(QTestState *s, const char *string);

/**
 * qtest_outb:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port.
 */
void qtest_outb(QTestState *s, uint16_t addr, uint8_t value);

/**
 * qtest_outw:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port.
 */
void qtest_outw(QTestState *s, uint16_t addr, uint16_t value);

/**
 * qtest_outl:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port.
 */
void qtest_outl(QTestState *s, uint16_t addr, uint32_t value);

/**
 * qtest_inb:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns an 8-bit value from an I/O port.
 */
uint8_t qtest_inb(QTestState *s, uint16_t addr);

/**
 * qtest_inw:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns a 16-bit value from an I/O port.
 */
uint16_t qtest_inw(QTestState *s, uint16_t addr);

/**
 * qtest_inl:
 * @s: QTestState instance to operate on.
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns a 32-bit value from an I/O port.
 */
uint32_t qtest_inl(QTestState *s, uint16_t addr);

/**
 * qtest_memread:
 * @s: QTestState instance to operate on.
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer.
 */
void qtest_memread(QTestState *s, uint64_t addr, void *data, size_t size);

/**
 * qtest_memwrite:
 * @s: QTestState instance to operate on.
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory.
 */
void qtest_memwrite(QTestState *s, uint64_t addr, const void *data, size_t size);

/**
 * qtest_clock_step_next:
 * @s: QTestState instance to operate on.
 *
 * Advance the vm_clock to the next deadline.  Return the current
 * value of the vm_clock in nanoseconds.
 */
int64_t qtest_clock_step_next(QTestState *s);

/**
 * qtest_clock_step:
 * @s: QTestState instance to operate on.
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the vm_clock by @step nanoseconds.  Return the current
 * value of the vm_clock in nanoseconds.
 */
int64_t qtest_clock_step(QTestState *s, int64_t step);

/**
 * qtest_clock_set:
 * @s: QTestState instance to operate on.
 * @val: Nanoseconds value to advance the clock to.
 *
 * Advance the vm_clock to @val nanoseconds since the VM was launched.
 * Return the current value of the vm_clock in nanoseconds.
 */
int64_t qtest_clock_set(QTestState *s, int64_t val);

/**
 * qtest_get_arch:
 *
 * Returns the architecture for the QEMU executable under test.
 */
const char *qtest_get_arch(void);

/**
 * qtest_add_func:
 * @str: Test case path.
 * @fn: Test case function
 *
 * Add a GTester testcase with the given name and function.
 * The path is prefixed with the architecture under test, as
 * returned by qtest_get_arch.
 */
void qtest_add_func(const char *str, void (*fn));

/**
 * qtest_start:
 * @args: other arguments to pass to QEMU
 *
 * Start QEMU and assign the resulting QTestState to a global variable.
 * The global variable is used by "shortcut" macros documented below.
 */
#define qtest_start(args) (            \
    global_qtest = qtest_init((args)) \
        )

/**
 * get_irq:
 * @num: Interrupt to observe.
 *
 * Return the level of the @num interrupt.
 */
#define get_irq(num) qtest_get_irq(global_qtest, num)

/**
 * irq_intercept_in:
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-in pins of the device
 * whose path is specified by @string.
 */
#define irq_intercept_in(string) qtest_irq_intercept_in(global_qtest, string)

/**
 * qtest_irq_intercept_out:
 * @string: QOM path of a device.
 *
 * Associate qtest irqs with the GPIO-out pins of the device
 * whose path is specified by @string.
 */
#define irq_intercept_out(string) qtest_irq_intercept_out(global_qtest, string)

/**
 * outb:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port.
 */
#define outb(addr, val) qtest_outb(global_qtest, addr, val)

/**
 * outw:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port.
 */
#define outw(addr, val) qtest_outw(global_qtest, addr, val)

/**
 * outl:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port.
 */
#define outl(addr, val) qtest_outl(global_qtest, addr, val)

/**
 * inb:
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns an 8-bit value from an I/O port.
 */
#define inb(addr) qtest_inb(global_qtest, addr)

/**
 * inw:
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns a 16-bit value from an I/O port.
 */
#define inw(addr) qtest_inw(global_qtest, addr)

/**
 * inl:
 * @addr: I/O port to read from.
 * @value: Value being written.
 *
 * Returns a 32-bit value from an I/O port.
 */
#define inl(addr) qtest_inl(global_qtest, addr)

/**
 * memread:
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer.
 */
#define memread(addr, data, size) qtest_memread(global_qtest, addr, data, size)

/**
 * memwrite:
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory.
 */
#define memwrite(addr, data, size) qtest_memwrite(global_qtest, addr, data, size)

/**
 * clock_step_next:
 *
 * Advance the vm_clock to the next deadline.  Return the current
 * value of the vm_clock in nanoseconds.
 */
#define clock_step_next() qtest_clock_step_next(global_qtest)

/**
 * clock_step:
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the vm_clock by @step nanoseconds.  Return the current
 * value of the vm_clock in nanoseconds.
 */
#define clock_step(step) qtest_clock_step(global_qtest, step)

/**
 * clock_set:
 * @val: Nanoseconds value to advance the clock to.
 *
 * Advance the vm_clock to @val nanoseconds since the VM was launched.
 * Return the current value of the vm_clock in nanoseconds.
 */
#define clock_set(val) qtest_clock_set(global_qtest, val)

#endif
