/*
 * KVM PIC functions for counting the delivered IRQs.
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#include "qemu/osdep.h"
#include "hw/intc/kvm_irqcount.h"
#include "trace.h"

static int kvm_irq_delivered;

void kvm_report_irq_delivered(int delivered)
{
    kvm_irq_delivered += delivered;

    trace_kvm_report_irq_delivered(kvm_irq_delivered);
}

void kvm_reset_irq_delivered(void)
{
    /*
     * Copy this into a local variable to encourage gcc to emit a plain
     * register for a sys/sdt.h marker.  For details on this workaround, see:
     * https://sourceware.org/bugzilla/show_bug.cgi?id=13296
     */
    volatile int k_i_d = kvm_irq_delivered;
    trace_kvm_reset_irq_delivered(k_i_d);

    kvm_irq_delivered = 0;
}

int kvm_get_irq_delivered(void)
{
    trace_kvm_get_irq_delivered(kvm_irq_delivered);

    return kvm_irq_delivered;
}
