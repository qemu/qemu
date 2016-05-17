/*
 * TPR optimization for 32-bit Windows guests (XP and Server 2003)
 *
 * Copyright (C) 2007-2008 Qumranet Technologies
 * Copyright (C) 2012      Jan Kiszka, Siemens AG
 *
 * This work is licensed under the terms of the GNU GPL version 2, or
 * (at your option) any later version. See the COPYING file in the
 * top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "sysemu/sysemu.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "hw/i386/apic_internal.h"
#include "hw/sysbus.h"

#define VAPIC_IO_PORT           0x7e

#define VAPIC_CPU_SHIFT         7

#define ROM_BLOCK_SIZE          512
#define ROM_BLOCK_MASK          (~(ROM_BLOCK_SIZE - 1))

typedef enum VAPICMode {
    VAPIC_INACTIVE = 0,
    VAPIC_ACTIVE   = 1,
    VAPIC_STANDBY  = 2,
} VAPICMode;

typedef struct VAPICHandlers {
    uint32_t set_tpr;
    uint32_t set_tpr_eax;
    uint32_t get_tpr[8];
    uint32_t get_tpr_stack;
} QEMU_PACKED VAPICHandlers;

typedef struct GuestROMState {
    char signature[8];
    uint32_t vaddr;
    uint32_t fixup_start;
    uint32_t fixup_end;
    uint32_t vapic_vaddr;
    uint32_t vapic_size;
    uint32_t vcpu_shift;
    uint32_t real_tpr_addr;
    VAPICHandlers up;
    VAPICHandlers mp;
} QEMU_PACKED GuestROMState;

typedef struct VAPICROMState {
    SysBusDevice busdev;
    MemoryRegion io;
    MemoryRegion rom;
    uint32_t state;
    uint32_t rom_state_paddr;
    uint32_t rom_state_vaddr;
    uint32_t vapic_paddr;
    uint32_t real_tpr_addr;
    GuestROMState rom_state;
    size_t rom_size;
    bool rom_mapped_writable;
    VMChangeStateEntry *vmsentry;
} VAPICROMState;

#define TYPE_VAPIC "kvmvapic"
#define VAPIC(obj) OBJECT_CHECK(VAPICROMState, (obj), TYPE_VAPIC)

#define TPR_INSTR_ABS_MODRM             0x1
#define TPR_INSTR_MATCH_MODRM_REG       0x2

typedef struct TPRInstruction {
    uint8_t opcode;
    uint8_t modrm_reg;
    unsigned int flags;
    TPRAccess access;
    size_t length;
    off_t addr_offset;
} TPRInstruction;

/* must be sorted by length, shortest first */
static const TPRInstruction tpr_instr[] = {
    { /* mov abs to eax */
        .opcode = 0xa1,
        .access = TPR_ACCESS_READ,
        .length = 5,
        .addr_offset = 1,
    },
    { /* mov eax to abs */
        .opcode = 0xa3,
        .access = TPR_ACCESS_WRITE,
        .length = 5,
        .addr_offset = 1,
    },
    { /* mov r32 to r/m32 */
        .opcode = 0x89,
        .flags = TPR_INSTR_ABS_MODRM,
        .access = TPR_ACCESS_WRITE,
        .length = 6,
        .addr_offset = 2,
    },
    { /* mov r/m32 to r32 */
        .opcode = 0x8b,
        .flags = TPR_INSTR_ABS_MODRM,
        .access = TPR_ACCESS_READ,
        .length = 6,
        .addr_offset = 2,
    },
    { /* push r/m32 */
        .opcode = 0xff,
        .modrm_reg = 6,
        .flags = TPR_INSTR_ABS_MODRM | TPR_INSTR_MATCH_MODRM_REG,
        .access = TPR_ACCESS_READ,
        .length = 6,
        .addr_offset = 2,
    },
    { /* mov imm32, r/m32 (c7/0) */
        .opcode = 0xc7,
        .modrm_reg = 0,
        .flags = TPR_INSTR_ABS_MODRM | TPR_INSTR_MATCH_MODRM_REG,
        .access = TPR_ACCESS_WRITE,
        .length = 10,
        .addr_offset = 2,
    },
};

