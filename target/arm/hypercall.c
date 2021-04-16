#include "hypercall.h"

/*
 * intercept_hypercall
 * Intercepts a HVC instruction.
 */
void intercept_hypercall(CPUARMState *cpu_env) {
    qemu_log("Intercepted a hypercall.\n");
    for (int i = 0; i < 32; i++) {
        qemu_log("R%d: 0x%lX\n", i, cpu_env->xregs[i]);
    }

    // Read from guest memory
    uint64_t buf[16];
    ssize_t error_code = hypervisor_read_from_virt_mem(cpu_env, cpu_env->xregs[0], buf, sizeof(buf));
    if (0 == error_code) {
        for (int i = 0; i < 16; i++) {
            qemu_log("\tbuf[%d] = %lx\n", i, buf[i]);
        }
    }
    else {
        qemu_log("Error reading from %p\n", (void *)cpu_env->xregs[0]);
    }

    // Write something new to guest memory
    for (int i = 0; i < 16; i++) {
        buf[i] = 0x7331 + i;
    }
    hypervisor_write_to_virt_mem(cpu_env, cpu_env->xregs[0], buf, sizeof(buf));

    // Pass back a return value
    cpu_env->xregs[0] = 0x1337;
}

ssize_t hypervisor_read_from_virt_mem (CPUARMState *cpu_env, uint64_t virt_addr, void *buf, size_t len) {
    return hypervisor_virt_mem_rw(cpu_env, virt_addr, buf, len, false);
}

ssize_t hypervisor_write_to_virt_mem (CPUARMState *cpu_env, uint64_t virt_addr, void *buf, size_t len) {
    return hypervisor_virt_mem_rw(cpu_env, virt_addr, buf, len, true);
}

ssize_t hypervisor_virt_mem_rw (CPUARMState *cpu_env, uint64_t virt_addr, void *buf, size_t len, bool is_write) {
    CPUState *cs = env_cpu(cpu_env);
    MemTxAttrs attrs = {};
    AddressSpace *space = arm_addressspace(cs, attrs);

    hwaddr physaddr;
    int prot;
    target_ulong page_size;
    ARMMMUFaultInfo fi = {};
    ARMCacheAttrs cacheattrs = {};

    if (get_phys_addr(cpu_env, virt_addr, MMU_DATA_LOAD, ARMMMUIdx_MUser, &physaddr,
                      &attrs, &prot, &page_size, &fi, &cacheattrs)) {
        qemu_log("Physical Address Lookup failed");
        return -1;
    }
    else {
        address_space_rw(space, physaddr, MEMTXATTRS_UNSPECIFIED, buf, len, is_write);
    }

    return 0;
}