/*
 * QEMU GT64120 PCI host
 *
 * Copyright (c) 2006,2007 Aurelien Jarno
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/registerfields.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "hw/misc/empty_slot.h"
#include "migration/vmstate.h"
#include "hw/intc/i8259.h"
#include "hw/irq.h"
#include "trace.h"
#include "qom/object.h"

#define GT_REGS                 (0x1000 >> 2)

/* CPU Configuration */
#define GT_CPU                  (0x000 >> 2)
#define GT_MULTI                (0x120 >> 2)

REG32(GT_CPU,                   0x000)
FIELD(GT_CPU,                   Endianness,     12, 1)

/* CPU Address Decode */
#define GT_SCS10LD              (0x008 >> 2)
#define GT_SCS10HD              (0x010 >> 2)
#define GT_SCS32LD              (0x018 >> 2)
#define GT_SCS32HD              (0x020 >> 2)
#define GT_CS20LD               (0x028 >> 2)
#define GT_CS20HD               (0x030 >> 2)
#define GT_CS3BOOTLD            (0x038 >> 2)
#define GT_CS3BOOTHD            (0x040 >> 2)
#define GT_PCI0IOLD             (0x048 >> 2)
#define GT_PCI0IOHD             (0x050 >> 2)
#define GT_PCI0M0LD             (0x058 >> 2)
#define GT_PCI0M0HD             (0x060 >> 2)
#define GT_PCI0M1LD             (0x080 >> 2)
#define GT_PCI0M1HD             (0x088 >> 2)
#define GT_PCI1IOLD             (0x090 >> 2)
#define GT_PCI1IOHD             (0x098 >> 2)
#define GT_PCI1M0LD             (0x0a0 >> 2)
#define GT_PCI1M0HD             (0x0a8 >> 2)
#define GT_PCI1M1LD             (0x0b0 >> 2)
#define GT_PCI1M1HD             (0x0b8 >> 2)
#define GT_ISD                  (0x068 >> 2)

#define GT_SCS10AR              (0x0d0 >> 2)
#define GT_SCS32AR              (0x0d8 >> 2)
#define GT_CS20R                (0x0e0 >> 2)
#define GT_CS3BOOTR             (0x0e8 >> 2)

#define GT_PCI0IOREMAP          (0x0f0 >> 2)
#define GT_PCI0M0REMAP          (0x0f8 >> 2)
#define GT_PCI0M1REMAP          (0x100 >> 2)
#define GT_PCI1IOREMAP          (0x108 >> 2)
#define GT_PCI1M0REMAP          (0x110 >> 2)
#define GT_PCI1M1REMAP          (0x118 >> 2)

/* CPU Error Report */
#define GT_CPUERR_ADDRLO        (0x070 >> 2)
#define GT_CPUERR_ADDRHI        (0x078 >> 2)
#define GT_CPUERR_DATALO        (0x128 >> 2)        /* GT-64120A only  */
#define GT_CPUERR_DATAHI        (0x130 >> 2)        /* GT-64120A only  */
#define GT_CPUERR_PARITY        (0x138 >> 2)        /* GT-64120A only  */

/* CPU Sync Barrier */
#define GT_PCI0SYNC             (0x0c0 >> 2)
#define GT_PCI1SYNC             (0x0c8 >> 2)

/* SDRAM and Device Address Decode */
#define GT_SCS0LD               (0x400 >> 2)
#define GT_SCS0HD               (0x404 >> 2)
#define GT_SCS1LD               (0x408 >> 2)
#define GT_SCS1HD               (0x40c >> 2)
#define GT_SCS2LD               (0x410 >> 2)
#define GT_SCS2HD               (0x414 >> 2)
#define GT_SCS3LD               (0x418 >> 2)
#define GT_SCS3HD               (0x41c >> 2)
#define GT_CS0LD                (0x420 >> 2)
#define GT_CS0HD                (0x424 >> 2)
#define GT_CS1LD                (0x428 >> 2)
#define GT_CS1HD                (0x42c >> 2)
#define GT_CS2LD                (0x430 >> 2)
#define GT_CS2HD                (0x434 >> 2)
#define GT_CS3LD                (0x438 >> 2)
#define GT_CS3HD                (0x43c >> 2)
#define GT_BOOTLD               (0x440 >> 2)
#define GT_BOOTHD               (0x444 >> 2)
#define GT_ADERR                (0x470 >> 2)

/* SDRAM Configuration */
#define GT_SDRAM_CFG            (0x448 >> 2)
#define GT_SDRAM_OPMODE         (0x474 >> 2)
#define GT_SDRAM_BM             (0x478 >> 2)
#define GT_SDRAM_ADDRDECODE     (0x47c >> 2)

/* SDRAM Parameters */
#define GT_SDRAM_B0             (0x44c >> 2)
#define GT_SDRAM_B1             (0x450 >> 2)
#define GT_SDRAM_B2             (0x454 >> 2)
#define GT_SDRAM_B3             (0x458 >> 2)

/* Device Parameters */
#define GT_DEV_B0               (0x45c >> 2)
#define GT_DEV_B1               (0x460 >> 2)
#define GT_DEV_B2               (0x464 >> 2)
#define GT_DEV_B3               (0x468 >> 2)
#define GT_DEV_BOOT             (0x46c >> 2)

/* ECC */
#define GT_ECC_ERRDATALO        (0x480 >> 2)        /* GT-64120A only  */
#define GT_ECC_ERRDATAHI        (0x484 >> 2)        /* GT-64120A only  */
#define GT_ECC_MEM              (0x488 >> 2)        /* GT-64120A only  */
#define GT_ECC_CALC             (0x48c >> 2)        /* GT-64120A only  */
#define GT_ECC_ERRADDR          (0x490 >> 2)        /* GT-64120A only  */

