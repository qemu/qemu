/*
 * FNIRSI-1013D scope GPIO devices emulation
 *
 * Copyright (C) 2022 froloff
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_GPIO_FNIRSI_1013D_PIO_H
#define HW_GPIO_FNIRSI_1013D_PIO_H

void fnirsi_tp_init(struct AwPIOState *s);
void fnirsi_fpga_init(AwPIOState *s);

#endif