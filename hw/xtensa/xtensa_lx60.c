/*
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "hw/char/serial.h"
#include "net/net.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "sysemu/blockdev.h"
#include "sysemu/char.h"
#include "xtensa_bootparam.h"

typedef struct LxBoardDesc {
    hwaddr flash_base;
    size_t flash_size;
    size_t flash_sector_size;
    size_t sram_size;
} LxBoardDesc;

typedef struct Lx60FpgaState {
    MemoryRegion iomem;
    uint32_t leds;
    uint32_t switches;
} Lx60FpgaState;

static void lx60_fpga_reset(void *opaque)
{
    Lx60FpgaState *s = opaque;

    s->leds = 0;
    s->switches = 0;
}

static uint64_t lx60_fpga_read(void *opaque, hwaddr addr,
        unsigned size)
{
    Lx60FpgaState *s = opaque;

    switch (addr) {
    case 0x0: /*build date code*/
        return 0x09272011;

    case 0x4: /*processor clock frequency, Hz*/
        return 10000000;

    case 0x8: /*LEDs (off = 0, on = 1)*/
        return s->leds;

    case 0xc: /*DIP switches (off = 0, on = 1)*/
        return s->switches;
    }
    return 0;
}

static void lx60_fpga_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    Lx60FpgaState *s = opaque;

    switch (addr) {
    case 0x8: /*LEDs (off = 0, on = 1)*/
        s->leds = val;
        break;

    case 0x10: /*board reset*/
        if (val == 0xdead) {
            qemu_system_reset_request();
        }
        break;
    }
}

static const MemoryRegionOps lx60_fpga_ops = {
    .read = lx60_fpga_read,
    .write = lx60_fpga_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static Lx60FpgaState *lx60_fpga_init(MemoryRegion *address_space,
        hwaddr base)
{
    Lx60FpgaState *s = g_malloc(sizeof(Lx60FpgaState));

    memory_region_init_io(&s->iomem, NULL, &lx60_fpga_ops, s,
            "lx60.fpga", 0x10000);
    memory_region_add_subregion(address_space, base, &s->iomem);
    lx60_fpga_reset(s);
    qemu_register_reset(lx60_fpga_reset, s);
    return s;
}

static void lx60_net_init(MemoryRegion *address_space,
        hwaddr base,
        hwaddr descriptors,
        hwaddr buffers,
        qemu_irq irq, NICInfo *nd)
{
    DeviceState *dev;
    SysBusDevice *s;
    MemoryRegion *ram;

    dev = qdev_create(NULL, "open_eth");
    qdev_set_nic_properties(dev, nd);
    qdev_init_nofail(dev);

    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, irq);
    memory_region_add_subregion(address_space, base,
            sysbus_mmio_get_region(s, 0));
    memory_region_add_subregion(address_space, descriptors,
            sysbus_mmio_get_region(s, 1));

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, OBJECT(s), "open_eth.ram", 16384);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space, buffers, ram);
}

static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