static void read_guest_rom_state(VAPICROMState *s)
{
    cpu_physical_memory_read(s->rom_state_paddr, &s->rom_state,
                             sizeof(GuestROMState));
}

static void write_guest_rom_state(VAPICROMState *s)
{
    cpu_physical_memory_write(s->rom_state_paddr, &s->rom_state,
                              sizeof(GuestROMState));
}

static void update_guest_rom_state(VAPICROMState *s)
{
    read_guest_rom_state(s);

    s->rom_state.real_tpr_addr = cpu_to_le32(s->real_tpr_addr);
    s->rom_state.vcpu_shift = cpu_to_le32(VAPIC_CPU_SHIFT);

    write_guest_rom_state(s);
}

static int find_real_tpr_addr(VAPICROMState *s, CPUX86State *env)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    hwaddr paddr;
    target_ulong addr;

    if (s->state == VAPIC_ACTIVE) {
        return 0;
    }
    /*
     * If there is no prior TPR access instruction we could analyze (which is
     * the case after resume from hibernation), we need to scan the possible
     * virtual address space for the APIC mapping.
     */
    for (addr = 0xfffff000; addr >= 0x80000000; addr -= TARGET_PAGE_SIZE) {
        paddr = cpu_get_phys_page_debug(cs, addr);
        if (paddr != APIC_DEFAULT_ADDRESS) {
            continue;
        }
        s->real_tpr_addr = addr + 0x80;
        update_guest_rom_state(s);
        return 0;
    }
    return -1;
}

static uint8_t modrm_reg(uint8_t modrm)
{
    return (modrm >> 3) & 7;
}

static bool is_abs_modrm(uint8_t modrm)
{
    return (modrm & 0xc7) == 0x05;
}

static bool opcode_matches(uint8_t *opcode, const TPRInstruction *instr)
{
    return opcode[0] == instr->opcode &&
        (!(instr->flags & TPR_INSTR_ABS_MODRM) || is_abs_modrm(opcode[1])) &&
        (!(instr->flags & TPR_INSTR_MATCH_MODRM_REG) ||
         modrm_reg(opcode[1]) == instr->modrm_reg);
}

static int evaluate_tpr_instruction(VAPICROMState *s, X86CPU *cpu,
                                    target_ulong *pip, TPRAccess access)
{
    CPUState *cs = CPU(cpu);
    const TPRInstruction *instr;
    target_ulong ip = *pip;
    uint8_t opcode[2];
    uint32_t real_tpr_addr;
    int i;

    if ((ip & 0xf0000000ULL) != 0x80000000ULL &&
        (ip & 0xf0000000ULL) != 0xe0000000ULL) {
        return -1;
    }

    /*
     * Early Windows 2003 SMP initialization contains a
     *
     *   mov imm32, r/m32
     *
     * instruction that is patched by TPR optimization. The problem is that
     * RSP, used by the patched instruction, is zero, so the guest gets a
     * double fault and dies.
     */
    if (cpu->env.regs[R_ESP] == 0) {
        return -1;
    }

    if (kvm_enabled() && !kvm_irqchip_in_kernel()) {
        /*
         * KVM without kernel-based TPR access reporting will pass an IP that
         * points after the accessing instruction. So we need to look backward
         * to find the reason.
         */
        for (i = 0; i < ARRAY_SIZE(tpr_instr); i++) {
            instr = &tpr_instr[i];
            if (instr->access != access) {
                continue;
            }
            if (cpu_memory_rw_debug(cs, ip - instr->length, opcode,
                                    sizeof(opcode), 0) < 0) {
                return -1;
            }
            if (opcode_matches(opcode, instr)) {
                ip -= instr->length;
                goto instruction_ok;
            }
        }
        return -1;
    } else {
        if (cpu_memory_rw_debug(cs, ip, opcode, sizeof(opcode), 0) < 0) {
            return -1;
        }
        for (i = 0; i < ARRAY_SIZE(tpr_instr); i++) {
            instr = &tpr_instr[i];
            if (opcode_matches(opcode, instr)) {
                goto instruction_ok;
            }
        }
        return -1;
    }

instruction_ok:
    /*
     * Grab the virtual TPR address from the instruction
     * and update the cached values.
     */
    if (cpu_memory_rw_debug(cs, ip + instr->addr_offset,
                            (void *)&real_tpr_addr,
                            sizeof(real_tpr_addr), 0) < 0) {
        return -1;
    }
    real_tpr_addr = le32_to_cpu(real_tpr_addr);
    if ((real_tpr_addr & 0xfff) != 0x80) {
        return -1;
    }
    s->real_tpr_addr = real_tpr_addr;
    update_guest_rom_state(s);

    *pip = ip;
    return 0;
}

