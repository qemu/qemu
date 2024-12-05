/*!
 * @file panda/common.h
 * @brief Common PANDA utility functions.
 *
 * @note Functions that are both simple and frequently called are
 * defined here as inlines. Functions that are either complex or
 * infrequently called are decalred here and defined in `src/common.c`.
 */
#pragma once
#if !defined(__cplusplus)
#include <stdint.h>
#include <stdbool.h>
#else
#include <cstdint>
#include <cstdbool>
#endif
#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "panda/types.h"
#include "gdbstub/internals.h"

/*
 * @brief Branch predition hint macros.
 */
#if !defined(likely)
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#if !defined(unlikely)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifdef __cplusplus
extern "C" {
#endif
    
#if defined(TARGET_MIPS)
#define MIPS_HFLAG_KSU    0x00003 /* kernel/supervisor/user mode mask   */
#define MIPS_HFLAG_KM     0x00000 /* kernel mode flag                   */
/*
 *  Register values from: http://www.cs.uwm.edu/classes/cs315/Bacon/Lecture/HTML/ch05s03.html
 */
#define MIPS_SP           29      /* value for MIPS stack pointer offset into GPR */
#define MIPS_V0           2
#define MIPS_V1           3
#endif
// BEGIN_PYPANDA_NEEDS_THIS -- do not delete this comment bc pypanda
// api autogen needs it.  And don't put any compiler directives
// between this and END_PYPANDA_NEEDS_THIS except includes of other
// files in this directory that contain subsections like this one.

void panda_cleanup(void);
void panda_set_os_name(char *os_name);
void panda_before_find_fast(void);
void panda_disas(FILE *out, void *code, unsigned long size);
void panda_break_main_loop(void);
MemoryRegion* panda_find_ram(void);
    
extern bool panda_exit_loop;
extern bool panda_break_vl_loop_req;


/**
 * panda_current_asid() - Obtain guest ASID.
 * @env: Pointer to cpu state.
 * 
 * This function figures out and returns the ASID (address space
 * identifier) for a number of archiectures (e.g., cr3 for x86). In
 * many cases, this can be used to distinguish between processes.
 * 
 * Return: A guest pointer is returned, the ASID.
*/
target_ulong panda_current_asid(CPUState *env);
    
/**
 * panda_current_pc() - Get current program counter.
 * @cpu: Cpu state.
 *
 * Note that Qemu typically only updates the pc after executing each
 * basic block of code. If you want this value to be more accurate,
 * you will have to call panda_enable_precise_pc.
 * 
 * Return: Program counter is returned.
 */
target_ulong panda_current_pc(CPUState *cpu);

// END_PYPANDA_NEEDS_THIS -- do not delete this comment!

/**
 * panda_find_max_ram_address() - Get max guest ram address.
 *
 * Computes the maximum address of guest system memory that maps to
 * RAM.
 * 
 * Return: The max ram address is returned.
 */
Int128 panda_find_max_ram_address(void);


#ifdef CONFIG_SOFTMMU
/* (not kernel-doc)
 * panda_physical_memory_rw() - Copy data between host and guest.
 * @addr: Guest physical addr of start of read or write.
 * @buf: Host pointer to a buffer either containing the data to be
 *    written to guest memory, or into which data will be copied
 *    from guest memory.
 * @len: The number of bytes to copy
 * @is_write: If true, then buf will be copied into guest
 *    memory, else buf will be copied out of guest memory.
 *
 * Either reads memory out of the guest into a buffer if
 * (is_write==false), or writes data from a buffer into guest memory
 * (is_write==true). Note that buf has to be big enough for read or
 * write indicated by len.
 *
 * Return:
 * * MEMTX_OK      - Read/write succeeded
 * * MEMTX_ERROR   - An error 
 */
static inline int panda_physical_memory_rw(hwaddr addr, uint8_t *buf, int len,
                                           bool is_write) {
    return address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                     buf, len, is_write);
}


/* (not kernel-doc)
 * panda_physical_memory_read() - Copy data from guest memory into host buffer.
 * @addr: Guest physical address of start of read.
 * @buf: Host pointer to a buffer into which data will be copied from guest.
 * @len: Number of bytes to copy.
 * 
 * Return: 
 * * MEMTX_OK      - Read succeeded
 * * MEMTX_ERROR   - An error
 */
