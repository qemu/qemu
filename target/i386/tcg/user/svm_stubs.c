/*
 *  x86 SVM helpers (user-mode)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "tcg/helper-tcg.h"

void helper_vmrun(CPUX86State *env, int aflag, int next_eip_addend)
{
}

void helper_vmmcall(CPUX86State *env)
{
}

void helper_vmload(CPUX86State *env, int aflag)
{
}

void helper_vmsave(CPUX86State *env, int aflag)
{
}

void helper_stgi(CPUX86State *env)
{
}

void helper_clgi(CPUX86State *env)
{
}

void helper_invlpga(CPUX86State *env, int aflag)
{
}

void cpu_vmexit(CPUX86State *nenv, uint32_t exit_code, uint64_t exit_info_1,
                uintptr_t retaddr)
{
    assert(0);
}

void helper_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                      uint64_t param)
{
}

void cpu_svm_check_intercept_param(CPUX86State *env, uint32_t type,
                                   uint64_t param, uintptr_t retaddr)
{
}

void helper_svm_check_io(CPUX86State *env, uint32_t port, uint32_t param,
                         uint32_t next_eip_addend)
{
}
