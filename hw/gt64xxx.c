/*
 * QEMU GT64120 PCI host
 *
 * Copyright (c) 2006 Aurelien Jarno
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

#include "vl.h"
typedef target_phys_addr_t pci_addr_t;
#include "pci_host.h"

#define GT_REGS			(0x1000 >> 2)

/* CPU Configuration */
#define GT_CPU    		(0x000 >> 2)
#define GT_MULTI    		(0x120 >> 2)

/* CPU Address Decode */
#define GT_SCS10LD    		(0x008 >> 2)
#define GT_SCS10HD    		(0x010 >> 2)
#define GT_SCS32LD    		(0x018 >> 2)
#define GT_SCS32HD    		(0x020 >> 2)
#define GT_CS20LD    		(0x028 >> 2)
#define GT_CS20HD    		(0x030 >> 2)
#define GT_CS3BOOTLD    	(0x038 >> 2)
#define GT_CS3BOOTHD    	(0x040 >> 2)
#define GT_PCI0IOLD    		(0x048 >> 2)
#define GT_PCI0IOHD    		(0x050 >> 2)
#define GT_PCI0M0LD    		(0x058 >> 2)
#define GT_PCI0M0HD    		(0x060 >> 2)
#define GT_ISD    		(0x068 >> 2)

#define GT_PCI0M1LD    		(0x080 >> 2)
#define GT_PCI0M1HD    		(0x088 >> 2)
#define GT_PCI1IOLD    		(0x090 >> 2)
#define GT_PCI1IOHD    		(0x098 >> 2)
#define GT_PCI1M0LD    		(0x0a0 >> 2)
#define GT_PCI1M0HD    		(0x0a8 >> 2)
#define GT_PCI1M1LD    		(0x0b0 >> 2)
#define GT_PCI1M1HD    		(0x0b8 >> 2)
#define GT_PCI1M1LD    		(0x0b0 >> 2)
#define GT_PCI1M1HD    		(0x0b8 >> 2)

#define GT_SCS10AR    		(0x0d0 >> 2)
#define GT_SCS32AR    		(0x0d8 >> 2)
#define GT_CS20R    		(0x0e0 >> 2)
#define GT_CS3BOOTR    		(0x0e8 >> 2)

#define GT_PCI0IOREMAP    	(0x0f0 >> 2)
#define GT_PCI0M0REMAP    	(0x0f8 >> 2)
#define GT_PCI0M1REMAP    	(0x100 >> 2)
#define GT_PCI1IOREMAP    	(0x108 >> 2)
#define GT_PCI1M0REMAP    	(0x110 >> 2)
#define GT_PCI1M1REMAP    	(0x118 >> 2)

/* CPU Error Report */
#define GT_CPUERR_ADDRLO    	(0x070 >> 2)
#define GT_CPUERR_ADDRHI    	(0x078 >> 2)
#define GT_CPUERR_DATALO    	(0x128 >> 2)		/* GT-64120A only  */
#define GT_CPUERR_DATAHI    	(0x130 >> 2)		/* GT-64120A only  */
#define GT_CPUERR_PARITY    	(0x138 >> 2)		/* GT-64120A only  */

/* CPU Sync Barrier */
#define GT_PCI0SYNC    		(0x0c0 >> 2)
#define GT_PCI1SYNC    		(0x0c8 >> 2)

/* SDRAM and Device Address Decode */
#define GT_SCS0LD    		(0x400 >> 2)
#define GT_SCS0HD    		(0x404 >> 2)
#define GT_SCS1LD    		(0x408 >> 2)
#define GT_SCS1HD    		(0x40c >> 2)
#define GT_SCS2LD    		(0x410 >> 2)
#define GT_SCS2HD    		(0x414 >> 2)
#define GT_SCS3LD    		(0x418 >> 2)
#define GT_SCS3HD    		(0x41c >> 2)
#define GT_CS0LD    		(0x420 >> 2)
#define GT_CS0HD    		(0x424 >> 2)
#define GT_CS1LD    		(0x428 >> 2)
#define GT_CS1HD    		(0x42c >> 2)
#define GT_CS2LD    		(0x430 >> 2)
#define GT_CS2HD    		(0x434 >> 2)
#define GT_CS3LD    		(0x438 >> 2)
#define GT_CS3HD    		(0x43c >> 2)
#define GT_BOOTLD    		(0x440 >> 2)
#define GT_BOOTHD    		(0x444 >> 2)
#define GT_ADERR    		(0x470 >> 2)

