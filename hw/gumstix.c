/*
 * Gumstix Platforms
 *
 * Copyright (c) 2007 by Thorsten Zitterell <info@bitmux.org>
 *
 * Code based on spitz platform by Andrzej Zaborowski <balrog@zabor.org>
 *
 * This code is licensed under the GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
 
/* 
 * Example usage:
 * 
 * connex:
 * =======
 * create image:
 * # dd of=flash bs=1k count=16k if=/dev/zero
 * # dd of=flash bs=1k conv=notrunc if=u-boot.bin
 * # dd of=flash bs=1k conv=notrunc seek=256 if=rootfs.arm_nofpu.jffs2
 * start it:
 * # qemu-system-arm -M connex -pflash flash -monitor null -nographic
 *
 * verdex:
 * =======
 * create image:
 * # dd of=flash bs=1k count=32k if=/dev/zero
 * # dd of=flash bs=1k conv=notrunc if=u-boot.bin
 * # dd of=flash bs=1k conv=notrunc seek=256 if=rootfs.arm_nofpu.jffs2
 * # dd of=flash bs=1k conv=notrunc seek=31744 if=uImage
 * start it:
 * # qemu-system-arm -M verdex -pflash flash -monitor null -nographic -m 289
 */

#include "hw.h"
#include "pxa.h"
#include "net.h"
#include "flash.h"
#include "devices.h"
#include "boards.h"
#include "blockdev.h"
#include "exec-memory.h"

static const int sector_len = 128 * 1024;

static void connex_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;
    int be;
    MemoryRegion *address_space_mem = get_system_memory();

    uint32_t connex_rom = 0x01000000;
    uint32_t connex_ram = 0x04000000;

    cpu = pxa255_init(address_space_mem, connex_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo) {
        fprintf(stderr, "A flash image must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    if (!pflash_cfi01_register(0x00000000, NULL, "connext.rom", connex_rom,
                               dinfo->bdrv, sector_len, connex_rom / sector_len,
                               2, 0, 0, 0, 0, be)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
        exit(1);
    }

    /* Interrupt line of NIC is connected to GPIO line 36 */
    smc91c111_init(&nd_table[0], 0x04000300,
                    qdev_get_gpio_in(cpu->gpio, 36));
}

static void verdex_init(ram_addr_t ram_size,
                const char *boot_device,
                const char *kernel_filename, const char *kernel_cmdline,
                const char *initrd_filename, const char *cpu_model)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;
    int be;
    MemoryRegion *address_space_mem = get_system_memory();

    uint32_t verdex_rom = 0x02000000;
    uint32_t verdex_ram = 0x10000000;

    cpu = pxa270_init(address_space_mem, verdex_ram, cpu_model ?: "pxa270-c0");

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo) {
        fprintf(stderr, "A flash image must be given with the "
                "'pflash' parameter\n");
        exit(1);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    if (!pflash_cfi01_register(0x00000000, NULL, "verdex.rom", verdex_rom,
                               dinfo->bdrv, sector_len, verdex_rom / sector_len,
                               2, 0, 0, 0, 0, be)) {
        fprintf(stderr, "qemu: Error registering flash memory.\n");
        exit(1);
    }

    /* Interrupt line of NIC is connected to GPIO line 99 */
    smc91c111_init(&nd_table[0], 0x04000300,
                    qdev_get_gpio_in(cpu->gpio, 99));
}

static QEMUMachine connex_machine = {
    .name = "connex",
    .desc = "Gumstix Connex (PXA255)",
    .init = connex_init,
};

static QEMUMachine verdex_machine = {
    .name = "verdex",
    .desc = "Gumstix Verdex (PXA270)",
    .init = verdex_init,
};

static void gumstix_machine_init(void)
{
    qemu_register_machine(&connex_machine);
    qemu_register_machine(&verdex_machine);
}

machine_init(gumstix_machine_init);