static inline int panda_physical_memory_read(hwaddr addr,
                                            uint8_t *buf, int len) {
    return panda_physical_memory_rw(addr, buf, len, 0);
}


/* (not kernel-doc)
 * panda_physical_memory_write() - Copy data from host buffer into guest memory.
 * @addr: Guest physical address of start of desired write.
 * @buf: Host pointer to a buffer from which data will be copied into guest.
 * @len: Number of bytes to copy.
 * 
 * Return: 
 * * MEMTX_OK      - Write succeeded
 * * MEMTX_ERROR   - An error
 */
static inline int panda_physical_memory_write(hwaddr addr,
                                             uint8_t *buf, int len) {
    return panda_physical_memory_rw(addr, buf, len, 1);
}


/**
 * panda_virt_to_phys() - Translate guest virtual to physical address.
 * @env: Cpu state.
 * @addr: Guest virtual address.
 *
 * This conversion will fail if asked about a virtual address that is
 * not currently mapped to a physical one in the guest. Good luck on MIPS.
 *
 * Return: A guest physical address.
 */
static inline hwaddr panda_virt_to_phys(CPUState *env, target_ulong addr) {
    target_ulong page;
    hwaddr phys_addr;
    MemTxAttrs attrs;
    page = addr & TARGET_PAGE_MASK;
    phys_addr = cpu_get_phys_page_attrs_debug(env, page, &attrs);
    if (phys_addr == -1) {
        // no physical page mapped
        return -1;
    }
    phys_addr += (addr & ~TARGET_PAGE_MASK);
    return phys_addr;
}
#endif


/**
 * enter_priv() - Enter privileged mode.
 * @cpu: Cpu state.
 * 
 * Enter into a higher-privileged mode, e.g., in order to conduct some
 * memory access. This is a NO-OP on systems without different
 * privilege modes.
 * 
 * Return: 
 * * True      -  Switch into high-privilege happened.
 * * False     -  Switch did not happen.
 */
bool enter_priv(CPUState* cpu);


/**
 * exit_priv() - Exit privileged mode.
 * @cpu: Cpu state. 
 *
 * Revert the guest to the privilege mode it was in prior to the last call
 * to enter_priv(). A NO-OP for architectures where enter_priv() is a NO-OP.
 */
void exit_priv(CPUState* cpu);

    
/* (not kernel-doc)
 * panda_virtual_memory_rw() - Copy data between host and guest.
 * @env: Cpu sate.
 * @addr: Guest virtual addr of start of read or write.
 * @buf: Host pointer to a buffer either containing the data to be
 *    written to guest memory, or into which data will be copied
 *    from guest memory.
 * @len: The number of bytes to copy
 * @is_write: If true, then buf will be copied into guest
 *    memory, else buf will be copied out of guest memory.
 *
 * Either reads memory out of the guest into a buffer if
 * (is_write==false), or writes data from a buffer into guest memory
 * (is_write==true). Note that buf has to be big enough for read or
 * write indicated by len. Also note that if the virtual address is
 * not mapped, then the read or write will fail.
 *
 * We switch into privileged mode if the access fails. The mode is always reset
 * before we return.
 * 
 * Return:
 * * 0      - Read/write succeeded
 * * -1     - An error 
 */
static inline int panda_virtual_memory_rw(CPUState *cpu, target_ulong addr,
                                          uint8_t *buf, int len, bool is_write) {
    CPUClass *cc;

    cc = CPU_GET_CLASS(cpu);
    if (cc->memory_rw_debug) {
        return cc->memory_rw_debug(cpu, addr, buf, len, is_write);
    }
    return cpu_memory_rw_debug(cpu, addr, buf, len, is_write);
}


/* (not kernel-doc)
 * panda_virtual_memory_read() - Copy data from guest memory into host buffer.
 * @env: Cpu sate.
 * @addr: Guest virtual address of start of desired read
 * @buf: Host pointer to a buffer into which data will be copied from guest.
 * @len: Number of bytes to copy.
 * 
 * Return:
 * * 0      - Read succeeded
 * * -1     - An error 
 */ 
static inline int panda_virtual_memory_read(CPUState *env, target_ulong addr,
                                            uint8_t *buf, int len) {
    return panda_virtual_memory_rw(env, addr, buf, len, 0);
}

    
/* (not kernel-doc)
 * panda_virtual_memory_write() - Copy data from host buffer into guest memory.
 * @env: Cpu sate.
 * @addr: Guest virtual address of start of desired write.
 * @buf: Host pointer to a buffer from which data will be copied into guest.
 * @len: Number of bytes to copy.
 * 
 * Return:
 * * 0      - Write succeeded
 * * -1     - An error 
 */ 