static int update_rom_mapping(VAPICROMState *s, CPUX86State *env, target_ulong ip)
{
    CPUState *cs = CPU(x86_env_get_cpu(env));
    hwaddr paddr;
    uint32_t rom_state_vaddr;
    uint32_t pos, patch, offset;

    /* nothing to do if already activated */
    if (s->state == VAPIC_ACTIVE) {
        return 0;
    }

    /* bail out if ROM init code was not executed (missing ROM?) */
    if (s->state == VAPIC_INACTIVE) {
        return -1;
    }

    /* find out virtual address of the ROM */
    rom_state_vaddr = s->rom_state_paddr + (ip & 0xf0000000);
    paddr = cpu_get_phys_page_debug(cs, rom_state_vaddr);
    if (paddr == -1) {
        return -1;
    }
    paddr += rom_state_vaddr & ~TARGET_PAGE_MASK;
    if (paddr != s->rom_state_paddr) {
        return -1;
    }
    read_guest_rom_state(s);
    if (memcmp(s->rom_state.signature, "kvm aPiC", 8) != 0) {
        return -1;
    }
    s->rom_state_vaddr = rom_state_vaddr;

    /* fixup addresses in ROM if needed */
    if (rom_state_vaddr == le32_to_cpu(s->rom_state.vaddr)) {
        return 0;
    }
    for (pos = le32_to_cpu(s->rom_state.fixup_start);
         pos < le32_to_cpu(s->rom_state.fixup_end);
         pos += 4) {
        cpu_physical_memory_read(paddr + pos - s->rom_state.vaddr,
                                 &offset, sizeof(offset));
        offset = le32_to_cpu(offset);
        cpu_physical_memory_read(paddr + offset, &patch, sizeof(patch));
        patch = le32_to_cpu(patch);
        patch += rom_state_vaddr - le32_to_cpu(s->rom_state.vaddr);
        patch = cpu_to_le32(patch);
        cpu_physical_memory_write(paddr + offset, &patch, sizeof(patch));
    }
    read_guest_rom_state(s);
    s->vapic_paddr = paddr + le32_to_cpu(s->rom_state.vapic_vaddr) -
        le32_to_cpu(s->rom_state.vaddr);

    return 0;
}

/*
 * Tries to read the unique processor number from the Kernel Processor Control
 * Region (KPCR) of 32-bit Windows XP and Server 2003. Returns -1 if the KPCR
 * cannot be accessed or is considered invalid. This also ensures that we are
 * not patching the wrong guest.
 */
static int get_kpcr_number(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    struct kpcr {
        uint8_t  fill1[0x1c];
        uint32_t self;
        uint8_t  fill2[0x31];
        uint8_t  number;
    } QEMU_PACKED kpcr;

    if (cpu_memory_rw_debug(CPU(cpu), env->segs[R_FS].base,
                            (void *)&kpcr, sizeof(kpcr), 0) < 0 ||
        kpcr.self != env->segs[R_FS].base) {
        return -1;
    }
    return kpcr.number;
}

