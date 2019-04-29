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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "hw/arm/pxa.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "hw/net/smc91c111.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"
#include "cpu.h"

static const int sector_len = 128 * 1024;

static void connex_init(MachineState *machine)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;
    int be;
    MemoryRegion *address_space_mem = get_system_memory();

    uint32_t connex_rom = 0x01000000;
    uint32_t connex_ram = 0x04000000;

    cpu = pxa255_init(address_space_mem, connex_ram);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        error_report("A flash image must be given with the "
                     "'pflash' parameter");
        exit(1);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    if (!pflash_cfi01_register(0x00000000, "connext.rom", connex_rom,
                               dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                               sector_len, 2, 0, 0, 0, 0, be)) {
        error_report("Error registering flash memory");
        exit(1);
    }

    /* Interrupt line of NIC is connected to GPIO line 36 */
    smc91c111_init(&nd_table[0], 0x04000300,
                    qdev_get_gpio_in(cpu->gpio, 36));
}

static void verdex_init(MachineState *machine)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;
    int be;
    MemoryRegion *address_space_mem = get_system_memory();

    uint32_t verdex_rom = 0x02000000;
    uint32_t verdex_ram = 0x10000000;

    cpu = pxa270_init(address_space_mem, verdex_ram, machine->cpu_type);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        error_report("A flash image must be given with the "
                     "'pflash' parameter");
        exit(1);
    }

#ifdef TARGET_WORDS_BIGENDIAN
    be = 1;
#else
    be = 0;
#endif
    if (!pflash_cfi01_register(0x00000000, "verdex.rom", verdex_rom,
                               dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                               sector_len, 2, 0, 0, 0, 0, be)) {
        error_report("Error registering flash memory");
        exit(1);
    }

    /* Interrupt line of NIC is connected to GPIO line 99 */
    smc91c111_init(&nd_table[0], 0x04000300,
                    qdev_get_gpio_in(cpu->gpio, 99));
}

static void connex_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Gumstix Connex (PXA255)";
    mc->init = connex_init;
    mc->ignore_memory_transaction_failures = true;
}

static const TypeInfo connex_type = {
    .name = MACHINE_TYPE_NAME("connex"),
    .parent = TYPE_MACHINE,
    .class_init = connex_class_init,
};

static void verdex_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Gumstix Verdex (PXA270)";
    mc->init = verdex_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("pxa270-c0");
}

static const TypeInfo verdex_type = {
    .name = MACHINE_TYPE_NAME("verdex"),
    .parent = TYPE_MACHINE,
    .class_init = verdex_class_init,
};

static void gumstix_machine_init(void)
{
    type_register_static(&connex_type);
    type_register_static(&verdex_type);
}

type_init(gumstix_machine_init)
