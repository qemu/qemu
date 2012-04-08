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
#include "trace.h"
#include "memory.h"

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
static IOPortDestructor *ioport_destructor_table[MAX_IOPORTS];

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
    for(i = start; i < start + length; ++i) {
        ioport_read_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_read: invalid opaque for address 0x%x",
                     i);
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
    for(i = start; i < start + length; ++i) {
        ioport_write_table[bsize][i] = func;
        if (ioport_opaque[i] != NULL && ioport_opaque[i] != opaque)
            hw_error("register_ioport_write: invalid opaque for address 0x%x",
                     i);
        ioport_opaque[i] = opaque;
    }
    return 0;
}

static uint32_t ioport_readb_thunk(void *opaque, uint32_t addr)
{
    IORange *ioport = opaque;
    uint64_t data;

    ioport->ops->read(ioport, addr - ioport->base, 1, &data);
    return data;
}

static uint32_t ioport_readw_thunk(void *opaque, uint32_t addr)
{
    IORange *ioport = opaque;
    uint64_t data;

    ioport->ops->read(ioport, addr - ioport->base, 2, &data);
    return data;
}

static uint32_t ioport_readl_thunk(void *opaque, uint32_t addr)
{
    IORange *ioport = opaque;
    uint64_t data;

    ioport->ops->read(ioport, addr - ioport->base, 4, &data);
    return data;
}

static void ioport_writeb_thunk(void *opaque, uint32_t addr, uint32_t data)
{
    IORange *ioport = opaque;

    ioport->ops->write(ioport, addr - ioport->base, 1, data);
}

static void ioport_writew_thunk(void *opaque, uint32_t addr, uint32_t data)
{
    IORange *ioport = opaque;

    ioport->ops->write(ioport, addr - ioport->base, 2, data);
}

static void ioport_writel_thunk(void *opaque, uint32_t addr, uint32_t data)
{
    IORange *ioport = opaque;

    ioport->ops->write(ioport, addr - ioport->base, 4, data);
}

static void iorange_destructor_thunk(void *opaque)
{
    IORange *iorange = opaque;

    if (iorange->ops->destructor) {
        iorange->ops->destructor(iorange);
    }
}

void ioport_register(IORange *ioport)
{
    register_ioport_read(ioport->base, ioport->len, 1,
                         ioport_readb_thunk, ioport);
    register_ioport_read(ioport->base, ioport->len, 2,
                         ioport_readw_thunk, ioport);
    register_ioport_read(ioport->base, ioport->len, 4,
                         ioport_readl_thunk, ioport);
    register_ioport_write(ioport->base, ioport->len, 1,
                          ioport_writeb_thunk, ioport);
    register_ioport_write(ioport->base, ioport->len, 2,
                          ioport_writew_thunk, ioport);
    register_ioport_write(ioport->base, ioport->len, 4,
                          ioport_writel_thunk, ioport);
    ioport_destructor_table[ioport->base] = iorange_destructor_thunk;
}

void isa_unassign_ioport(pio_addr_t start, int length)
{
    int i;

    if (ioport_destructor_table[start]) {
        ioport_destructor_table[start](ioport_opaque[start]);
        ioport_destructor_table[start] = NULL;
    }
    for(i = start; i < start + length; i++) {
        ioport_read_table[0][i] = NULL;
        ioport_read_table[1][i] = NULL;
        ioport_read_table[2][i] = NULL;

        ioport_write_table[0][i] = NULL;
        ioport_write_table[1][i] = NULL;
        ioport_write_table[2][i] = NULL;

        ioport_opaque[i] = NULL;
    }
}

bool isa_is_ioport_assigned(pio_addr_t start)
{
    return (ioport_read_table[0][start] || ioport_write_table[0][start] ||
	    ioport_read_table[1][start] || ioport_write_table[1][start] ||
	    ioport_read_table[2][start] || ioport_write_table[2][start]);
}

/***********************************************************/

void cpu_outb(pio_addr_t addr, uint8_t val)
{
    LOG_IOPORT("outb: %04"FMT_pioaddr" %02"PRIx8"\n", addr, val);
    trace_cpu_out(addr, val);
    ioport_write(0, addr, val);
}

void cpu_outw(pio_addr_t addr, uint16_t val)
{
    LOG_IOPORT("outw: %04"FMT_pioaddr" %04"PRIx16"\n", addr, val);
    trace_cpu_out(addr, val);
    ioport_write(1, addr, val);
}

void cpu_outl(pio_addr_t addr, uint32_t val)
{
    LOG_IOPORT("outl: %04"FMT_pioaddr" %08"PRIx32"\n", addr, val);
    trace_cpu_out(addr, val);
    ioport_write(2, addr, val);
}

