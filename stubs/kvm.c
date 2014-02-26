#include "qemu-common.h"
#include "sysemu/kvm.h"

int kvm_arch_irqchip_create(KVMState *s)
{
    return 0;
}