static inline int panda_virtual_memory_write(CPUState *env, target_ulong addr,
                                             uint8_t *buf, int len) {
    return panda_virtual_memory_rw(env, addr, buf, len, 1);
}

#ifdef CONFIG_SOFTMMU
/**
 * panda_map_virt_to_host() - Map guest virtual addresses into host.
 * @env: Cpu state.
 * @addr: Guest virtual address start of range.
 * @len: Length of address range.
 * 
 * Returns a pointer to host memory that is an alias for a range of
 * guest virtual addresses.
 *
 * Return: A host pointer.
 */
static inline void *panda_map_virt_to_host(CPUState *env, target_ulong addr,
                                           int len)
{
    hwaddr phys = panda_virt_to_phys(env, addr);
    hwaddr l = len;
    hwaddr addr1;
    MemoryRegion *mr =
        address_space_translate(&address_space_memory, phys, &addr1, &l, true,
                                MEMTXATTRS_UNSPECIFIED);

    if (!memory_access_is_direct(mr, true)) {
        return NULL;
    }

    return qemu_map_ram_ptr(mr->ram_block, addr1);
}
#endif


/**
 * PandaPhysicalAddressToRamOffset() - Translate guest physical address to ram offset.
 * @out: A pointer to the ram_offset_t, which will be written by this function.
 * @addr: The guest physical address.
 * @is_write: Is this mapping for ultimate read or write.
 *  
 * This function is useful for callers needing to know not merely the
 * size of physical memory, but the actual largest physical address
 * that might arise given non-contiguous ram map.  Panda's taint
 * system needs it to set up its shadow ram, e.g..
 * 
 * Return: The desired return value is pointed to by out.
 * * MEMTX_OK      - Read succeeded
 * * MEMTX_ERROR   - An error
 */
// static inline MemTxResult PandaPhysicalAddressToRamOffset(ram_addr_t* out, hwaddr addr, bool is_write)
// {
//     hwaddr TranslatedAddress;
//     hwaddr AccessLength = 1;
//     MemoryRegion* mr;
//     ram_addr_t RamOffset;

//     rcu_read_lock();
//     mr = address_space_translate(&address_space_memory, addr, &TranslatedAddress, &AccessLength, is_write, MEMTXATTRS_UNSPECIFIED);

//     if (!mr || !memory_region_is_ram(mr) || memory_region_is_ram_device(mr) || memory_region_is_romd(mr) || (is_write && mr->readonly))
//     {
//         /*
//             We only want actual RAM.
//             I can't find a concrete instance of a RAM Device,
//             but from the docs/comments I can find, this seems
//             like the appropriate check.
//         */
//         rcu_read_unlock();
//         return MEMTX_ERROR;
//     }

//     if ((RamOffset = memory_region_get_ram_addr(mr)) == RAM_ADDR_INVALID)
//     {
//         rcu_read_unlock();
//         return MEMTX_ERROR;
//     }

//     rcu_read_unlock();

//     RamOffset += TranslatedAddress;

//     if (RamOffset >= ram_size)
//     {
//         /*
//             HACK
//             For the moment, the taint system (the only consumer of this) will die in very unfortunate
//             ways if the translated offset exceeds the size of "RAM" (the argument given to -m in
//             qemu's invocation)...
//             Unfortunately there's other "RAM" qemu tracks that's not differentiable in a target-independent
//             way. For instance: the PC BIOS memory and VGA memory. In the future it would probably be easier
//             to modify the taint system to use last_ram_offset() rather tham ram_size, and/or register an
//             address space listener to update it's shadow RAM with qemu's hotpluggable memory.
//             From brief observation, the qemu machine implementations seem to map the system "RAM"
//             people are most likely thinking about when they say "RAM" first, so the ram_addr_t values
//             below ram_size should belong to those memory regions. This isn't required however, so beware.
//         */
//         fprintf(stderr, "PandaPhysicalAddressToRamOffset: Translated Physical Address 0x" TARGET_FMT_plx " has RAM Offset Above ram_size (0x" RAM_ADDR_FMT " >= 0x" RAM_ADDR_FMT ")\n", addr, RamOffset, ram_size);
//         return MEMTX_DECODE_ERROR;
//     }

