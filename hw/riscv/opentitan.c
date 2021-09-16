/*
 * QEMU RISC-V Board Compatible with OpenTitan FPGA platform
 *
 * Copyright (c) 2020 Western Digital
 *
 * Provides a board compatible with the OpenTitan FPGA platform:
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
#include "hw/riscv/opentitan.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"

static const MemMapEntry ibex_memmap[] = {
    [IBEX_DEV_ROM] =            {  0x00008000, 16 * KiB },
    [IBEX_DEV_RAM] =            {  0x10000000,  0x10000 },
    [IBEX_DEV_FLASH] =          {  0x20000000,  0x80000 },
    [IBEX_DEV_UART] =           {  0x40000000,  0x1000  },
    [IBEX_DEV_GPIO] =           {  0x40040000,  0x1000  },
    [IBEX_DEV_SPI] =            {  0x40050000,  0x1000  },
    [IBEX_DEV_I2C] =            {  0x40080000,  0x1000  },
    [IBEX_DEV_PATTGEN] =        {  0x400e0000,  0x1000  },
    [IBEX_DEV_TIMER] =          {  0x40100000,  0x1000  },
    [IBEX_DEV_SENSOR_CTRL] =    {  0x40110000,  0x1000  },
    [IBEX_DEV_OTP_CTRL] =       {  0x40130000,  0x4000  },
    [IBEX_DEV_USBDEV] =         {  0x40150000,  0x1000  },
    [IBEX_DEV_PWRMGR] =         {  0x40400000,  0x1000  },
    [IBEX_DEV_RSTMGR] =         {  0x40410000,  0x1000  },
    [IBEX_DEV_CLKMGR] =         {  0x40420000,  0x1000  },
    [IBEX_DEV_PINMUX] =         {  0x40460000,  0x1000  },
    [IBEX_DEV_PADCTRL] =        {  0x40470000,  0x1000  },
    [IBEX_DEV_FLASH_CTRL] =     {  0x41000000,  0x1000  },
    [IBEX_DEV_PLIC] =           {  0x41010000,  0x1000  },
    [IBEX_DEV_AES] =            {  0x41100000,  0x1000  },
    [IBEX_DEV_HMAC] =           {  0x41110000,  0x1000  },
    [IBEX_DEV_KMAC] =           {  0x41120000,  0x1000  },
    [IBEX_DEV_KEYMGR] =         {  0x41130000,  0x1000  },
    [IBEX_DEV_CSRNG] =          {  0x41150000,  0x1000  },
    [IBEX_DEV_ENTROPY] =        {  0x41160000,  0x1000  },
    [IBEX_DEV_EDNO] =           {  0x41170000,  0x1000  },
    [IBEX_DEV_EDN1] =           {  0x41180000,  0x1000  },
    [IBEX_DEV_ALERT_HANDLER] =  {  0x411b0000,  0x1000  },
    [IBEX_DEV_NMI_GEN] =        {  0x411c0000,  0x1000  },
    [IBEX_DEV_OTBN] =           {  0x411d0000,  0x10000 },
    [IBEX_DEV_PERI] =           {  0x411f0000,  0x10000 },
    [IBEX_DEV_FLASH_VIRTUAL] =  {  0x80000000,  0x80000 },
};

static void opentitan_board_init(MachineState *machine)
{
    const MemMapEntry *memmap = ibex_memmap;
    OpenTitanState *s = g_new0(OpenTitanState, 1);
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            TYPE_RISCV_IBEX_SOC);
    qdev_realize(DEVICE(&s->soc), NULL, &error_abort);

    memory_region_init_ram(main_mem, NULL, "riscv.lowrisc.ibex.ram",
        memmap[IBEX_DEV_RAM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[IBEX_DEV_RAM].base, main_mem);

    if (machine->firmware) {
        riscv_load_firmware(machine->firmware, memmap[IBEX_DEV_RAM].base, NULL);
    }

    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename,
                          memmap[IBEX_DEV_RAM].base, NULL);
    }
}

static void opentitan_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Board compatible with OpenTitan";
    mc->init = opentitan_board_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = TYPE_RISCV_CPU_IBEX;
}

DEFINE_MACHINE("opentitan", opentitan_machine_init)

static void lowrisc_ibex_soc_init(Object *obj)
{
    LowRISCIbexSoCState *s = RISCV_IBEX_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus, TYPE_RISCV_HART_ARRAY);

    object_initialize_child(obj, "plic", &s->plic, TYPE_IBEX_PLIC);

    object_initialize_child(obj, "uart", &s->uart, TYPE_IBEX_UART);

    object_initialize_child(obj, "timer", &s->timer, TYPE_IBEX_TIMER);
}

static void lowrisc_ibex_soc_realize(DeviceState *dev_soc, Error **errp)
{
    const MemMapEntry *memmap = ibex_memmap;
    MachineState *ms = MACHINE(qdev_get_machine());
    LowRISCIbexSoCState *s = RISCV_IBEX_SOC(dev_soc);
    MemoryRegion *sys_mem = get_system_memory();
    int i;

    object_property_set_str(OBJECT(&s->cpus), "cpu-type", ms->cpu_type,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", ms->smp.cpus,
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", 0x8080, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    /* Boot ROM */
    memory_region_init_rom(&s->rom, OBJECT(dev_soc), "riscv.lowrisc.ibex.rom",
                           memmap[IBEX_DEV_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[IBEX_DEV_ROM].base, &s->rom);

    /* Flash memory */
    memory_region_init_rom(&s->flash_mem, OBJECT(dev_soc), "riscv.lowrisc.ibex.flash",
                           memmap[IBEX_DEV_FLASH].size, &error_fatal);
    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "riscv.lowrisc.ibex.flash_virtual", &s->flash_mem, 0,
                             memmap[IBEX_DEV_FLASH_VIRTUAL].size);
    memory_region_add_subregion(sys_mem, memmap[IBEX_DEV_FLASH].base,
                                &s->flash_mem);
    memory_region_add_subregion(sys_mem, memmap[IBEX_DEV_FLASH_VIRTUAL].base,
                                &s->flash_alias);

    /* PLIC */
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->plic), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->plic), 0, memmap[IBEX_DEV_PLIC].base);

    for (i = 0; i < ms->smp.cpus; i++) {
        CPUState *cpu = qemu_get_cpu(i);

        qdev_connect_gpio_out(DEVICE(&s->plic), i,
                              qdev_get_gpio_in(DEVICE(cpu), IRQ_M_EXT));
    }

    /* UART */
    qdev_prop_set_chr(DEVICE(&(s->uart)), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->uart), 0, memmap[IBEX_DEV_UART].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart),
                       0, qdev_get_gpio_in(DEVICE(&s->plic),
                       IBEX_UART0_TX_WATERMARK_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart),
                       1, qdev_get_gpio_in(DEVICE(&s->plic),
                       IBEX_UART0_RX_WATERMARK_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart),
                       2, qdev_get_gpio_in(DEVICE(&s->plic),
                       IBEX_UART0_TX_EMPTY_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uart),
                       3, qdev_get_gpio_in(DEVICE(&s->plic),
                       IBEX_UART0_RX_OVERFLOW_IRQ));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->timer), errp)) {
        return;
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0, memmap[IBEX_DEV_TIMER].base);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->timer),
                       0, qdev_get_gpio_in(DEVICE(&s->plic),
                       IBEX_TIMER_TIMEREXPIRED0_0));
    qdev_connect_gpio_out(DEVICE(&s->timer), 0,
                          qdev_get_gpio_in(DEVICE(qemu_get_cpu(0)),
                                           IRQ_M_TIMER));

    create_unimplemented_device("riscv.lowrisc.ibex.gpio",
        memmap[IBEX_DEV_GPIO].base, memmap[IBEX_DEV_GPIO].size);
    create_unimplemented_device("riscv.lowrisc.ibex.spi",
        memmap[IBEX_DEV_SPI].base, memmap[IBEX_DEV_SPI].size);
    create_unimplemented_device("riscv.lowrisc.ibex.i2c",
        memmap[IBEX_DEV_I2C].base, memmap[IBEX_DEV_I2C].size);
    create_unimplemented_device("riscv.lowrisc.ibex.pattgen",
        memmap[IBEX_DEV_PATTGEN].base, memmap[IBEX_DEV_PATTGEN].size);
    create_unimplemented_device("riscv.lowrisc.ibex.sensor_ctrl",
        memmap[IBEX_DEV_SENSOR_CTRL].base, memmap[IBEX_DEV_SENSOR_CTRL].size);
    create_unimplemented_device("riscv.lowrisc.ibex.otp_ctrl",
        memmap[IBEX_DEV_OTP_CTRL].base, memmap[IBEX_DEV_OTP_CTRL].size);
    create_unimplemented_device("riscv.lowrisc.ibex.pwrmgr",
        memmap[IBEX_DEV_PWRMGR].base, memmap[IBEX_DEV_PWRMGR].size);
    create_unimplemented_device("riscv.lowrisc.ibex.rstmgr",
        memmap[IBEX_DEV_RSTMGR].base, memmap[IBEX_DEV_RSTMGR].size);
    create_unimplemented_device("riscv.lowrisc.ibex.clkmgr",
        memmap[IBEX_DEV_CLKMGR].base, memmap[IBEX_DEV_CLKMGR].size);
    create_unimplemented_device("riscv.lowrisc.ibex.pinmux",
        memmap[IBEX_DEV_PINMUX].base, memmap[IBEX_DEV_PINMUX].size);
    create_unimplemented_device("riscv.lowrisc.ibex.padctrl",
        memmap[IBEX_DEV_PADCTRL].base, memmap[IBEX_DEV_PADCTRL].size);
    create_unimplemented_device("riscv.lowrisc.ibex.usbdev",
        memmap[IBEX_DEV_USBDEV].base, memmap[IBEX_DEV_USBDEV].size);
    create_unimplemented_device("riscv.lowrisc.ibex.flash_ctrl",
        memmap[IBEX_DEV_FLASH_CTRL].base, memmap[IBEX_DEV_FLASH_CTRL].size);
    create_unimplemented_device("riscv.lowrisc.ibex.aes",
        memmap[IBEX_DEV_AES].base, memmap[IBEX_DEV_AES].size);
    create_unimplemented_device("riscv.lowrisc.ibex.hmac",
        memmap[IBEX_DEV_HMAC].base, memmap[IBEX_DEV_HMAC].size);
    create_unimplemented_device("riscv.lowrisc.ibex.kmac",
        memmap[IBEX_DEV_KMAC].base, memmap[IBEX_DEV_KMAC].size);
    create_unimplemented_device("riscv.lowrisc.ibex.keymgr",
        memmap[IBEX_DEV_KEYMGR].base, memmap[IBEX_DEV_KEYMGR].size);
    create_unimplemented_device("riscv.lowrisc.ibex.csrng",
        memmap[IBEX_DEV_CSRNG].base, memmap[IBEX_DEV_CSRNG].size);
    create_unimplemented_device("riscv.lowrisc.ibex.entropy",
        memmap[IBEX_DEV_ENTROPY].base, memmap[IBEX_DEV_ENTROPY].size);
    create_unimplemented_device("riscv.lowrisc.ibex.edn0",
        memmap[IBEX_DEV_EDNO].base, memmap[IBEX_DEV_EDNO].size);
    create_unimplemented_device("riscv.lowrisc.ibex.edn1",
        memmap[IBEX_DEV_EDN1].base, memmap[IBEX_DEV_EDN1].size);
    create_unimplemented_device("riscv.lowrisc.ibex.alert_handler",
        memmap[IBEX_DEV_ALERT_HANDLER].base, memmap[IBEX_DEV_ALERT_HANDLER].size);
    create_unimplemented_device("riscv.lowrisc.ibex.nmi_gen",
        memmap[IBEX_DEV_NMI_GEN].base, memmap[IBEX_DEV_NMI_GEN].size);
    create_unimplemented_device("riscv.lowrisc.ibex.otbn",
        memmap[IBEX_DEV_OTBN].base, memmap[IBEX_DEV_OTBN].size);
    create_unimplemented_device("riscv.lowrisc.ibex.peri",
        memmap[IBEX_DEV_PERI].base, memmap[IBEX_DEV_PERI].size);
}

static void lowrisc_ibex_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = lowrisc_ibex_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo lowrisc_ibex_soc_type_info = {
    .name = TYPE_RISCV_IBEX_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(LowRISCIbexSoCState),
    .instance_init = lowrisc_ibex_soc_init,
    .class_init = lowrisc_ibex_soc_class_init,
};

static void lowrisc_ibex_soc_register_types(void)
{
    type_register_static(&lowrisc_ibex_soc_type_info);
}

type_init(lowrisc_ibex_soc_register_types)