/* SDRAM Configuration */
#define GT_SDRAM_CFG    	(0x448 >> 2)
#define GT_SDRAM_OPMODE    	(0x474 >> 2)
#define GT_SDRAM_BM    		(0x478 >> 2)
#define GT_SDRAM_ADDRDECODE    	(0x47c >> 2)

/* SDRAM Parameters */
#define GT_SDRAM_B0    		(0x44c >> 2)
#define GT_SDRAM_B1    		(0x450 >> 2)
#define GT_SDRAM_B2    		(0x454 >> 2)
#define GT_SDRAM_B3    		(0x458 >> 2)

/* Device Parameters */
#define GT_DEV_B0    		(0x45c >> 2)
#define GT_DEV_B1    		(0x460 >> 2)
#define GT_DEV_B2    		(0x464 >> 2)
#define GT_DEV_B3    		(0x468 >> 2)
#define GT_DEV_BOOT    		(0x46c >> 2)

/* ECC */
#define GT_ECC_ERRDATALO	(0x480 >> 2)		/* GT-64120A only  */
#define GT_ECC_ERRDATAHI	(0x484 >> 2)		/* GT-64120A only  */
#define GT_ECC_MEM		(0x488 >> 2)		/* GT-64120A only  */
#define GT_ECC_CALC		(0x48c >> 2)		/* GT-64120A only  */
#define GT_ECC_ERRADDR		(0x490 >> 2)		/* GT-64120A only  */

/* DMA Record */
#define GT_DMA0_CNT    		(0x800 >> 2)
#define GT_DMA1_CNT    		(0x804 >> 2)
#define GT_DMA2_CNT    		(0x808 >> 2)
#define GT_DMA3_CNT    		(0x80c >> 2)
#define GT_DMA0_SA    		(0x810 >> 2)
#define GT_DMA1_SA    		(0x814 >> 2)
#define GT_DMA2_SA    		(0x818 >> 2)
#define GT_DMA3_SA    		(0x81c >> 2)
#define GT_DMA0_DA    		(0x820 >> 2)
#define GT_DMA1_DA    		(0x824 >> 2)
#define GT_DMA2_DA    		(0x828 >> 2)
#define GT_DMA3_DA    		(0x82c >> 2)
#define GT_DMA0_NEXT    	(0x830 >> 2)
#define GT_DMA1_NEXT    	(0x834 >> 2)
#define GT_DMA2_NEXT    	(0x838 >> 2)
#define GT_DMA3_NEXT    	(0x83c >> 2)
#define GT_DMA0_CUR    		(0x870 >> 2)
#define GT_DMA1_CUR    		(0x874 >> 2)
#define GT_DMA2_CUR    		(0x878 >> 2)
#define GT_DMA3_CUR    		(0x87c >> 2)

/* DMA Channel Control */
#define GT_DMA0_CTRL    	(0x840 >> 2)
#define GT_DMA1_CTRL    	(0x844 >> 2)
#define GT_DMA2_CTRL    	(0x848 >> 2)
#define GT_DMA3_CTRL    	(0x84c >> 2)

/* DMA Arbiter */
#define GT_DMA_ARB    		(0x860 >> 2)

/* Timer/Counter */
#define GT_TC0    		(0x850 >> 2)
#define GT_TC1    		(0x854 >> 2)
#define GT_TC2    		(0x858 >> 2)
#define GT_TC3    		(0x85c >> 2)
#define GT_TC_CONTROL    	(0x864 >> 2)

