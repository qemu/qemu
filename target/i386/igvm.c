/*
 * QEMU IGVM, support for native x86 guests
 *
 * Copyright (C) 2026 Red Hat
 *
 * Authors:
 *  Gerd Hoffmann <kraxel@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "hw/i386/e820_memory_layout.h"
#include "system/igvm.h"

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

#define FLAGS_TO_SEGCACHE(flags)                \
    (((unsigned int)flags) << 8)

static void qigvm_x86_load_context(struct IgvmNativeVpContextX64 *context,
                                   CPUX86State *env)
{
    cpu_load_efer(env, context->efer);
    cpu_x86_update_cr4(env, context->cr4);
    cpu_x86_update_cr0(env, context->cr0);
    cpu_x86_update_cr3(env, context->cr3);

    cpu_x86_load_seg_cache(
        env, R_CS, context->code_selector,
        context->code_base, context->code_limit,
        FLAGS_TO_SEGCACHE(context->code_attributes));
    cpu_x86_load_seg_cache(
        env, R_DS, context->data_selector,
        context->data_base, context->data_limit,
        FLAGS_TO_SEGCACHE(context->data_attributes));
    cpu_x86_load_seg_cache(
        env, R_ES, context->data_selector,
        context->data_base, context->data_limit,
        FLAGS_TO_SEGCACHE(context->data_attributes));
    cpu_x86_load_seg_cache(
        env, R_FS, context->data_selector,
        context->data_base, context->data_limit,
        FLAGS_TO_SEGCACHE(context->data_attributes));
    cpu_x86_load_seg_cache(
        env, R_GS, context->data_selector,
        context->data_base, context->data_limit,
        FLAGS_TO_SEGCACHE(context->data_attributes));
    cpu_x86_load_seg_cache(
        env, R_SS, context->data_selector,
        context->data_base, context->data_limit,
        FLAGS_TO_SEGCACHE(context->data_attributes));

    env->gdt.base = context->gdtr_base;
    env->gdt.limit = context->gdtr_limit;
    env->idt.base = context->idtr_base;
    env->idt.limit = context->idtr_limit;

    env->regs[R_EAX] = context->rax;
    env->regs[R_ECX] = context->rcx;
    env->regs[R_EDX] = context->rdx;
    env->regs[R_EBX] = context->rbx;
    env->regs[R_ESP] = context->rsp;
    env->regs[R_EBP] = context->rbp;
    env->regs[R_ESI] = context->rsi;
    env->regs[R_EDI] = context->rdi;
#ifdef TARGET_X86_64
    env->regs[R_R8] = context->r8;
    env->regs[R_R9] = context->r9;
    env->regs[R_R10] = context->r10;
    env->regs[R_R11] = context->r11;
    env->regs[R_R12] = context->r12;
    env->regs[R_R13] = context->r13;
    env->regs[R_R14] = context->r14;
    env->regs[R_R15] = context->r15;
#endif
    env->eip = context->rip;
    env->eflags = context->rflags;
}

/*
 * convert e820 table into igvm memory map
 */
int qigvm_x86_get_mem_map_entry(int index,
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
        entry->type = CGS_MEM_RAM;
        break;
    case E820_RESERVED:
        entry->type = CGS_MEM_RESERVED;
        break;
    default:
        /* should not happen */
        error_setg(errp, "unknown e820 type");
        return -1;
    }
    return 0;
}

/*
 * set initial cpu context
 */
static struct IgvmNativeVpContextX64 *bsp_context;

int qigvm_x86_set_vp_context(void *data, int index, Error **errp)
{
    if (index != 0) {
        error_setg(errp, "context can be set for BSP only");
        return -1;
    }

    if (bsp_context == NULL) {
        bsp_context = g_new0(struct IgvmNativeVpContextX64, 1);
    }
    memcpy(bsp_context, data, sizeof(struct IgvmNativeVpContextX64));
    return 0;
}

void qigvm_x86_bsp_reset(CPUX86State *env)
{
    if (bsp_context == NULL) {
        return;
    }

    qigvm_x86_load_context(bsp_context, env);
}
