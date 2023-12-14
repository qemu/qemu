/*
 * Generic Virtual-Device Fuzzing Target
 *
 * Copyright Red Hat Inc., 2020
 *
 * Authors:
 *  Alexander Bulekov   <alxndr@bu.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include <wordexp.h>

#include "hw/core/cpu.h"
#include "tests/qtest/libqtest.h"
#include "tests/qtest/libqos/pci-pc.h"
#include "fuzz.h"
#include "string.h"
#include "exec/memory.h"
#include "exec/ramblock.h"
#include "hw/qdev-core.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/boards.h"
#include "generic_fuzz_configs.h"
#include "hw/mem/sparse-mem.h"

static void pci_enum(gpointer pcidev, gpointer bus);

/*
 * SEPARATOR is used to separate "operations" in the fuzz input
 */
#define SEPARATOR "FUZZ"

enum cmds {
    OP_IN,
    OP_OUT,
    OP_READ,
    OP_WRITE,
    OP_PCI_READ,
    OP_PCI_WRITE,
    OP_DISABLE_PCI,
    OP_ADD_DMA_PATTERN,
    OP_CLEAR_DMA_PATTERNS,
    OP_CLOCK_STEP,
};

#define USEC_IN_SEC 1000000000

#define MAX_DMA_FILL_SIZE 0x10000
#define MAX_TOTAL_DMA_SIZE 0x10000000

#define PCI_HOST_BRIDGE_CFG 0xcf8
#define PCI_HOST_BRIDGE_DATA 0xcfc

typedef struct {
    ram_addr_t addr;
    ram_addr_t size; /* The number of bytes until the end of the I/O region */
} address_range;

static bool qtest_log_enabled;
size_t dma_bytes_written;

MemoryRegion *sparse_mem_mr;

/*
 * A pattern used to populate a DMA region or perform a memwrite. This is
 * useful for e.g. populating tables of unique addresses.
 * Example {.index = 1; .stride = 2; .len = 3; .data = "\x00\x01\x02"}
 * Renders as: 00 01 02   00 03 02   00 05 02   00 07 02 ...
 */
typedef struct {
    uint8_t index;      /* Index of a byte to increment by stride */
    uint8_t stride;     /* Increment each index'th byte by this amount */
    size_t len;
    const uint8_t *data;
} pattern;

/* Avoid filling the same DMA region between MMIO/PIO commands ? */
static bool avoid_double_fetches;

static QTestState *qts_global; /* Need a global for the DMA callback */

/*
 * List of memory regions that are children of QOM objects specified by the
 * user for fuzzing.
 */
static GHashTable *fuzzable_memoryregions;
static GPtrArray *fuzzable_pci_devices;

struct get_io_cb_info {
    int index;
    int found;
    address_range result;
};

static bool get_io_address_cb(Int128 start, Int128 size,
                              const MemoryRegion *mr,
                              hwaddr offset_in_region,
                              void *opaque)
{
    struct get_io_cb_info *info = opaque;
    if (g_hash_table_lookup(fuzzable_memoryregions, mr)) {
        if (info->index == 0) {
            info->result.addr = (ram_addr_t)start;
            info->result.size = (ram_addr_t)size;
            info->found = 1;
            return true;
        }
        info->index--;
    }
    return false;
}

/*
 * List of dma regions populated since the last fuzzing command. Used to ensure
 * that we only write to each DMA address once, to avoid race conditions when
 * building reproducers.
 */
static GArray *dma_regions;

static GArray *dma_patterns;
static int dma_pattern_index;
static bool pci_disabled;

/*
 * Allocate a block of memory and populate it with a pattern.
 */
static void *pattern_alloc(pattern p, size_t len)
{
    int i;
    uint8_t *buf = g_malloc(len);
    uint8_t sum = 0;

    for (i = 0; i < len; ++i) {
        buf[i] = p.data[i % p.len];
        if ((i % p.len) == p.index) {
            buf[i] += sum;
            sum += p.stride;
        }
    }
    return buf;
}