/* DMA Record */
#define GT_DMA0_CNT             (0x800 >> 2)
#define GT_DMA1_CNT             (0x804 >> 2)
#define GT_DMA2_CNT             (0x808 >> 2)
#define GT_DMA3_CNT             (0x80c >> 2)
#define GT_DMA0_SA              (0x810 >> 2)
#define GT_DMA1_SA              (0x814 >> 2)
#define GT_DMA2_SA              (0x818 >> 2)
#define GT_DMA3_SA              (0x81c >> 2)
#define GT_DMA0_DA              (0x820 >> 2)
#define GT_DMA1_DA              (0x824 >> 2)
#define GT_DMA2_DA              (0x828 >> 2)
#define GT_DMA3_DA              (0x82c >> 2)
#define GT_DMA0_NEXT            (0x830 >> 2)
#define GT_DMA1_NEXT            (0x834 >> 2)
#define GT_DMA2_NEXT            (0x838 >> 2)
#define GT_DMA3_NEXT            (0x83c >> 2)
#define GT_DMA0_CUR             (0x870 >> 2)
#define GT_DMA1_CUR             (0x874 >> 2)
#define GT_DMA2_CUR             (0x878 >> 2)
#define GT_DMA3_CUR             (0x87c >> 2)

/* DMA Channel Control */
#define GT_DMA0_CTRL            (0x840 >> 2)
#define GT_DMA1_CTRL            (0x844 >> 2)
#define GT_DMA2_CTRL            (0x848 >> 2)
#define GT_DMA3_CTRL            (0x84c >> 2)

/* DMA Arbiter */
#define GT_DMA_ARB              (0x860 >> 2)

/* Timer/Counter */
#define GT_TC0                  (0x850 >> 2)
#define GT_TC1                  (0x854 >> 2)
#define GT_TC2                  (0x858 >> 2)
#define GT_TC3                  (0x85c >> 2)
#define GT_TC_CONTROL           (0x864 >> 2)

/* PCI Internal */
#define GT_PCI0_CMD             (0xc00 >> 2)
#define GT_PCI0_TOR             (0xc04 >> 2)
#define GT_PCI0_BS_SCS10        (0xc08 >> 2)
#define GT_PCI0_BS_SCS32        (0xc0c >> 2)
#define GT_PCI0_BS_CS20         (0xc10 >> 2)
#define GT_PCI0_BS_CS3BT        (0xc14 >> 2)
#define GT_PCI1_IACK            (0xc30 >> 2)
#define GT_PCI0_IACK            (0xc34 >> 2)
#define GT_PCI0_BARE            (0xc3c >> 2)
#define GT_PCI0_PREFMBR         (0xc40 >> 2)
#define GT_PCI0_SCS10_BAR       (0xc48 >> 2)
#define GT_PCI0_SCS32_BAR       (0xc4c >> 2)
#define GT_PCI0_CS20_BAR        (0xc50 >> 2)
#define GT_PCI0_CS3BT_BAR       (0xc54 >> 2)
#define GT_PCI0_SSCS10_BAR      (0xc58 >> 2)
#define GT_PCI0_SSCS32_BAR      (0xc5c >> 2)
#define GT_PCI0_SCS3BT_BAR      (0xc64 >> 2)
#define GT_PCI1_CMD             (0xc80 >> 2)
#define GT_PCI1_TOR             (0xc84 >> 2)
#define GT_PCI1_BS_SCS10        (0xc88 >> 2)
#define GT_PCI1_BS_SCS32        (0xc8c >> 2)
#define GT_PCI1_BS_CS20         (0xc90 >> 2)
#define GT_PCI1_BS_CS3BT        (0xc94 >> 2)
#define GT_PCI1_BARE            (0xcbc >> 2)
#define GT_PCI1_PREFMBR         (0xcc0 >> 2)
#define GT_PCI1_SCS10_BAR       (0xcc8 >> 2)
#define GT_PCI1_SCS32_BAR       (0xccc >> 2)
#define GT_PCI1_CS20_BAR        (0xcd0 >> 2)
#define GT_PCI1_CS3BT_BAR       (0xcd4 >> 2)
#define GT_PCI1_SSCS10_BAR      (0xcd8 >> 2)
#define GT_PCI1_SSCS32_BAR      (0xcdc >> 2)
#define GT_PCI1_SCS3BT_BAR      (0xce4 >> 2)
#define GT_PCI1_CFGADDR         (0xcf0 >> 2)
#define GT_PCI1_CFGDATA         (0xcf4 >> 2)
#define GT_PCI0_CFGADDR         (0xcf8 >> 2)
#define GT_PCI0_CFGDATA         (0xcfc >> 2)

REG32(GT_PCI0_CMD,              0xc00)
FIELD(GT_PCI0_CMD,              MByteSwap,      0,  1)
FIELD(GT_PCI0_CMD,              SByteSwap,      16, 1)
#define  R_GT_PCI0_CMD_ByteSwap_MASK \
        (R_GT_PCI0_CMD_MByteSwap_MASK | R_GT_PCI0_CMD_SByteSwap_MASK)
REG32(GT_PCI1_CMD,              0xc80)
FIELD(GT_PCI1_CMD,              MByteSwap,      0,  1)
FIELD(GT_PCI1_CMD,              SByteSwap,      16, 1)
#define  R_GT_PCI1_CMD_ByteSwap_MASK \
        (R_GT_PCI1_CMD_MByteSwap_MASK | R_GT_PCI1_CMD_SByteSwap_MASK)

/* Interrupts */
#define GT_INTRCAUSE            (0xc18 >> 2)
#define GT_INTRMASK             (0xc1c >> 2)
#define GT_PCI0_ICMASK          (0xc24 >> 2)
#define GT_PCI0_SERR0MASK       (0xc28 >> 2)
#define GT_CPU_INTSEL           (0xc70 >> 2)
#define GT_PCI0_INTSEL          (0xc74 >> 2)
#define GT_HINTRCAUSE           (0xc98 >> 2)
#define GT_HINTRMASK            (0xc9c >> 2)
#define GT_PCI0_HICMASK         (0xca4 >> 2)
#define GT_PCI1_SERR1MASK       (0xca8 >> 2)

#define PCI_MAPPING_ENTRY(regname)            \
    hwaddr regname ##_start;      \
    hwaddr regname ##_length;     \
    MemoryRegion regname ##_mem

#define TYPE_GT64120_PCI_HOST_BRIDGE "gt64120"

OBJECT_DECLARE_SIMPLE_TYPE(GT64120State, GT64120_PCI_HOST_BRIDGE)

struct GT64120State {
    PCIHostState parent_obj;

    uint32_t regs[GT_REGS];
    PCI_MAPPING_ENTRY(PCI0IO);
    PCI_MAPPING_ENTRY(PCI0M0);
    PCI_MAPPING_ENTRY(PCI0M1);
    PCI_MAPPING_ENTRY(ISD);
    MemoryRegion pci0_mem;
    AddressSpace pci0_mem_as;

