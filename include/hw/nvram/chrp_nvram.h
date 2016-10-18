/*
 * Common Hardware Reference Platform NVRAM functions.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CHRP_NVRAM_H
#define CHRP_NVRAM_H

int chrp_nvram_create_system_partition(uint8_t *data, int min_len);
int chrp_nvram_create_free_partition(uint8_t *data, int len);

#endif
