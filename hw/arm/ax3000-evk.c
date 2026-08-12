/*
 * Axiado Evaluation Kit Emulation
 *
 * Author: Kuan-Jui Chiu <kchiu@axiado.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/arm/ax3000-boards.h"

static void axiado_scm3003_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Axiado SCM3003 EVK Board";
}

static const TypeInfo ax3000_evk_types[] = {
    {
        .name          = MACHINE_TYPE_NAME("axiado-scm3003"),
        .parent        = TYPE_AX3000_MACHINE,
        .class_init    = axiado_scm3003_class_init,
    }
};

DEFINE_TYPES(ax3000_evk_types)
