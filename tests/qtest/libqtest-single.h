/*
 * QTest - wrappers for test with single QEMU instances
 *
 * Copyright IBM, Corp. 2012
 * Copyright Red Hat, Inc. 2012
 * Copyright SUSE LINUX Products GmbH 2013
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef LIBQTEST_SINGLE_H
#define LIBQTEST_SINGLE_H

#include "libqtest.h"

QTestState *global_qtest __attribute__((common, weak));

/**
 * qtest_start:
 * @args: other arguments to pass to QEMU
 *
 * Start QEMU and assign the resulting #QTestState to a global variable.
 * The global variable is used by "shortcut" functions documented below.
 *
 * Returns: #QTestState instance.
 */
static inline QTestState *qtest_start(const char *args)
{
    global_qtest = qtest_init(args);
    return global_qtest;
}

/**
 * qtest_end:
 *
 * Shut down the QEMU process started by qtest_start().
 */
static inline void qtest_end(void)
{
    if (!global_qtest) {
        return;
    }
    qtest_quit(global_qtest);
    global_qtest = NULL;
}

/**
 * qmp:
 * @fmt...: QMP message to send to qemu, formatted like
 * qobject_from_jsonf_nofail().  See parse_escape() for what's
 * supported after '%'.
 *
 * Sends a QMP message to QEMU and returns the response.
 */
GCC_FMT_ATTR(1, 2)
static inline QDict *qmp(const char *fmt, ...)
{
    va_list ap;
    QDict *response;

    va_start(ap, fmt);
    response = qtest_vqmp(global_qtest, fmt, ap);
    va_end(ap);
    return response;
}

/**
 * qmp_eventwait:
 * @s: #event event to wait for.
 *
 * Continuously polls for QMP responses until it receives the desired event.
 */
static inline void qmp_eventwait(const char *event)
{
    return qtest_qmp_eventwait(global_qtest, event);
}

/**
 * get_irq:
 * @num: Interrupt to observe.
 *
 * Returns: The level of the @num interrupt.
 */
static inline bool get_irq(int num)
{
    return qtest_get_irq(global_qtest, num);
}

/**
 * outb:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write an 8-bit value to an I/O port.
 */
static inline void outb(uint16_t addr, uint8_t value)
{
    qtest_outb(global_qtest, addr, value);
}

/**
 * outw:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 16-bit value to an I/O port.
 */
static inline void outw(uint16_t addr, uint16_t value)
{
    qtest_outw(global_qtest, addr, value);
}

/**
 * outl:
 * @addr: I/O port to write to.
 * @value: Value being written.
 *
 * Write a 32-bit value to an I/O port.
 */
static inline void outl(uint16_t addr, uint32_t value)
{
    qtest_outl(global_qtest, addr, value);
}

/**
 * inb:
 * @addr: I/O port to read from.
 *
 * Reads an 8-bit value from an I/O port.
 *
 * Returns: Value read.
 */
static inline uint8_t inb(uint16_t addr)
{
    return qtest_inb(global_qtest, addr);
}

/**
 * inw:
 * @addr: I/O port to read from.
 *
 * Reads a 16-bit value from an I/O port.
 *
 * Returns: Value read.
 */
static inline uint16_t inw(uint16_t addr)
{
    return qtest_inw(global_qtest, addr);
}

/**
 * inl:
 * @addr: I/O port to read from.
 *
 * Reads a 32-bit value from an I/O port.
 *
 * Returns: Value read.
 */
static inline uint32_t inl(uint16_t addr)
{
    return qtest_inl(global_qtest, addr);
}

/**
 * writeb:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes an 8-bit value to guest memory.
 */
static inline void writeb(uint64_t addr, uint8_t value)
{
    qtest_writeb(global_qtest, addr, value);
}

/**
 * writew:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 16-bit value to guest memory.
 */
static inline void writew(uint64_t addr, uint16_t value)
{
    qtest_writew(global_qtest, addr, value);
}

/**
 * writel:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 32-bit value to guest memory.
 */
static inline void writel(uint64_t addr, uint32_t value)
{
    qtest_writel(global_qtest, addr, value);
}

/**
 * writeq:
 * @addr: Guest address to write to.
 * @value: Value being written.
 *
 * Writes a 64-bit value to guest memory.
 */
static inline void writeq(uint64_t addr, uint64_t value)
{
    qtest_writeq(global_qtest, addr, value);
}

/**
 * readb:
 * @addr: Guest address to read from.
 *
 * Reads an 8-bit value from guest memory.
 *
 * Returns: Value read.
 */
static inline uint8_t readb(uint64_t addr)
{
    return qtest_readb(global_qtest, addr);
}

/**
 * readw:
 * @addr: Guest address to read from.
 *
 * Reads a 16-bit value from guest memory.
 *
 * Returns: Value read.
 */
static inline uint16_t readw(uint64_t addr)
{
    return qtest_readw(global_qtest, addr);
}

/**
 * readl:
 * @addr: Guest address to read from.
 *
 * Reads a 32-bit value from guest memory.
 *
 * Returns: Value read.
 */
static inline uint32_t readl(uint64_t addr)
{
    return qtest_readl(global_qtest, addr);
}

/**
 * readq:
 * @addr: Guest address to read from.
 *
 * Reads a 64-bit value from guest memory.
 *
 * Returns: Value read.
 */
static inline uint64_t readq(uint64_t addr)
{
    return qtest_readq(global_qtest, addr);
}

/**
 * memread:
 * @addr: Guest address to read from.
 * @data: Pointer to where memory contents will be stored.
 * @size: Number of bytes to read.
 *
 * Read guest memory into a buffer.
 */
static inline void memread(uint64_t addr, void *data, size_t size)
{
    qtest_memread(global_qtest, addr, data, size);
}

/**
 * memwrite:
 * @addr: Guest address to write to.
 * @data: Pointer to the bytes that will be written to guest memory.
 * @size: Number of bytes to write.
 *
 * Write a buffer to guest memory.
 */
static inline void memwrite(uint64_t addr, const void *data, size_t size)
{
    qtest_memwrite(global_qtest, addr, data, size);
}

/**
 * clock_step_next:
 *
 * Advance the QEMU_CLOCK_VIRTUAL to the next deadline.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
static inline int64_t clock_step_next(void)
{
    return qtest_clock_step_next(global_qtest);
}

/**
 * clock_step:
 * @step: Number of nanoseconds to advance the clock by.
 *
 * Advance the QEMU_CLOCK_VIRTUAL by @step nanoseconds.
 *
 * Returns: The current value of the QEMU_CLOCK_VIRTUAL in nanoseconds.
 */
static inline int64_t clock_step(int64_t step)
{
    return qtest_clock_step(global_qtest, step);
}

#endif
