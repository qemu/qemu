/*
 * QEMU PowerPC 405 shared definitions
 *
 * Copyright (c) 2007 Jocelyn Mayer
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

#ifndef PPC405_H
#define PPC405_H

#include "qom/object.h"
#include "hw/ppc/ppc4xx.h"

#define PPC405EP_SDRAM_BASE 0x00000000
#define PPC405EP_NVRAM_BASE 0xF0000000
#define PPC405EP_FPGA_BASE  0xF0300000
#define PPC405EP_SRAM_BASE  0xFFF00000
#define PPC405EP_SRAM_SIZE  (512 * KiB)
#define PPC405EP_FLASH_BASE 0xFFF80000

/* Bootinfo as set-up by u-boot */
typedef struct ppc4xx_bd_info_t ppc4xx_bd_info_t;
struct ppc4xx_bd_info_t {
    uint32_t bi_memstart;
    uint32_t bi_memsize;
    uint32_t bi_flashstart;
    uint32_t bi_flashsize;
    uint32_t bi_flashoffset; /* 0x10 */
    uint32_t bi_sramstart;
    uint32_t bi_sramsize;
    uint32_t bi_bootflags;
    uint32_t bi_ipaddr; /* 0x20 */
    uint8_t  bi_enetaddr[6];
    uint16_t bi_ethspeed;
    uint32_t bi_intfreq;
    uint32_t bi_busfreq; /* 0x30 */
    uint32_t bi_baudrate;
    uint8_t  bi_s_version[4];
    uint8_t  bi_r_version[32];
    uint32_t bi_procfreq;
    uint32_t bi_plb_busfreq;
    uint32_t bi_pci_busfreq;
    uint8_t  bi_pci_enetaddr[6];
    uint8_t  bi_pci_enetaddr2[6]; /* PPC405EP specific */
    uint32_t bi_opbfreq;
    uint32_t bi_iic_fast[2];
};

/* DMA controller */
#define TYPE_PPC405_DMA "ppc405-dma"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405DmaState, PPC405_DMA);
struct Ppc405DmaState {
    Ppc4xxDcrDeviceState parent_obj;

    qemu_irq irqs[4];
    uint32_t cr[4];
    uint32_t ct[4];
    uint32_t da[4];
    uint32_t sa[4];
    uint32_t sg[4];
    uint32_t sr;
    uint32_t sgc;
    uint32_t slp;
    uint32_t pol;
};

/* GPIO */
#define TYPE_PPC405_GPIO "ppc405-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405GpioState, PPC405_GPIO);
struct Ppc405GpioState {
    SysBusDevice parent_obj;

    MemoryRegion io;
    uint32_t or;
    uint32_t tcr;
    uint32_t osrh;
    uint32_t osrl;
    uint32_t tsrh;
    uint32_t tsrl;
    uint32_t odr;
    uint32_t ir;
    uint32_t rr1;
    uint32_t isr1h;
    uint32_t isr1l;
};

/* On Chip Memory */
#define TYPE_PPC405_OCM "ppc405-ocm"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405OcmState, PPC405_OCM);
struct Ppc405OcmState {
    Ppc4xxDcrDeviceState parent_obj;

    MemoryRegion ram;
    MemoryRegion isarc_ram;
    MemoryRegion dsarc_ram;
    uint32_t isarc;
    uint32_t isacntl;
    uint32_t dsarc;
    uint32_t dsacntl;
};

/* General purpose timers */
#define TYPE_PPC405_GPT "ppc405-gpt"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405GptState, PPC405_GPT);
struct Ppc405GptState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    int64_t tb_offset;
    uint32_t tb_freq;
    QEMUTimer *timer;
    qemu_irq irqs[5];
    uint32_t oe;
    uint32_t ol;
    uint32_t im;
    uint32_t is;
    uint32_t ie;
    uint32_t comp[5];
    uint32_t mask[5];
};

#define TYPE_PPC405_CPC "ppc405-cpc"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405CpcState, PPC405_CPC);

enum {
    PPC405EP_CPU_CLK   = 0,
    PPC405EP_PLB_CLK   = 1,
    PPC405EP_OPB_CLK   = 2,
    PPC405EP_EBC_CLK   = 3,
    PPC405EP_MAL_CLK   = 4,
    PPC405EP_PCI_CLK   = 5,
    PPC405EP_UART0_CLK = 6,
    PPC405EP_UART1_CLK = 7,
    PPC405EP_CLK_NB    = 8,
};

struct Ppc405CpcState {
    Ppc4xxDcrDeviceState parent_obj;

    uint32_t sysclk;
    clk_setup_t clk_setup[PPC405EP_CLK_NB];
    uint32_t boot;
    uint32_t epctl;
    uint32_t pllmr[2];
    uint32_t ucr;
    uint32_t srr;
    uint32_t jtagid;
    uint32_t pci;
    /* Clock and power management */
    uint32_t er;
    uint32_t fr;
    uint32_t sr;
};

#define TYPE_PPC405_SOC "ppc405-soc"
OBJECT_DECLARE_SIMPLE_TYPE(Ppc405SoCState, PPC405_SOC);

struct Ppc405SoCState {
    /* Private */
    DeviceState parent_obj;

    /* Public */
    MemoryRegion ram_banks[2];
    hwaddr ram_bases[2], ram_sizes[2];
    bool do_dram_init;

    MemoryRegion *dram_mr;
    hwaddr ram_size;

    PowerPCCPU cpu;
    DeviceState *uic;
    Ppc405CpcState cpc;
    Ppc405GptState gpt;
    Ppc405OcmState ocm;
    Ppc405GpioState gpio;
    Ppc405DmaState dma;
};

/* PowerPC 405 core */
ram_addr_t ppc405_set_bootinfo(CPUPPCState *env, ram_addr_t ram_size);

void ppc4xx_plb_init(CPUPPCState *env);
void ppc405_ebc_init(CPUPPCState *env);

#endif /* PPC405_H */