/* PCI Internal */
#define GT_PCI0_CMD    		(0xc00 >> 2)
#define GT_PCI0_TOR    		(0xc04 >> 2)
#define GT_PCI0_BS_SCS10    	(0xc08 >> 2)
#define GT_PCI0_BS_SCS32    	(0xc0c >> 2)
#define GT_PCI0_BS_CS20    	(0xc10 >> 2)
#define GT_PCI0_BS_CS3BT    	(0xc14 >> 2)
#define GT_PCI1_IACK    	(0xc30 >> 2)
#define GT_PCI0_IACK    	(0xc34 >> 2)
#define GT_PCI0_BARE    	(0xc3c >> 2)
#define GT_PCI0_PREFMBR    	(0xc40 >> 2)
#define GT_PCI0_SCS10_BAR    	(0xc48 >> 2)
#define GT_PCI0_SCS32_BAR    	(0xc4c >> 2)
#define GT_PCI0_CS20_BAR    	(0xc50 >> 2)
#define GT_PCI0_CS3BT_BAR    	(0xc54 >> 2)
#define GT_PCI0_SSCS10_BAR    	(0xc58 >> 2)
#define GT_PCI0_SSCS32_BAR    	(0xc5c >> 2)
#define GT_PCI0_SCS3BT_BAR    	(0xc64 >> 2)
#define GT_PCI1_CMD    		(0xc80 >> 2)
#define GT_PCI1_TOR    		(0xc84 >> 2)
#define GT_PCI1_BS_SCS10    	(0xc88 >> 2)
#define GT_PCI1_BS_SCS32    	(0xc8c >> 2)
#define GT_PCI1_BS_CS20    	(0xc90 >> 2)
#define GT_PCI1_BS_CS3BT    	(0xc94 >> 2)
#define GT_PCI1_BARE    	(0xcbc >> 2)
#define GT_PCI1_PREFMBR    	(0xcc0 >> 2)
#define GT_PCI1_SCS10_BAR    	(0xcc8 >> 2)
#define GT_PCI1_SCS32_BAR    	(0xccc >> 2)
#define GT_PCI1_CS20_BAR    	(0xcd0 >> 2)
#define GT_PCI1_CS3BT_BAR    	(0xcd4 >> 2)
#define GT_PCI1_SSCS10_BAR    	(0xcd8 >> 2)
#define GT_PCI1_SSCS32_BAR    	(0xcdc >> 2)
#define GT_PCI1_SCS3BT_BAR    	(0xce4 >> 2)
#define GT_PCI1_CFGADDR    	(0xcf0 >> 2)
#define GT_PCI1_CFGDATA    	(0xcf4 >> 2)
#define GT_PCI0_CFGADDR    	(0xcf8 >> 2)
#define GT_PCI0_CFGDATA    	(0xcfc >> 2)

/* Interrupts */
#define GT_INTRCAUSE    	(0xc18 >> 2)
#define GT_INTRMASK    		(0xc1c >> 2)
#define GT_PCI0_ICMASK    	(0xc24 >> 2)
#define GT_PCI0_SERR0MASK    	(0xc28 >> 2)
#define GT_CPU_INTSEL    	(0xc70 >> 2)
#define GT_PCI0_INTSEL    	(0xc74 >> 2)
#define GT_HINTRCAUSE    	(0xc98 >> 2)
#define GT_HINTRMASK    	(0xc9c >> 2)
#define GT_PCI0_HICMASK    	(0xca4 >> 2)
#define GT_PCI1_SERR1MASK    	(0xca8 >> 2)

//~ #define DEBUG

#if defined(DEBUG)
#define logout(fmt, args...) fprintf(stderr, "GT64XXX\t%-24s" fmt, __func__, ##args)
#else
#define logout(fmt, args...) ((void)0)
#endif

typedef PCIHostState GT64120PCIState;

typedef struct GT64120State {
    GT64120PCIState *pci;
    uint32_t regs[GT_REGS];
} GT64120State;

static void gt64120_pci_mapping(GT64120State *s)
{
    target_phys_addr_t start, length;		   

    /* Update IO mapping */
    start = s->regs[GT_PCI0IOLD] << 21;
    length = ((s->regs[GT_PCI0IOHD] + 1) - (s->regs[GT_PCI0IOLD] & 0x7f)) << 21;
    logout("start = 0x%08x, length = 0x%08x\n", start, length);
    if (length <= 0x10000000)
        isa_mmio_init(start, length);
}

