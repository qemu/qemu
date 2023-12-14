/* SPDX-License-Identifier: LGPL-2.1-or-later */

#ifndef KVM_IRQCOUNT_H
#define KVM_IRQCOUNT_H

void kvm_report_irq_delivered(int delivered);
void kvm_reset_irq_delivered(void);
int kvm_get_irq_delivered(void);

#endif
