/*
 * QEMU RISC-V Board Compatible with SiFive Freedom E SDK
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * Provides a board compatible with the SiFive Freedom E SDK:
 *
 * 0) UART
 * 1) CLINT (Core Level Interruptor)
 * 2) PLIC (Platform Level Interrupt Controller)
 * 3) PRCI (Power, Reset, Clock, Interrupt)
 * 4) Registers emulated as RAM: AON, GPIO, QSPI, PWM
 * 5) Flash memory emulated as RAM
 *
 * The Mask ROM reset vector jumps to the flash payload at 0x2040_0000.
 * The OTP ROM and Flash boot code will be emulated in a future version.
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
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/sysbus.h"
#include "hw/char/serial.h"
#include "target/riscv/cpu.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_plic.h"
#include "hw/riscv/sifive_clint.h"
#include "hw/riscv/sifive_prci.h"
#include "hw/riscv/sifive_uart.h"
#include "hw/riscv/sifive_e.h"
#include "hw/riscv/boot.h"
#include "chardev/char.h"
#include "sysemu/arch_init.h"
#include "exec/address-spaces.h"

static const struct MemmapEntry {
    hwaddr base;
    hwaddr size;
} sifive_e_memmap[] = {
    [SIFIVE_E_DEBUG] =    {        0x0,      0x100 },
    [SIFIVE_E_MROM] =     {     0x1000,     0x2000 },
    [SIFIVE_E_OTP] =      {    0x20000,     0x2000 },
    [SIFIVE_E_CLINT] =    {  0x2000000,    0x10000 },
    [SIFIVE_E_PLIC] =     {  0xc000000,  0x4000000 },
    [SIFIVE_E_AON] =      { 0x10000000,     0x8000 },
    [SIFIVE_E_PRCI] =     { 0x10008000,     0x8000 },
    [SIFIVE_E_OTP_CTRL] = { 0x10010000,     0x1000 },
    [SIFIVE_E_GPIO0] =    { 0x10012000,     0x1000 },
    [SIFIVE_E_UART0] =    { 0x10013000,     0x1000 },
    [SIFIVE_E_QSPI0] =    { 0x10014000,     0x1000 },
    [SIFIVE_E_PWM0] =     { 0x10015000,     0x1000 },
    [SIFIVE_E_UART1] =    { 0x10023000,     0x1000 },
    [SIFIVE_E_QSPI1] =    { 0x10024000,     0x1000 },
    [SIFIVE_E_PWM1] =     { 0x10025000,     0x1000 },
    [SIFIVE_E_QSPI2] =    { 0x10034000,     0x1000 },
    [SIFIVE_E_PWM2] =     { 0x10035000,     0x1000 },
    [SIFIVE_E_XIP] =      { 0x20000000, 0x20000000 },
    [SIFIVE_E_DTIM] =     { 0x80000000,     0x4000 }
};

static void sifive_mmio_emulate(MemoryRegion *parent, const char *name,
                             uintptr_t offset, uintptr_t length)
{
    MemoryRegion *mock_mmio = g_new(MemoryRegion, 1);
    memory_region_init_ram(mock_mmio, NULL, name, length, &error_fatal);
    memory_region_add_subregion(parent, offset, mock_mmio);
}

static void riscv_sifive_e_init(MachineState *machine)
{
    const struct MemmapEntry *memmap = sifive_e_memmap;

    SiFiveEState *s = g_new0(SiFiveEState, 1);
    MemoryRegion *sys_mem = get_system_memory();
    MemoryRegion *main_mem = g_new(MemoryRegion, 1);
    int i;

    /* Initialize SoC */
    object_initialize_child(OBJECT(machine), "soc", &s->soc,
                            sizeof(s->soc), TYPE_RISCV_E_SOC,
                            &error_abort, NULL);
    object_property_set_bool(OBJECT(&s->soc), true, "realized",
                            &error_abort);

    /* Data Tightly Integrated Memory */
    memory_region_init_ram(main_mem, NULL, "riscv.sifive.e.ram",
        memmap[SIFIVE_E_DTIM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[SIFIVE_E_DTIM].base, main_mem);

    /* Mask ROM reset vector */
    uint32_t reset_vec[2] = {
        0x204002b7,        /* 0x1000: lui     t0,0x20400 */
        0x00028067,        /* 0x1004: jr      t0 */
    };

    /* copy in the reset vector in little_endian byte order */
    for (i = 0; i < sizeof(reset_vec) >> 2; i++) {
        reset_vec[i] = cpu_to_le32(reset_vec[i]);
    }
    rom_add_blob_fixed_as("mrom.reset", reset_vec, sizeof(reset_vec),
                          memmap[SIFIVE_E_MROM].base, &address_space_memory);

    if (machine->kernel_filename) {
        riscv_load_kernel(machine->kernel_filename);
    }
}

static void riscv_sifive_e_soc_init(Object *obj)
{
    SiFiveESoCState *s = RISCV_E_SOC(obj);

    object_initialize_child(obj, "cpus", &s->cpus,
                            sizeof(s->cpus), TYPE_RISCV_HART_ARRAY,
                            &error_abort, NULL);
    object_property_set_str(OBJECT(&s->cpus), SIFIVE_E_CPU, "cpu-type",
                            &error_abort);
    object_property_set_int(OBJECT(&s->cpus), smp_cpus, "num-harts",
                            &error_abort);
    sysbus_init_child_obj(obj, "riscv.sifive.e.gpio0",
                          &s->gpio, sizeof(s->gpio),
                          TYPE_SIFIVE_GPIO);
}

