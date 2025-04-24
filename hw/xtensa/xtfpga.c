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

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "system/system.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "elf.h"
#include "system/memory.h"
#include "exec/tswap.h"
#include "hw/char/serial-mm.h"
#include "net/net.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "chardev/char.h"
#include "system/device_tree.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "bootparam.h"
#include "xtensa_memory.h"
#include "hw/xtensa/mx_pic.h"
#include "migration/vmstate.h"

typedef struct XtfpgaFlashDesc {
    hwaddr base;
    size_t size;
    size_t boot_base;
    size_t sector_size;
} XtfpgaFlashDesc;

typedef struct XtfpgaBoardDesc {
    const XtfpgaFlashDesc *flash;
    size_t sram_size;
    const hwaddr *io;
} XtfpgaBoardDesc;

typedef struct XtfpgaFpgaState {
    MemoryRegion iomem;
    uint32_t freq;
    uint32_t leds;
    uint32_t switches;
} XtfpgaFpgaState;

static void xtfpga_fpga_reset(void *opaque)
{
    XtfpgaFpgaState *s = opaque;

    s->leds = 0;
    s->switches = 0;
}

static uint64_t xtfpga_fpga_read(void *opaque, hwaddr addr,
        unsigned size)
{
    XtfpgaFpgaState *s = opaque;

    switch (addr) {
    case 0x0: /*build date code*/
        return 0x09272011;

    case 0x4: /*processor clock frequency, Hz*/
        return s->freq;

    case 0x8: /*LEDs (off = 0, on = 1)*/
        return s->leds;

    case 0xc: /*DIP switches (off = 0, on = 1)*/
        return s->switches;
    }
    return 0;
}

static void xtfpga_fpga_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
    XtfpgaFpgaState *s = opaque;

    switch (addr) {
    case 0x8: /*LEDs (off = 0, on = 1)*/
        s->leds = val;
        break;

    case 0x10: /*board reset*/
        if (val == 0xdead) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    }
}

static const MemoryRegionOps xtfpga_fpga_ops = {
    .read = xtfpga_fpga_read,
    .write = xtfpga_fpga_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static XtfpgaFpgaState *xtfpga_fpga_init(MemoryRegion *address_space,
                                         hwaddr base, uint32_t freq)
{
    XtfpgaFpgaState *s = g_new(XtfpgaFpgaState, 1);

    memory_region_init_io(&s->iomem, NULL, &xtfpga_fpga_ops, s,
                          "xtfpga.fpga", 0x10000);
    memory_region_add_subregion(address_space, base, &s->iomem);
    s->freq = freq;
    xtfpga_fpga_reset(s);
    qemu_register_reset(xtfpga_fpga_reset, s);
    return s;
}

static void xtfpga_net_init(MemoryRegion *address_space,
        hwaddr base,
        hwaddr descriptors,
        hwaddr buffers,
        qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;
    MemoryRegion *ram;

    dev = qemu_create_nic_device("open_eth", true, NULL);
    if (!dev) {
        return;
    }

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, irq);
    memory_region_add_subregion(address_space, base,
            sysbus_mmio_get_region(s, 0));
    memory_region_add_subregion(address_space, descriptors,
            sysbus_mmio_get_region(s, 1));

    ram = g_malloc(sizeof(*ram));
    memory_region_init_ram_nomigrate(ram, OBJECT(s), "open_eth.ram", 16 * KiB,
                           &error_fatal);
    vmstate_register_ram_global(ram);
    memory_region_add_subregion(address_space, buffers, ram);
}

static PFlashCFI01 *xtfpga_flash_init(MemoryRegion *address_space,
                                      const XtfpgaBoardDesc *board,
                                      DriveInfo *dinfo, int be)
{
    SysBusDevice *s;
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(dinfo));
    qdev_prop_set_uint32(dev, "num-blocks",
                         board->flash->size / board->flash->sector_size);
    qdev_prop_set_uint64(dev, "sector-length", board->flash->sector_size);
    qdev_prop_set_uint8(dev, "width", 2);
    qdev_prop_set_bit(dev, "big-endian", be);
    qdev_prop_set_string(dev, "name", "xtfpga.io.flash");
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(address_space, board->flash->base,
                                sysbus_mmio_get_region(s, 0));
    return PFLASH_CFI01(dev);
}

static uint64_t translate_phys_addr(void *opaque, uint64_t addr)
{
    XtensaCPU *cpu = opaque;

    return cpu_get_phys_page_debug(CPU(cpu), addr);
}

static void xtfpga_reset(void *opaque)
{
    XtensaCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}