static int fuzz_memory_access_size(MemoryRegion *mr, unsigned l, hwaddr addr)
{
    unsigned access_size_max = mr->ops->valid.max_access_size;

    /*
     * Regions are assumed to support 1-4 byte accesses unless
     * otherwise specified.
     */
    if (access_size_max == 0) {
        access_size_max = 4;
    }

    /* Bound the maximum access by the alignment of the address.  */
    if (!mr->ops->impl.unaligned) {
        unsigned align_size_max = addr & -addr;
        if (align_size_max != 0 && align_size_max < access_size_max) {
            access_size_max = align_size_max;
        }
    }

    /* Don't attempt accesses larger than the maximum.  */
    if (l > access_size_max) {
        l = access_size_max;
    }
    l = pow2floor(l);

    return l;
}

/*
 * Call-back for functions that perform DMA reads from guest memory. Confirm
 * that the region has not already been populated since the last loop in
 * generic_fuzz(), avoiding potential race-conditions, which we don't have
 * a good way for reproducing right now.
 */
void fuzz_dma_read_cb(size_t addr, size_t len, MemoryRegion *mr)
{
    /* Are we in the generic-fuzzer or are we using another fuzz-target? */
    if (!qts_global) {
        return;
    }

    /*
     * Return immediately if:
     * - We have no DMA patterns defined
     * - The length of the DMA read request is zero
     * - The DMA read is hitting an MR other than the machine's main RAM
     * - The DMA request hits past the bounds of our RAM
     */
    if (dma_patterns->len == 0
        || len == 0
        || dma_bytes_written + len > MAX_TOTAL_DMA_SIZE
        || (mr != current_machine->ram && mr != sparse_mem_mr)) {
        return;
    }

    /*
     * If we overlap with any existing dma_regions, split the range and only
     * populate the non-overlapping parts.
     */
    address_range region;
    bool double_fetch = false;
    for (int i = 0;
         i < dma_regions->len && (avoid_double_fetches || qtest_log_enabled);
         ++i) {
        region = g_array_index(dma_regions, address_range, i);
        if (addr < region.addr + region.size && addr + len > region.addr) {
            double_fetch = true;
            if (addr < region.addr
                && avoid_double_fetches) {
                fuzz_dma_read_cb(addr, region.addr - addr, mr);
            }
            if (addr + len > region.addr + region.size
                && avoid_double_fetches) {
                fuzz_dma_read_cb(region.addr + region.size,
                        addr + len - (region.addr + region.size), mr);
            }
            return;
        }
    }

    /* Cap the length of the DMA access to something reasonable */
    len = MIN(len, MAX_DMA_FILL_SIZE);

    address_range ar = {addr, len};
    g_array_append_val(dma_regions, ar);
    pattern p = g_array_index(dma_patterns, pattern, dma_pattern_index);
    void *buf_base = pattern_alloc(p, ar.size);
    void *buf = buf_base;
    hwaddr l, addr1;
    MemoryRegion *mr1;
    while (len > 0) {
        l = len;
        mr1 = address_space_translate(first_cpu->as,
                                      addr, &addr1, &l, true,
                                      MEMTXATTRS_UNSPECIFIED);

        /*
         *  If mr1 isn't RAM, address_space_translate doesn't update l. Use
         *  fuzz_memory_access_size to identify the number of bytes that it
         *  is safe to write without accidentally writing to another
         *  MemoryRegion.
         */
        if (!memory_region_is_ram(mr1)) {
            l = fuzz_memory_access_size(mr1, l, addr1);
        }
        if (memory_region_is_ram(mr1) ||
            memory_region_is_romd(mr1) ||
            mr1 == sparse_mem_mr) {
            /* ROM/RAM case */
            if (qtest_log_enabled) {
                /*
                * With QTEST_LOG, use a normal, slow QTest memwrite. Prefix the log
                * that will be written by qtest.c with a DMA tag, so we can reorder
                * the resulting QTest trace so the DMA fills precede the last PIO/MMIO
                * command.
                */
                fprintf(stderr, "[DMA] ");
                if (double_fetch) {
                    fprintf(stderr, "[DOUBLE-FETCH] ");
                }
                fflush(stderr);
            }
            qtest_memwrite(qts_global, addr, buf, l);
            dma_bytes_written += l;
        }
        len -= l;
        buf += l;
        addr += l;

    }
    g_free(buf_base);

    /* Increment the index of the pattern for the next DMA access */
    dma_pattern_index = (dma_pattern_index + 1) % dma_patterns->len;
}

