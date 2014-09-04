/*
 * Empty machine
 *
 * Copyright IBM, Corp. 2012
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/boards.h"

static void machine_none_init(MachineState *machine)
{
}

static QEMUMachine machine_none = {
    .name = "none",
    .desc = "empty machine",
    .init = machine_none_init,
    .max_cpus = 0,
};

static void register_machines(void)
{
    qemu_register_machine(&machine_none);
}

machine_init(register_machines);