static uint64_t xtfpga_io_read(void *opaque, hwaddr addr,
        unsigned size)
{
    return 0;
}

static void xtfpga_io_write(void *opaque, hwaddr addr,
        uint64_t val, unsigned size)
{
}

static const MemoryRegionOps xtfpga_io_ops = {
    .read = xtfpga_io_read,
    .write = xtfpga_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void xtfpga_init(const XtfpgaBoardDesc *board, MachineState *machine)
{
    MemoryRegion *system_memory = get_system_memory();
    XtensaCPU *cpu = NULL;
    CPUXtensaState *env = NULL;
    MemoryRegion *system_io;
    XtensaMxPic *mx_pic = NULL;
    qemu_irq *extints;
    DriveInfo *dinfo;
    PFlashCFI01 *flash = NULL;
    const char *kernel_filename = machine->kernel_filename;
    const char *kernel_cmdline = machine->kernel_cmdline;
    const char *dtb_filename = machine->dtb;
    const char *initrd_filename = machine->initrd_filename;
    const unsigned system_io_size = 224 * MiB;
    uint32_t freq = 10000000;
    int n;
    unsigned int smp_cpus = machine->smp.cpus;

    if (smp_cpus > 1) {
        mx_pic = xtensa_mx_pic_init(31);
        qemu_register_reset(xtensa_mx_pic_reset, mx_pic);
    }
    for (n = 0; n < smp_cpus; n++) {
        CPUXtensaState *cenv = NULL;

        cpu = XTENSA_CPU(cpu_create(machine->cpu_type));
        cenv = &cpu->env;
        if (!env) {
            env = cenv;
            freq = env->config->clock_freq_khz * 1000;
        }

        if (mx_pic) {
            MemoryRegion *mx_eri;

            mx_eri = xtensa_mx_pic_register_cpu(mx_pic,
                                                xtensa_get_extints(cenv),
                                                xtensa_get_runstall(cenv));
            memory_region_add_subregion(xtensa_get_er_region(cenv),
                                        0, mx_eri);
        }
        cenv->sregs[PRID] = n;
        xtensa_select_static_vectors(cenv, n != 0);
        qemu_register_reset(xtfpga_reset, cpu);
        /* Need MMU initialized prior to ELF loading,
         * so that ELF gets loaded into virtual addresses
         */
        cpu_reset(CPU(cpu));
    }
    if (smp_cpus > 1) {
        extints = xtensa_mx_pic_get_extints(mx_pic);
    } else {
        extints = xtensa_get_extints(env);
    }

    if (env) {
        XtensaMemory sysram = env->config->sysram;

        sysram.location[0].size = machine->ram_size;
        xtensa_create_memory_regions(&env->config->instrom, "xtensa.instrom",
                                     system_memory);
        xtensa_create_memory_regions(&env->config->instram, "xtensa.instram",
                                     system_memory);
        xtensa_create_memory_regions(&env->config->datarom, "xtensa.datarom",
                                     system_memory);
        xtensa_create_memory_regions(&env->config->dataram, "xtensa.dataram",
                                     system_memory);
        xtensa_create_memory_regions(&sysram, "xtensa.sysram",
                                     system_memory);
    }

    system_io = g_malloc(sizeof(*system_io));
    memory_region_init_io(system_io, NULL, &xtfpga_io_ops, NULL, "xtfpga.io",
                          system_io_size);
    memory_region_add_subregion(system_memory, board->io[0], system_io);
    if (board->io[1]) {
        MemoryRegion *io = g_malloc(sizeof(*io));

        memory_region_init_alias(io, NULL, "xtfpga.io.cached",
                                 system_io, 0, system_io_size);
        memory_region_add_subregion(system_memory, board->io[1], io);
    }
    xtfpga_fpga_init(system_io, 0x0d020000, freq);
    xtfpga_net_init(system_io, 0x0d030000, 0x0d030400, 0x0d800000, extints[1]);

    serial_mm_init(system_io, 0x0d050020, 2, extints[0],
                   115200, serial_hd(0), DEVICE_NATIVE_ENDIAN);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (dinfo) {
        flash = xtfpga_flash_init(system_io, board, dinfo, TARGET_BIG_ENDIAN);
    }

    /* Use presence of kernel file name as 'boot from SRAM' switch. */
    if (kernel_filename) {
        uint32_t entry_point = env->pc;
        size_t bp_size = 3 * get_tag_size(0); /* first/last and memory tags */
        uint32_t tagptr = env->config->sysrom.location[0].addr +
            board->sram_size;
        uint32_t cur_tagptr;
        BpMemInfo memory_location = {
            .type = tswap32(MEMORY_TYPE_CONVENTIONAL),
            .start = tswap32(env->config->sysram.location[0].addr),
            .end = tswap32(env->config->sysram.location[0].addr +
                           machine->ram_size),
        };
        uint32_t lowmem_end = machine->ram_size < 0x08000000 ?
            machine->ram_size : 0x08000000;
        uint32_t cur_lowmem = QEMU_ALIGN_UP(lowmem_end / 2, 4096);

        lowmem_end += env->config->sysram.location[0].addr;
        cur_lowmem += env->config->sysram.location[0].addr;

        xtensa_create_memory_regions(&env->config->sysrom, "xtensa.sysrom",
                                     system_memory);

        if (kernel_cmdline) {
            bp_size += get_tag_size(strlen(kernel_cmdline) + 1);
        }
        if (dtb_filename) {
            bp_size += get_tag_size(sizeof(uint32_t));
        }
        if (initrd_filename) {
            bp_size += get_tag_size(sizeof(BpMemInfo));
        }

        /* Put kernel bootparameters to the end of that SRAM */
        tagptr = (tagptr - bp_size) & ~0xff;
        cur_tagptr = put_tag(tagptr, BP_TAG_FIRST, 0, NULL);
        cur_tagptr = put_tag(cur_tagptr, BP_TAG_MEMORY,
                             sizeof(memory_location), &memory_location);

        if (kernel_cmdline) {
            cur_tagptr = put_tag(cur_tagptr, BP_TAG_COMMAND_LINE,
                                 strlen(kernel_cmdline) + 1, kernel_cmdline);
        }
        if (dtb_filename) {
            int fdt_size;
            void *fdt = load_device_tree(dtb_filename, &fdt_size);
            uint32_t dtb_addr = tswap32(cur_lowmem);

            if (!fdt) {
                error_report("could not load DTB '%s'", dtb_filename);
                exit(EXIT_FAILURE);
            }

            cpu_physical_memory_write(cur_lowmem, fdt, fdt_size);
            cur_tagptr = put_tag(cur_tagptr, BP_TAG_FDT,
                                 sizeof(dtb_addr), &dtb_addr);
            cur_lowmem = QEMU_ALIGN_UP(cur_lowmem + fdt_size, 4 * KiB);
            g_free(fdt);
        }
        if (initrd_filename) {
            BpMemInfo initrd_location = { 0 };
            int initrd_size = load_ramdisk(initrd_filename, cur_lowmem,
                                           lowmem_end - cur_lowmem);

            if (initrd_size < 0) {
                initrd_size = load_image_targphys(initrd_filename,
                                                  cur_lowmem,
                                                  lowmem_end - cur_lowmem);
            }
            if (initrd_size < 0) {
                error_report("could not load initrd '%s'", initrd_filename);
                exit(EXIT_FAILURE);
            }
            initrd_location.start = tswap32(cur_lowmem);
            initrd_location.end = tswap32(cur_lowmem + initrd_size);
            cur_tagptr = put_tag(cur_tagptr, BP_TAG_INITRD,
                                 sizeof(initrd_location), &initrd_location);
            cur_lowmem = QEMU_ALIGN_UP(cur_lowmem + initrd_size, 4 * KiB);
        }
        cur_tagptr = put_tag(cur_tagptr, BP_TAG_LAST, 0, NULL);
        env->regs[2] = tagptr;

        uint64_t elf_entry;
        int success = load_elf(kernel_filename, NULL, translate_phys_addr, cpu,
                               &elf_entry, NULL, NULL, NULL,
                               TARGET_BIG_ENDIAN ? ELFDATA2MSB : ELFDATA2LSB,
                               EM_XTENSA, 0, 0);
        if (success > 0) {
            entry_point = elf_entry;
        } else {
            hwaddr ep;
            int is_linux;
            success = load_uimage(kernel_filename, &ep, NULL, &is_linux,
                                  translate_phys_addr, cpu);
            if (success > 0 && is_linux) {
                entry_point = ep;
            } else {
                error_report("could not load kernel '%s'",
                             kernel_filename);
                exit(EXIT_FAILURE);
            }
        }
        if (entry_point != env->pc) {
            uint8_t boot_be[] = {
                0x60, 0x00, 0x08,       /* j    1f */
                0x00,                   /* .literal_position */
                0x00, 0x00, 0x00, 0x00, /* .literal entry_pc */
                0x00, 0x00, 0x00, 0x00, /* .literal entry_a2 */
                                        /* 1: */
                0x10, 0xff, 0xfe,       /* l32r a0, entry_pc */
                0x12, 0xff, 0xfe,       /* l32r a2, entry_a2 */
                0x0a, 0x00, 0x00,       /* jx   a0 */
            };
            uint8_t boot_le[] = {
                0x06, 0x02, 0x00,       /* j    1f */
                0x00,                   /* .literal_position */
                0x00, 0x00, 0x00, 0x00, /* .literal entry_pc */
                0x00, 0x00, 0x00, 0x00, /* .literal entry_a2 */
                                        /* 1: */
                0x01, 0xfe, 0xff,       /* l32r a0, entry_pc */
                0x21, 0xfe, 0xff,       /* l32r a2, entry_a2 */
                0xa0, 0x00, 0x00,       /* jx   a0 */
            };
            const size_t boot_sz = TARGET_BIG_ENDIAN ? sizeof(boot_be)
                                                     : sizeof(boot_le);
            uint8_t *boot = TARGET_BIG_ENDIAN ? boot_be : boot_le;
            uint32_t entry_pc = tswap32(entry_point);
            uint32_t entry_a2 = tswap32(tagptr);

            memcpy(boot + 4, &entry_pc, sizeof(entry_pc));
            memcpy(boot + 8, &entry_a2, sizeof(entry_a2));
            cpu_physical_memory_write(env->pc, boot, boot_sz);
        }
    } else {
        if (flash) {
            MemoryRegion *flash_mr = pflash_cfi01_get_memory(flash);
            MemoryRegion *flash_io = g_malloc(sizeof(*flash_io));
            uint32_t size = env->config->sysrom.location[0].size;

            if (board->flash->size - board->flash->boot_base < size) {
                size = board->flash->size - board->flash->boot_base;
            }

            memory_region_init_alias(flash_io, NULL, "xtfpga.flash",
                                     flash_mr, board->flash->boot_base, size);
            memory_region_add_subregion(system_memory,
                                        env->config->sysrom.location[0].addr,
                                        flash_io);
        } else {
            xtensa_create_memory_regions(&env->config->sysrom, "xtensa.sysrom",
                                         system_memory);
        }
    }
}

#define XTFPGA_MMU_RESERVED_MEMORY_SIZE (128 * MiB)

static const hwaddr xtfpga_mmu_io[2] = {
    0xf0000000,
};

static const hwaddr xtfpga_nommu_io[2] = {
    0x90000000,
    0x70000000,
};

static const XtfpgaFlashDesc lx60_flash = {
    .base = 0x08000000,
    .size = 0x00400000,
    .sector_size = 0x10000,
};

static void xtfpga_lx60_init(MachineState *machine)
{
    static const XtfpgaBoardDesc lx60_board = {
        .flash = &lx60_flash,
        .sram_size = 0x20000,
        .io = xtfpga_mmu_io,
    };
    xtfpga_init(&lx60_board, machine);
}

static void xtfpga_lx60_nommu_init(MachineState *machine)
{
    static const XtfpgaBoardDesc lx60_board = {
        .flash = &lx60_flash,
        .sram_size = 0x20000,
        .io = xtfpga_nommu_io,
    };
    xtfpga_init(&lx60_board, machine);
}

static const XtfpgaFlashDesc lx200_flash = {
    .base = 0x08000000,
    .size = 0x01000000,
    .sector_size = 0x20000,
};

static void xtfpga_lx200_init(MachineState *machine)
{
    static const XtfpgaBoardDesc lx200_board = {
        .flash = &lx200_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_mmu_io,
    };
    xtfpga_init(&lx200_board, machine);
}

static void xtfpga_lx200_nommu_init(MachineState *machine)
{
    static const XtfpgaBoardDesc lx200_board = {
        .flash = &lx200_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_nommu_io,
    };
    xtfpga_init(&lx200_board, machine);
}

static const XtfpgaFlashDesc ml605_flash = {
    .base = 0x08000000,
    .size = 0x01000000,
    .sector_size = 0x20000,
};

static void xtfpga_ml605_init(MachineState *machine)
{
    static const XtfpgaBoardDesc ml605_board = {
        .flash = &ml605_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_mmu_io,
    };
    xtfpga_init(&ml605_board, machine);
}

static void xtfpga_ml605_nommu_init(MachineState *machine)
{
    static const XtfpgaBoardDesc ml605_board = {
        .flash = &ml605_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_nommu_io,
    };
    xtfpga_init(&ml605_board, machine);
}

static const XtfpgaFlashDesc kc705_flash = {
    .base = 0x00000000,
    .size = 0x08000000,
    .boot_base = 0x06000000,
    .sector_size = 0x20000,
};

static void xtfpga_kc705_init(MachineState *machine)
{
    static const XtfpgaBoardDesc kc705_board = {
        .flash = &kc705_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_mmu_io,
    };
    xtfpga_init(&kc705_board, machine);
}

static void xtfpga_kc705_nommu_init(MachineState *machine)
{
    static const XtfpgaBoardDesc kc705_board = {
        .flash = &kc705_flash,
        .sram_size = 0x2000000,
        .io = xtfpga_nommu_io,
    };
    xtfpga_init(&kc705_board, machine);
}

static void xtfpga_lx60_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "lx60 EVB (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->init = xtfpga_lx60_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
    mc->default_ram_size = 64 * MiB;
}

