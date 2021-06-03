// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "qemu/osdep.h"
#include "panic.h"
#include "qemu-common.h"
#include "qemu/error-report.h"

#include "sysemu/hvf.h"
#include "hvf-i386.h"
#include "vmcs.h"
#include "vmx.h"
#include "x86.h"
#include "x86_descr.h"
#include "x86_mmu.h"
#include "x86_decode.h"
#include "x86_emu.h"
#include "x86_task.h"
#include "x86hvf.h"

#include <Hypervisor/hv.h>
#include <Hypervisor/hv_vmx.h>

#include "hw/i386/apic_internal.h"
#include "qemu/main-loop.h"
#include "qemu/accel.h"
#include "target/i386/cpu.h"

// TODO: taskswitch handling
static void save_state_to_tss32(CPUState *cpu, struct x86_tss_segment32 *tss)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    /* CR3 and ldt selector are not saved intentionally */
    tss->eip = (uint32_t)env->eip;
    tss->eflags = (uint32_t)env->eflags;
    tss->eax = EAX(env);
    tss->ecx = ECX(env);
    tss->edx = EDX(env);
    tss->ebx = EBX(env);
    tss->esp = ESP(env);
    tss->ebp = EBP(env);
    tss->esi = ESI(env);
    tss->edi = EDI(env);

    tss->es = vmx_read_segment_selector(cpu, R_ES).sel;
    tss->cs = vmx_read_segment_selector(cpu, R_CS).sel;
    tss->ss = vmx_read_segment_selector(cpu, R_SS).sel;
    tss->ds = vmx_read_segment_selector(cpu, R_DS).sel;
    tss->fs = vmx_read_segment_selector(cpu, R_FS).sel;
    tss->gs = vmx_read_segment_selector(cpu, R_GS).sel;
}

static void load_state_from_tss32(CPUState *cpu, struct x86_tss_segment32 *tss)
{
    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    wvmcs(cpu->hvf->fd, VMCS_GUEST_CR3, tss->cr3);

    env->eip = tss->eip;
    env->eflags = tss->eflags | 2;

    /* General purpose registers */
    RAX(env) = tss->eax;
    RCX(env) = tss->ecx;
    RDX(env) = tss->edx;
    RBX(env) = tss->ebx;
    RSP(env) = tss->esp;
    RBP(env) = tss->ebp;
    RSI(env) = tss->esi;
    RDI(env) = tss->edi;

    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->ldt}}, R_LDTR);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->es}}, R_ES);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->cs}}, R_CS);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->ss}}, R_SS);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->ds}}, R_DS);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->fs}}, R_FS);
    vmx_write_segment_selector(cpu, (x68_segment_selector){{tss->gs}}, R_GS);
}

static int task_switch_32(CPUState *cpu, x68_segment_selector tss_sel, x68_segment_selector old_tss_sel,
                          uint64_t old_tss_base, struct x86_segment_descriptor *new_desc)
{
    struct x86_tss_segment32 tss_seg;
    uint32_t new_tss_base = x86_segment_base(new_desc);
    uint32_t eip_offset = offsetof(struct x86_tss_segment32, eip);
    uint32_t ldt_sel_offset = offsetof(struct x86_tss_segment32, ldt);

    vmx_read_mem(cpu, &tss_seg, old_tss_base, sizeof(tss_seg));
    save_state_to_tss32(cpu, &tss_seg);

    vmx_write_mem(cpu, old_tss_base + eip_offset, &tss_seg.eip, ldt_sel_offset - eip_offset);
    vmx_read_mem(cpu, &tss_seg, new_tss_base, sizeof(tss_seg));

    if (old_tss_sel.sel != 0xffff) {
        tss_seg.prev_tss = old_tss_sel.sel;

        vmx_write_mem(cpu, new_tss_base, &tss_seg.prev_tss, sizeof(tss_seg.prev_tss));
    }
    load_state_from_tss32(cpu, &tss_seg);
    return 0;
}

void vmx_handle_task_switch(CPUState *cpu, x68_segment_selector tss_sel, int reason, bool gate_valid, uint8_t gate, uint64_t gate_type)
{
    uint64_t rip = rreg(cpu->hvf->fd, HV_X86_RIP);
    if (!gate_valid || (gate_type != VMCS_INTR_T_HWEXCEPTION &&
                        gate_type != VMCS_INTR_T_HWINTR &&
                        gate_type != VMCS_INTR_T_NMI)) {
        int ins_len = rvmcs(cpu->hvf->fd, VMCS_EXIT_INSTRUCTION_LENGTH);
        macvm_set_rip(cpu, rip + ins_len);
        return;
    }

    load_regs(cpu);

    struct x86_segment_descriptor curr_tss_desc, next_tss_desc;
    int ret;
    x68_segment_selector old_tss_sel = vmx_read_segment_selector(cpu, R_TR);
    uint64_t old_tss_base = vmx_read_segment_base(cpu, R_TR);
    uint32_t desc_limit;
    struct x86_call_gate task_gate_desc;
    struct vmx_segment vmx_seg;

    X86CPU *x86_cpu = X86_CPU(cpu);
    CPUX86State *env = &x86_cpu->env;

    x86_read_segment_descriptor(cpu, &next_tss_desc, tss_sel);
    x86_read_segment_descriptor(cpu, &curr_tss_desc, old_tss_sel);

    if (reason == TSR_IDT_GATE && gate_valid) {
        int dpl;

        ret = x86_read_call_gate(cpu, &task_gate_desc, gate);

        dpl = task_gate_desc.dpl;
        x68_segment_selector cs = vmx_read_segment_selector(cpu, R_CS);
        if (tss_sel.rpl > dpl || cs.rpl > dpl)
            ;//DPRINTF("emulate_gp");
    }

    desc_limit = x86_segment_limit(&next_tss_desc);
    if (!next_tss_desc.p || ((desc_limit < 0x67 && (next_tss_desc.type & 8)) || desc_limit < 0x2b)) {
        VM_PANIC("emulate_ts");
    }

    if (reason == TSR_IRET || reason == TSR_JMP) {
        curr_tss_desc.type &= ~(1 << 1); /* clear busy flag */
        x86_write_segment_descriptor(cpu, &curr_tss_desc, old_tss_sel);
    }

    if (reason == TSR_IRET)
        env->eflags &= ~NT_MASK;

    if (reason != TSR_CALL && reason != TSR_IDT_GATE)
        old_tss_sel.sel = 0xffff;

    if (reason != TSR_IRET) {
        next_tss_desc.type |= (1 << 1); /* set busy flag */
        x86_write_segment_descriptor(cpu, &next_tss_desc, tss_sel);
    }

    if (next_tss_desc.type & 8)
        ret = task_switch_32(cpu, tss_sel, old_tss_sel, old_tss_base, &next_tss_desc);
    else
        //ret = task_switch_16(cpu, tss_sel, old_tss_sel, old_tss_base, &next_tss_desc);
        VM_PANIC("task_switch_16");

    macvm_set_cr0(cpu->hvf->fd, rvmcs(cpu->hvf->fd, VMCS_GUEST_CR0) | CR0_TS);
    x86_segment_descriptor_to_vmx(cpu, tss_sel, &next_tss_desc, &vmx_seg);
    vmx_write_segment_descriptor(cpu, &vmx_seg, R_TR);

    store_regs(cpu);

    hv_vcpu_invalidate_tlb(cpu->hvf->fd);
    hv_vcpu_flush(cpu->hvf->fd);
}
