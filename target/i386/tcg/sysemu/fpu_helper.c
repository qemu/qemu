/*
 *  x86 FPU, MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4/PNI helpers (sysemu code)
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
#include "qemu/main-loop.h"
#include "cpu.h"
#include "hw/irq.h"

static qemu_irq ferr_irq;

void x86_register_ferr_irq(qemu_irq irq)
{
    ferr_irq = irq;
}

void fpu_check_raise_ferr_irq(CPUX86State *env)
{
    if (ferr_irq && !(env->hflags2 & HF2_IGNNE_MASK)) {
        bql_lock();
        qemu_irq_raise(ferr_irq);
        bql_unlock();
        return;
    }
}

void cpu_clear_ignne(void)
{
    CPUX86State *env = &X86_CPU(first_cpu)->env;
    env->hflags2 &= ~HF2_IGNNE_MASK;
}

void cpu_set_ignne(void)
{
    CPUX86State *env = &X86_CPU(first_cpu)->env;

    assert(bql_locked());

    env->hflags2 |= HF2_IGNNE_MASK;
    /*
     * We get here in response to a write to port F0h.  The chipset should
     * deassert FP_IRQ and FERR# instead should stay signaled until FPSW_SE is
     * cleared, because FERR# and FP_IRQ are two separate pins on real
     * hardware.  However, we don't model FERR# as a qemu_irq, so we just
     * do directly what the chipset would do, i.e. deassert FP_IRQ.
     */
    qemu_irq_lower(ferr_irq);
}
