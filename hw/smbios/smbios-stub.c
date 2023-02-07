/*
 * SMBIOS stubs for platforms that don't support SMBIOS.
 *
 * Copyright (c) 2010 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (c) 2016 Leif Lindholm <leif.lindholm@linaro.org>
 *                    Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/firmware/smbios.h"

void smbios_entry_add(QemuOpts *opts, Error **errp)
{
    g_assert_not_reached();
}
