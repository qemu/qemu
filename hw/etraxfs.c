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

#include "sysbus.h"
#include "boards.h"
#include "sysemu.h"
#include "net.h"
#include "flash.h"
#include "etraxfs.h"
#include "loader.h"
#include "elf.h"
#include "cris-boot.h"
#include "blockdev.h"

#define FLASH_SIZE 0x2000000
#define INTMEM_SIZE (128 * 1024)

static struct cris_load_info li;

static void flash_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    cpu_reset(env);
}

static
void bareetraxfs_init (ram_addr_t ram_size,
                       const char *boot_device,
                       const char *kernel_filename, const char *kernel_cmdline,
                       const char *initrd_filename, const char *cpu_model)
{
    DeviceState *dev;
    SysBusDevice *s;
    CPUState *env;
    qemu_irq irq[30], nmi[2], *cpu_irq; 
    void *etraxfs_dmac;
    struct etraxfs_dma_client *eth[2] = {NULL, NULL};
    DriveInfo *dinfo;
    int i;
    ram_addr_t phys_ram;
    ram_addr_t phys_flash;
    ram_addr_t phys_intmem;

    /* init CPUs */
    if (cpu_model == NULL) {
        cpu_model = "crisv32";
    }
    env = cpu_init(cpu_model);

    /* allocate RAM */
    phys_ram = qemu_ram_alloc(NULL, "etraxfs.ram", ram_size);
    cpu_register_physical_memory(0x40000000, ram_size, phys_ram | IO_MEM_RAM);

    /* The ETRAX-FS has 128Kb on chip ram, the docs refer to it as the 
       internal memory.  */
    phys_intmem = qemu_ram_alloc(NULL, "etraxfs.chipram", INTMEM_SIZE);
    cpu_register_physical_memory(0x38000000, INTMEM_SIZE,
                                 phys_intmem | IO_MEM_RAM);


    phys_flash = qemu_ram_alloc(NULL, "etraxfs.flash", FLASH_SIZE);
    dinfo = drive_get(IF_PFLASH, 0, 0);
    pflash_cfi02_register(0x0, phys_flash,
                          dinfo ? dinfo->bdrv : NULL, (64 * 1024),
                          FLASH_SIZE >> 16,
                          1, 2, 0x0000, 0x0000, 0x0000, 0x0000,
                          0x555, 0x2aa, 0);
    cpu_irq = cris_pic_init_cpu(env);
    dev = qdev_create(NULL, "etraxfs,pic");
    /* FIXME: Is there a proper way to signal vectors to the CPU core?  */
    qdev_prop_set_ptr(dev, "interrupt_vector", &env->interrupt_vector);
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    sysbus_mmio_map(s, 0, 0x3001c000);
    sysbus_connect_irq(s, 0, cpu_irq[0]);
    sysbus_connect_irq(s, 1, cpu_irq[1]);
    for (i = 0; i < 30; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }
    nmi[0] = qdev_get_gpio_in(dev, 30);
    nmi[1] = qdev_get_gpio_in(dev, 31);

    etraxfs_dmac = etraxfs_dmac_init(0x30000000, 10);
    for (i = 0; i < 10; i++) {
        /* On ETRAX, odd numbered channels are inputs.  */
        etraxfs_dmac_connect(etraxfs_dmac, i, irq + 7 + i, i & 1);
    }

    /* Add the two ethernet blocks.  */
    eth[0] = etraxfs_eth_init(&nd_table[0], 0x30034000, 1);
    if (nb_nics > 1)
        eth[1] = etraxfs_eth_init(&nd_table[1], 0x30036000, 2);

    /* The DMA Connector block is missing, hardwire things for now.  */
    etraxfs_dmac_connect_client(etraxfs_dmac, 0, eth[0]);
    etraxfs_dmac_connect_client(etraxfs_dmac, 1, eth[0] + 1);
    if (eth[1]) {
        etraxfs_dmac_connect_client(etraxfs_dmac, 6, eth[1]);
        etraxfs_dmac_connect_client(etraxfs_dmac, 7, eth[1] + 1);
    }

    /* 2 timers.  */
    sysbus_create_varargs("etraxfs,timer", 0x3001e000, irq[0x1b], nmi[1], NULL);
    sysbus_create_varargs("etraxfs,timer", 0x3005e000, irq[0x1b], nmi[1], NULL);

    for (i = 0; i < 4; i++) {
        sysbus_create_simple("etraxfs,serial", 0x30026000 + i * 0x2000,
                             irq[0x14 + i]); 
    }

    if (kernel_filename) {
        li.image_filename = kernel_filename;
        li.cmdline = kernel_cmdline;
        cris_load_image(env, &li);
    } else {
        if (!dinfo) {
            fprintf(stderr,
                    "Provide a kernel image or a flash image to boot from.\n");
           exit(1);
        }

        /* Nothing more to do for flash images, those boot from addr 0.  */
        qemu_register_reset(flash_cpu_reset, env);
    }
}

static QEMUMachine bareetraxfs_machine = {
    .name = "bareetraxfs",
    .desc = "Bare ETRAX FS board",
    .init = bareetraxfs_init,
    .is_default = 1,
};

static void bareetraxfs_machine_init(void)
{
    qemu_register_machine(&bareetraxfs_machine);
}

machine_init(bareetraxfs_machine_init);