/*
 * Here we want to convert a fuzzer-provided [io-region-index, offset] to
 * a physical address. To do this, we iterate over all of the matched
 * MemoryRegions. Check whether each region exists within the particular io
 * space. Return the absolute address of the offset within the index'th region
 * that is a subregion of the io_space and the distance until the end of the
 * memory region.
 */
static bool get_io_address(address_range *result, AddressSpace *as,
                            uint8_t index,
                            uint32_t offset) {
    FlatView *view;
    view = as->current_map;
    g_assert(view);
    struct get_io_cb_info cb_info = {};

    cb_info.index = index;

    /*
     * Loop around the FlatView until we match "index" number of
     * fuzzable_memoryregions, or until we know that there are no matching
     * memory_regions.
     */
    do {
        flatview_for_each_range(view, get_io_address_cb , &cb_info);
    } while (cb_info.index != index && !cb_info.found);

    *result = cb_info.result;
    if (result->size) {
        offset = offset % result->size;
        result->addr += offset;
        result->size -= offset;
    }
    return cb_info.found;
}

static bool get_pio_address(address_range *result,
                            uint8_t index, uint16_t offset)
{
    /*
     * PIO BARs can be set past the maximum port address (0xFFFF). Thus, result
     * can contain an addr that extends past the PIO space. When we pass this
     * address to qtest_in/qtest_out, it is cast to a uint16_t, so we might end
     * up fuzzing a completely different MemoryRegion/Device. Therefore, check
     * that the address here is within the PIO space limits.
     */
    bool found = get_io_address(result, &address_space_io, index, offset);
    return result->addr <= 0xFFFF ? found : false;
}

static bool get_mmio_address(address_range *result,
                             uint8_t index, uint32_t offset)
{
    return get_io_address(result, &address_space_memory, index, offset);
}

static void op_in(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, abs.addr);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_inw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_inl(s, abs.addr);
        }
        break;
    }
}

static void op_out(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint16_t offset;
        uint32_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_pio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_outw(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_outl(s, abs.addr, a.value);
        }
        break;
    }
}

static void op_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
        qtest_readb(s, abs.addr);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_readw(s, abs.addr);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_readl(s, abs.addr);
        }
        break;
    case Quad:
        if (abs.size >= 8) {
            qtest_readq(s, abs.addr);
        }
        break;
    }
}

static void op_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, Quad, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint32_t offset;
        uint64_t value;
    } a;
    address_range abs;

    if (len < sizeof(a)) {
        return;
    }
    memcpy(&a, data, sizeof(a));

    if (get_mmio_address(&abs, a.base, a.offset) == 0) {
        return;
    }

    switch (a.size %= end_sizes) {
    case Byte:
            qtest_writeb(s, abs.addr, a.value & 0xFF);
        break;
    case Word:
        if (abs.size >= 2) {
            qtest_writew(s, abs.addr, a.value & 0xFFFF);
        }
        break;
    case Long:
        if (abs.size >= 4) {
            qtest_writel(s, abs.addr, a.value & 0xFFFFFFFF);
        }
        break;
    case Quad:
        if (abs.size >= 8) {
            qtest_writeq(s, abs.addr, a.value);
        }
        break;
    }
}

static void op_pci_read(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint8_t offset;
    } a;
    if (len < sizeof(a) || fuzzable_pci_devices->len == 0 || pci_disabled) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    PCIDevice *dev = g_ptr_array_index(fuzzable_pci_devices,
                                  a.base % fuzzable_pci_devices->len);
    int devfn = dev->devfn;
    qtest_outl(s, PCI_HOST_BRIDGE_CFG, (1U << 31) | (devfn << 8) | a.offset);
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_inb(s, PCI_HOST_BRIDGE_DATA);
        break;
    case Word:
        qtest_inw(s, PCI_HOST_BRIDGE_DATA);
        break;
    case Long:
        qtest_inl(s, PCI_HOST_BRIDGE_DATA);
        break;
    }
}