static int vapic_enable(VAPICROMState *s, X86CPU *cpu)
{
    int cpu_number = get_kpcr_number(cpu);
    hwaddr vapic_paddr;
    static const uint8_t enabled = 1;

    if (cpu_number < 0) {
        return -1;
    }
    vapic_paddr = s->vapic_paddr +
        (((hwaddr)cpu_number) << VAPIC_CPU_SHIFT);
    cpu_physical_memory_write(vapic_paddr + offsetof(VAPICState, enabled),
                              &enabled, sizeof(enabled));
    apic_enable_vapic(cpu->apic_state, vapic_paddr);

    s->state = VAPIC_ACTIVE;

    return 0;
}

static void patch_byte(X86CPU *cpu, target_ulong addr, uint8_t byte)
{
    cpu_memory_rw_debug(CPU(cpu), addr, &byte, 1, 1);
}

static void patch_call(VAPICROMState *s, X86CPU *cpu, target_ulong ip,
                       uint32_t target)
{
    uint32_t offset;

    offset = cpu_to_le32(target - ip - 5);
    patch_byte(cpu, ip, 0xe8); /* call near */
    cpu_memory_rw_debug(CPU(cpu), ip + 1, (void *)&offset, sizeof(offset), 1);
}

static void patch_instruction(VAPICROMState *s, X86CPU *cpu, target_ulong ip)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    VAPICHandlers *handlers;
    uint8_t opcode[2];
    uint32_t imm32 = 0;
    target_ulong current_pc = 0;
    target_ulong current_cs_base = 0;
    uint32_t current_flags = 0;

    if (smp_cpus == 1) {
        handlers = &s->rom_state.up;
    } else {
        handlers = &s->rom_state.mp;
    }

    if (!kvm_enabled()) {
        cpu_get_tb_cpu_state(env, &current_pc, &current_cs_base,
                             &current_flags);
    }

    pause_all_vcpus();

    cpu_memory_rw_debug(cs, ip, opcode, sizeof(opcode), 0);

    switch (opcode[0]) {
    case 0x89: /* mov r32 to r/m32 */
        patch_byte(cpu, ip, 0x50 + modrm_reg(opcode[1]));  /* push reg */
        patch_call(s, cpu, ip + 1, handlers->set_tpr);
        break;
    case 0x8b: /* mov r/m32 to r32 */
        patch_byte(cpu, ip, 0x90);
        patch_call(s, cpu, ip + 1, handlers->get_tpr[modrm_reg(opcode[1])]);
        break;
    case 0xa1: /* mov abs to eax */
        patch_call(s, cpu, ip, handlers->get_tpr[0]);
        break;
    case 0xa3: /* mov eax to abs */
        patch_call(s, cpu, ip, handlers->set_tpr_eax);
        break;
    case 0xc7: /* mov imm32, r/m32 (c7/0) */
        patch_byte(cpu, ip, 0x68);  /* push imm32 */
        cpu_memory_rw_debug(cs, ip + 6, (void *)&imm32, sizeof(imm32), 0);
        cpu_memory_rw_debug(cs, ip + 1, (void *)&imm32, sizeof(imm32), 1);
        patch_call(s, cpu, ip + 5, handlers->set_tpr);
        break;
    case 0xff: /* push r/m32 */
        patch_byte(cpu, ip, 0x50); /* push eax */
        patch_call(s, cpu, ip + 1, handlers->get_tpr_stack);
        break;
    default:
        abort();
    }

    resume_all_vcpus();

    if (!kvm_enabled()) {
        tb_gen_code(cs, current_pc, current_cs_base, current_flags, 1);
        cpu_loop_exit_noexc(cs);
    }
}

void vapic_report_tpr_access(DeviceState *dev, CPUState *cs, target_ulong ip,
                             TPRAccess access)
{
    VAPICROMState *s = VAPIC(dev);
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    cpu_synchronize_state(cs);

    if (evaluate_tpr_instruction(s, cpu, &ip, access) < 0) {
        if (s->state == VAPIC_ACTIVE) {
            vapic_enable(s, cpu);
        }
        return;
    }
    if (update_rom_mapping(s, env, ip) < 0) {
        return;
    }
    if (vapic_enable(s, cpu) < 0) {
        return;
    }
    patch_instruction(s, cpu, ip);
}