//     if (out)
//         *out = RamOffset;

//     return MEMTX_OK;
// }


/**
 * PandaVirtualAddressToRamOffset() - Translate guest virtual address to ram offset,
 * @out: A pointer to the ram_offset_t, which will be written by this function.
 * @cpu: Cpu state.
 * @addr: The guest virtual address.
 * @is_write: Is this mapping for ultimate read or write.
 *  
 * This function is useful for callers needing to know not merely the
 * size of virtual memory, but the actual largest virtual address that
 * might arise given non-contiguous ram map.  Panda's taint system
 * needs it to set up its shadow ram.
 * 
 * Return: The desired return value is pointed to by out.
 * * MEMTX_OK      - Read succeeded
 * * MEMTX_ERROR   - An error
 */
// static inline MemTxResult PandaVirtualAddressToRamOffset(ram_addr_t* out, CPUState* cpu, target_ulong addr, bool is_write)
// {
//     hwaddr PhysicalAddress = panda_virt_to_phys(cpu, addr);
//     if (PhysicalAddress == (hwaddr)-1)
//         return MEMTX_ERROR;
//     return PandaPhysicalAddressToRamOffset(out, PhysicalAddress, is_write);
// }


/* (not kernel-doc)
 * panda_in_kernel_mode() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 *
 * Return: True if in kernel, false otherwise.
 */
static inline bool panda_in_kernel_mode(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState*) cpu);
#if defined(TARGET_I386)
    return ((env->hflags & HF_CPL_MASK) == 0);
#elif defined(TARGET_ARM)
    // See target/arm/cpu.h arm_current_el
    if (env->aarch64) {
        return extract32(env->pstate, 2, 2) > 0;
    }
    // Note: returns true for non-SVC modes (hypervisor, monitor, system, etc).
    // See: https://www.keil.com/pack/doc/cmsis/Core_A/html/group__CMSIS__CPSR__M.html
    return ((env->uncached_cpsr & CPSR_M) > ARM_CPU_MODE_USR);
#elif defined(TARGET_PPC)
    return ((env->msr >> MSR_PR) & 1);
#elif defined(TARGET_MIPS)
    return (env->hflags & MIPS_HFLAG_KSU) == MIPS_HFLAG_KM;
#else
#error "panda_in_kernel_mode() not implemented for target architecture."
    return false;
#endif
}


/* (not kernel-doc)
 * panda_in_kernel() - Determine if guest is in kernel.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing in kernel mode, e.g. execution privilege level.
 * DEPRECATED.
 * 
 * Return: True if in kernel, false otherwise.
 */
static inline bool panda_in_kernel(const CPUState *cpu) {
    return panda_in_kernel_mode(cpu);
}


/* (not kernel-doc)
 * address_in_kernel_code_linux() - Determine if virtual address is in kernel.                                                                           *                                                                                                                       
 * @addr: Virtual address to check.
 *
 * Checks the top bit of the address to determine if the address is in
 * kernel space. Checking the MSB means this should work even if KASLR
 * is enabled.
 *
 * Return: True if address is in kernel, false otherwise.
 */
static inline bool address_in_kernel_code_linux(target_ulong addr){
    // TODO: Find a way to ask QEMU what the permissions are on an area.
    #if (defined(TARGET_ARM) && !defined(TARGET_AARCH64)) || (defined(TARGET_I386) && !defined(TARGET_X86_64))
    // I386: https://elixir.bootlin.com/linux/latest/source/arch/x86/include/asm/page_32_types.h#L18
    // ARM32: https://people.kernel.org/linusw/how-the-arm32-kernel-starts
    // ARM has a variable VMSPLIT. Technically this can be several values,
    // but the most common offset is 0xc0000000. 
    target_ulong vmsplit =  0xc0000000;
    return addr >= vmsplit;
    #else
    // MIPS32: https://elixir.bootlin.com/linux/latest/source/arch/mips/include/asm/mach-malta/spaces.h#L36
    // https://techpubs.jurassic.nl/manuals/0620/developer/DevDriver_PG/sgi_html/ch01.html
    // https://www.kernel.org/doc/html/latest/vm/highmem.html
    // x86_64: https://github.com/torvalds/linux/blob/master/Documentation/x86/x86_64/mm.rst
    // If addr MSB set -> kernelspace!
    // AARCH64: https://elixir.bootlin.com/linux/latest/source/arch/arm64/include/asm/memory.h#L45
    target_ulong msb_mask = ((target_ulong)1 << ((sizeof(target_long) * 8) - 1));
    return msb_mask & addr;
    #endif
}


