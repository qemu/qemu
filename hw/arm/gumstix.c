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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "hw/arm/pxa.h"
#include "net/net.h"
#include "hw/block/flash.h"
#include "hw/net/smc91c111.h"
#include "hw/boards.h"
#include "exec/address-spaces.h"
#include "sysemu/qtest.h"

#define CONNEX_FLASH_SIZE   (16 * MiB)
#define CONNEX_RAM_SIZE     (64 * MiB)

#define VERDEX_FLASH_SIZE   (32 * MiB)
#define VERDEX_RAM_SIZE     (256 * MiB)

#define FLASH_SECTOR_SIZE   (128 * KiB)

static void connex_init(MachineState *machine)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;

    cpu = pxa255_init(CONNEX_RAM_SIZE);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        error_report("A flash image must be given with the "
                     "'pflash' parameter");
        exit(1);
    }

    /* Numonyx RC28F128J3F75 */
    pflash_cfi01_register(0x00000000, "connext.rom", CONNEX_FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          FLASH_SECTOR_SIZE, 2, 0, 0, 0, 0, 0);

    /* Interrupt line of NIC is connected to GPIO line 36 */
    smc91c111_init(0x04000300, qdev_get_gpio_in(cpu->gpio, 36));
}

static void verdex_init(MachineState *machine)
{
    PXA2xxState *cpu;
    DriveInfo *dinfo;

    cpu = pxa270_init(VERDEX_RAM_SIZE, machine->cpu_type);

    dinfo = drive_get(IF_PFLASH, 0, 0);
    if (!dinfo && !qtest_enabled()) {
        error_report("A flash image must be given with the "
                     "'pflash' parameter");
        exit(1);
    }

    /* Micron RC28F256P30TFA */
    pflash_cfi01_register(0x00000000, "verdex.rom", VERDEX_FLASH_SIZE,
                          dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                          FLASH_SECTOR_SIZE, 2, 0, 0, 0, 0, 0);

    /* Interrupt line of NIC is connected to GPIO line 99 */
    smc91c111_init(0x04000300, qdev_get_gpio_in(cpu->gpio, 99));
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

    mc->desc = "Gumstix Verdex Pro XL6P COMs (PXA270)";
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