static void op_pci_write(QTestState *s, const unsigned char * data, size_t len)
{
    enum Sizes {Byte, Word, Long, end_sizes};
    struct {
        uint8_t size;
        uint8_t base;
        uint8_t offset;
        uint32_t value;
    } a;
    if (len < sizeof(a) || fuzzable_pci_devices->len == 0 || pci_disabled) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    PCIDevice *dev = g_ptr_array_index(fuzzable_pci_devices,
                                  a.base % fuzzable_pci_devices->len);
    int devfn = dev->devfn;
    qtest_outl(s, PCI_HOST_BRIDGE_CFG, (1U << 31) | (devfn << 8) | a.offset);
    switch (a.size %= end_sizes) {
    case Byte:
        qtest_outb(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFF);
        break;
    case Word:
        qtest_outw(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFFFF);
        break;
    case Long:
        qtest_outl(s, PCI_HOST_BRIDGE_DATA, a.value & 0xFFFFFFFF);
        break;
    }
}

static void op_add_dma_pattern(QTestState *s,
                               const unsigned char *data, size_t len)
{
    struct {
        /*
         * index and stride can be used to increment the index-th byte of the
         * pattern by the value stride, for each loop of the pattern.
         */
        uint8_t index;
        uint8_t stride;
    } a;

    if (len < sizeof(a) + 1) {
        return;
    }
    memcpy(&a, data, sizeof(a));
    pattern p = {a.index, a.stride, len - sizeof(a), data + sizeof(a)};
    p.index = a.index % p.len;
    g_array_append_val(dma_patterns, p);
    return;
}

static void op_clear_dma_patterns(QTestState *s,
                                  const unsigned char *data, size_t len)
{
    g_array_set_size(dma_patterns, 0);
    dma_pattern_index = 0;
}

static void op_clock_step(QTestState *s, const unsigned char *data, size_t len)
{
    qtest_clock_step_next(s);
}

static void op_disable_pci(QTestState *s, const unsigned char *data, size_t len)
{
    pci_disabled = true;
}

/*
 * Here, we interpret random bytes from the fuzzer, as a sequence of commands.
 * Some commands can be variable-width, so we use a separator, SEPARATOR, to
 * specify the boundaries between commands. SEPARATOR is used to separate
 * "operations" in the fuzz input. Why use a separator, instead of just using
 * the operations' length to identify operation boundaries?
 *   1. This is a simple way to support variable-length operations
 *   2. This adds "stability" to the input.
 *      For example take the input "AbBcgDefg", where there is no separator and
 *      Opcodes are capitalized.
 *      Simply, by removing the first byte, we end up with a very different
 *      sequence:
 *      BbcGdefg...
 *      By adding a separator, we avoid this problem:
 *      Ab SEP Bcg SEP Defg -> B SEP Bcg SEP Defg
 *      Since B uses two additional bytes as operands, the first "B" will be
 *      ignored. The fuzzer actively tries to reduce inputs, so such unused
 *      bytes are likely to be pruned, eventually.
 *
 *  SEPARATOR is trivial for the fuzzer to discover when using ASan. Optionally,
 *  SEPARATOR can be manually specified as a dictionary value (see libfuzzer's
 *  -dict), though this should not be necessary.
 *
 * As a result, the stream of bytes is converted into a sequence of commands.
 * In a simplified example where SEPARATOR is 0xFF:
 * 00 01 02 FF 03 04 05 06 FF 01 FF ...
 * becomes this sequence of commands:
 * 00 01 02    -> op00 (0102)   -> in (0102, 2)
 * 03 04 05 06 -> op03 (040506) -> write (040506, 3)
 * 01          -> op01 (-,0)    -> out (-,0)
 * ...
 *
 * Note here that it is the job of the individual opcode functions to check
 * that enough data was provided. I.e. in the last command out (,0), out needs
 * to check that there is not enough data provided to select an address/value
 * for the operation.
 */
