#include "qemu/osdep.h"
#include "qemu/error-report.h"

#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "hw/i386/pc.h"
#include "hw/i386/e820_memory_layout.h"

#include "cpu.h"
#include "confidential-guest.h"

#define TYPE_NO_CC "nocc"
OBJECT_DECLARE_TYPE(NoCCState, NoCCStateClass, NO_CC);

struct IgvmNativeVpContextX64 {
    uint64_t rax;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rbx;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t idtr_base;
    uint16_t idtr_limit;
    uint16_t reserved[2];
    uint16_t gdtr_limit;
    uint64_t gdtr_base;

    uint16_t code_selector;
    uint16_t code_attributes;
    uint32_t code_base;
    uint32_t code_limit;

    uint16_t data_selector;
    uint16_t data_attributes;
    uint32_t data_base;
    uint32_t data_limit;

    uint64_t gs_base;
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t efer;
};

struct NoCCState {
    X86ConfidentialGuest parent_obj;
    struct IgvmNativeVpContextX64 regs;
};

struct NoCCStateClass {
    X86ConfidentialGuestClass parent_class;
};

#define FLAGS_TO_SEGCACHE(flags)                \
    (((unsigned int)flags) << 8)

static void no_cc_set_regs(NoCCState *nocc)
{
    X86CPU *x86 = X86_CPU(first_cpu);
    CPUX86State *env = &x86->env;

    cpu_load_efer(env, nocc->regs.efer);
    cpu_x86_update_cr4(env, nocc->regs.cr4);
    cpu_x86_update_cr0(env, nocc->regs.cr0);
    cpu_x86_update_cr3(env, nocc->regs.cr3);

    cpu_x86_load_seg_cache(
        env, R_CS, nocc->regs.code_selector,
        nocc->regs.code_base, nocc->regs.code_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.code_attributes));
    cpu_x86_load_seg_cache(
        env, R_DS, nocc->regs.data_selector,
        nocc->regs.data_base, nocc->regs.data_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.data_attributes));
    cpu_x86_load_seg_cache(
        env, R_ES, nocc->regs.data_selector,
        nocc->regs.data_base, nocc->regs.data_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.data_attributes));
    cpu_x86_load_seg_cache(
        env, R_FS, nocc->regs.data_selector,
        nocc->regs.data_base, nocc->regs.data_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.data_attributes));
    cpu_x86_load_seg_cache(
        env, R_GS, nocc->regs.data_selector,
        nocc->regs.data_base, nocc->regs.data_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.data_attributes));
    cpu_x86_load_seg_cache(
        env, R_SS, nocc->regs.data_selector,
        nocc->regs.data_base, nocc->regs.data_limit,
        FLAGS_TO_SEGCACHE(nocc->regs.data_attributes));

    env->gdt.base = nocc->regs.gdtr_base;
    env->gdt.limit = nocc->regs.gdtr_limit;
    env->idt.base = nocc->regs.idtr_base;
    env->idt.limit = nocc->regs.idtr_limit;

    env->regs[R_EAX] = nocc->regs.rax;
    env->regs[R_ECX] = nocc->regs.rcx;
    env->regs[R_EDX] = nocc->regs.rdx;
    env->regs[R_EBX] = nocc->regs.rbx;
    env->regs[R_ESP] = nocc->regs.rsp;
    env->regs[R_EBP] = nocc->regs.rbp;
    env->regs[R_ESI] = nocc->regs.rsi;
    env->regs[R_EDI] = nocc->regs.rdi;
#ifdef TARGET_X86_64
    env->regs[R_R8] = nocc->regs.r8;
    env->regs[R_R9] = nocc->regs.r9;
    env->regs[R_R10] = nocc->regs.r10;
    env->regs[R_R11] = nocc->regs.r11;
    env->regs[R_R12] = nocc->regs.r12;
    env->regs[R_R13] = nocc->regs.r13;
    env->regs[R_R14] = nocc->regs.r14;
    env->regs[R_R15] = nocc->regs.r15;
#endif
    env->eip = nocc->regs.rip;
    env->eflags = nocc->regs.rflags;
}

static int no_cc_kvm_init(ConfidentialGuestSupport *cgs, Error **errp)
{
    info_report("%s:", __func__);
    return 0;
}

static int no_cc_kvm_reset(ConfidentialGuestSupport *cgs, Error **errp)
{
    NoCCState *nocc = NO_CC(cgs);

    info_report("%s: rip 0x%lx", __func__, nocc->regs.rip);
    no_cc_set_regs(nocc);
    return 0;
}

