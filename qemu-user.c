/*
 * Stubs for QEMU user emulation
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu-common.h"
#include "monitor.h"

Monitor *cur_mon;

int monitor_cur_is_qmp(void)
{
    return 0;
}

void monitor_vprintf(Monitor *mon, const char *fmt, va_list ap)
{
}

void monitor_set_error(Monitor *mon, QError *qerror)
{
}
