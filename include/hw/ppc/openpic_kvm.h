#ifndef OPENPIC_KVM_H
#define OPENPIC_KVM_H

#define TYPE_KVM_OPENPIC "kvm-openpic"
int kvm_openpic_connect_vcpu(DeviceState *d, CPUState *cs);

#endif /* OPENPIC_KVM_H */
