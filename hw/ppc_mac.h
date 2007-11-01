/*
 * QEMU PowerMac emulation shared definitions and prototypes
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
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
#if !defined(__PPC_MAC_H__)
#define __PPC_MAC_H__

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define BIOS_FILENAME "ppc_rom.bin"
#define VGABIOS_FILENAME "video.x"
#define NVRAM_SIZE        0x2000

#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

/* DBDMA */
void dbdma_init (int *dbdma_mem_index);

/* Cuda */
void cuda_init (int *cuda_mem_index, qemu_irq irq);

/* MacIO */
void macio_init (PCIBus *bus, int device_id, int is_oldworld, int pic_mem_index,
                 int dbdma_mem_index, int cuda_mem_index, int nvram_mem_index,
                 int nb_ide, int *ide_mem_index);

/* NewWorld PowerMac IDE */
int pmac_ide_init (BlockDriverState **hd_table, qemu_irq irq);

/* Heathrow PIC */
qemu_irq *heathrow_pic_init(int *pmem_index,
                            int nb_cpus, qemu_irq **irqs);

/* Grackle PCI */
PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic);

/* UniNorth PCI */
PCIBus *pci_pmac_init(qemu_irq *pic);

/* Mac NVRAM */
typedef struct MacIONVRAMState MacIONVRAMState;

MacIONVRAMState *macio_nvram_init (int *mem_index);
void pmac_format_nvram_partition (MacIONVRAMState *nvr, int len);
uint32_t macio_nvram_read (void *opaque, uint32_t addr);
void macio_nvram_write (void *opaque, uint32_t addr, uint32_t val);

#endif /* !defined(__PPC_MAC_H__) */
