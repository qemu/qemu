#include "hypercall.h"

bool hypervisor_log_enabled = false;

/*
 * intercept_hypercall
 * Intercepts a HVC instruction.
 */
void intercept_hypercall(CPUARMState *cpu_env) {
    qemu_log("Intercepted a hypercall.\n");
    // for (int i = 0; i < 32; i++) {
    //     qemu_log("X%d: 0x%lX\n", i, cpu_env->xregs[i]);
    // }

    switch (cpu_env->xregs[0]) {
        case HYPERCALL_SUBMIT_PANIC:
            hypervisor_patch_panic(cpu_env, cpu_env->xregs[1]);
            break;
        case HYPERCALL_PANIC:
            qemu_log("Panic received\n");
            break;
        case HYPERCALL_TEST:
            hypervisor_test(cpu_env);
            break;
        case HYPERCALL_TEST_2:
            hypervisor_test_2(cpu_env);
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

    // TODO: Figure out why kernel adders cannot be resolved when cpu_env is in userland
    if (get_phys_addr(cpu_env, virt_addr, MMU_DATA_LOAD, arm_mmu_idx(cpu_env), &physaddr,
                      &attrs, &prot, &page_size, &fi, &cacheattrs)) {
        qemu_log("Physical Address Lookup failed\n");

        qemu_log("\tfi.type = %d\n", fi.type);
        qemu_log("\tfi.s2addr = %lx\n", fi.s2addr);
        qemu_log("\tfi.level = %d\n", fi.level);
        qemu_log("\tfi.domain = %d\n", fi.domain);
        qemu_log("\tfi.stage2 = %d\n", fi.stage2);
        qemu_log("\tfi.s1ptw = %d\n", fi.s1ptw);
        qemu_log("\tfi.s1ns = %d\n", fi.s1ns);
        qemu_log("\tfi.ea = %d\n", fi.ea);

        return -1;
    }
    else {
        address_space_rw(space, physaddr, MEMTXATTRS_UNSPECIFIED, buf, len, is_write);
    }

    return 0;
}

void hypervisor_patch_panic(CPUARMState *cpu_env, uint64_t virt_panic_handler_addr) {
    qemu_log("Submitted panic handler at addr: 0x%lX\n", virt_panic_handler_addr);
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

void hypervisor_test(CPUARMState *cpu_env) {
    uint64_t function_addr = cpu_env->xregs[1];
    qemu_log("Submitted test at addr: 0x%lX\n", function_addr);
    if (!function_addr) {
        qemu_log("Test address is null. Did you forget sudo?\n");
        return;
    }

    uint8_t pre_patch_bytes[8];
    hypervisor_read_from_virt_mem(cpu_env, function_addr, pre_patch_bytes, sizeof(pre_patch_bytes));
    qemu_log("Pre patch bytes: 0x%X%X\n", *(uint32_t*)pre_patch_bytes, *(uint32_t*)&pre_patch_bytes[4]);

    /*
    mov x0, #3
    hvc #0x1337
    */
    uint8_t patch_bytes[] = { 0x60, 0x00, 0x80, 0xd2, 0xe2, 0x66, 0x02, 0xd4 };
    hypervisor_write_to_virt_mem(cpu_env, function_addr, patch_bytes, sizeof(patch_bytes));
    qemu_log("Test patched\n");

    uint8_t post_patch_bytes[8];
    hypervisor_read_from_virt_mem(cpu_env, function_addr, post_patch_bytes, sizeof(post_patch_bytes));
    qemu_log("Post patch bytes: 0x%X%X\n", *(uint32_t*)post_patch_bytes, *(uint32_t*)&post_patch_bytes[4]);
}

void hypervisor_test_2(CPUARMState *cpu_env) {
    qemu_log("Test intercepted!\n");
}