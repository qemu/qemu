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

    switch (cpu_env->xregs[0]) {
        case HYPERCALL_SUBMIT_PANIC:
            hypervisor_patch_panic(cpu_env, cpu_env->xregs[1]);
            break;
        case HYPERCALL_PANIC:
            qemu_log("Panic received\n");
            break;
        default:
            qemu_log("Undefined hypercall\n");
            break;
    }

    // Read from guest memory
    // uint64_t buf[16];
    // ssize_t error_code = hypervisor_read_from_virt_mem(cpu_env, cpu_env->xregs[0], buf, sizeof(buf));
    // if (0 == error_code) {
    //     for (int i = 0; i < 16; i++) {
    //         qemu_log("\tbuf[%d] = %lx\n", i, buf[i]);
    //     }
    // }
    // else {
    //     qemu_log("Error reading from %p\n", (void *)cpu_env->xregs[0]);
    // }

    // Write something new to guest memory
    // for (int i = 0; i < 16; i++) {
    //     buf[i] = 0x7331 + i;
    // }
    // hypervisor_write_to_virt_mem(cpu_env, cpu_env->xregs[0], buf, sizeof(buf));

    // Pass back a return value
    // cpu_env->xregs[0] = 0x1337;
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

    if (get_phys_addr(cpu_env, virt_addr, MMU_DATA_LOAD, ARMMMUIdx_MPriv, &physaddr,
                      &attrs, &prot, &page_size, &fi, &cacheattrs)) {
        qemu_log("Physical Address Lookup failed");
        return -1;
    }
    else {
        address_space_rw(space, physaddr, MEMTXATTRS_UNSPECIFIED, buf, len, is_write);
    }

    return 0;
}

void hypervisor_patch_panic(CPUARMState *cpu_env, uint64_t virt_panic_handler_addr) {
    qemu_log("Panic handler submitted at addr: 0x%lX\n", virt_panic_handler_addr);
    if (!virt_panic_handler_addr) {
        qemu_log("Panic handler is null. Did you forget sudo?\n");
        return;
    }

    /*
    mov x0, #1
    hvc #0x1337
    */
    uint8_t patch_bytes[] = { 0x20, 0x00, 0x80, 0xd2, 0xe2, 0x66, 0x02, 0xd4 };
    hypervisor_write_to_virt_mem(cpu_env, virt_panic_handler_addr, patch_bytes, sizeof(patch_bytes));
    qemu_log("Panic handler patched\n");
}