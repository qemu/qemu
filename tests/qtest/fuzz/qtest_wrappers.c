/*
 * qtest function wrappers
 *
 * Copyright Red Hat Inc., 2019
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "system/ioport.h"

#include "fuzz.h"

static bool serialize = true;

#define WRAP(RET_TYPE, NAME_AND_ARGS)\
    RET_TYPE __wrap_##NAME_AND_ARGS;\
    RET_TYPE __real_##NAME_AND_ARGS;

WRAP(uint8_t  , qtest_inb(QTestState *s, uint16_t addr))
WRAP(uint16_t , qtest_inw(QTestState *s, uint16_t addr))
WRAP(uint32_t , qtest_inl(QTestState *s, uint16_t addr))
WRAP(void     , qtest_outb(QTestState *s, uint16_t addr, uint8_t value))
WRAP(void     , qtest_outw(QTestState *s, uint16_t addr, uint16_t value))
WRAP(void     , qtest_outl(QTestState *s, uint16_t addr, uint32_t value))
WRAP(uint8_t  , qtest_readb(QTestState *s, uint64_t addr))
WRAP(uint16_t , qtest_readw(QTestState *s, uint64_t addr))
WRAP(uint32_t , qtest_readl(QTestState *s, uint64_t addr))
WRAP(uint64_t , qtest_readq(QTestState *s, uint64_t addr))
WRAP(void     , qtest_writeb(QTestState *s, uint64_t addr, uint8_t value))
WRAP(void     , qtest_writew(QTestState *s, uint64_t addr, uint16_t value))
WRAP(void     , qtest_writel(QTestState *s, uint64_t addr, uint32_t value))
WRAP(void     , qtest_writeq(QTestState *s, uint64_t addr, uint64_t value))
WRAP(void     , qtest_memread(QTestState *s, uint64_t addr,
                              void *data, size_t size))
WRAP(void     , qtest_bufread(QTestState *s, uint64_t addr, void *data,
                              size_t size))
WRAP(void     , qtest_memwrite(QTestState *s, uint64_t addr, const void *data,
                               size_t size))
WRAP(void,      qtest_bufwrite(QTestState *s, uint64_t addr,
                               const void *data, size_t size))
WRAP(void,      qtest_memset(QTestState *s, uint64_t addr,
                             uint8_t patt, size_t size))


uint8_t __wrap_qtest_inb(QTestState *s, uint16_t addr)
{
    if (!serialize) {
        return cpu_inb(addr);
    } else {
        return __real_qtest_inb(s, addr);
    }
}

uint16_t __wrap_qtest_inw(QTestState *s, uint16_t addr)
{
    if (!serialize) {
        return cpu_inw(addr);
    } else {
        return __real_qtest_inw(s, addr);
    }
}

uint32_t __wrap_qtest_inl(QTestState *s, uint16_t addr)
{
    if (!serialize) {
        return cpu_inl(addr);
    } else {
        return __real_qtest_inl(s, addr);
    }
}

void __wrap_qtest_outb(QTestState *s, uint16_t addr, uint8_t value)
{
    if (!serialize) {
        cpu_outb(addr, value);
    } else {
        __real_qtest_outb(s, addr, value);
    }
}

void __wrap_qtest_outw(QTestState *s, uint16_t addr, uint16_t value)
{
    if (!serialize) {
        cpu_outw(addr, value);
    } else {
        __real_qtest_outw(s, addr, value);
    }
}

void __wrap_qtest_outl(QTestState *s, uint16_t addr, uint32_t value)
{
    if (!serialize) {
        cpu_outl(addr, value);
    } else {
        __real_qtest_outl(s, addr, value);
    }
}

uint8_t __wrap_qtest_readb(QTestState *s, uint64_t addr)
{
    uint8_t value;
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 1);
        return value;
    } else {
        return __real_qtest_readb(s, addr);
    }
}

uint16_t __wrap_qtest_readw(QTestState *s, uint64_t addr)
{
    uint16_t value;
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 2);
        return value;
    } else {
        return __real_qtest_readw(s, addr);
    }
}

uint32_t __wrap_qtest_readl(QTestState *s, uint64_t addr)
{
    uint32_t value;
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 4);
        return value;
    } else {
        return __real_qtest_readl(s, addr);
    }
}

uint64_t __wrap_qtest_readq(QTestState *s, uint64_t addr)
{
    uint64_t value;
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 8);
        return value;
    } else {
        return __real_qtest_readq(s, addr);
    }
}

void __wrap_qtest_writeb(QTestState *s, uint64_t addr, uint8_t value)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 1);
    } else {
        __real_qtest_writeb(s, addr, value);
    }
}

void __wrap_qtest_writew(QTestState *s, uint64_t addr, uint16_t value)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 2);
    } else {
        __real_qtest_writew(s, addr, value);
    }
}

void __wrap_qtest_writel(QTestState *s, uint64_t addr, uint32_t value)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 4);
    } else {
        __real_qtest_writel(s, addr, value);
    }
}

void __wrap_qtest_writeq(QTestState *s, uint64_t addr, uint64_t value)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            &value, 8);
    } else {
        __real_qtest_writeq(s, addr, value);
    }
}

void __wrap_qtest_memread(QTestState *s, uint64_t addr, void *data, size_t size)
{
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                           size);
    } else {
        __real_qtest_memread(s, addr, data, size);
    }
}

void __wrap_qtest_bufread(QTestState *s, uint64_t addr, void *data, size_t size)
{
    if (!serialize) {
        address_space_read(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED, data,
                           size);
    } else {
        __real_qtest_bufread(s, addr, data, size);
    }
}

void __wrap_qtest_memwrite(QTestState *s, uint64_t addr, const void *data,
                           size_t size)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            data, size);
    } else {
        __real_qtest_memwrite(s, addr, data, size);
    }
}

void __wrap_qtest_bufwrite(QTestState *s, uint64_t addr,
                    const void *data, size_t size)
{
    if (!serialize) {
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            data, size);
    } else {
        __real_qtest_bufwrite(s, addr, data, size);
    }
}
void __wrap_qtest_memset(QTestState *s, uint64_t addr,
                         uint8_t patt, size_t size)
{
    void *data;
    if (!serialize) {
        data = malloc(size);
        memset(data, patt, size);
        address_space_write(first_cpu->as, addr, MEMTXATTRS_UNSPECIFIED,
                            data, size);
    } else {
        __real_qtest_memset(s, addr, patt, size);
    }
}

void fuzz_qtest_set_serialize(bool option)
{
    serialize = option;
}