    /* properties */
    bool cpu_little_endian;
};

/* Adjust range to avoid touching space which isn't mappable via PCI */
/*
 * XXX: Hardcoded values for Malta: 0x1e000000 - 0x1f100000
 *                                  0x1fc00000 - 0x1fd00000
 */
static void check_reserved_space(hwaddr *start, hwaddr *length)
{
    hwaddr begin = *start;
    hwaddr end = *start + *length;

    if (end >= 0x1e000000LL && end < 0x1f100000LL) {
        end = 0x1e000000LL;
    }
    if (begin >= 0x1e000000LL && begin < 0x1f100000LL) {
        begin = 0x1f100000LL;
    }
    if (end >= 0x1fc00000LL && end < 0x1fd00000LL) {
        end = 0x1fc00000LL;
    }
    if (begin >= 0x1fc00000LL && begin < 0x1fd00000LL) {
        begin = 0x1fd00000LL;
    }
    /* XXX: This is broken when a reserved range splits the requested range */
    if (end >= 0x1f100000LL && begin < 0x1e000000LL) {
        end = 0x1e000000LL;
    }
    if (end >= 0x1fd00000LL && begin < 0x1fc00000LL) {
        end = 0x1fc00000LL;
    }

    *start = begin;
    *length = end - begin;
}

static void gt64120_isd_mapping(GT64120State *s)
{
    /* Bits 14:0 of ISD map to bits 35:21 of the start address.  */
    hwaddr start = ((hwaddr)s->regs[GT_ISD] << 21) & 0xFFFE00000ull;
    hwaddr length = 0x1000;

    memory_region_transaction_begin();

    if (s->ISD_length) {
        memory_region_del_subregion(get_system_memory(), &s->ISD_mem);
    }
    check_reserved_space(&start, &length);
    length = 0x1000;
    /* Map new address */
    trace_gt64120_isd_remap(s->ISD_length, s->ISD_start, length, start);
    s->ISD_start = start;
    s->ISD_length = length;
    memory_region_add_subregion(get_system_memory(), s->ISD_start, &s->ISD_mem);

    memory_region_transaction_commit();
}

static void gt64120_update_pci_cfgdata_mapping(GT64120State *s)
{
    /* Indexed on MByteSwap bit, see Table 158: PCI_0 Command, Offset: 0xc00 */
    static const MemoryRegionOps *pci_host_data_ops[] = {
        &pci_host_data_be_ops, &pci_host_data_le_ops
    };
    PCIHostState *phb = PCI_HOST_BRIDGE(s);

    memory_region_transaction_begin();

    /*
     * The setting of the MByteSwap bit and MWordSwap bit in the PCI Internal
     * Command Register determines how data transactions from the CPU to/from
     * PCI are handled along with the setting of the Endianness bit in the CPU
     * Configuration Register. See:
     * - Table 16: 32-bit PCI Transaction Endianness
     * - Table 158: PCI_0 Command, Offset: 0xc00
     */

    if (memory_region_is_mapped(&phb->data_mem)) {
        memory_region_del_subregion(&s->ISD_mem, &phb->data_mem);
        object_unparent(OBJECT(&phb->data_mem));
    }
    memory_region_init_io(&phb->data_mem, OBJECT(phb),
                          pci_host_data_ops[s->regs[GT_PCI0_CMD] & 1],
                          s, "pci-conf-data", 4);
    memory_region_add_subregion_overlap(&s->ISD_mem, GT_PCI0_CFGDATA << 2,
                                        &phb->data_mem, 1);

    memory_region_transaction_commit();
}

static void gt64120_pci_mapping(GT64120State *s)
{
    memory_region_transaction_begin();

    /* Update PCI0IO mapping */
    if ((s->regs[GT_PCI0IOLD] & 0x7f) <= s->regs[GT_PCI0IOHD]) {
        /* Unmap old IO address */
        if (s->PCI0IO_length) {
            memory_region_del_subregion(get_system_memory(), &s->PCI0IO_mem);
            object_unparent(OBJECT(&s->PCI0IO_mem));
        }
        /* Map new IO address */
        s->PCI0IO_start = s->regs[GT_PCI0IOLD] << 21;
        s->PCI0IO_length = ((s->regs[GT_PCI0IOHD] + 1) -
                            (s->regs[GT_PCI0IOLD] & 0x7f)) << 21;
        if (s->PCI0IO_length) {
            memory_region_init_alias(&s->PCI0IO_mem, OBJECT(s), "pci0-io",
                                     get_system_io(), 0, s->PCI0IO_length);
            memory_region_add_subregion(get_system_memory(), s->PCI0IO_start,
                                        &s->PCI0IO_mem);
        }
    }

    /* Update PCI0M0 mapping */
    if ((s->regs[GT_PCI0M0LD] & 0x7f) <= s->regs[GT_PCI0M0HD]) {
        /* Unmap old MEM address */
        if (s->PCI0M0_length) {
            memory_region_del_subregion(get_system_memory(), &s->PCI0M0_mem);
            object_unparent(OBJECT(&s->PCI0M0_mem));
        }
        /* Map new mem address */
        s->PCI0M0_start = s->regs[GT_PCI0M0LD] << 21;
        s->PCI0M0_length = ((s->regs[GT_PCI0M0HD] + 1) -
                            (s->regs[GT_PCI0M0LD] & 0x7f)) << 21;
        if (s->PCI0M0_length) {
            memory_region_init_alias(&s->PCI0M0_mem, OBJECT(s), "pci0-mem0",
                                     &s->pci0_mem, s->PCI0M0_start,
                                     s->PCI0M0_length);
            memory_region_add_subregion(get_system_memory(), s->PCI0M0_start,
                                        &s->PCI0M0_mem);
        }
    }

    /* Update PCI0M1 mapping */
    if ((s->regs[GT_PCI0M1LD] & 0x7f) <= s->regs[GT_PCI0M1HD]) {
        /* Unmap old MEM address */
        if (s->PCI0M1_length) {
            memory_region_del_subregion(get_system_memory(), &s->PCI0M1_mem);
            object_unparent(OBJECT(&s->PCI0M1_mem));
        }
        /* Map new mem address */
        s->PCI0M1_start = s->regs[GT_PCI0M1LD] << 21;
        s->PCI0M1_length = ((s->regs[GT_PCI0M1HD] + 1) -
                            (s->regs[GT_PCI0M1LD] & 0x7f)) << 21;
        if (s->PCI0M1_length) {
            memory_region_init_alias(&s->PCI0M1_mem, OBJECT(s), "pci0-mem1",
                                     &s->pci0_mem, s->PCI0M1_start,
                                     s->PCI0M1_length);
            memory_region_add_subregion(get_system_memory(), s->PCI0M1_start,
                                        &s->PCI0M1_mem);
        }
    }

    memory_region_transaction_commit();
}