static void gt64120_writel (void *opaque, target_phys_addr_t addr,
                            uint32_t val)
{
    GT64120State *s = opaque;
    uint32_t saddr;

#ifdef TARGET_WORDS_BIGENDIAN
    val = bswap32(val);
#endif

    saddr = (addr & 0xfff) >> 2;
    logout("addr = 0x%08x, val = 0x%08x\n", saddr, val);

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
        gt64120_pci_mapping(s);
        break;
    case GT_PCI1M0LD:
        s->regs[GT_PCI1M0LD]    = val & 0x00007fff;
        s->regs[GT_PCI1M0REMAP] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI1M1LD:
        s->regs[GT_PCI1M1LD]    = val & 0x00007fff;
        s->regs[GT_PCI1M1REMAP] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI0IOHD:
    case GT_PCI0M0HD:
    case GT_PCI0M1HD:
    case GT_PCI1IOHD:
    case GT_PCI1M0HD:
    case GT_PCI1M1HD:
        s->regs[saddr] = val & 0x0000007f;
        gt64120_pci_mapping(s);
        break;
    case GT_PCI0IOREMAP:
    case GT_PCI0M0REMAP:
    case GT_PCI0M1REMAP:
    case GT_PCI1IOREMAP:
    case GT_PCI1M0REMAP:
    case GT_PCI1M1REMAP:
        s->regs[saddr] = val & 0x000007ff;
        gt64120_pci_mapping(s);
        break;

    /* CPU Error Report */
    case GT_CPUERR_ADDRLO:
    case GT_CPUERR_ADDRHI:
    case GT_CPUERR_DATALO:
    case GT_CPUERR_DATAHI:
    case GT_CPUERR_PARITY:
	/* Read-only registers, do nothing */
        break;

    /* CPU Sync Barrier */
    case GT_PCI0SYNC:
    case GT_PCI1SYNC:
	/* Read-only registers, do nothing */
        break;

    /* ECC */
    case GT_ECC_ERRDATALO:
    case GT_ECC_ERRDATAHI:
    case GT_ECC_MEM:
    case GT_ECC_CALC:
    case GT_ECC_ERRADDR:
        /* Read-only registers, do nothing */
        break;

    /* PCI Internal */
    case GT_PCI0_CMD:
    case GT_PCI1_CMD:
        s->regs[saddr] = val & 0x0401fc0f;
        break;
    case GT_PCI0_CFGADDR:
        s->pci->config_reg = val & 0x80fffffc;
        break;
    case GT_PCI0_CFGDATA:
        pci_host_data_writel(s->pci, 0, val);
        break;

    /* SDRAM Parameters */
    case GT_SDRAM_B0:
    case GT_SDRAM_B1:
    case GT_SDRAM_B2:
    case GT_SDRAM_B3:
        /* We don't simulate electrical parameters of the SDRAM.
           Accept, but ignore the values. */
        s->regs[saddr] = val;
        break;

    default:
#if 0
        printf ("gt64120_writel: Bad register offset 0x%x\n", (int)addr);
#endif
        break;
    }
}

