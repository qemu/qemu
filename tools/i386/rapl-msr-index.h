/*
 * Allowed list of MSR for Privileged RAPL MSR helper commands for QEMU
 *
 * Copyright (C) 2023 Red Hat, Inc. <aharivel@redhat.com>
 *
 * Author: Anthony Harivel <aharivel@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Should stay in sync with the RAPL MSR
 * in target/i386/cpu.h
 */
#define MSR_RAPL_POWER_UNIT             0x00000606
#define MSR_PKG_POWER_LIMIT             0x00000610
#define MSR_PKG_ENERGY_STATUS           0x00000611
#define MSR_PKG_POWER_INFO              0x00000614
