/*
 * QEMU RISC-V Board Compatible with Neorv32 IP
 *
 * Provides a board compatible with the Neorv32 IP:
 *
 * 0) SYSINFO
 * 1) IMEM
 * 2) DMEM
 * 3) UART
 * 4) SPI
 *
 * Author:
 *   Michael Levit <michael@videogpu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "hw/misc/unimp.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/boot.h"
#include "hw/intc/riscv_aclint.h"
#include "chardev/char.h"
#include "system/system.h"
#include "hw/ssi/ssi.h"    /* For ssi_realize_and_unref() */

#include "hw/riscv/neorv32.h"
#include "hw/misc/neorv32_sysinfo.h"
#include "hw/char/neorv32_uart.h"
#include "hw/ssi/neorv32_spi.h"

static const MemMapEntry neorv32_memmap[] = {

    [NEORV32_IMEM]           = { NEORV32_IMEM_BASE,               SYSINFO_IMEM_SIZE},
    [NEORV32_BOOTLOADER_ROM] = { NEORV32_BOOTLOADER_BASE_ADDRESS, 0x2000},     /* 8K  ROM for bootloader */
    [NEORV32_DMEM]           = { NEORV32_DMEM_BASE,               SYSINFO_DMEM_SIZE},
    [NEORV32_SYSINFO]        = { NEORV32_SYSINFO_BASE,            0x100},
    [NEORV32_UART0]          = { NEORV32_UART0_BASE,              0x100},
	[NEORV32_SPI0]           = { NEORV32_SPI_BASE,                0x100},
};

static void neorv32_machine_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const MemMapEntry *memmap = neorv32_memmap;

    Neorv32State *s = NEORV32_MACHINE(machine);
    MemoryRegion *sys_mem = get_system_memory();
    int i;
    RISCVBootInfo boot_info;
    hwaddr start_addr = memmap[NEORV32_BOOTLOADER_ROM].base;

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc, TYPE_RISCV_NEORV32_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_fatal);

    /* Data Tightly Integrated Memory */
    memory_region_add_subregion(sys_mem,
        memmap[NEORV32_DMEM].base, machine->ram);

    /* Instruction Memory (IMEM) */
	memory_region_init_ram(&s->soc.imem_region, OBJECT(&s->soc), "riscv.neorv32.imem",
						 memmap[NEORV32_IMEM].size, &error_fatal);
	memory_region_add_subregion(sys_mem, memmap[NEORV32_IMEM].base, &s->soc.imem_region);

    /* Mask ROM reset vector */
    uint32_t reset_vec[4];

    reset_vec[1] = 0x204002b7;  /* 0x1004: lui     t0,0x20400 */
    reset_vec[2] = 0x00028067;      /* 0x1008: jr      t0 */
    reset_vec[0] = reset_vec[3] = 0;

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }

    /* Neorv32 bootloader */
    if (machine->firmware) {
        riscv_find_and_load_firmware(machine, machine->firmware,
        		                     &start_addr, NULL);
    }

    /* Neorv32 example applications */
    riscv_boot_info_init(&boot_info, &s->soc.cpus);
    if (machine->kernel_filename) {
        riscv_load_kernel(machine, &boot_info,
                          memmap[NEORV32_IMEM].base,
                          false, NULL);
    }
}

static void neorv32_machine_instance_init(Object *obj)
{

    /* Placeholder for now */
    /* Neorv32State *s = NEORV32_MACHINE(obj); */
}

static void neorv32_machine_class_init(ObjectClass *oc,const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "RISC-V SOC compatible with Neorv32 SDK";
    mc->init = neorv32_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = NEORV32_CPU;
    mc->default_ram_id = "riscv.neorv32.dmem";
    mc->default_ram_size = neorv32_memmap[NEORV32_DMEM].size;

}

static const TypeInfo neorv32_machine_typeinfo = {
    .name          = MACHINE_TYPE_NAME("neorv32"),
    .parent        = TYPE_MACHINE,
    .class_init    = neorv32_machine_class_init,
    .instance_init = neorv32_machine_instance_init,
    .instance_size = sizeof(Neorv32State),
};

static void neorv32_machine_init_register_types(void)
{
    type_register_static(&neorv32_machine_typeinfo);
}

type_init(neorv32_machine_init_register_types)

static void neorv32_soc_init(Object *obj)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    Neorv32SoCState *s = RISCV_NEORV32_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus,
                            &error_abort);

    object_property_set_int(OBJECT(&s->cpus), "resetvec", NEORV32_BOOTLOADER_BASE_ADDRESS, &error_abort);

}

static void neorv32_soc_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    const MemMapEntry *memmap = neorv32_memmap;
    Neorv32SoCState *s = RISCV_NEORV32_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();

    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type,
                            &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_fatal);

    /* Bootloader ROM */
    memory_region_init_rom(&s->bootloader_rom, OBJECT(dev), "riscv.bootloader.rom",
                           memmap[NEORV32_BOOTLOADER_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[NEORV32_BOOTLOADER_ROM].base, &s->bootloader_rom);


    /* Sysinfo ROM */
    neorv32_sysinfo_create(sys_mem, memmap[NEORV32_SYSINFO].base);

    /* Uart0 */
    neorv32_uart_create(sys_mem, memmap[NEORV32_UART0].base,serial_hd(0));

    /* SPI controller */
	NEORV32SPIState *spi = neorv32_spi_create(sys_mem, memmap[NEORV32_SPI0].base);

	if (!spi) {
		error_setg(errp, "SPI is not created");
		return;
	}
}

static void neorv32_soc_class_init(ObjectClass *oc,const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = neorv32_soc_realize;
    dc->user_creatable = false;
}

static const TypeInfo neorv32_soc_type_info = {
    .name = TYPE_RISCV_NEORV32_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Neorv32SoCState),
    .instance_init = neorv32_soc_init,
    .class_init = neorv32_soc_class_init,
};

static void neorv32_soc_register_types(void)
{
    type_register_static(&neorv32_soc_type_info);
}

type_init(neorv32_soc_register_types)