static int gt64120_post_load(void *opaque, int version_id)
{
    GT64120State *s = opaque;

    gt64120_isd_mapping(s);
    gt64120_pci_mapping(s);

    return 0;
}

static const VMStateDescription vmstate_gt64120 = {
    .name = "gt64120",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = gt64120_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, GT64120State, GT_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void gt64120_writel(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    GT64120State *s = opaque;
    uint32_t saddr = addr >> 2;

    trace_gt64120_write(addr, val);
    if (!(s->regs[GT_CPU] & 0x00001000)) {
        val = bswap32(val);
    }

    switch (saddr) {

    /* CPU Configuration */
    case GT_CPU:
        s->regs[GT_CPU] = val;
        break;
    case GT_MULTI:
        /* Read-only register as only one GT64xxx is present on the CPU bus */
        break;

    /* CPU Address Decode */
    case GT_PCI0IOLD:
        s->regs[GT_PCI0IOLD]    = val & 0x00007fff;
        s->regs[GT_PCI0IOREMAP] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI0M0LD:
        s->regs[GT_PCI0M0LD]    = val & 0x00007fff;
        s->regs[GT_PCI0M0REMAP] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI0M1LD:
        s->regs[GT_PCI0M1LD]    = val & 0x00007fff;
        s->regs[GT_PCI0M1REMAP] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI1IOLD:
        s->regs[GT_PCI1IOLD]    = val & 0x00007fff;
        s->regs[GT_PCI1IOREMAP] = val & 0x000007ff;
        break;
    case GT_PCI1M0LD:
        s->regs[GT_PCI1M0LD]    = val & 0x00007fff;
        s->regs[GT_PCI1M0REMAP] = val & 0x000007ff;
        break;
    case GT_PCI1M1LD:
        s->regs[GT_PCI1M1LD]    = val & 0x00007fff;
        s->regs[GT_PCI1M1REMAP] = val & 0x000007ff;
        break;
    case GT_PCI0M0HD:
    case GT_PCI0M1HD:
    case GT_PCI0IOHD:
        s->regs[saddr] = val & 0x0000007f;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI1IOHD:
    case GT_PCI1M0HD:
    case GT_PCI1M1HD:
        s->regs[saddr] = val & 0x0000007f;
        break;
    case GT_ISD:
        s->regs[saddr] = val & 0x00007fff;
        gt64120_isd_mapping(s);
        break;

    case GT_PCI0IOREMAP:
    case GT_PCI0M0REMAP:
    case GT_PCI0M1REMAP:
    case GT_PCI1IOREMAP:
    case GT_PCI1M0REMAP:
    case GT_PCI1M1REMAP:
        s->regs[saddr] = val & 0x000007ff;
        break;

    /* CPU Error Report */
    case GT_CPUERR_ADDRLO:
    case GT_CPUERR_ADDRHI:
    case GT_CPUERR_DATALO:
    case GT_CPUERR_DATAHI:
    case GT_CPUERR_PARITY:
        /* Read-only registers, do nothing */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gt64120: Read-only register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* CPU Sync Barrier */
    case GT_PCI0SYNC:
    case GT_PCI1SYNC:
        /* Read-only registers, do nothing */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gt64120: Read-only register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* SDRAM and Device Address Decode */
    case GT_SCS0LD:
    case GT_SCS0HD:
    case GT_SCS1LD:
    case GT_SCS1HD:
    case GT_SCS2LD:
    case GT_SCS2HD:
    case GT_SCS3LD:
    case GT_SCS3HD:
    case GT_CS0LD:
    case GT_CS0HD:
    case GT_CS1LD:
    case GT_CS1HD:
    case GT_CS2LD:
    case GT_CS2HD:
    case GT_CS3LD:
    case GT_CS3HD:
    case GT_BOOTLD:
    case GT_BOOTHD:
    case GT_ADERR:
    /* SDRAM Configuration */
    case GT_SDRAM_CFG:
    case GT_SDRAM_OPMODE:
    case GT_SDRAM_BM:
    case GT_SDRAM_ADDRDECODE:
        /* Accept and ignore SDRAM interleave configuration */
        s->regs[saddr] = val;
        break;

    /* Device Parameters */
    case GT_DEV_B0:
    case GT_DEV_B1:
    case GT_DEV_B2:
    case GT_DEV_B3:
    case GT_DEV_BOOT:
        /* Not implemented */
        qemu_log_mask(LOG_UNIMP,
                      "gt64120: Unimplemented device register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* ECC */
    case GT_ECC_ERRDATALO:
    case GT_ECC_ERRDATAHI:
    case GT_ECC_MEM:
    case GT_ECC_CALC:
    case GT_ECC_ERRADDR:
        /* Read-only registers, do nothing */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gt64120: Read-only register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* DMA Record */
    case GT_DMA0_CNT:
    case GT_DMA1_CNT:
    case GT_DMA2_CNT:
    case GT_DMA3_CNT:
    case GT_DMA0_SA:
    case GT_DMA1_SA:
    case GT_DMA2_SA:
    case GT_DMA3_SA:
    case GT_DMA0_DA:
    case GT_DMA1_DA:
    case GT_DMA2_DA:
    case GT_DMA3_DA:
    case GT_DMA0_NEXT:
    case GT_DMA1_NEXT:
    case GT_DMA2_NEXT:
    case GT_DMA3_NEXT:
    case GT_DMA0_CUR:
    case GT_DMA1_CUR:
    case GT_DMA2_CUR:
    case GT_DMA3_CUR:

    /* DMA Channel Control */
    case GT_DMA0_CTRL:
    case GT_DMA1_CTRL:
    case GT_DMA2_CTRL:
    case GT_DMA3_CTRL:

    /* DMA Arbiter */
    case GT_DMA_ARB:
        /* Not implemented */
        qemu_log_mask(LOG_UNIMP,
                      "gt64120: Unimplemented DMA register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* Timer/Counter */
    case GT_TC0:
    case GT_TC1:
    case GT_TC2:
    case GT_TC3:
    case GT_TC_CONTROL:
        /* Not implemented */
        qemu_log_mask(LOG_UNIMP,
                      "gt64120: Unimplemented timer register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;

    /* PCI Internal */
    case GT_PCI0_CMD:
    case GT_PCI1_CMD:
        s->regs[saddr] = val & 0x0401fc0f;
        gt64120_update_pci_cfgdata_mapping(s);
        break;
    case GT_PCI0_TOR:
    case GT_PCI0_BS_SCS10:
    case GT_PCI0_BS_SCS32:
    case GT_PCI0_BS_CS20:
    case GT_PCI0_BS_CS3BT:
    case GT_PCI1_IACK:
    case GT_PCI0_IACK:
    case GT_PCI0_BARE:
    case GT_PCI0_PREFMBR:
    case GT_PCI0_SCS10_BAR:
    case GT_PCI0_SCS32_BAR:
    case GT_PCI0_CS20_BAR:
    case GT_PCI0_CS3BT_BAR:
    case GT_PCI0_SSCS10_BAR:
    case GT_PCI0_SSCS32_BAR:
    case GT_PCI0_SCS3BT_BAR:
    case GT_PCI1_TOR:
    case GT_PCI1_BS_SCS10:
    case GT_PCI1_BS_SCS32:
    case GT_PCI1_BS_CS20:
    case GT_PCI1_BS_CS3BT:
    case GT_PCI1_BARE:
    case GT_PCI1_PREFMBR:
    case GT_PCI1_SCS10_BAR:
    case GT_PCI1_SCS32_BAR:
    case GT_PCI1_CS20_BAR:
    case GT_PCI1_CS3BT_BAR:
    case GT_PCI1_SSCS10_BAR:
    case GT_PCI1_SSCS32_BAR:
    case GT_PCI1_SCS3BT_BAR:
    case GT_PCI1_CFGADDR:
    case GT_PCI1_CFGDATA:
        /* not implemented */
        qemu_log_mask(LOG_UNIMP,
                      "gt64120: Unimplemented PCI register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;
    case GT_PCI0_CFGADDR:
    case GT_PCI0_CFGDATA:
        /* Mapped via in gt64120_pci_mapping() */
        g_assert_not_reached();
        break;

    /* Interrupts */
    case GT_INTRCAUSE:
        /* not really implemented */
        s->regs[saddr] = ~(~(s->regs[saddr]) | ~(val & 0xfffffffe));
        s->regs[saddr] |= !!(s->regs[saddr] & 0xfffffffe);
        trace_gt64120_write_intreg("INTRCAUSE", size, val);
        break;
    case GT_INTRMASK:
        s->regs[saddr] = val & 0x3c3ffffe;
        trace_gt64120_write_intreg("INTRMASK", size, val);
        break;
    case GT_PCI0_ICMASK:
        s->regs[saddr] = val & 0x03fffffe;
        trace_gt64120_write_intreg("ICMASK", size, val);
        break;
    case GT_PCI0_SERR0MASK:
        s->regs[saddr] = val & 0x0000003f;
        trace_gt64120_write_intreg("SERR0MASK", size, val);
        break;

    /* Reserved when only PCI_0 is configured. */
    case GT_HINTRCAUSE:
    case GT_CPU_INTSEL:
    case GT_PCI0_INTSEL:
    case GT_HINTRMASK:
    case GT_PCI0_HICMASK:
    case GT_PCI1_SERR1MASK:
        /* not implemented */
        break;

    /* SDRAM Parameters */
    case GT_SDRAM_B0:
    case GT_SDRAM_B1:
    case GT_SDRAM_B2:
    case GT_SDRAM_B3:
        /*
         * We don't simulate electrical parameters of the SDRAM.
         * Accept, but ignore the values.
         */
        s->regs[saddr] = val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gt64120: Illegal register write "
                      "reg:0x%03x size:%u value:0x%0*" PRIx64 "\n",
                      saddr << 2, size, size << 1, val);
        break;
    }
}

static uint64_t gt64120_readl(void *opaque,
                              hwaddr addr, unsigned size)
{
    GT64120State *s = opaque;
    uint32_t val;
    uint32_t saddr = addr >> 2;

    switch (saddr) {

    /* CPU Configuration */
    case GT_MULTI:
        /*
         * Only one GT64xxx is present on the CPU bus, return
         * the initial value.
         */
        val = s->regs[saddr];
        break;

    /* CPU Error Report */
    case GT_CPUERR_ADDRLO:
    case GT_CPUERR_ADDRHI:
    case GT_CPUERR_DATALO:
    case GT_CPUERR_DATAHI:
    case GT_CPUERR_PARITY:
        /* Emulated memory has no error, always return the initial values. */
        val = s->regs[saddr];
        break;

    /* CPU Sync Barrier */
    case GT_PCI0SYNC:
    case GT_PCI1SYNC:
        /*
         * Reading those register should empty all FIFO on the PCI
         * bus, which are not emulated. The return value should be
         * a random value that should be ignored.
         */
        val = 0xc000ffee;
        break;

    /* ECC */
    case GT_ECC_ERRDATALO:
    case GT_ECC_ERRDATAHI:
    case GT_ECC_MEM:
    case GT_ECC_CALC:
    case GT_ECC_ERRADDR:
        /* Emulated memory has no error, always return the initial values. */
        val = s->regs[saddr];
        break;

    case GT_CPU:
    case GT_SCS10LD:
    case GT_SCS10HD:
    case GT_SCS32LD:
    case GT_SCS32HD:
    case GT_CS20LD:
    case GT_CS20HD:
    case GT_CS3BOOTLD:
    case GT_CS3BOOTHD:
    case GT_SCS10AR:
    case GT_SCS32AR:
    case GT_CS20R:
    case GT_CS3BOOTR:
    case GT_PCI0IOLD:
    case GT_PCI0M0LD:
    case GT_PCI0M1LD:
    case GT_PCI1IOLD:
    case GT_PCI1M0LD:
    case GT_PCI1M1LD:
    case GT_PCI0IOHD:
    case GT_PCI0M0HD:
    case GT_PCI0M1HD:
    case GT_PCI1IOHD:
    case GT_PCI1M0HD:
    case GT_PCI1M1HD:
    case GT_PCI0IOREMAP:
    case GT_PCI0M0REMAP:
    case GT_PCI0M1REMAP:
    case GT_PCI1IOREMAP:
    case GT_PCI1M0REMAP:
    case GT_PCI1M1REMAP:
    case GT_ISD:
        val = s->regs[saddr];
        break;
    case GT_PCI0_IACK:
        /* Read the IRQ number */
        val = pic_read_irq(isa_pic);
        break;

    /* SDRAM and Device Address Decode */
    case GT_SCS0LD:
    case GT_SCS0HD:
    case GT_SCS1LD:
    case GT_SCS1HD:
    case GT_SCS2LD:
    case GT_SCS2HD:
    case GT_SCS3LD:
    case GT_SCS3HD:
    case GT_CS0LD:
    case GT_CS0HD:
    case GT_CS1LD:
    case GT_CS1HD:
    case GT_CS2LD:
    case GT_CS2HD:
    case GT_CS3LD:
    case GT_CS3HD:
    case GT_BOOTLD:
    case GT_BOOTHD:
    case GT_ADERR:
        val = s->regs[saddr];
        break;

    /* SDRAM Configuration */
    case GT_SDRAM_CFG:
    case GT_SDRAM_OPMODE:
    case GT_SDRAM_BM:
    case GT_SDRAM_ADDRDECODE:
        val = s->regs[saddr];
        break;

    /* SDRAM Parameters */
    case GT_SDRAM_B0:
    case GT_SDRAM_B1:
    case GT_SDRAM_B2:
    case GT_SDRAM_B3:
        /*
         * We don't simulate electrical parameters of the SDRAM.
         * Just return the last written value.
         */
        val = s->regs[saddr];
        break;

    /* Device Parameters */
    case GT_DEV_B0:
    case GT_DEV_B1:
    case GT_DEV_B2:
    case GT_DEV_B3:
    case GT_DEV_BOOT:
        val = s->regs[saddr];
        break;

    /* DMA Record */
    case GT_DMA0_CNT:
    case GT_DMA1_CNT:
    case GT_DMA2_CNT:
    case GT_DMA3_CNT:
    case GT_DMA0_SA:
    case GT_DMA1_SA:
    case GT_DMA2_SA:
    case GT_DMA3_SA:
    case GT_DMA0_DA:
    case GT_DMA1_DA:
    case GT_DMA2_DA:
    case GT_DMA3_DA:
    case GT_DMA0_NEXT:
    case GT_DMA1_NEXT:
    case GT_DMA2_NEXT:
    case GT_DMA3_NEXT:
    case GT_DMA0_CUR:
    case GT_DMA1_CUR:
    case GT_DMA2_CUR:
    case GT_DMA3_CUR:
        val = s->regs[saddr];
        break;

    /* DMA Channel Control */
    case GT_DMA0_CTRL:
    case GT_DMA1_CTRL:
    case GT_DMA2_CTRL:
    case GT_DMA3_CTRL:
        val = s->regs[saddr];
        break;

    /* DMA Arbiter */
    case GT_DMA_ARB:
        val = s->regs[saddr];
        break;

    /* Timer/Counter */
    case GT_TC0:
    case GT_TC1:
    case GT_TC2:
    case GT_TC3:
    case GT_TC_CONTROL:
        val = s->regs[saddr];
        break;

    /* PCI Internal */
    case GT_PCI0_CFGADDR:
    case GT_PCI0_CFGDATA:
        /* Mapped via in gt64120_pci_mapping() */
        g_assert_not_reached();
        break;

    case GT_PCI0_CMD:
    case GT_PCI0_TOR:
    case GT_PCI0_BS_SCS10:
    case GT_PCI0_BS_SCS32:
    case GT_PCI0_BS_CS20:
    case GT_PCI0_BS_CS3BT:
    case GT_PCI1_IACK:
    case GT_PCI0_BARE:
    case GT_PCI0_PREFMBR:
    case GT_PCI0_SCS10_BAR:
    case GT_PCI0_SCS32_BAR:
    case GT_PCI0_CS20_BAR:
    case GT_PCI0_CS3BT_BAR:
    case GT_PCI0_SSCS10_BAR:
    case GT_PCI0_SSCS32_BAR:
    case GT_PCI0_SCS3BT_BAR:
    case GT_PCI1_CMD:
    case GT_PCI1_TOR:
    case GT_PCI1_BS_SCS10:
    case GT_PCI1_BS_SCS32:
    case GT_PCI1_BS_CS20:
    case GT_PCI1_BS_CS3BT:
    case GT_PCI1_BARE:
    case GT_PCI1_PREFMBR:
    case GT_PCI1_SCS10_BAR:
    case GT_PCI1_SCS32_BAR:
    case GT_PCI1_CS20_BAR:
    case GT_PCI1_CS3BT_BAR:
    case GT_PCI1_SSCS10_BAR:
    case GT_PCI1_SSCS32_BAR:
    case GT_PCI1_SCS3BT_BAR:
    case GT_PCI1_CFGADDR:
    case GT_PCI1_CFGDATA:
        val = s->regs[saddr];
        break;

    /* Interrupts */
    case GT_INTRCAUSE:
        val = s->regs[saddr];
        trace_gt64120_read_intreg("INTRCAUSE", size, val);
        break;
    case GT_INTRMASK:
        val = s->regs[saddr];
        trace_gt64120_read_intreg("INTRMASK", size, val);
        break;
    case GT_PCI0_ICMASK:
        val = s->regs[saddr];
        trace_gt64120_read_intreg("ICMASK", size, val);
        break;
    case GT_PCI0_SERR0MASK:
        val = s->regs[saddr];
        trace_gt64120_read_intreg("SERR0MASK", size, val);
        break;

    /* Reserved when only PCI_0 is configured. */
    case GT_HINTRCAUSE:
    case GT_CPU_INTSEL:
    case GT_PCI0_INTSEL:
    case GT_HINTRMASK:
    case GT_PCI0_HICMASK:
    case GT_PCI1_SERR1MASK:
        val = s->regs[saddr];
        break;

    default:
        val = s->regs[saddr];
        qemu_log_mask(LOG_GUEST_ERROR,
                      "gt64120: Illegal register read "
                      "reg:0x%03x size:%u value:0x%0*x\n",
                      saddr << 2, size, size << 1, val);
        break;
    }

    if (!(s->regs[GT_CPU] & 0x00001000)) {
        val = bswap32(val);
    }
    trace_gt64120_read(addr, val);

    return val;
}

static const MemoryRegionOps isd_mem_ops = {
    .read = gt64120_readl,
    .write = gt64120_writel,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void gt64120_reset(DeviceState *dev)
{
    GT64120State *s = GT64120_PCI_HOST_BRIDGE(dev);

    /* FIXME: Malta specific hw assumptions ahead */

    /* CPU Configuration */
    s->regs[GT_CPU] = s->cpu_little_endian ? R_GT_CPU_Endianness_MASK : 0;
    s->regs[GT_MULTI]         = 0x00000003;

    /* CPU Address decode */
    s->regs[GT_SCS10LD]       = 0x00000000;
    s->regs[GT_SCS10HD]       = 0x00000007;
    s->regs[GT_SCS32LD]       = 0x00000008;
    s->regs[GT_SCS32HD]       = 0x0000000f;
    s->regs[GT_CS20LD]        = 0x000000e0;
    s->regs[GT_CS20HD]        = 0x00000070;
    s->regs[GT_CS3BOOTLD]     = 0x000000f8;
    s->regs[GT_CS3BOOTHD]     = 0x0000007f;

    s->regs[GT_PCI0IOLD]      = 0x00000080;
    s->regs[GT_PCI0IOHD]      = 0x0000000f;
    s->regs[GT_PCI0M0LD]      = 0x00000090;
    s->regs[GT_PCI0M0HD]      = 0x0000001f;
    s->regs[GT_ISD]           = 0x000000a0;
    s->regs[GT_PCI0M1LD]      = 0x00000790;
    s->regs[GT_PCI0M1HD]      = 0x0000001f;
    s->regs[GT_PCI1IOLD]      = 0x00000100;
    s->regs[GT_PCI1IOHD]      = 0x0000000f;
    s->regs[GT_PCI1M0LD]      = 0x00000110;
    s->regs[GT_PCI1M0HD]      = 0x0000001f;
    s->regs[GT_PCI1M1LD]      = 0x00000120;
    s->regs[GT_PCI1M1HD]      = 0x0000002f;

    s->regs[GT_SCS10AR]       = 0x00000000;
    s->regs[GT_SCS32AR]       = 0x00000008;
    s->regs[GT_CS20R]         = 0x000000e0;
    s->regs[GT_CS3BOOTR]      = 0x000000f8;

    s->regs[GT_PCI0IOREMAP]   = 0x00000080;
    s->regs[GT_PCI0M0REMAP]   = 0x00000090;
    s->regs[GT_PCI0M1REMAP]   = 0x00000790;
    s->regs[GT_PCI1IOREMAP]   = 0x00000100;
    s->regs[GT_PCI1M0REMAP]   = 0x00000110;
    s->regs[GT_PCI1M1REMAP]   = 0x00000120;

    /* CPU Error Report */
    s->regs[GT_CPUERR_ADDRLO] = 0x00000000;
    s->regs[GT_CPUERR_ADDRHI] = 0x00000000;
    s->regs[GT_CPUERR_DATALO] = 0xffffffff;
    s->regs[GT_CPUERR_DATAHI] = 0xffffffff;
    s->regs[GT_CPUERR_PARITY] = 0x000000ff;

    /* CPU Sync Barrier */
    s->regs[GT_PCI0SYNC]      = 0x00000000;
    s->regs[GT_PCI1SYNC]      = 0x00000000;

    /* SDRAM and Device Address Decode */
    s->regs[GT_SCS0LD]        = 0x00000000;
    s->regs[GT_SCS0HD]        = 0x00000007;
    s->regs[GT_SCS1LD]        = 0x00000008;
    s->regs[GT_SCS1HD]        = 0x0000000f;
    s->regs[GT_SCS2LD]        = 0x00000010;
    s->regs[GT_SCS2HD]        = 0x00000017;
    s->regs[GT_SCS3LD]        = 0x00000018;
    s->regs[GT_SCS3HD]        = 0x0000001f;
    s->regs[GT_CS0LD]         = 0x000000c0;
    s->regs[GT_CS0HD]         = 0x000000c7;
    s->regs[GT_CS1LD]         = 0x000000c8;
    s->regs[GT_CS1HD]         = 0x000000cf;
    s->regs[GT_CS2LD]         = 0x000000d0;
    s->regs[GT_CS2HD]         = 0x000000df;
    s->regs[GT_CS3LD]         = 0x000000f0;
    s->regs[GT_CS3HD]         = 0x000000fb;
    s->regs[GT_BOOTLD]        = 0x000000fc;
    s->regs[GT_BOOTHD]        = 0x000000ff;
    s->regs[GT_ADERR]         = 0xffffffff;

    /* SDRAM Configuration */
    s->regs[GT_SDRAM_CFG]     = 0x00000200;
    s->regs[GT_SDRAM_OPMODE]  = 0x00000000;
    s->regs[GT_SDRAM_BM]      = 0x00000007;
    s->regs[GT_SDRAM_ADDRDECODE] = 0x00000002;

    /* SDRAM Parameters */
    s->regs[GT_SDRAM_B0]      = 0x00000005;
    s->regs[GT_SDRAM_B1]      = 0x00000005;
    s->regs[GT_SDRAM_B2]      = 0x00000005;
    s->regs[GT_SDRAM_B3]      = 0x00000005;

    /* ECC */
    s->regs[GT_ECC_ERRDATALO] = 0x00000000;
    s->regs[GT_ECC_ERRDATAHI] = 0x00000000;
    s->regs[GT_ECC_MEM]       = 0x00000000;
    s->regs[GT_ECC_CALC]      = 0x00000000;
    s->regs[GT_ECC_ERRADDR]   = 0x00000000;

    /* Device Parameters */
    s->regs[GT_DEV_B0]        = 0x386fffff;
    s->regs[GT_DEV_B1]        = 0x386fffff;
    s->regs[GT_DEV_B2]        = 0x386fffff;
    s->regs[GT_DEV_B3]        = 0x386fffff;
    s->regs[GT_DEV_BOOT]      = 0x146fffff;

    /* DMA registers are all zeroed at reset */

    /* Timer/Counter */
    s->regs[GT_TC0]           = 0xffffffff;
    s->regs[GT_TC1]           = 0x00ffffff;
    s->regs[GT_TC2]           = 0x00ffffff;
    s->regs[GT_TC3]           = 0x00ffffff;
    s->regs[GT_TC_CONTROL]    = 0x00000000;

    /* PCI Internal */
    s->regs[GT_PCI0_CMD] = s->cpu_little_endian ? R_GT_PCI0_CMD_ByteSwap_MASK : 0;
    s->regs[GT_PCI0_TOR]      = 0x0000070f;
    s->regs[GT_PCI0_BS_SCS10] = 0x00fff000;
    s->regs[GT_PCI0_BS_SCS32] = 0x00fff000;
    s->regs[GT_PCI0_BS_CS20]  = 0x01fff000;
    s->regs[GT_PCI0_BS_CS3BT] = 0x00fff000;
    s->regs[GT_PCI1_IACK]     = 0x00000000;
    s->regs[GT_PCI0_IACK]     = 0x00000000;
    s->regs[GT_PCI0_BARE]     = 0x0000000f;
    s->regs[GT_PCI0_PREFMBR]  = 0x00000040;
    s->regs[GT_PCI0_SCS10_BAR] = 0x00000000;
    s->regs[GT_PCI0_SCS32_BAR] = 0x01000000;
    s->regs[GT_PCI0_CS20_BAR] = 0x1c000000;
    s->regs[GT_PCI0_CS3BT_BAR] = 0x1f000000;
    s->regs[GT_PCI0_SSCS10_BAR] = 0x00000000;
    s->regs[GT_PCI0_SSCS32_BAR] = 0x01000000;
    s->regs[GT_PCI0_SCS3BT_BAR] = 0x1f000000;
    s->regs[GT_PCI1_CMD] = s->cpu_little_endian ? R_GT_PCI1_CMD_ByteSwap_MASK : 0;
    s->regs[GT_PCI1_TOR]      = 0x0000070f;
    s->regs[GT_PCI1_BS_SCS10] = 0x00fff000;
    s->regs[GT_PCI1_BS_SCS32] = 0x00fff000;
    s->regs[GT_PCI1_BS_CS20]  = 0x01fff000;
    s->regs[GT_PCI1_BS_CS3BT] = 0x00fff000;
    s->regs[GT_PCI1_BARE]     = 0x0000000f;
    s->regs[GT_PCI1_PREFMBR]  = 0x00000040;
    s->regs[GT_PCI1_SCS10_BAR] = 0x00000000;
    s->regs[GT_PCI1_SCS32_BAR] = 0x01000000;
    s->regs[GT_PCI1_CS20_BAR] = 0x1c000000;
    s->regs[GT_PCI1_CS3BT_BAR] = 0x1f000000;
    s->regs[GT_PCI1_SSCS10_BAR] = 0x00000000;
    s->regs[GT_PCI1_SSCS32_BAR] = 0x01000000;
    s->regs[GT_PCI1_SCS3BT_BAR] = 0x1f000000;
    s->regs[GT_PCI1_CFGADDR]  = 0x00000000;
    s->regs[GT_PCI1_CFGDATA]  = 0x00000000;
    s->regs[GT_PCI0_CFGADDR]  = 0x00000000;

    /* Interrupt registers are all zeroed at reset */

    gt64120_isd_mapping(s);
    gt64120_pci_mapping(s);
    gt64120_update_pci_cfgdata_mapping(s);
}

static void gt64120_realize(DeviceState *dev, Error **errp)
{
    GT64120State *s = GT64120_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&s->ISD_mem, OBJECT(dev), &isd_mem_ops, s,
                          "gt64120-isd", 0x1000);
    memory_region_init(&s->pci0_mem, OBJECT(dev), "pci0-mem", 4 * GiB);
    address_space_init(&s->pci0_mem_as, &s->pci0_mem, "pci0-mem");
    phb->bus = pci_root_bus_new(dev, "pci",
                                &s->pci0_mem,
                                get_system_io(),
                                PCI_DEVFN(18, 0), TYPE_PCI_BUS);

    pci_create_simple(phb->bus, PCI_DEVFN(0, 0), "gt64120_pci");
    memory_region_init_io(&phb->conf_mem, OBJECT(phb),
                          &pci_host_conf_le_ops,
                          s, "pci-conf-idx", 4);
    memory_region_add_subregion_overlap(&s->ISD_mem, GT_PCI0_CFGADDR << 2,
                                        &phb->conf_mem, 1);


    /*
     * The whole address space decoded by the GT-64120A doesn't generate
     * exception when accessing invalid memory. Create an empty slot to
     * emulate this feature.
     */
    empty_slot_init("GT64120", 0, 0x20000000);
}

static void gt64120_pci_realize(PCIDevice *d, Error **errp)
{
    /* FIXME: Malta specific hw assumptions ahead */
    pci_set_word(d->config + PCI_COMMAND, 0);
    pci_set_word(d->config + PCI_STATUS,
                 PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);
    pci_config_set_prog_interface(d->config, 0);
    pci_set_long(d->config + PCI_BASE_ADDRESS_0, 0x00000008);
    pci_set_long(d->config + PCI_BASE_ADDRESS_1, 0x01000008);
    pci_set_long(d->config + PCI_BASE_ADDRESS_2, 0x1c000000);
    pci_set_long(d->config + PCI_BASE_ADDRESS_3, 0x1f000000);
    pci_set_long(d->config + PCI_BASE_ADDRESS_4, 0x14000000);
    pci_set_long(d->config + PCI_BASE_ADDRESS_5, 0x14000001);
    pci_set_byte(d->config + 0x3d, 0x01);
}

static void gt64120_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = gt64120_pci_realize;
    k->vendor_id = PCI_VENDOR_ID_MARVELL;
    k->device_id = PCI_DEVICE_ID_MARVELL_GT6412X;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo gt64120_pci_info = {
    .name          = "gt64120_pci",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = gt64120_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static Property gt64120_properties[] = {
    DEFINE_PROP_BOOL("cpu-little-endian", GT64120State,
                     cpu_little_endian, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void gt64120_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    device_class_set_props(dc, gt64120_properties);
    dc->realize = gt64120_realize;
    dc->reset = gt64120_reset;
    dc->vmsd = &vmstate_gt64120;
}

static const TypeInfo gt64120_info = {
    .name          = TYPE_GT64120_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(GT64120State),
    .class_init    = gt64120_class_init,
};

static void gt64120_pci_register_types(void)
{
    type_register_static(&gt64120_info);
    type_register_static(&gt64120_pci_info);
}

type_init(gt64120_pci_register_types)
