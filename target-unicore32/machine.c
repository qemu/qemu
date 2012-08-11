/*
 * Generic machine functions for UniCore32 ISA
 *
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or any later version.
 * See the COPYING file in the top-level directory.
 */
#include "hw/hw.h"

void cpu_save(QEMUFile *f, void *opaque)
{
    hw_error("%s not supported yet.\n", __func__);
}

int cpu_load(QEMUFile *f, void *opaque, int version_id)
{
    hw_error("%s not supported yet.\n", __func__);

    return 0;
}
