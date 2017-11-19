/*
 * a dummy similar with CSKY Trilobite V2 System emulation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#undef NEED_CPU_H
#define NEED_CPU_H

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "target-csky/cpu.h"
#include "hw/csky/csky.h"
#include "hw/sysbus.h"
#include "hw/devices.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "hw/csky/cskydev.h"
#include "hw/char/csky_uart.h"
#include "hw/csky/dynsoc.h"

#define CORET_IRQ_NUM   0

static struct csky_boot_info dummyh_binfo = {
    .loader_start = 0x0,
    .dtb_addr = 0x8f000000,
    .magic = 0x20150401,
};

#define RAM_NUM     8
#define DEV_NUM     32
static void dummyh_init(MachineState *machine)
{
    ObjectClass *cpu_oc;
    Object *cpuobj;
    CSKYCPU *cpu;
    CPUCSKYState *env;
    qemu_irq *cpu_intc;
    qemu_irq intc[32];
    DeviceState *dev = NULL;
    int i, j;
    struct dynsoc_board_info *b_info = dynsoc_b_info;
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegion *ram[RAM_NUM];
    int dev_type;
//    csky_dma_state *csky_dma = NULL;

    if (!machine->cpu_model) {
        machine->cpu_model = "ck810f";
    }

    cpu_oc = cpu_class_by_name(TYPE_CSKY_CPU, machine->cpu_model);
    if (!cpu_oc) {
        fprintf(stderr, "Unable to find CPU definition\n");
        exit(1);
    }

    cpuobj = object_new(object_class_get_name(cpu_oc));

    object_property_set_bool(cpuobj, true, "realized", &error_fatal);

    cpu = CSKY_CPU(cpuobj);
    env = &cpu->env;

    for (i = 0; i < RAM_NUM; i++) {
        ram[i] = g_new(MemoryRegion, 1);
        if (b_info->mem[i].size != 0) {
            if (b_info->mem[i].writeable == 1) {
                memory_region_allocate_system_memory(ram[i], NULL,
                                                     b_info->mem[i].name,
                                                     b_info->mem[i].size);
                memory_region_add_subregion(sysmem, b_info->mem[i].addr,
                                            ram[i]);
            } else if (b_info->mem[i].writeable == 0) {
                /* !writeable means this ram is alias of a rom/flash */
                memory_region_allocate_system_memory(ram[i], NULL,
                                                     b_info->mem[i].name,
                                                     b_info->mem[i].size);
                memory_region_set_readonly(ram[i], true);
                memory_region_add_subregion(sysmem, b_info->mem[i].addr,
                                            ram[i]);
            }
        }

        if (b_info->mem[i].size == 0) {
            continue;
        }
    }

    for (i = 0; i < DEV_NUM; i++) {
        dev_type = b_info->dev[i].type;
        switch (dev_type) {
        case DYNSOC_EMPTY:
            /* do nothing */
            break;
        case DYNSOC_INTC:
            if (!strcmp(b_info->dev[i].name, "csky_intc")) {
                cpu_intc = csky_intc_init_cpu(env);
                dev = sysbus_create_simple("csky_intc", b_info->dev[i].addr,
                                            cpu_intc[0]);

            } else if (!strcmp(b_info->dev[i].name, "csky_tcip_v1")) {
                cpu_intc = csky_vic_v1_init_cpu(env, b_info->dev[i + 1].irq);
                csky_tcip_v1_set_freq(1000000000ll);
                dev = sysbus_create_simple("csky_tcip_v1", b_info->dev[i].addr,
                                           cpu_intc[0]);
            }

            for (j = 0; j < 32; j++) {
                intc[j] = qdev_get_gpio_in(dev, j);
            }

            break;
        case DYNSOC_UART:
            dev = qdev_create(NULL, b_info->dev[i].name);
            SysBusDevice *s = SYS_BUS_DEVICE(dev);
            qdev_prop_set_chr(dev, "chardev", serial_hds[0]);
            qdev_init_nofail(dev);
            sysbus_mmio_map(s, 0, b_info->dev[i].addr);
            sysbus_connect_irq(s, 0, intc[b_info->dev[i].irq]);

            break;
        case DYNSOC_TIMER:
            if (!strcmp(b_info->dev[i].name, "csky_coret")) {
                continue;
            }
            sysbus_create_varargs(b_info->dev[i].name, b_info->dev[i].addr,
                                  intc[b_info->dev[i].irq],
                                  intc[b_info->dev[i].irq + 1],
                                  intc[b_info->dev[i].irq + 2],
                                  intc[b_info->dev[i].irq + 3],
                                  NULL);

            break;
        case DYNSOC_LCDC:
            sysbus_create_simple(b_info->dev[i].name, b_info->dev[i].addr,
                                 intc[b_info->dev[i].irq]);

            break;
        case DYNSOC_MAC:
            if (nd_table[0].used) {
                if (!strcmp(b_info->dev[i].name, "csky_mac_v2")) {
                    csky_mac_v2_create(&nd_table[0], b_info->dev[i].addr,
                                       intc[b_info->dev[i].irq]);
                } else if (!strcmp(b_info->dev[i].name, "csky_mac")) {
                    csky_mac_create(&nd_table[0], b_info->dev[i].addr,
                                    intc[b_info->dev[i].irq]);
                }
            }

            break;
        case DYNSOC_EXIT:
        case DYNSOC_MEMLOG:
            sysbus_create_simple(b_info->dev[i].name, b_info->dev[i].addr,
                                 NULL);

            break;
        case DYNSOC_DMA:
            /* todo */
//            csky_dma = csky_dma_create(b_info->dev[i].name, b_info->dev[i].addr,
//                                       intc[b_info->dev[i].irq]);

            break;
        case DYNSOC_IIS:
            /* todo */
//            csky_iis_create(b_info->dev[i].name, b_info->dev[i].addr,
//                            intc[b_info->dev[i].irq], csky_dma);

            break;
        case DYNSOC_NAND:
        case DYNSOC_SDHC:
            sysbus_create_simple(b_info->dev[i].name, b_info->dev[i].addr,
                                 intc[b_info->dev[i].irq]);

            break;
        case DYNSOC_USB:
        case DYNSOC_CUSTOM:
                /* todo */
            break;
        default:
            fprintf(stderr, "not support device type\n");
            exit(1);
        }

    }

    dummyh_binfo.ram_size = machine->ram_size;
    dummyh_binfo.kernel_filename = machine->kernel_filename;
    dummyh_binfo.kernel_cmdline = machine->kernel_cmdline;
    dummyh_binfo.initrd_filename = machine->initrd_filename;
    csky_load_kernel(cpu, &dummyh_binfo);
}

static void dummyh_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "CSKY dummyh";
    mc->init = dummyh_init;
}

static const TypeInfo dummyh_type = {
    .name = MACHINE_TYPE_NAME("dummyh"),
    .parent = TYPE_MACHINE,
    .class_init = dummyh_class_init,
};

static void dummyh_machine_init(void)
{
    type_register_static(&dummyh_type);
}

type_init(dummyh_machine_init)