uint8_t cpu_inb(pio_addr_t addr)
{
    uint8_t val;
    val = ioport_read(0, addr);
    trace_cpu_in(addr, val);
    LOG_IOPORT("inb : %04"FMT_pioaddr" %02"PRIx8"\n", addr, val);
    return val;
}

uint16_t cpu_inw(pio_addr_t addr)
{
    uint16_t val;
    val = ioport_read(1, addr);
    trace_cpu_in(addr, val);
    LOG_IOPORT("inw : %04"FMT_pioaddr" %04"PRIx16"\n", addr, val);
    return val;
}

uint32_t cpu_inl(pio_addr_t addr)
{
    uint32_t val;
    val = ioport_read(2, addr);
    trace_cpu_in(addr, val);
    LOG_IOPORT("inl : %04"FMT_pioaddr" %08"PRIx32"\n", addr, val);
    return val;
}

void portio_list_init(PortioList *piolist,
                      const MemoryRegionPortio *callbacks,
                      void *opaque, const char *name)
{
    unsigned n = 0;

    while (callbacks[n].size) {
        ++n;
    }

    piolist->ports = callbacks;
    piolist->nr = 0;
    piolist->regions = g_new0(MemoryRegion *, n);
    piolist->aliases = g_new0(MemoryRegion *, n);
    piolist->address_space = NULL;
    piolist->opaque = opaque;
    piolist->name = name;
}

void portio_list_destroy(PortioList *piolist)
{
    g_free(piolist->regions);
    g_free(piolist->aliases);
}

static void portio_list_add_1(PortioList *piolist,
                              const MemoryRegionPortio *pio_init,
                              unsigned count, unsigned start,
                              unsigned off_low, unsigned off_high)
{
    MemoryRegionPortio *pio;
    MemoryRegionOps *ops;
    MemoryRegion *region, *alias;
    unsigned i;

    /* Copy the sub-list and null-terminate it.  */
    pio = g_new(MemoryRegionPortio, count + 1);
    memcpy(pio, pio_init, sizeof(MemoryRegionPortio) * count);
    memset(pio + count, 0, sizeof(MemoryRegionPortio));

    /* Adjust the offsets to all be zero-based for the region.  */
    for (i = 0; i < count; ++i) {
        pio[i].offset -= off_low;
    }

    ops = g_new0(MemoryRegionOps, 1);
    ops->old_portio = pio;

    region = g_new(MemoryRegion, 1);
    alias = g_new(MemoryRegion, 1);
    /*
     * Use an alias so that the callback is called with an absolute address,
     * rather than an offset relative to to start + off_low.
     */
    memory_region_init_io(region, ops, piolist->opaque, piolist->name,
                          INT64_MAX);
    memory_region_init_alias(alias, piolist->name,
                             region, start + off_low, off_high - off_low);
    memory_region_add_subregion(piolist->address_space,
                                start + off_low, alias);
    piolist->regions[piolist->nr] = region;
    piolist->aliases[piolist->nr] = alias;
    ++piolist->nr;
}

void portio_list_add(PortioList *piolist,
                     MemoryRegion *address_space,
                     uint32_t start)
{
    const MemoryRegionPortio *pio, *pio_start = piolist->ports;
    unsigned int off_low, off_high, off_last, count;

    piolist->address_space = address_space;

    /* Handle the first entry specially.  */
    off_last = off_low = pio_start->offset;
    off_high = off_low + pio_start->len;
    count = 1;

    for (pio = pio_start + 1; pio->size != 0; pio++, count++) {
        /* All entries must be sorted by offset.  */
        assert(pio->offset >= off_last);
        off_last = pio->offset;

        /* If we see a hole, break the region.  */
        if (off_last > off_high) {
            portio_list_add_1(piolist, pio_start, count, start, off_low,
                              off_high);
            /* ... and start collecting anew.  */
            pio_start = pio;
            off_low = off_last;
            off_high = off_low + pio->len;
            count = 0;
        } else if (off_last + pio->len > off_high) {
            off_high = off_last + pio->len;
        }
    }

    /* There will always be an open sub-list.  */
    portio_list_add_1(piolist, pio_start, count, start, off_low, off_high);
}

void portio_list_del(PortioList *piolist)
{
    MemoryRegion *mr, *alias;
    unsigned i;

    for (i = 0; i < piolist->nr; ++i) {
        mr = piolist->regions[i];
        alias = piolist->aliases[i];
        memory_region_del_subregion(piolist->address_space, alias);
        memory_region_destroy(alias);
        memory_region_destroy(mr);
        g_free((MemoryRegionOps *)mr->ops);
        g_free(mr);
        g_free(alias);
        piolist->regions[i] = NULL;
        piolist->aliases[i] = NULL;
    }
}
