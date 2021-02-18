/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_TARGET_ELF_H
#define HEXAGON_TARGET_ELF_H

static inline const char *cpu_get_model(uint32_t eflags)
{
    /* For now, treat anything newer than v5 as a v67 */
    /* FIXME - Disable instructions that are newer than the specified arch */
    if (eflags == 0x04 ||    /* v5  */
        eflags == 0x05 ||    /* v55 */
        eflags == 0x60 ||    /* v60 */
        eflags == 0x61 ||    /* v61 */
        eflags == 0x62 ||    /* v62 */
        eflags == 0x65 ||    /* v65 */
        eflags == 0x66 ||    /* v66 */
        eflags == 0x67 ||    /* v67 */
        eflags == 0x8067     /* v67t */
       ) {
        return "v67";
    }
    return "unknown";
}

#endif