static bool no_cc_check_support(ConfidentialGuestPlatformType platform,
                                uint16_t platform_version, uint8_t highest_vtl,
                                uint64_t shared_gpa_boundary)
{
    return false;
}

static int no_cc_set_guest_state(hwaddr gpa, uint8_t *ptr, uint64_t len,
                                 ConfidentialGuestPageType memory_type,
                                 uint16_t cpu_index, Error **errp)
{
    static const char *names[] = {
        [ CGS_PAGE_TYPE_NORMAL ]          = "normal",
        [ CGS_PAGE_TYPE_VMSA ]            = "vmsa",
        [ CGS_PAGE_TYPE_ZERO ]            = "zero",
        [ CGS_PAGE_TYPE_UNMEASURED ]      = "unmeasured",
        [ CGS_PAGE_TYPE_SECRETS ]         = "secrets",
        [ CGS_PAGE_TYPE_CPUID ]           = "cpuid",
        [ CGS_PAGE_TYPE_REQUIRED_MEMORY ] = "required-mem",
    };

    ConfidentialGuestSupport *cgs = MACHINE(qdev_get_machine())->cgs;
    NoCCState *nocc = NO_CC(cgs);
    struct IgvmNativeVpContextX64 *regs;

    switch (memory_type) {
    case CGS_PAGE_TYPE_VMSA:
        info_report("%s: %lx +%lx [%s]",
                    __func__, gpa, len, names[memory_type]);
        regs = (void *)ptr;
        nocc->regs = *regs;
        no_cc_set_regs(nocc);
        return 0;

    case CGS_PAGE_TYPE_NORMAL:
    case CGS_PAGE_TYPE_ZERO:
    case CGS_PAGE_TYPE_UNMEASURED:
    case CGS_PAGE_TYPE_REQUIRED_MEMORY:
        info_report("%s: %lx +%lx [%s]",
                    __func__, gpa, len, names[memory_type]);
        return 0;

    case CGS_PAGE_TYPE_SECRETS:
    case CGS_PAGE_TYPE_CPUID:
        error_report("%s: %lx +%lx [%s, unsupported]",
                     __func__, gpa, len, names[memory_type]);
        return -1;

    default:
        error_setg(errp, "%s: unknown memory type: %d", __func__, memory_type);
        return -1;
    }
}

static int no_cc_get_mem_map_entry(int index,
                                   ConfidentialGuestMemoryMapEntry *entry,
                                   Error **errp)
{
    struct e820_entry *table;
    int num_entries;

    num_entries = e820_get_table(&table);
    if ((index < 0) || (index >= num_entries)) {
        return 1;
    }
    entry->gpa = table[index].address;
    entry->size = table[index].length;
    switch (table[index].type) {
    case E820_RAM:
        info_report("%s: ram: %lx +%lx", __func__, entry->gpa, entry->size);
        entry->type = CGS_MEM_RAM;
        break;
    case E820_RESERVED:
        info_report("%s: reserved: %lx +%lx", __func__, entry->gpa, entry->size);
        entry->type = CGS_MEM_RESERVED;
        break;
    default:
        return -1;
    }
    return 0;
}

static void
no_cc_class_init(ObjectClass *oc, void *data)
{
    ConfidentialGuestSupportClass *cgsc =
        CONFIDENTIAL_GUEST_SUPPORT_CLASS(oc);

    cgsc->kvm_init = no_cc_kvm_init;
    cgsc->kvm_reset = no_cc_kvm_reset;
    cgsc->check_support = no_cc_check_support;
    cgsc->set_guest_state = no_cc_set_guest_state;
    cgsc->get_mem_map_entry = no_cc_get_mem_map_entry;
}

static void
no_cc_instance_init(Object *obj)
{
    ConfidentialGuestSupport *cgs =
        CONFIDENTIAL_GUEST_SUPPORT(obj);

    cgs->ready = true;
}

static const TypeInfo no_cc_info = {
    .parent = TYPE_X86_CONFIDENTIAL_GUEST,
    .name = TYPE_NO_CC,
    .instance_size = sizeof(NoCCState),
    .instance_init = no_cc_instance_init,
    .class_size = sizeof(NoCCStateClass),
    .class_init = no_cc_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void
no_cc_register_types(void)
{
    type_register_static(&no_cc_info);
}

type_init(no_cc_register_types);