static void lx60_reset(void *opaque)
{
    XtensaCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static void lx_init(const LxBoardDesc *board, MachineState *machine)
{
#ifdef TARGET_WORDS_BIGENDIAN
    int be = 1;
#else
    int be = 0;
#endif
    MemoryRegion *system_memory = get_system_memory();
    XtensaCPU *cpu = NULL;
    CPUXtensaState *env = NULL;
    MemoryRegion *ram, *rom, *system_io;
    DriveInfo *dinfo;
    pflash_t *flash = NULL;
    const char *cpu_model = machine->cpu_model;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    int n;

    if (!cpu_model) {
        cpu_model = XTENSA_DEFAULT_CPU_MODEL;
    }

    for (n = 0; n < smp_cpus; n++) {
        cpu = cpu_xtensa_init(cpu_model);
        if (cpu == NULL) {
            fprintf(stderr, "Unable to find CPU definition\n");
            exit(1);
        }
        env = &cpu->env;

        env->sregs[PRID] = n;
        qemu_register_reset(lx60_reset, cpu);
        /* Need MMU initialized prior to ELF loading,
         * so that ELF gets loaded into virtual addresses
         */
        cpu_reset(CPU(cpu));
    }

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram(ram, NULL, "lx60.dram", machine->ram_size);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(system_memory, 0, ram);

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init(system_io, NULL, "lx60.io", 224 * 1024 * 1024);
    memory_region_add_subregion(system_memory, 0xf0000000, system_io);
    lx60_fpga_init(system_io, 0x0d020000);
    if (nd_table[0].used) {
        lx60_net_init(system_io, 0x0d030000, 0x0d030400, 0x0d800000,
                xtensa_get_extint(env, 1), nd_table);
    }

    if (!serial_hds[0]) {
        serial_hds[0] = qemu_chr_new("serial0", "null", NULL);
    }

    serial_mm_init(system_io, 0x0d050020, 2, xtensa_get_extint(env, 0),
            115200, serial_hds[0], DEVICE_NATIVE_ENDIAN);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        flash = pflash_cfi01_register(board->flash_base,
                NULL, "lx60.io.flash", board->flash_size,
                dinfo->bdrv, board->flash_sector_size,
                board->flash_size / board->flash_sector_size,
                4, 0x0000, 0x0000, 0x0000, 0x0000, be);
        if (flash == NULL) {
            fprintf(stderr, "Unable to mount pflash\n");
            exit(1);
        }
    }

    /* Use presence of kernel file name as 'boot from SRAM' switch. */
    if (kernel_filename) {
        rom = g_malloc(sizeof(*rom));
        memory_region_init_ram(rom, NULL, "lx60.sram", board->sram_size);
        vmstate_register_ram_global(rom);
        memory_region_add_subregion(system_memory, 0xfe000000, rom);

        /* Put kernel bootparameters to the end of that SRAM */
        if (kernel_cmdline) {
            size_t cmdline_size = strlen(kernel_cmdline) + 1;
            size_t bp_size = sizeof(BpTag[4]) + cmdline_size;
            uint32_t tagptr = (0xfe000000 + board->sram_size - bp_size) & ~0xff;

            env->regs[2] = tagptr;

            tagptr = put_tag(tagptr, 0x7b0b, 0, NULL);
            if (cmdline_size > 1) {
                tagptr = put_tag(tagptr, 0x1001,
                        cmdline_size, kernel_cmdline);
            }
            tagptr = put_tag(tagptr, 0x7e0b, 0, NULL);
        }
        uint64_t elf_entry;
        uint64_t elf_lowaddr;
        int success = load_elf(kernel_filename, translate_phys_addr, cpu,
                &elf_entry, &elf_lowaddr, NULL, be, ELF_MACHINE, 0);
        if (success > 0) {
            env->pc = elf_entry;
        }
    } else {
        if (flash) {
            MemoryRegion *flash_mr = pflash_cfi01_get_memory(flash);
            MemoryRegion *flash_io = g_malloc(sizeof(*flash_io));

            memory_region_init_alias(flash_io, NULL, "lx60.flash",
                    flash_mr, 0,
                    board->flash_size < 0x02000000 ?
                    board->flash_size : 0x02000000);
            memory_region_add_subregion(system_memory, 0xfe000000,
                    flash_io);
        }
    }
}

static void xtensa_lx60_init(MachineState *machine)
{
    static const LxBoardDesc lx60_board = {
        .flash_base = 0xf8000000,
        .flash_size = 0x00400000,
        .flash_sector_size = 0x10000,
        .sram_size = 0x20000,
    };
    lx_init(&lx60_board, machine);
}

static void xtensa_lx200_init(MachineState *machine)
{
    static const LxBoardDesc lx200_board = {
        .flash_base = 0xf8000000,
        .flash_size = 0x01000000,
        .flash_sector_size = 0x20000,
        .sram_size = 0x2000000,
    };
    lx_init(&lx200_board, machine);
}

static void xtensa_ml605_init(MachineState *machine)
{
    static const LxBoardDesc ml605_board = {
        .flash_base = 0xf8000000,
        .flash_size = 0x02000000,
        .flash_sector_size = 0x20000,
        .sram_size = 0x2000000,
    };
    lx_init(&ml605_board, machine);
}

static void xtensa_kc705_init(MachineState *machine)
{
    static const LxBoardDesc kc705_board = {
        .flash_base = 0xf0000000,
        .flash_size = 0x08000000,
        .flash_sector_size = 0x20000,
        .sram_size = 0x2000000,
    };
    lx_init(&kc705_board, machine);
}

static QEMUMachine xtensa_lx60_machine = {
    .name = "lx60",
    .desc = "lx60 EVB (" XTENSA_DEFAULT_CPU_MODEL ")",
    .init = xtensa_lx60_init,
    .max_cpus = 4,
};

static QEMUMachine xtensa_lx200_machine = {
    .name = "lx200",
    .desc = "lx200 EVB (" XTENSA_DEFAULT_CPU_MODEL ")",
    .init = xtensa_lx200_init,
    .max_cpus = 4,
};

static QEMUMachine xtensa_ml605_machine = {
    .name = "ml605",
    .desc = "ml605 EVB (" XTENSA_DEFAULT_CPU_MODEL ")",
    .init = xtensa_ml605_init,
    .max_cpus = 4,
};

static QEMUMachine xtensa_kc705_machine = {
    .name = "kc705",
    .desc = "kc705 EVB (" XTENSA_DEFAULT_CPU_MODEL ")",
    .init = xtensa_kc705_init,
    .max_cpus = 4,
};

static void xtensa_lx_machines_init(void)
{
    qemu_register_machine(&xtensa_lx60_machine);
    qemu_register_machine(&xtensa_lx200_machine);
    qemu_register_machine(&xtensa_ml605_machine);
    qemu_register_machine(&xtensa_kc705_machine);
}

machine_init(xtensa_lx_machines_init);
