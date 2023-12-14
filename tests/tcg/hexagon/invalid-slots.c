/*
 *  Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

char mem[8] __attribute__((aligned(8)));

int main()
{
    asm volatile(
        "r0 = #mem\n"
        /* Invalid packet (2 instructions at slot 0): */
        ".word 0xa1804100\n" /* { memw(r0) = r1;      */
        ".word 0x28032804\n" /*   r3 = #0; r4 = #0 }  */
        : : : "r0", "r3", "r4", "memory");
    return 0;
}