/* (not kernel-doc)
 * panda_in_kernel_code_linux() - Determine if current pc is kernel code.
 * @cpu: Cpu state.
 *
 * Determines if guest is currently executing kernelspace code,
 * regardless of privilege level.  Necessary because there's a small
 * bit of kernelspace code that runs AFTER a switch to usermode
 * privileges.  Therefore, certain analysis logic can't rely on
 * panda_in_kernel_mode() alone. 
 *
 * Return: true if pc is in kernel, false otherwise.
 */
static inline bool panda_in_kernel_code_linux(CPUState *cpu) {
    return address_in_kernel_code_linux(panda_current_pc(cpu));
}


/* (not kernel-doc)
 * panda_current_ksp() - Get guest kernel stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Guest pointer value.
 */
static inline target_ulong panda_current_ksp(CPUState *cpu) {
    CPUArchState *env = cpu_env(cpu);
#if defined(TARGET_I386)
    if (panda_in_kernel(cpu)) {
        // Return directly the ESP register value.
        return env->regs[R_ESP];
    } else {
        // Returned kernel ESP stored in the TSS.
        // Related reading: https://css.csail.mit.edu/6.858/2018/readings/i386/c07.htm
        const uint32_t esp0 = 4;
        const target_ulong tss_base = ((CPUX86State *)env)->tr.base + esp0;
        target_ulong kernel_esp = 0;
        if (panda_virtual_memory_rw(cpu, tss_base, (uint8_t *)&kernel_esp, sizeof(kernel_esp), false ) < 0) {
            return 0;
        }
        return kernel_esp;
    }
#elif defined(TARGET_ARM)
    if(env->aarch64) {
        return env->sp_el[1];
    } else {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_SVC) {
            return env->regs[13];
        }else {
            // Read banked R13 for SVC mode to get the kernel SP (1=>SVC bank from target/arm/internals.h)
            return env->banked_r13[1];
        }
    }
#elif defined(TARGET_PPC)
    // R1 on PPC.
    return env->gpr[1];
#elif defined(TARGET_MIPS)
    return env->active_tc.gpr[MIPS_SP];
#else
#error "panda_current_ksp() not implemented for target architecture."
    return 0;
#endif
}


/* (not kernel-doc)
 * panda_current_sp() - Get current guest stack pointer.
 * @cpu: Cpu state.
 * 
 * Return: Returns guest pointer.
 */
static inline target_ulong panda_current_sp(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState *)cpu);
#if defined(TARGET_I386)
    // valid on x86 and x86_64
    return env->regs[R_ESP];
#elif defined(TARGET_ARM)
    if (env->aarch64) {
        // X31 on AARCH64.
        return env->xregs[31];
    } else {
        // R13 on ARM.
        return env->regs[13];
    }
#elif defined(TARGET_PPC)
    // R1 on PPC.
    return env->gpr[1];
#elif defined(TARGET_MIPS)
    return env->active_tc.gpr[MIPS_SP];
#else
#error "panda_current_sp() not implemented for target architecture."
    return 0;
#endif
}


/* (not kernel-doc)
 * panda_get_retval() - Get return value for function.
 * @cpu: Cpu state.
 *
 * This function provides a platform-independent abstraction for
 * retrieving a call return value. It still has to be used in the
 * proper context to retrieve a meaningful value, such as just after a
 * RET instruction under x86.
 *
 * Return: Guest ulong value.
 */
static inline target_ulong panda_get_retval(const CPUState *cpu) {
    CPUArchState *env = cpu_env((CPUState *)cpu);
#if defined(TARGET_I386)
    // EAX for x86.
    return env->regs[R_EAX];
#elif defined(TARGET_ARM)
    // R0 on ARM.
    return env->regs[0];
#elif defined(TARGET_PPC)
    // R3 on PPC.
    return env->gpr[3];
#elif defined(TARGET_MIPS)
    // MIPS has 2 return registers v0 and v1. Here we choose v0.
    return env->active_tc.gpr[MIPS_V0];
#else
#error "panda_get_retval() not implemented for target architecture."
    return 0;
#endif
}

#ifdef __cplusplus
}
#endif

/* vim:set tabstop=4 softtabstop=4 expandtab: */
