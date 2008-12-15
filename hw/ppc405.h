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

#if !defined(PPC_405_H)
#define PPC_405_H

#include "ppc4xx.h"

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
    uint32_t bi_pci_enetaddr2[6];
    uint32_t bi_opbfreq;
    uint32_t bi_iic_fast[2];
};

/* PowerPC 405 core */
ram_addr_t ppc405_set_bootinfo (CPUState *env, ppc4xx_bd_info_t *bd,
                                uint32_t flags);

/* PowerPC 4xx peripheral local bus arbitrer */
void ppc4xx_plb_init (CPUState *env);
/* PLB to OPB bridge */
void ppc4xx_pob_init (CPUState *env);
/* OPB arbitrer */
void ppc4xx_opba_init (CPUState *env, ppc4xx_mmio_t *mmio,
                       target_phys_addr_t offset);
/* Peripheral controller */
void ppc405_ebc_init (CPUState *env);
/* DMA controller */
void ppc405_dma_init (CPUState *env, qemu_irq irqs[4]);
/* GPIO */
void ppc405_gpio_init (CPUState *env, ppc4xx_mmio_t *mmio,
                       target_phys_addr_t offset);
/* Serial ports */
void ppc405_serial_init (CPUState *env, ppc4xx_mmio_t *mmio,
                         target_phys_addr_t offset, qemu_irq irq,
                         CharDriverState *chr);
/* On Chip Memory */
void ppc405_ocm_init (CPUState *env, unsigned long offset);
/* I2C controller */
void ppc405_i2c_init (CPUState *env, ppc4xx_mmio_t *mmio,
                      target_phys_addr_t offset, qemu_irq irq);
/* General purpose timers */
void ppc4xx_gpt_init (CPUState *env, ppc4xx_mmio_t *mmio,
                      target_phys_addr_t offset, qemu_irq irq[5]);
/* Memory access layer */
void ppc405_mal_init (CPUState *env, qemu_irq irqs[4]);
/* PowerPC 405 microcontrollers */
CPUState *ppc405cr_init (target_phys_addr_t ram_bases[4],
                         target_phys_addr_t ram_sizes[4],
                         uint32_t sysclk, qemu_irq **picp,
                         ram_addr_t *offsetp, int do_init);
CPUState *ppc405ep_init (target_phys_addr_t ram_bases[2],
                         target_phys_addr_t ram_sizes[2],
                         uint32_t sysclk, qemu_irq **picp,
                         ram_addr_t *offsetp, int do_init);
/* IBM STBxxx microcontrollers */
CPUState *ppc_stb025_init (target_phys_addr_t ram_bases[2],
                           target_phys_addr_t ram_sizes[2],
                           uint32_t sysclk, qemu_irq **picp,
                           ram_addr_t *offsetp);

#endif /* !defined(PPC_405_H) */