static void generic_fuzz(QTestState *s, const unsigned char *Data, size_t Size)
{
    void (*ops[]) (QTestState *s, const unsigned char* , size_t) = {
        [OP_IN]                 = op_in,
        [OP_OUT]                = op_out,
        [OP_READ]               = op_read,
        [OP_WRITE]              = op_write,
        [OP_PCI_READ]           = op_pci_read,
        [OP_PCI_WRITE]          = op_pci_write,
        [OP_DISABLE_PCI]        = op_disable_pci,
        [OP_ADD_DMA_PATTERN]    = op_add_dma_pattern,
        [OP_CLEAR_DMA_PATTERNS] = op_clear_dma_patterns,
        [OP_CLOCK_STEP]         = op_clock_step,
    };
    const unsigned char *cmd = Data;
    const unsigned char *nextcmd;
    size_t cmd_len;
    uint8_t op;

    op_clear_dma_patterns(s, NULL, 0);
    pci_disabled = false;
    dma_bytes_written = 0;

    QPCIBus *pcibus = qpci_new_pc(s, NULL);
    g_ptr_array_foreach(fuzzable_pci_devices, pci_enum, pcibus);
    qpci_free_pc(pcibus);

    while (cmd && Size) {
        /* Get the length until the next command or end of input */
        nextcmd = memmem(cmd, Size, SEPARATOR, strlen(SEPARATOR));
        cmd_len = nextcmd ? nextcmd - cmd : Size;

        if (cmd_len > 0) {
            /* Interpret the first byte of the command as an opcode */
            op = *cmd % (sizeof(ops) / sizeof((ops)[0]));
            ops[op](s, cmd + 1, cmd_len - 1);

            /* Run the main loop */
            flush_events(s);
        }
        /* Advance to the next command */
        cmd = nextcmd ? nextcmd + sizeof(SEPARATOR) - 1 : nextcmd;
        Size = Size - (cmd_len + sizeof(SEPARATOR) - 1);
        g_array_set_size(dma_regions, 0);
    }
    fuzz_reset(s);
}

static void usage(void)
{
    printf("Please specify the following environment variables:\n");
    printf("QEMU_FUZZ_ARGS= the command line arguments passed to qemu\n");
    printf("QEMU_FUZZ_OBJECTS= "
            "a space separated list of QOM type names for objects to fuzz\n");
    printf("Optionally: QEMU_AVOID_DOUBLE_FETCH= "
            "Try to avoid racy DMA double fetch bugs? %d by default\n",
            avoid_double_fetches);
    exit(0);
}

static int locate_fuzz_memory_regions(Object *child, void *opaque)
{
    MemoryRegion *mr;
    if (object_dynamic_cast(child, TYPE_MEMORY_REGION)) {
        mr = MEMORY_REGION(child);
        if ((memory_region_is_ram(mr) ||
            memory_region_is_ram_device(mr) ||
            memory_region_is_rom(mr)) == false) {
            /*
             * We don't want duplicate pointers to the same MemoryRegion, so
             * try to remove copies of the pointer, before adding it.
             */
            g_hash_table_insert(fuzzable_memoryregions, mr, (gpointer)true);
        }
    }
    return 0;
}

