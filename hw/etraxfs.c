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
#include "sysemu.h"
#include "flash.h"
#include "boards.h"

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

/* Init functions for different blocks.  */
extern qemu_irq *etraxfs_pic_init(CPUState *env, target_phys_addr_t base);
void etraxfs_timer_init(CPUState *env, qemu_irq *irqs, 
			target_phys_addr_t base);
void etraxfs_ser_init(CPUState *env, qemu_irq *irq, CharDriverState *chr,
		      target_phys_addr_t base);

#define FLASH_SIZE 0x2000000
#define INTMEM_SIZE (128 * 1024)

static
void bareetraxfs_init (ram_addr_t ram_size, int vga_ram_size,
                       const char *boot_device, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    qemu_irq *pic;
    int kernel_size;
    int index;
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
    index = drive_get_index(IF_PFLASH, 0, 0);
    pflash_cfi01_register(0x80000000, FLASH_SIZE,
			  drives_table[index].bdrv, 65536, FLASH_SIZE >> 16,
			  4, 0x0000, 0x0000, 0x0000, 0x0000);

    pic = etraxfs_pic_init(env, 0xb001c000);
    /* 2 timers.  */
    etraxfs_timer_init(env, pic + 26, 0xb001e000);
    etraxfs_timer_init(env, pic + 26, 0xb005e000);
    /* 4 serial ports.  */
    etraxfs_ser_init(env, pic + 19, serial_hds[0], 0xb0026000);
    if (serial_hds[1])
	    etraxfs_ser_init(env, pic + 20, serial_hds[1], 0xb0028000);
    if (serial_hds[2])
	    etraxfs_ser_init(env, pic + 21, serial_hds[2], 0xb002a000);
    if (serial_hds[3])
	    etraxfs_ser_init(env, pic + 22, serial_hds[3], 0xb002c000);

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
    printf ("ram size =%d\n", ram_size);
    printf ("kernel name =%s\n", kernel_filename);
    printf ("kernel size =%d\n", kernel_size);
    printf ("cpu haltd =%d\n", env->halted);
}

void DMA_run(void)
{
}

QEMUMachine bareetraxfs_machine = {
    "bareetraxfs",
    "Bare ETRAX FS board",
    bareetraxfs_init,
    0x800000,
};