static void riscv_sifive_e_soc_realize(DeviceState *dev, Error **errp)
{
    const struct MemmapEntry *memmap = sifive_e_memmap;
    Error *err = NULL;

    SiFiveESoCState *s = RISCV_E_SOC(dev);
    MemoryRegion *sys_mem = get_system_memory();

    object_property_set_bool(OBJECT(&s->cpus), true, "realized",
                            &error_abort);

    /* Mask ROM */
    memory_region_init_rom(&s->mask_rom, NULL, "riscv.sifive.e.mrom",
        memmap[SIFIVE_E_MROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem,
        memmap[SIFIVE_E_MROM].base, &s->mask_rom);

    /* MMIO */
    s->plic = sifive_plic_create(memmap[SIFIVE_E_PLIC].base,
        (char *)SIFIVE_E_PLIC_HART_CONFIG,
        SIFIVE_E_PLIC_NUM_SOURCES,
        SIFIVE_E_PLIC_NUM_PRIORITIES,
        SIFIVE_E_PLIC_PRIORITY_BASE,
        SIFIVE_E_PLIC_PENDING_BASE,
        SIFIVE_E_PLIC_ENABLE_BASE,
        SIFIVE_E_PLIC_ENABLE_STRIDE,
        SIFIVE_E_PLIC_CONTEXT_BASE,
        SIFIVE_E_PLIC_CONTEXT_STRIDE,
        memmap[SIFIVE_E_PLIC].size);
    sifive_clint_create(memmap[SIFIVE_E_CLINT].base,
        memmap[SIFIVE_E_CLINT].size, smp_cpus,
        SIFIVE_SIP_BASE, SIFIVE_TIMECMP_BASE, SIFIVE_TIME_BASE);
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.aon",
        memmap[SIFIVE_E_AON].base, memmap[SIFIVE_E_AON].size);
    sifive_prci_create(memmap[SIFIVE_E_PRCI].base);

    /* GPIO */

    object_property_set_bool(OBJECT(&s->gpio), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* Map GPIO registers */
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, memmap[SIFIVE_E_GPIO0].base);

    /* Pass all GPIOs to the SOC layer so they are available to the board */
    qdev_pass_gpios(DEVICE(&s->gpio), dev, NULL);

    /* Connect GPIO interrupts to the PLIC */
    for (int i = 0; i < 32; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gpio), i,
                           qdev_get_gpio_in(DEVICE(s->plic),
                                            SIFIVE_E_GPIO0_IRQ0 + i));
    }

    sifive_uart_create(sys_mem, memmap[SIFIVE_E_UART0].base,
        serial_hd(0), qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_E_UART0_IRQ));
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.qspi0",
        memmap[SIFIVE_E_QSPI0].base, memmap[SIFIVE_E_QSPI0].size);
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.pwm0",
        memmap[SIFIVE_E_PWM0].base, memmap[SIFIVE_E_PWM0].size);
    sifive_uart_create(sys_mem, memmap[SIFIVE_E_UART1].base,
        serial_hd(1), qdev_get_gpio_in(DEVICE(s->plic), SIFIVE_E_UART1_IRQ));
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.qspi1",
        memmap[SIFIVE_E_QSPI1].base, memmap[SIFIVE_E_QSPI1].size);
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.pwm1",
        memmap[SIFIVE_E_PWM1].base, memmap[SIFIVE_E_PWM1].size);
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.qspi2",
        memmap[SIFIVE_E_QSPI2].base, memmap[SIFIVE_E_QSPI2].size);
    sifive_mmio_emulate(sys_mem, "riscv.sifive.e.pwm2",
        memmap[SIFIVE_E_PWM2].base, memmap[SIFIVE_E_PWM2].size);

    /* Flash memory */
    memory_region_init_ram(&s->xip_mem, NULL, "riscv.sifive.e.xip",
        memmap[SIFIVE_E_XIP].size, &error_fatal);
    memory_region_set_readonly(&s->xip_mem, true);
    memory_region_add_subregion(sys_mem, memmap[SIFIVE_E_XIP].base,
        &s->xip_mem);
}

static void riscv_sifive_e_machine_init(MachineClass *mc)
{
    mc->desc = "RISC-V Board compatible with SiFive E SDK";
    mc->init = riscv_sifive_e_init;
    mc->max_cpus = 1;
}

DEFINE_MACHINE("sifive_e", riscv_sifive_e_machine_init)

static void riscv_sifive_e_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = riscv_sifive_e_soc_realize;
    /* Reason: Uses serial_hds in realize function, thus can't be used twice */
    dc->user_creatable = false;
}

static const TypeInfo riscv_sifive_e_soc_type_info = {
    .name = TYPE_RISCV_E_SOC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(SiFiveESoCState),
    .instance_init = riscv_sifive_e_soc_init,
    .class_init = riscv_sifive_e_soc_class_init,
};

static void riscv_sifive_e_soc_register_types(void)
{
    type_register_static(&riscv_sifive_e_soc_type_info);
}

type_init(riscv_sifive_e_soc_register_types)
