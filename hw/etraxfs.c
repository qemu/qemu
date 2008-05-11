/*
 * QEMU ETRAX System Emulator
 *
 * Copyright (c) 2007 Edgar E. Iglesias, Axis Communications AB.
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
#include <time.h>
#include <sys/time.h>
#include "hw.h"
#include "net.h"
#include "flash.h"
#include "sysemu.h"
#include "devices.h"
#include "boards.h"

#include "etraxfs_dma.h"

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

/* Init functions for different blocks.  */
extern qemu_irq *etraxfs_pic_init(CPUState *env, target_phys_addr_t base);
void etraxfs_timer_init(CPUState *env, qemu_irq *irqs,
			target_phys_addr_t base);
void *etraxfs_eth_init(NICInfo *nd, CPUState *env, 
		       qemu_irq *irq, target_phys_addr_t base);
void etraxfs_ser_init(CPUState *env, qemu_irq *irq, CharDriverState *chr,
		      target_phys_addr_t base);

#define FLASH_SIZE 0x2000000
#define INTMEM_SIZE (128 * 1024)

static void *etraxfs_dmac;

static
void bareetraxfs_init (ram_addr_t ram_size, int vga_ram_size,
                       const char *boot_device, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    qemu_irq *pic;
    struct etraxfs_dma_client *eth0;
    int kernel_size;
    int i;
    ram_addr_t phys_ram;
    ram_addr_t phys_intmem;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);
/*    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env); */
    qemu_register_reset(main_cpu_reset, env);

    /* allocate RAM */
    phys_ram = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(0x40000000, ram_size, phys_ram | IO_MEM_RAM);
    /* Unached mapping.  */
    cpu_register_physical_memory(0xc0000000, ram_size, phys_ram | IO_MEM_RAM);

    /* The ETRAX-FS has 128Kb on chip ram, the docs refer to it as the 
       internal memory. Cached and uncached mappings.  */
    phys_intmem = qemu_ram_alloc(INTMEM_SIZE);
    cpu_register_physical_memory(0xb8000000, INTMEM_SIZE, 
				 phys_intmem | IO_MEM_RAM);
    cpu_register_physical_memory(0x38000000, INTMEM_SIZE, 
				 phys_intmem | IO_MEM_RAM);

    cpu_register_physical_memory(0, FLASH_SIZE, IO_MEM_ROM);
    cpu_register_physical_memory(0x80000000, FLASH_SIZE, IO_MEM_ROM);
    cpu_register_physical_memory(0x04000000, FLASH_SIZE, IO_MEM_ROM);
    cpu_register_physical_memory(0x84000000, FLASH_SIZE, 
				 0x04000000 | IO_MEM_ROM);
    i = drive_get_index(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0x80000000, qemu_ram_alloc(FLASH_SIZE),
			  drives_table[i].bdrv, (64 * 1024), 
			  FLASH_SIZE >> 16,
			  1, 2, 0x0000, 0x0000, 0x0000, 0x0000, 0x555, 0x2aa);

    pic = etraxfs_pic_init(env, 0xb001c000);
    etraxfs_dmac = etraxfs_dmac_init(env, 0xb0000000, 10);
    for (i = 0; i < 10; i++) {
	    /* On ETRAX, odd numbered channels are inputs.  */
	    etraxfs_dmac_connect(etraxfs_dmac, i, pic + 7 + i, i & 1);
    }

    /* It has 2, but let's start with one ethernet block.  */
    eth0 = etraxfs_eth_init(&nd_table[0], env, pic + 25, 0xb0034000);
    
    /* The DMA Connector block is missing, hardwire things for now.  */
    etraxfs_dmac_connect_client(etraxfs_dmac, 0, eth0);
    etraxfs_dmac_connect_client(etraxfs_dmac, 1, eth0 + 1);

    /* 2 timers.  */
    etraxfs_timer_init(env, pic + 0x1b, 0xb001e000);
    etraxfs_timer_init(env, pic + 0x1b, 0xb005e000);

    for (i = 0; i < 4; i++) {
	    if (serial_hds[i]) {
		    etraxfs_ser_init(env, pic + 0x14 + i, 
				     serial_hds[i], 0xb0026000 + i * 0x2000);
	    }
    }

#if 1
    /* Boots a kernel elf binary, os/linux-2.6/vmlinux from the axis devboard
       SDK.  */
    kernel_size = load_elf(kernel_filename, 0, &env->pc, NULL, NULL);
#else
    /* Takes a kimage from the axis devboard SDK.  */
    kernel_size = load_image(kernel_filename, phys_ram_base + 0x4000);
    env->pc = 0x40004000;
#endif
    /* magic for boot.  */
    env->regs[8] = 0x56902387;
    env->regs[9] = 0x40004000 + kernel_size;

    {
       unsigned char *ptr = phys_ram_base + 0x4000;
       int i;
       for (i = 0; i < 8; i++)
       {
		printf ("%2.2x ", ptr[i]);
       }
	printf("\n");
    }

    printf ("pc =%x\n", env->pc);
    printf ("ram size =%ld\n", ram_size);
    printf ("kernel name =%s\n", kernel_filename);
    printf ("kernel size =%d\n", kernel_size);
    printf ("cpu haltd =%d\n", env->halted);
}

void DMA_run(void)
{
	etraxfs_dmac_run(etraxfs_dmac);
}

QEMUMachine bareetraxfs_machine = {
    "bareetraxfs",
    "Bare ETRAX FS board",
    bareetraxfs_init,
    0x4000000,
};
