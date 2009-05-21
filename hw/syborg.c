/*
 * Syborg (Symbian Virtual Platform) reference board
 *
 * Copyright (c) 2009 CodeSourcery
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
#include "arm-misc.h"
#include "sysemu.h"
#include "net.h"

static struct arm_boot_info syborg_binfo;

static void syborg_init(ram_addr_t ram_size,
                        const char *boot_device,
                        const char *kernel_filename, const char *kernel_cmdline,
                        const char *initrd_filename, const char *cpu_model)
{
    CPUState *env;
    qemu_irq *cpu_pic;
    qemu_irq pic[64];
    ram_addr_t ram_addr;
    DeviceState *dev;
    int i;

    if (!cpu_model)
        cpu_model = "cortex-a8";
    env = cpu_init(cpu_model);
    if (!env) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    /* RAM at address zero. */
    ram_addr = qemu_ram_alloc(ram_size);
    cpu_register_physical_memory(0, ram_size, ram_addr | IO_MEM_RAM);

    cpu_pic = arm_pic_init_cpu(env);
    dev = sysbus_create_simple("syborg,interrupt", 0xC0000000,
                               cpu_pic[ARM_PIC_CPU_IRQ]);
    for (i = 0; i < 64; i++) {
        pic[i] = qdev_get_irq_sink(dev, i);
    }

    sysbus_create_simple("syborg,rtc", 0xC0001000, NULL);

    dev = qdev_create(NULL, "syborg,timer");
    qdev_set_prop_int(dev, "frequency", 1000000);
    qdev_init(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, 0xC0002000);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, pic[1]);

    sysbus_create_simple("syborg,keyboard", 0xC0003000, pic[2]);
    sysbus_create_simple("syborg,pointer", 0xC0004000, pic[3]);
    sysbus_create_simple("syborg,framebuffer", 0xC0005000, pic[4]);
    sysbus_create_simple("syborg,serial", 0xC0006000, pic[5]);
    sysbus_create_simple("syborg,serial", 0xC0007000, pic[6]);
    sysbus_create_simple("syborg,serial", 0xC0008000, pic[7]);
    sysbus_create_simple("syborg,serial", 0xC0009000, pic[8]);

    if (nd_table[0].vlan) {
        DeviceState *dev;
        SysBusDevice *s;

        qemu_check_nic_model(&nd_table[0], "virtio");
        dev = qdev_create(NULL, "syborg,virtio-net");
        qdev_set_netdev(dev, &nd_table[0]);
        qdev_init(dev);
        s = sysbus_from_qdev(dev);
        sysbus_mmio_map(s, 0, 0xc000c000);
        sysbus_connect_irq(s, 0, pic[9]);
    }

    syborg_binfo.ram_size = ram_size;
    syborg_binfo.kernel_filename = kernel_filename;
    syborg_binfo.kernel_cmdline = kernel_cmdline;
    syborg_binfo.initrd_filename = initrd_filename;
    syborg_binfo.board_id = 0;
    arm_load_kernel(env, &syborg_binfo);
}

static QEMUMachine syborg_machine = {
    .name = "syborg",
    .desc = "Syborg (Symbian Virtual Platform)",
    .init = syborg_init,
};

static void syborg_machine_init(void)
{
    qemu_register_machine(&syborg_machine);
}

machine_init(syborg_machine_init);