static int locate_fuzz_objects(Object *child, void *opaque)
{
    GString *type_name;
    GString *path_name;
    char *pattern = opaque;

    type_name = g_string_new(object_get_typename(child));
    g_string_ascii_down(type_name);
    if (g_pattern_match_simple(pattern, type_name->str)) {
        /* Find and save ptrs to any child MemoryRegions */
        object_child_foreach_recursive(child, locate_fuzz_memory_regions, NULL);

        /*
         * We matched an object. If its a PCI device, store a pointer to it so
         * we can map BARs and fuzz its config space.
         */
        if (object_dynamic_cast(OBJECT(child), TYPE_PCI_DEVICE)) {
            /*
             * Don't want duplicate pointers to the same PCIDevice, so remove
             * copies of the pointer, before adding it.
             */
            g_ptr_array_remove_fast(fuzzable_pci_devices, PCI_DEVICE(child));
            g_ptr_array_add(fuzzable_pci_devices, PCI_DEVICE(child));
        }
    } else if (object_dynamic_cast(OBJECT(child), TYPE_MEMORY_REGION)) {
        path_name = g_string_new(object_get_canonical_path_component(child));
        g_string_ascii_down(path_name);
        if (g_pattern_match_simple(pattern, path_name->str)) {
            MemoryRegion *mr;
            mr = MEMORY_REGION(child);
            if ((memory_region_is_ram(mr) ||
                 memory_region_is_ram_device(mr) ||
                 memory_region_is_rom(mr)) == false) {
                g_hash_table_insert(fuzzable_memoryregions, mr, (gpointer)true);
            }
        }
        g_string_free(path_name, true);
    }
    g_string_free(type_name, true);
    return 0;
}


static void pci_enum(gpointer pcidev, gpointer bus)
{
    PCIDevice *dev = pcidev;
    QPCIDevice *qdev;
    int i;

    qdev = qpci_device_find(bus, dev->devfn);
    g_assert(qdev != NULL);
    for (i = 0; i < 6; i++) {
        if (dev->io_regions[i].size) {
            qpci_iomap(qdev, i, NULL);
        }
    }
    qpci_device_enable(qdev);
    g_free(qdev);
}

static void generic_pre_fuzz(QTestState *s)
{
    GHashTableIter iter;
    MemoryRegion *mr;
    char **result;
    GString *name_pattern;

    if (!getenv("QEMU_FUZZ_OBJECTS")) {
        usage();
    }
    if (getenv("QTEST_LOG")) {
        qtest_log_enabled = 1;
    }
    if (getenv("QEMU_AVOID_DOUBLE_FETCH")) {
        avoid_double_fetches = 1;
    }
    qts_global = s;

    /*
     * Create a special device that we can use to back DMA buffers at very
     * high memory addresses
     */
    sparse_mem_mr = sparse_mem_init(0, UINT64_MAX);

    dma_regions = g_array_new(false, false, sizeof(address_range));
    dma_patterns = g_array_new(false, false, sizeof(pattern));

    fuzzable_memoryregions = g_hash_table_new(NULL, NULL);
    fuzzable_pci_devices   = g_ptr_array_new();

    result = g_strsplit(getenv("QEMU_FUZZ_OBJECTS"), " ", -1);
    for (int i = 0; result[i] != NULL; i++) {
        name_pattern = g_string_new(result[i]);
        /*
         * Make the pattern lowercase. We do the same for all the MemoryRegion
         * and Type names so the configs are case-insensitive.
         */
        g_string_ascii_down(name_pattern);
        printf("Matching objects by name %s\n", result[i]);
        object_child_foreach_recursive(qdev_get_machine(),
                                    locate_fuzz_objects,
                                    name_pattern->str);
        g_string_free(name_pattern, true);
    }
    g_strfreev(result);
    printf("This process will try to fuzz the following MemoryRegions:\n");

    g_hash_table_iter_init(&iter, fuzzable_memoryregions);
    while (g_hash_table_iter_next(&iter, (gpointer)&mr, NULL)) {
        printf("  * %s (size 0x%" PRIx64 ")\n",
               object_get_canonical_path_component(&(mr->parent_obj)),
               memory_region_size(mr));
    }

    if (!g_hash_table_size(fuzzable_memoryregions)) {
        printf("No fuzzable memory regions found...\n");
        exit(1);
    }
}

