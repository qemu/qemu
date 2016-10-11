/*
 *  Moxie mmu emulation.
 *
 *  Copyright (c) 2008, 2013 Anthony Green
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "mmu.h"
#include "exec/exec-all.h"

int moxie_mmu_translate(MoxieMMUResult *res,
                       CPUMoxieState *env, uint32_t vaddr,
                       int rw, int mmu_idx)
{
    /* Perform no translation yet.  */
    res->phy = vaddr;
    return 0;
}