static const TypeInfo xtfpga_lx60_type = {
    .name = MACHINE_TYPE_NAME("lx60"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_lx60_class_init,
};

static void xtfpga_lx60_nommu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "lx60 noMMU EVB (" XTENSA_DEFAULT_CPU_NOMMU_MODEL ")";
    mc->init = xtfpga_lx60_nommu_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_NOMMU_TYPE;
    mc->default_ram_size = 64 * MiB;
}

static const TypeInfo xtfpga_lx60_nommu_type = {
    .name = MACHINE_TYPE_NAME("lx60-nommu"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_lx60_nommu_class_init,
};

static void xtfpga_lx200_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "lx200 EVB (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->init = xtfpga_lx200_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
    mc->default_ram_size = 96 * MiB;
}

static const TypeInfo xtfpga_lx200_type = {
    .name = MACHINE_TYPE_NAME("lx200"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_lx200_class_init,
};

static void xtfpga_lx200_nommu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "lx200 noMMU EVB (" XTENSA_DEFAULT_CPU_NOMMU_MODEL ")";
    mc->init = xtfpga_lx200_nommu_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_NOMMU_TYPE;
    mc->default_ram_size = 96 * MiB;
}

static const TypeInfo xtfpga_lx200_nommu_type = {
    .name = MACHINE_TYPE_NAME("lx200-nommu"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_lx200_nommu_class_init,
};

static void xtfpga_ml605_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ml605 EVB (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->init = xtfpga_ml605_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
    mc->default_ram_size = 512 * MiB - XTFPGA_MMU_RESERVED_MEMORY_SIZE;
}

static const TypeInfo xtfpga_ml605_type = {
    .name = MACHINE_TYPE_NAME("ml605"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_ml605_class_init,
};

static void xtfpga_ml605_nommu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "ml605 noMMU EVB (" XTENSA_DEFAULT_CPU_NOMMU_MODEL ")";
    mc->init = xtfpga_ml605_nommu_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_NOMMU_TYPE;
    mc->default_ram_size = 256 * MiB;
}

static const TypeInfo xtfpga_ml605_nommu_type = {
    .name = MACHINE_TYPE_NAME("ml605-nommu"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_ml605_nommu_class_init,
};

static void xtfpga_kc705_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "kc705 EVB (" XTENSA_DEFAULT_CPU_MODEL ")";
    mc->init = xtfpga_kc705_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_TYPE;
    mc->default_ram_size = 1 * GiB - XTFPGA_MMU_RESERVED_MEMORY_SIZE;
}

static const TypeInfo xtfpga_kc705_type = {
    .name = MACHINE_TYPE_NAME("kc705"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_kc705_class_init,
};

static void xtfpga_kc705_nommu_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "kc705 noMMU EVB (" XTENSA_DEFAULT_CPU_NOMMU_MODEL ")";
    mc->init = xtfpga_kc705_nommu_init;
    mc->max_cpus = 32;
    mc->default_cpu_type = XTENSA_DEFAULT_CPU_NOMMU_TYPE;
    mc->default_ram_size = 256 * MiB;
}

static const TypeInfo xtfpga_kc705_nommu_type = {
    .name = MACHINE_TYPE_NAME("kc705-nommu"),
    .parent = TYPE_MACHINE,
    .class_init = xtfpga_kc705_nommu_class_init,
};

static void xtfpga_machines_init(void)
{
    type_register_static(&xtfpga_lx60_type);
    type_register_static(&xtfpga_lx200_type);
    type_register_static(&xtfpga_ml605_type);
    type_register_static(&xtfpga_kc705_type);
    type_register_static(&xtfpga_lx60_nommu_type);
    type_register_static(&xtfpga_lx200_nommu_type);
    type_register_static(&xtfpga_ml605_nommu_type);
    type_register_static(&xtfpga_kc705_nommu_type);
}

type_init(xtfpga_machines_init)
