/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 * splitted out ioport related stuffs from vl.c.
 */

#include "ioport.h"

/***********************************************************/
/* IO Port */

//#define DEBUG_UNUSED_IOPORT
//#define DEBUG_IOPORT

#ifdef DEBUG_UNUSED_IOPORT
#  define LOG_UNUSED_IOPORT(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
#  define LOG_UNUSED_IOPORT(fmt, ...) do{ } while (0)
#endif

#ifdef DEBUG_IOPORT
#  define LOG_IOPORT(...) qemu_log_mask(CPU_LOG_IOPORT, ## __VA_ARGS__)
#else
#  define LOG_IOPORT(...) do { } while (0)
#endif

/* XXX: use a two level table to limit memory usage */

static void *ioport_opaque[MAX_IOPORTS];
static IOPortReadFunc *ioport_read_table[3][MAX_IOPORTS];
static IOPortWriteFunc *ioport_write_table[3][MAX_IOPORTS];

static IOPortReadFunc default_ioport_readb, default_ioport_readw, default_ioport_readl;
static IOPortWriteFunc default_ioport_writeb, default_ioport_writew, default_ioport_writel;

static uint32_t ioport_read(int index, uint32_t address)
{
    static IOPortReadFunc * const default_func[3] = {
        default_ioport_readb,
        default_ioport_readw,
        default_ioport_readl
    };
    IOPortReadFunc *func = ioport_read_table[index][address];
    if (!func)
        func = default_func[index];
    return func(ioport_opaque[address], address);
}

static void ioport_write(int index, uint32_t address, uint32_t data)
{
    static IOPortWriteFunc * const default_func[3] = {
        default_ioport_writeb,
        default_ioport_writew,
        default_ioport_writel
    };
    IOPortWriteFunc *func = ioport_write_table[index][address];
    if (!func)
        func = default_func[index];
    func(ioport_opaque[address], address, data);
}

static uint32_t default_ioport_readb(void *opaque, uint32_t address)
{
    LOG_UNUSED_IOPORT("unused inb: port=0x%04"PRIx32"\n", address);
    return 0xff;
}

static void default_ioport_writeb(void *opaque, uint32_t address, uint32_t data)
{
    LOG_UNUSED_IOPORT("unused outb: port=0x%04"PRIx32" data=0x%02"PRIx32"\n",
                      address, data);
}

/* default is to make two byte accesses */
static uint32_t default_ioport_readw(void *opaque, uint32_t address)
{
    uint32_t data;
    data = ioport_read(0, address);
    address = (address + 1) & IOPORTS_MASK;
    data |= ioport_read(0, address) << 8;
    return data;
}

static void default_ioport_writew(void *opaque, uint32_t address, uint32_t data)
{
    ioport_write(0, address, data & 0xff);
    address = (address + 1) & IOPORTS_MASK;
    ioport_write(0, address, (data >> 8) & 0xff);
}

static uint32_t default_ioport_readl(void *opaque, uint32_t address)
{
    LOG_UNUSED_IOPORT("unused inl: port=0x%04"PRIx32"\n", address);
    return 0xffffffff;
}

static void default_ioport_writel(void *opaque, uint32_t address, uint32_t data)
{
    LOG_UNUSED_IOPORT("unused outl: port=0x%04"PRIx32" data=0x%02"PRIx32"\n",
                      address, data);
}

static int ioport_bsize(int size, int *bsize)
{
    if (size == 1) {
        *bsize = 0;
    } else if (size == 2) {
        *bsize = 1;
    } else if (size == 4) {
        *bsize = 2;
    } else {
        return -1;
    }
    return 0;
}

/* size is the word size in byte */
int register_ioport_read(pio_addr_t start, int length, int size,
                         IOPortReadFunc *func, void *opaque)
{
    int i, bsize;

    if (ioport_bsize(size, &bsize)) {
        hw_error("register_ioport_read: invalid size");
        return -1;
    }
    for(i = start; i < start + length; i += size) {
        ioport_read_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_read: invalid opaque");
        ioport_opaque[i] = opaque;
    }
    return 0;
}

/* size is the word size in byte */
int register_ioport_write(pio_addr_t start, int length, int size,
                          IOPortWriteFunc *func, void *opaque)
{
    int i, bsize;

    if (ioport_bsize(size, &bsize)) {
        hw_error("register_ioport_write: invalid size");
        return -1;
    }
    for(i = start; i < start + length; i += size) {
        ioport_write_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_write: invalid opaque");
        ioport_opaque[i] = opaque;
    }
    return 0;
}

void isa_unassign_ioport(pio_addr_t start, int length)
{
    int i;

    for(i = start; i < start + length; i++) {
        ioport_read_table[0][i] = default_ioport_readb;
        ioport_read_table[1][i] = default_ioport_readw;
        ioport_read_table[2][i] = default_ioport_readl;

        ioport_write_table[0][i] = default_ioport_writeb;
        ioport_write_table[1][i] = default_ioport_writew;
        ioport_write_table[2][i] = default_ioport_writel;

        ioport_opaque[i] = NULL;
    }
}

/***********************************************************/

void cpu_outb(pio_addr_t addr, uint8_t val)
{
    LOG_IOPORT("outb: %04"FMT_pioaddr" %02"PRIx8"\n", addr, val);
    ioport_write(0, addr, val);
}

void cpu_outw(pio_addr_t addr, uint16_t val)
{
    LOG_IOPORT("outw: %04"FMT_pioaddr" %04"PRIx16"\n", addr, val);
    ioport_write(1, addr, val);
}

void cpu_outl(pio_addr_t addr, uint32_t val)
{
    LOG_IOPORT("outl: %04"FMT_pioaddr" %08"PRIx32"\n", addr, val);
    ioport_write(2, addr, val);
}

uint8_t cpu_inb(pio_addr_t addr)
{
    uint8_t val;
    val = ioport_read(0, addr);
    LOG_IOPORT("inb : %04"FMT_pioaddr" %02"PRIx8"\n", addr, val);
    return val;
}

uint16_t cpu_inw(pio_addr_t addr)
{
    uint16_t val;
    val = ioport_read(1, addr);
    LOG_IOPORT("inw : %04"FMT_pioaddr" %04"PRIx16"\n", addr, val);
    return val;
}

uint32_t cpu_inl(pio_addr_t addr)
{
    uint32_t val;
    val = ioport_read(2, addr);
    LOG_IOPORT("inl : %04"FMT_pioaddr" %08"PRIx32"\n", addr, val);
    return val;
}
