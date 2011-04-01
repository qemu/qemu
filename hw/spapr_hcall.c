#include "sysemu.h"
#include "cpu.h"
#include "qemu-char.h"
#include "hw/spapr.h"

spapr_hcall_fn hypercall_table[(MAX_HCALL_OPCODE / 4) + 1];

void spapr_register_hypercall(target_ulong opcode, spapr_hcall_fn fn)
{
    spapr_hcall_fn old_fn;

    assert(opcode <= MAX_HCALL_OPCODE);
    assert((opcode & 0x3) == 0);

    old_fn = hypercall_table[opcode / 4];

    assert(!old_fn || (fn == old_fn));

    hypercall_table[opcode / 4] = fn;
}

target_ulong spapr_hypercall(CPUState *env, target_ulong opcode,
                             target_ulong *args)
{
    if (msr_pr) {
        hcall_dprintf("Hypercall made with MSR[PR]=1\n");
        return H_PRIVILEGE;
    }

    if ((opcode <= MAX_HCALL_OPCODE)
        && ((opcode & 0x3) == 0)) {
        spapr_hcall_fn fn = hypercall_table[opcode / 4];

        if (fn) {
            return fn(env, spapr, opcode, args);
        }
    }

    hcall_dprintf("Unimplemented hcall 0x" TARGET_FMT_lx "\n", opcode);
    return H_FUNCTION;
}