static uint32_t gt64120_readl (void *opaque,
                               target_phys_addr_t addr)
{
    GT64120State *s = opaque;
    uint32_t val;
    uint32_t saddr;

    val = 0;
    saddr = (addr & 0xfff) >> 2;

    switch (saddr) {

    /* CPU Configuration */
    case GT_MULTI:
        /* Only one GT64xxx is present on the CPU bus, return
           the initial value */
        val = s->regs[saddr];
        break;

    /* CPU Error Report */
    case GT_CPUERR_ADDRLO:
    case GT_CPUERR_ADDRHI:
    case GT_CPUERR_DATALO:
    case GT_CPUERR_DATAHI:
    case GT_CPUERR_PARITY:
        /* Emulated memory has no error, always return the initial
           values */ 
        val = s->regs[saddr];
        break;

    /* CPU Sync Barrier */
    case GT_PCI0SYNC:
    case GT_PCI1SYNC:
        /* Reading those register should empty all FIFO on the PCI
           bus, which are not emulated. The return value should be
           a random value that should be ignored. */
        val = 0xc000ffee; 
        break;

    /* ECC */
    case GT_ECC_ERRDATALO:
    case GT_ECC_ERRDATAHI:
    case GT_ECC_MEM:
    case GT_ECC_CALC:
    case GT_ECC_ERRADDR:
        /* Emulated memory has no error, always return the initial
           values */ 
        val = s->regs[saddr];
        break;

    case GT_CPU:
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
    case GT_PCI0_CMD:
    case GT_PCI1_CMD:
    case GT_PCI0IOREMAP:
    case GT_PCI0M0REMAP:
    case GT_PCI0M1REMAP:
    case GT_PCI1IOREMAP:
    case GT_PCI1M0REMAP:
    case GT_PCI1M1REMAP:
        val = s->regs[saddr];
        break;
    case GT_PCI0_IACK:
 	val = pic_intack_read(isa_pic);
        break;

    /* SDRAM Parameters */
    case GT_SDRAM_B0:
    case GT_SDRAM_B1:
    case GT_SDRAM_B2:
    case GT_SDRAM_B3:
        /* We don't simulate electrical parameters of the SDRAM.
           Just return the last written value. */
        val = s->regs[saddr];
        break;

    /* PCI Internal */
    case GT_PCI0_CFGADDR:
        val = s->pci->config_reg;
        break;
    case GT_PCI0_CFGDATA:
        val = pci_host_data_readl(s->pci, 0);
        break;

    default:
        val = s->regs[saddr];
#if 0
        printf ("gt64120_readl: Bad register offset 0x%x\n", (int)addr);
#endif
        break;
    }

    logout("addr = 0x%08x, val = 0x%08x\n", saddr, val);
#ifdef TARGET_WORDS_BIGENDIAN
    return bswap32(val);
#else
    return val;
#endif
}

static CPUWriteMemoryFunc *gt64120_write[] = {
    &gt64120_writel,
    &gt64120_writel,
    &gt64120_writel,
};

static CPUReadMemoryFunc *gt64120_read[] = {
    &gt64120_readl,
    &gt64120_readl,
    &gt64120_readl,
};

static int pci_gt64120_map_irq(PCIDevice *pci_dev, int irq_num)
{
    int slot;

    slot = (pci_dev->devfn >> 3);

    switch (slot) {
      /* PIIX4 USB */
      case 10:
        return 3;
      /* AMD 79C973 Ethernet */
      case 11:
        return 0;
      /* Crystal 4281 Sound */
      case 12:
        return 0;
      /* PCI slot 1 to 4 */
      case 18 ... 21:
        return ((slot - 18) + irq_num) & 0x03;
      /* Unknown device, don't do any translation */
      default:
        return irq_num;
    }
}

extern PCIDevice *piix4_dev;
static int pci_irq_levels[4];

static void pci_gt64120_set_irq(void *pic, int irq_num, int level)
{
    int i, pic_irq, pic_level;

    pci_irq_levels[irq_num] = level;

    /* now we change the pic irq level according to the piix irq mappings */
    /* XXX: optimize */
    pic_irq = piix4_dev->config[0x60 + irq_num];
    if (pic_irq < 16) {
        /* The pic level is the logical OR of all the PCI irqs mapped
           to it */
        pic_level = 0;
        for (i = 0; i < 4; i++) {
            if (pic_irq == piix4_dev->config[0x60 + i])
                pic_level |= pci_irq_levels[i];
        }
        pic_set_irq(pic_irq, pic_level);
    }
}