/*
 * When libfuzzer gives us two inputs to combine, return a new input with the
 * following structure:
 *
 * Input 1 (data1)
 * SEPARATOR
 * Clear out the DMA Patterns
 * SEPARATOR
 * Disable the pci_read/write instructions
 * SEPARATOR
 * Input 2 (data2)
 *
 * The idea is to collate the core behaviors of the two inputs.
 * For example:
 * Input 1: maps a device's BARs, sets up three DMA patterns, and triggers
 *          device functionality A
 * Input 2: maps a device's BARs, sets up one DMA pattern, and triggers device
 *          functionality B
 *
 * This function attempts to produce an input that:
 * Output: maps a device's BARs, set up three DMA patterns, triggers
 *          device functionality A, replaces the DMA patterns with a single
 *          pattern, and triggers device functionality B.
 */
static size_t generic_fuzz_crossover(const uint8_t *data1, size_t size1, const
                                     uint8_t *data2, size_t size2, uint8_t *out,
                                     size_t max_out_size, unsigned int seed)
{
    size_t copy_len = 0, size = 0;

    /* Check that we have enough space for data1 and at least part of data2 */
    if (max_out_size <= size1 + strlen(SEPARATOR) * 3 + 2) {
        return 0;
    }

    /* Copy_Len in the first input */
    copy_len = size1;
    memcpy(out + size, data1, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    /* Append a separator */
    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    /* Clear out the DMA Patterns */
    copy_len = 1;
    if (copy_len) {
        out[size] = OP_CLEAR_DMA_PATTERNS;
    }
    size += copy_len;
    max_out_size -= copy_len;

    /* Append a separator */
    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    /* Disable PCI ops. Assume data1 took care of setting up PCI */
    copy_len = 1;
    if (copy_len) {
        out[size] = OP_DISABLE_PCI;
    }
    size += copy_len;
    max_out_size -= copy_len;

    /* Append a separator */
    copy_len = strlen(SEPARATOR);
    memcpy(out + size, SEPARATOR, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    /* Copy_Len over the second input */
    copy_len = MIN(size2, max_out_size);
    memcpy(out + size, data2, copy_len);
    size += copy_len;
    max_out_size -= copy_len;

    return  size;
}


static GString *generic_fuzz_cmdline(FuzzTarget *t)
{
    GString *cmd_line = g_string_new(TARGET_NAME);
    if (!getenv("QEMU_FUZZ_ARGS")) {
        usage();
    }
    g_string_append_printf(cmd_line, " -display none \
                                      -machine accel=qtest, \
                                      -m 512M %s ", getenv("QEMU_FUZZ_ARGS"));
    return cmd_line;
}

static GString *generic_fuzz_predefined_config_cmdline(FuzzTarget *t)
{
    gchar *args;
    const generic_fuzz_config *config;
    g_assert(t->opaque);

    config = t->opaque;
    g_setenv("QEMU_AVOID_DOUBLE_FETCH", "1", 1);
    if (config->argfunc) {
        args = config->argfunc();
        g_setenv("QEMU_FUZZ_ARGS", args, 1);
        g_free(args);
    } else {
        g_assert_nonnull(config->args);
        g_setenv("QEMU_FUZZ_ARGS", config->args, 1);
    }
    g_setenv("QEMU_FUZZ_OBJECTS", config->objects, 1);
    return generic_fuzz_cmdline(t);
}

static void register_generic_fuzz_targets(void)
{
    fuzz_add_target(&(FuzzTarget){
            .name = "generic-fuzz",
            .description = "Fuzz based on any qemu command-line args. ",
            .get_init_cmdline = generic_fuzz_cmdline,
            .pre_fuzz = generic_pre_fuzz,
            .fuzz = generic_fuzz,
            .crossover = generic_fuzz_crossover
    });

    for (int i = 0; i < ARRAY_SIZE(predefined_configs); i++) {
        const generic_fuzz_config *config = predefined_configs + i;
        fuzz_add_target(&(FuzzTarget){
                .name = g_strconcat("generic-fuzz-", config->name, NULL),
                .description = "Predefined generic-fuzz config.",
                .get_init_cmdline = generic_fuzz_predefined_config_cmdline,
                .pre_fuzz = generic_pre_fuzz,
                .fuzz = generic_fuzz,
                .crossover = generic_fuzz_crossover,
                .opaque = (void *)config
        });
    }
}

fuzz_target_init(register_generic_fuzz_targets);
