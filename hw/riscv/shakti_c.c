/*
 * Shakti C-class SoC emulation
 *
 * Copyright (c) 2021 Vijai Kumar K <vijai@behindbytes.com>
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
#include "hw/boards.h"
#include "hw/riscv/shakti_c.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/intc/sifive_plic.h"
#include "hw/intc/riscv_aclint.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "exec/address-spaces.h"
#include "hw/riscv/boot.h"

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} shakti_c_memmap[] = {
    [SHAKTI_C_ROM]   =  {  0x00001000,  0x2000   },
    [SHAKTI_C_RAM]   =  {  0x80000000,  0x0      },
    [SHAKTI_C_UART]  =  {  0x00011300,  0x00040  },
    [SHAKTI_C_GPIO]  =  {  0x020d0000,  0x00100  },
    [SHAKTI_C_PLIC]  =  {  0x0c000000,  0x20000  },
    [SHAKTI_C_CLINT] =  {  0x02000000,  0xc0000  },
    [SHAKTI_C_I2C]   =  {  0x20c00000,  0x00100  },
};

static void shakti_c_machine_state_init(MachineState *mstate)
{
    ShaktiCMachineState *sms = RISCV_SHAKTI_MACHINE(mstate);
    MemoryRegion *system_memory = get_system_memory();

    /* Initialize SoC */
    object_initialize_child(OBJECT(mstate), "soc", &sms->soc,
                            TYPE_RISCV_SHAKTI_SOC);
    qdev_realize(DEVICE(&sms->soc), NULL, &error_abort);

    /* register RAM */
    memory_region_add_subregion(system_memory,
                                shakti_c_memmap[SHAKTI_C_RAM].base,
                                mstate->ram);

    /* ROM reset vector */
    riscv_setup_rom_reset_vec(mstate, &sms->soc.cpus,
                              shakti_c_memmap[SHAKTI_C_RAM].base,
                              shakti_c_memmap[SHAKTI_C_ROM].base,
                              shakti_c_memmap[SHAKTI_C_ROM].size, 0, 0);
    if (mstate->firmware) {
        riscv_load_firmware(mstate->firmware,
                            shakti_c_memmap[SHAKTI_C_RAM].base,
                            NULL);
    }
}

static void shakti_c_machine_instance_init(Object *obj)
{
}

static void shakti_c_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);
    static const char * const valid_cpu_types[] = {
        RISCV_CPU_TYPE_NAME("shakti-c"),
        NULL
    };

    mc->desc = "RISC-V Board compatible with Shakti SDK";
    mc->init = shakti_c_machine_state_init;
    mc->default_cpu_type = TYPE_RISCV_CPU_SHAKTI_C;
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_id = "riscv.shakti.c.ram";
}

static const TypeInfo shakti_c_machine_type_info = {
    .name = TYPE_RISCV_SHAKTI_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = shakti_c_machine_class_init,
    .instance_init = shakti_c_machine_instance_init,
    .instance_size = sizeof(ShaktiCMachineState),
};

static void shakti_c_machine_type_info_register(void)
{
    type_register_static(&shakti_c_machine_type_info);
}
type_init(shakti_c_machine_type_info_register)

static void shakti_c_soc_state_realize(DeviceState *dev, Error **errp)
{
    MachineState *ms = MACHINE(qdev_get_machine());
    ShaktiCSoCState *sss = RISCV_SHAKTI_SOC(dev);
    MemoryRegion *system_memory = get_system_memory();

    sysbus_realize(SYS_BUS_DEVICE(&sss->cpus), &error_abort);

    sss->plic = sifive_plic_create(shakti_c_memmap[SHAKTI_C_PLIC].base,
        (char *)SHAKTI_C_PLIC_HART_CONFIG, ms->smp.cpus, 0,
        SHAKTI_C_PLIC_NUM_SOURCES,
        SHAKTI_C_PLIC_NUM_PRIORITIES,
        SHAKTI_C_PLIC_PRIORITY_BASE,
        SHAKTI_C_PLIC_PENDING_BASE,
        SHAKTI_C_PLIC_ENABLE_BASE,
        SHAKTI_C_PLIC_ENABLE_STRIDE,
        SHAKTI_C_PLIC_CONTEXT_BASE,
        SHAKTI_C_PLIC_CONTEXT_STRIDE,
        shakti_c_memmap[SHAKTI_C_PLIC].size);

    riscv_aclint_swi_create(shakti_c_memmap[SHAKTI_C_CLINT].base,
        0, 1, false);
    riscv_aclint_mtimer_create(shakti_c_memmap[SHAKTI_C_CLINT].base +
            RISCV_ACLINT_SWI_SIZE,
        RISCV_ACLINT_DEFAULT_MTIMER_SIZE, 0, 1,
        RISCV_ACLINT_DEFAULT_MTIMECMP, RISCV_ACLINT_DEFAULT_MTIME,
        RISCV_ACLINT_DEFAULT_TIMEBASE_FREQ, false);

    qdev_prop_set_chr(DEVICE(&(sss->uart)), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&sss->uart), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&sss->uart), 0,
                    shakti_c_memmap[SHAKTI_C_UART].base);

    /* ROM */
    memory_region_init_rom(&sss->rom, OBJECT(dev), "riscv.shakti.c.rom",
                           shakti_c_memmap[SHAKTI_C_ROM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
        shakti_c_memmap[SHAKTI_C_ROM].base, &sss->rom);
}

static void shakti_c_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = shakti_c_soc_state_realize;
    /*
     * Reasons:
     *     - Creates CPUS in riscv_hart_realize(), and can create unintended
     *       CPUs
     *     - Uses serial_hds in realize function, thus can't be used twice
     */
    dc->user_creatable = false;
}

static void shakti_c_soc_instance_init(Object *obj)
{
    ShaktiCSoCState *sss = RISCV_SHAKTI_SOC(obj);

    object_initialize_child(obj, "cpus", &sss->cpus, TYPE_RISCV_HART_ARRAY);
    object_initialize_child(obj, "uart", &sss->uart, TYPE_SHAKTI_UART);

    /*
     * CPU type is fixed and we are not supporting passing from commandline yet.
     * So let it be in instance_init. When supported should use ms->cpu_type
     * instead of TYPE_RISCV_CPU_SHAKTI_C
     */
    object_property_set_str(OBJECT(&sss->cpus), "cpu-type",
                            TYPE_RISCV_CPU_SHAKTI_C, &error_abort);
    object_property_set_int(OBJECT(&sss->cpus), "num-harts", 1,
                            &error_abort);
}

static const TypeInfo shakti_c_type_info = {
    .name = TYPE_RISCV_SHAKTI_SOC,
    .parent = TYPE_DEVICE,
    .class_init = shakti_c_soc_class_init,
    .instance_init = shakti_c_soc_instance_init,
    .instance_size = sizeof(ShaktiCSoCState),
};

static void shakti_c_type_info_register(void)
{
    type_register_static(&shakti_c_type_info);
}
type_init(shakti_c_type_info_register)
