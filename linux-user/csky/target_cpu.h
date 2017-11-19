/*
 * CSKY virtual CPU header
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_CPU_H
#define TARGET_CPU_H

static inline void cpu_clone_regs(CPUCSKYState *env, target_ulong newsp)
{
#ifdef TARGET_CSKYV1
    if (newsp) {
        env->regs[0] = newsp;
    }
    env->regs[2] = 0;
#elif defined  TARGET_CSKYV2
    if (newsp) {
        env->regs[14] = newsp;
    }
    env->regs[0] = 0;
#endif
}

#ifdef TARGET_CSKYV1
static inline void cpu_set_tls(CPUCSKYState *env, target_ulong newtls)
{
    env->tls_value = newtls;
}
#elif defined TARGET_CSKYV2
static inline void cpu_set_tls(CPUCSKYState *env, target_ulong newtls)
{
    env->regs[31] = newtls;
}
#endif

#endif
