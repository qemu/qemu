/*
 * QEMU HPPA hardware system emulator.
 * Copyright 2018 Helge Deller <deller@gmx.de>
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "elf.h"
#include "hw/loader.h"
#include "hw/boards.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "hw/timer/mc146818rtc.h"
#include "hw/ide.h"
#include "hw/timer/i8254.h"
#include "hw/char/serial.h"
#include "qemu/cutils.h"
#include "qapi/error.h"


static void machine_hppa_init(MachineState *machine)
{
}

static void machine_hppa_machine_init(MachineClass *mc)
{
    mc->desc = "HPPA generic machine";
    mc->init = machine_hppa_init;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = 1;
    mc->is_default = 1;
    mc->default_ram_size = 512 * M_BYTE;
    mc->default_boot_order = "cd";
}

DEFINE_MACHINE("hppa", machine_hppa_machine_init)
