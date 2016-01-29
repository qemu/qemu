#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/kvm.h"

int kvm_arch_irqchip_create(MachineState *ms, KVMState *s)
{
    return 0;
}