typedef struct VAPICEnableTPRReporting {
    DeviceState *apic;
    bool enable;
} VAPICEnableTPRReporting;

static void vapic_do_enable_tpr_reporting(void *data)
{
    VAPICEnableTPRReporting *info = data;

    apic_enable_tpr_access_reporting(info->apic, info->enable);
}

static void vapic_enable_tpr_reporting(bool enable)
{
    VAPICEnableTPRReporting info = {
        .enable = enable,
    };
    CPUState *cs;
    X86CPU *cpu;

    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);
        info.apic = cpu->apic_state;
        run_on_cpu(cs, vapic_do_enable_tpr_reporting, &info);
    }
}

static void vapic_reset(DeviceState *dev)
{
    VAPICROMState *s = VAPIC(dev);

    s->state = VAPIC_INACTIVE;
    s->rom_state_paddr = 0;
    vapic_enable_tpr_reporting(false);
}

/*
 * Set the IRQ polling hypercalls to the supported variant:
 *  - vmcall if using KVM in-kernel irqchip
 *  - 32-bit VAPIC port write otherwise
 */
static int patch_hypercalls(VAPICROMState *s)
{
    hwaddr rom_paddr = s->rom_state_paddr & ROM_BLOCK_MASK;
    static const uint8_t vmcall_pattern[] = { /* vmcall */
        0xb8, 0x1, 0, 0, 0, 0xf, 0x1, 0xc1
    };
    static const uint8_t outl_pattern[] = { /* nop; outl %eax,0x7e */
        0xb8, 0x1, 0, 0, 0, 0x90, 0xe7, 0x7e
    };
    uint8_t alternates[2];
    const uint8_t *pattern;
    const uint8_t *patch;
    int patches = 0;
    off_t pos;
    uint8_t *rom;

    rom = g_malloc(s->rom_size);
    cpu_physical_memory_read(rom_paddr, rom, s->rom_size);

    for (pos = 0; pos < s->rom_size - sizeof(vmcall_pattern); pos++) {
        if (kvm_irqchip_in_kernel()) {
            pattern = outl_pattern;
            alternates[0] = outl_pattern[7];
            alternates[1] = outl_pattern[7];
            patch = &vmcall_pattern[5];
        } else {
            pattern = vmcall_pattern;
            alternates[0] = vmcall_pattern[7];
            alternates[1] = 0xd9; /* AMD's VMMCALL */
            patch = &outl_pattern[5];
        }
        if (memcmp(rom + pos, pattern, 7) == 0 &&
            (rom[pos + 7] == alternates[0] || rom[pos + 7] == alternates[1])) {
            cpu_physical_memory_write(rom_paddr + pos + 5, patch, 3);
            /*
             * Don't flush the tb here. Under ordinary conditions, the patched
             * calls are miles away from the current IP. Under malicious
             * conditions, the guest could trick us to crash.
             */
        }
    }

    g_free(rom);

    if (patches != 0 && patches != 2) {
        return -1;
    }

    return 0;
}

/*
 * For TCG mode or the time KVM honors read-only memory regions, we need to
 * enable write access to the option ROM so that variables can be updated by
 * the guest.
 */
