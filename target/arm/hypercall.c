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

    // Pass back a return value
    cpu_env->xregs[0] = 0x1337;
}

ssize_t hypervisor_read_from_virt_mem (CPUARMState *cpu_env, uint64_t virt_addr, void *buf, size_t len) {
    // Step 1: Translate to physical address
    // See target/arm/m_helper.c for interesting things regarding exception entry and memory management
    // See include/exec/memory.h for AddressSpace methods and stuff

    CPUState *cs = env_cpu(cpu_env); // See op_helper.c:36
    MemTxAttrs attrs = {}; // See m_helper.c:196
    AddressSpace *space = arm_addressspace(cs, attrs); // See cpu.h:3529

    // See m_helper.c:273
    hwaddr physaddr;
    int prot;
    target_ulong page_size;
    ARMMMUFaultInfo fi = {};
    ARMCacheAttrs cacheattrs = {};
    // qemu_log("Looking up virtual address %lx\n", virtual_addr_to_read);

    // Currently using MMU ARMMMUIdx_MUser which seems to work. We can try other MMU indeces if this is incorrect.
    if (get_phys_addr(cpu_env, virt_addr, MMU_DATA_LOAD, ARMMMUIdx_MUser, &physaddr,
                      &attrs, &prot, &page_size, &fi, &cacheattrs)) {
        qemu_log("Physical Address Lookup failed");
        return -1;
    }
    else {
        // qemu_log("Physical Address: %lx\n", physaddr);

        // Step 2: Read physical address using address_space_rw
        // See include/sysemu/dma.h:88

        // Returns MemTxResult
        address_space_rw(space, physaddr, MEMTXATTRS_UNSPECIFIED, buf, len, false);

        // @TODO: Check res to see if read succeeded
    }

    // Return success
    return 0;
}
