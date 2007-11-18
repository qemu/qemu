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
#include "boards.h"

extern FILE *logfile;

static void main_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static uint32_t fs_mmio_readb (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;
	printf ("%s %x pc=%x\n", __func__, addr, env->pc);
	return r;
}
static uint32_t fs_mmio_readw (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;
	printf ("%s %x pc=%x\n", __func__, addr, env->pc);
	return r;
}

static uint32_t fs_mmio_readl (void *opaque, target_phys_addr_t addr)
{
	CPUState *env = opaque;
	uint32_t r = 0;
	printf ("%s %x p=%x\n", __func__, addr, env->pc);
	return r;
}

static void
fs_mmio_writeb (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc);
}
static void
fs_mmio_writew (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc);
}
static void
fs_mmio_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	CPUState *env = opaque;
	printf ("%s %x %x pc=%x\n", __func__, addr, value, env->pc);
}

static CPUReadMemoryFunc *fs_mmio_read[] = {
    &fs_mmio_readb,
    &fs_mmio_readw,
    &fs_mmio_readl,
};

static CPUWriteMemoryFunc *fs_mmio_write[] = {
    &fs_mmio_writeb,
    &fs_mmio_writew,
    &fs_mmio_writel,
};


/* Init functions for different blocks.  */
extern void etraxfs_timer_init(CPUState *env, qemu_irq *irqs);
extern void etraxfs_ser_init(CPUState *env, qemu_irq *irqs);

void etrax_ack_irq(CPUState *env, uint32_t mask)
{
	env->pending_interrupts &= ~mask;
}

static void dummy_cpu_set_irq(void *opaque, int irq, int level)
{
	CPUState *env = opaque;

	/* Hmm, should this really be done here?  */
	env->pending_interrupts |= 1 << irq;
	cpu_interrupt(env, CPU_INTERRUPT_HARD);
}

static
void bareetraxfs_init (int ram_size, int vga_ram_size,
                       const char *boot_device, DisplayState *ds,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    qemu_irq *irqs;
    int kernel_size;
    int internal_regs;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);
/*    register_savevm("cpu", 0, 3, cpu_save, cpu_load, env); */
    qemu_register_reset(main_cpu_reset, env);
    irqs = qemu_allocate_irqs(dummy_cpu_set_irq, env, 32);

    internal_regs = cpu_register_io_memory(0,
					   fs_mmio_read, fs_mmio_write, env);
    /* 0xb0050000 is the last reg.  */
    cpu_register_physical_memory (0xac000000, 0x4010000, internal_regs);
    /* allocate RAM */
    cpu_register_physical_memory(0x40000000, ram_size, IO_MEM_RAM);

    etraxfs_timer_init(env, irqs);
    etraxfs_ser_init(env, irqs);

    kernel_size = load_image(kernel_filename, phys_ram_base + 0x4000);
    /* magic for boot.  */
    env->regs[8] = 0x56902387;
    env->regs[9] = 0x40004000 + kernel_size;
    env->pc = 0x40004000;

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

void pic_info()
{
}

void irq_info()
{
}

QEMUMachine bareetraxfs_machine = {
    "bareetraxfs",
    "Bare ETRAX FS board",
    bareetraxfs_init,
};