static int vapic_map_rom_writable(VAPICROMState *s)
{
    hwaddr rom_paddr = s->rom_state_paddr & ROM_BLOCK_MASK;
    MemoryRegionSection section;
    MemoryRegion *as;
    size_t rom_size;
    uint8_t *ram;

    as = sysbus_address_space(&s->busdev);

    if (s->rom_mapped_writable) {
        memory_region_del_subregion(as, &s->rom);
        object_unparent(OBJECT(&s->rom));
    }

    /* grab RAM memory region (region @rom_paddr may still be pc.rom) */
    section = memory_region_find(as, 0, 1);

    /* read ROM size from RAM region */
    if (rom_paddr + 2 >= memory_region_size(section.mr)) {
        return -1;
    }
    ram = memory_region_get_ram_ptr(section.mr);
    rom_size = ram[rom_paddr + 2] * ROM_BLOCK_SIZE;
    if (rom_size == 0) {
        return -1;
    }
    s->rom_size = rom_size;

    /* We need to round to avoid creating subpages
     * from which we cannot run code. */
    rom_size += rom_paddr & ~TARGET_PAGE_MASK;
    rom_paddr &= TARGET_PAGE_MASK;
    rom_size = TARGET_PAGE_ALIGN(rom_size);

    memory_region_init_alias(&s->rom, OBJECT(s), "kvmvapic-rom", section.mr,
                             rom_paddr, rom_size);
    memory_region_add_subregion_overlap(as, rom_paddr, &s->rom, 1000);
    s->rom_mapped_writable = true;
    memory_region_unref(section.mr);

    return 0;
}

static int vapic_prepare(VAPICROMState *s)
{
    if (vapic_map_rom_writable(s) < 0) {
        return -1;
    }

    if (patch_hypercalls(s) < 0) {
        return -1;
    }

    vapic_enable_tpr_reporting(true);

    return 0;
}

static void vapic_write(void *opaque, hwaddr addr, uint64_t data,
                        unsigned int size)
{
    VAPICROMState *s = opaque;
    X86CPU *cpu;
    CPUX86State *env;
    hwaddr rom_paddr;

    if (!current_cpu) {
        return;
    }

    cpu_synchronize_state(current_cpu);
    cpu = X86_CPU(current_cpu);
    env = &cpu->env;

    /*
     * The VAPIC supports two PIO-based hypercalls, both via port 0x7E.
     *  o 16-bit write access:
     *    Reports the option ROM initialization to the hypervisor. Written
     *    value is the offset of the state structure in the ROM.
     *  o 8-bit write access:
     *    Reactivates the VAPIC after a guest hibernation, i.e. after the
     *    option ROM content has been re-initialized by a guest power cycle.
     *  o 32-bit write access:
     *    Poll for pending IRQs, considering the current VAPIC state.
     */
    switch (size) {
    case 2:
        if (s->state == VAPIC_INACTIVE) {
            rom_paddr = (env->segs[R_CS].base + env->eip) & ROM_BLOCK_MASK;
            s->rom_state_paddr = rom_paddr + data;

            s->state = VAPIC_STANDBY;
        }
        if (vapic_prepare(s) < 0) {
            s->state = VAPIC_INACTIVE;
            s->rom_state_paddr = 0;
            break;
        }
        break;
    case 1:
        if (kvm_enabled()) {
            /*
             * Disable triggering instruction in ROM by writing a NOP.
             *
             * We cannot do this in TCG mode as the reported IP is not
             * accurate.
             */
            pause_all_vcpus();
            patch_byte(cpu, env->eip - 2, 0x66);
            patch_byte(cpu, env->eip - 1, 0x90);
            resume_all_vcpus();
        }

        if (s->state == VAPIC_ACTIVE) {
            break;
        }
        if (update_rom_mapping(s, env, env->eip) < 0) {
            break;
        }
        if (find_real_tpr_addr(s, env) < 0) {
            break;
        }
        vapic_enable(s, cpu);
        break;
    default:
    case 4:
        if (!kvm_irqchip_in_kernel()) {
            apic_poll_irq(cpu->apic_state);
        }
        break;
    }
}

static uint64_t vapic_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffff;
}

static const MemoryRegionOps vapic_ops = {
    .write = vapic_write,
    .read = vapic_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void vapic_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    VAPICROMState *s = VAPIC(dev);

    memory_region_init_io(&s->io, OBJECT(s), &vapic_ops, s, "kvmvapic", 2);
    sysbus_add_io(sbd, VAPIC_IO_PORT, &s->io);
    sysbus_init_ioports(sbd, VAPIC_IO_PORT, 2);

    option_rom[nb_option_roms].name = "kvmvapic.bin";
    option_rom[nb_option_roms].bootindex = -1;
    nb_option_roms++;
}