void gt64120_reset(void *opaque)
{
    GT64120State *s = opaque;

    /* CPU Configuration */
#ifdef TARGET_WORDS_BIGENDIAN
    s->regs[GT_CPU]           = 0x00000000;
#else
    s->regs[GT_CPU]           = 0x00000800;
#endif
    s->regs[GT_MULTI]         = 0x00000000;

    /* CPU Address decode FIXME: not complete*/
    s->regs[GT_PCI0IOLD]      = 0x00000080;
    s->regs[GT_PCI0IOHD]      = 0x0000000f;
    s->regs[GT_PCI0M0LD]      = 0x00000090;
    s->regs[GT_PCI0M0HD]      = 0x0000001f;
    s->regs[GT_PCI0M1LD]      = 0x00000790;
    s->regs[GT_PCI0M1HD]      = 0x0000001f;
    s->regs[GT_PCI1IOLD]      = 0x00000100;
    s->regs[GT_PCI1IOHD]      = 0x0000000f;
    s->regs[GT_PCI1M0LD]      = 0x00000110;
    s->regs[GT_PCI1M0HD]      = 0x0000001f;
    s->regs[GT_PCI1M1LD]      = 0x00000120;
    s->regs[GT_PCI1M1HD]      = 0x0000002f;
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

    /* ECC */
    s->regs[GT_ECC_ERRDATALO] = 0x00000000;
    s->regs[GT_ECC_ERRDATAHI] = 0x00000000;
    s->regs[GT_ECC_MEM]       = 0x00000000;
    s->regs[GT_ECC_CALC]      = 0x00000000;
    s->regs[GT_ECC_ERRADDR]   = 0x00000000;

    /* SDRAM Parameters */
    s->regs[GT_SDRAM_B0]      = 0x00000005;    
    s->regs[GT_SDRAM_B1]      = 0x00000005;    
    s->regs[GT_SDRAM_B2]      = 0x00000005;    
    s->regs[GT_SDRAM_B3]      = 0x00000005;    

    /* PCI Internal FIXME: not complete*/
#ifdef TARGET_WORDS_BIGENDIAN
    s->regs[GT_PCI0_CMD]      = 0x00000000;
    s->regs[GT_PCI1_CMD]      = 0x00000000;
#else
    s->regs[GT_PCI0_CMD]      = 0x00010001;
    s->regs[GT_PCI1_CMD]      = 0x00010001;
#endif
    s->regs[GT_PCI0_IACK]     = 0x00000000;
    s->regs[GT_PCI1_IACK]     = 0x00000000;

    gt64120_pci_mapping(s);
}

PCIBus *pci_gt64120_init(void *pic)
{
    GT64120State *s;
    PCIDevice *d;
    int gt64120;

    s = qemu_mallocz(sizeof(GT64120State));
    s->pci = qemu_mallocz(sizeof(GT64120PCIState));
    gt64120_reset(s);

    s->pci->bus = pci_register_bus(pci_gt64120_set_irq, pci_gt64120_map_irq,
                                   pic, 144, 4);

    gt64120 = cpu_register_io_memory(0, gt64120_read,
                                     gt64120_write, s);
    cpu_register_physical_memory(0x1be00000LL, 0x1000, gt64120);

    d = pci_register_device(s->pci->bus, "GT64120 PCI Bus", sizeof(PCIDevice),
                            0, NULL, NULL);

    d->config[0x00] = 0xab; // vendor_id
    d->config[0x01] = 0x11;
    d->config[0x02] = 0x46; // device_id
    d->config[0x03] = 0x20;
    d->config[0x04] = 0x06;
    d->config[0x05] = 0x00;
    d->config[0x06] = 0x80;
    d->config[0x07] = 0xa2;
    d->config[0x08] = 0x10;
    d->config[0x09] = 0x00;
    d->config[0x0A] = 0x80;
    d->config[0x0B] = 0x05;
    d->config[0x0C] = 0x08;
    d->config[0x0D] = 0x40;
    d->config[0x0E] = 0x00;
    d->config[0x0F] = 0x00;
    d->config[0x17] = 0x08;
    d->config[0x1B] = 0x1c;
    d->config[0x1F] = 0x1f;
    d->config[0x23] = 0x14;
    d->config[0x27] = 0x14;
    d->config[0x3D] = 0x01;

    return s->pci->bus;
}