static void do_vapic_enable(void *data)
{
    VAPICROMState *s = data;
    X86CPU *cpu = X86_CPU(first_cpu);

    static const uint8_t enabled = 1;
    cpu_physical_memory_write(s->vapic_paddr + offsetof(VAPICState, enabled),
                              &enabled, sizeof(enabled));
    apic_enable_vapic(cpu->apic_state, s->vapic_paddr);
    s->state = VAPIC_ACTIVE;
}

static void kvmvapic_vm_state_change(void *opaque, int running,
                                     RunState state)
{
    VAPICROMState *s = opaque;
    uint8_t *zero;

    if (!running) {
        return;
    }

    if (s->state == VAPIC_ACTIVE) {
        if (smp_cpus == 1) {
            run_on_cpu(first_cpu, do_vapic_enable, s);
        } else {
            zero = g_malloc0(s->rom_state.vapic_size);
            cpu_physical_memory_write(s->vapic_paddr, zero,
                                      s->rom_state.vapic_size);
            g_free(zero);
        }
    }

    qemu_del_vm_change_state_handler(s->vmsentry);
}

static int vapic_post_load(void *opaque, int version_id)
{
    VAPICROMState *s = opaque;

    /*
     * The old implementation of qemu-kvm did not provide the state
     * VAPIC_STANDBY. Reconstruct it.
     */
    if (s->state == VAPIC_INACTIVE && s->rom_state_paddr != 0) {
        s->state = VAPIC_STANDBY;
    }

    if (s->state != VAPIC_INACTIVE) {
        if (vapic_prepare(s) < 0) {
            return -1;
        }
    }

    if (!s->vmsentry) {
        s->vmsentry =
            qemu_add_vm_change_state_handler(kvmvapic_vm_state_change, s);
    }
    return 0;
}

static const VMStateDescription vmstate_handlers = {
    .name = "kvmvapic-handlers",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(set_tpr, VAPICHandlers),
        VMSTATE_UINT32(set_tpr_eax, VAPICHandlers),
        VMSTATE_UINT32_ARRAY(get_tpr, VAPICHandlers, 8),
        VMSTATE_UINT32(get_tpr_stack, VAPICHandlers),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_guest_rom = {
    .name = "kvmvapic-guest-rom",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UNUSED(8),     /* signature */
        VMSTATE_UINT32(vaddr, GuestROMState),
        VMSTATE_UINT32(fixup_start, GuestROMState),
        VMSTATE_UINT32(fixup_end, GuestROMState),
        VMSTATE_UINT32(vapic_vaddr, GuestROMState),
        VMSTATE_UINT32(vapic_size, GuestROMState),
        VMSTATE_UINT32(vcpu_shift, GuestROMState),
        VMSTATE_UINT32(real_tpr_addr, GuestROMState),
        VMSTATE_STRUCT(up, GuestROMState, 0, vmstate_handlers, VAPICHandlers),
        VMSTATE_STRUCT(mp, GuestROMState, 0, vmstate_handlers, VAPICHandlers),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_vapic = {
    .name = "kvm-tpr-opt",      /* compatible with qemu-kvm VAPIC */
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vapic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(rom_state, VAPICROMState, 0, vmstate_guest_rom,
                       GuestROMState),
        VMSTATE_UINT32(state, VAPICROMState),
        VMSTATE_UINT32(real_tpr_addr, VAPICROMState),
        VMSTATE_UINT32(rom_state_vaddr, VAPICROMState),
        VMSTATE_UINT32(vapic_paddr, VAPICROMState),
        VMSTATE_UINT32(rom_state_paddr, VAPICROMState),
        VMSTATE_END_OF_LIST()
    }
};

static void vapic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = vapic_reset;
    dc->vmsd    = &vmstate_vapic;
    dc->realize = vapic_realize;
}

static const TypeInfo vapic_type = {
    .name          = TYPE_VAPIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VAPICROMState),
    .class_init    = vapic_class_init,
};

static void vapic_register(void)
{
    type_register_static(&vapic_type);
}

type_init(vapic_register);
