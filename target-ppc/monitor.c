/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "hmp.h"

static target_long monitor_get_ccr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    unsigned int u;
    int i;

    u = 0;
    for (i = 0; i < 8; i++)
        u |= env->crf[i] << (32 - (4 * (i + 1)));

    return u;
}

static target_long monitor_get_msr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return env->msr;
}

static target_long monitor_get_xer (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return env->xer;
}

static target_long monitor_get_decr (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_decr(env);
}

static target_long monitor_get_tbu (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_tbu(env);
}

static target_long monitor_get_tbl (const struct MonitorDef *md, int val)
{
    CPUArchState *env = mon_get_cpu_env();
    return cpu_ppc_load_tbl(env);
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env1 = mon_get_cpu_env();

    dump_mmu((FILE*)mon, (fprintf_function)monitor_printf, env1);
}


const MonitorDef monitor_defs[] = {
    /* General purpose registers */
    { "r0", offsetof(CPUPPCState, gpr[0]) },
    { "r1", offsetof(CPUPPCState, gpr[1]) },
    { "r2", offsetof(CPUPPCState, gpr[2]) },
    { "r3", offsetof(CPUPPCState, gpr[3]) },
    { "r4", offsetof(CPUPPCState, gpr[4]) },
    { "r5", offsetof(CPUPPCState, gpr[5]) },
    { "r6", offsetof(CPUPPCState, gpr[6]) },
    { "r7", offsetof(CPUPPCState, gpr[7]) },
    { "r8", offsetof(CPUPPCState, gpr[8]) },
    { "r9", offsetof(CPUPPCState, gpr[9]) },
    { "r10", offsetof(CPUPPCState, gpr[10]) },
    { "r11", offsetof(CPUPPCState, gpr[11]) },
    { "r12", offsetof(CPUPPCState, gpr[12]) },
    { "r13", offsetof(CPUPPCState, gpr[13]) },
    { "r14", offsetof(CPUPPCState, gpr[14]) },
    { "r15", offsetof(CPUPPCState, gpr[15]) },
    { "r16", offsetof(CPUPPCState, gpr[16]) },
    { "r17", offsetof(CPUPPCState, gpr[17]) },
    { "r18", offsetof(CPUPPCState, gpr[18]) },
    { "r19", offsetof(CPUPPCState, gpr[19]) },
    { "r20", offsetof(CPUPPCState, gpr[20]) },
    { "r21", offsetof(CPUPPCState, gpr[21]) },
    { "r22", offsetof(CPUPPCState, gpr[22]) },
    { "r23", offsetof(CPUPPCState, gpr[23]) },
    { "r24", offsetof(CPUPPCState, gpr[24]) },
    { "r25", offsetof(CPUPPCState, gpr[25]) },
    { "r26", offsetof(CPUPPCState, gpr[26]) },
    { "r27", offsetof(CPUPPCState, gpr[27]) },
    { "r28", offsetof(CPUPPCState, gpr[28]) },
    { "r29", offsetof(CPUPPCState, gpr[29]) },
    { "r30", offsetof(CPUPPCState, gpr[30]) },
    { "r31", offsetof(CPUPPCState, gpr[31]) },
    /* Floating point registers */
    { "f0", offsetof(CPUPPCState, fpr[0]) },
    { "f1", offsetof(CPUPPCState, fpr[1]) },
    { "f2", offsetof(CPUPPCState, fpr[2]) },
    { "f3", offsetof(CPUPPCState, fpr[3]) },
    { "f4", offsetof(CPUPPCState, fpr[4]) },
    { "f5", offsetof(CPUPPCState, fpr[5]) },
    { "f6", offsetof(CPUPPCState, fpr[6]) },
    { "f7", offsetof(CPUPPCState, fpr[7]) },
    { "f8", offsetof(CPUPPCState, fpr[8]) },
    { "f9", offsetof(CPUPPCState, fpr[9]) },
    { "f10", offsetof(CPUPPCState, fpr[10]) },
    { "f11", offsetof(CPUPPCState, fpr[11]) },
    { "f12", offsetof(CPUPPCState, fpr[12]) },
    { "f13", offsetof(CPUPPCState, fpr[13]) },
    { "f14", offsetof(CPUPPCState, fpr[14]) },
    { "f15", offsetof(CPUPPCState, fpr[15]) },
    { "f16", offsetof(CPUPPCState, fpr[16]) },
    { "f17", offsetof(CPUPPCState, fpr[17]) },
    { "f18", offsetof(CPUPPCState, fpr[18]) },
    { "f19", offsetof(CPUPPCState, fpr[19]) },
    { "f20", offsetof(CPUPPCState, fpr[20]) },
    { "f21", offsetof(CPUPPCState, fpr[21]) },
    { "f22", offsetof(CPUPPCState, fpr[22]) },
    { "f23", offsetof(CPUPPCState, fpr[23]) },
    { "f24", offsetof(CPUPPCState, fpr[24]) },
    { "f25", offsetof(CPUPPCState, fpr[25]) },
    { "f26", offsetof(CPUPPCState, fpr[26]) },
    { "f27", offsetof(CPUPPCState, fpr[27]) },
    { "f28", offsetof(CPUPPCState, fpr[28]) },
    { "f29", offsetof(CPUPPCState, fpr[29]) },
    { "f30", offsetof(CPUPPCState, fpr[30]) },
    { "f31", offsetof(CPUPPCState, fpr[31]) },
    { "fpscr", offsetof(CPUPPCState, fpscr) },
    /* Next instruction pointer */
    { "nip|pc", offsetof(CPUPPCState, nip) },
    { "lr", offsetof(CPUPPCState, lr) },
    { "ctr", offsetof(CPUPPCState, ctr) },
    { "decr", 0, &monitor_get_decr, },
    { "ccr", 0, &monitor_get_ccr, },
    /* Machine state register */
    { "msr", 0, &monitor_get_msr, },
    { "xer", 0, &monitor_get_xer, },
    { "tbu", 0, &monitor_get_tbu, },
    { "tbl", 0, &monitor_get_tbl, },
    /* Segment registers */
    { "sdr1", offsetof(CPUPPCState, spr[SPR_SDR1]) },
    { "sr0", offsetof(CPUPPCState, sr[0]) },
    { "sr1", offsetof(CPUPPCState, sr[1]) },
    { "sr2", offsetof(CPUPPCState, sr[2]) },
    { "sr3", offsetof(CPUPPCState, sr[3]) },
    { "sr4", offsetof(CPUPPCState, sr[4]) },
    { "sr5", offsetof(CPUPPCState, sr[5]) },
    { "sr6", offsetof(CPUPPCState, sr[6]) },
    { "sr7", offsetof(CPUPPCState, sr[7]) },
    { "sr8", offsetof(CPUPPCState, sr[8]) },
    { "sr9", offsetof(CPUPPCState, sr[9]) },
    { "sr10", offsetof(CPUPPCState, sr[10]) },
    { "sr11", offsetof(CPUPPCState, sr[11]) },
    { "sr12", offsetof(CPUPPCState, sr[12]) },
    { "sr13", offsetof(CPUPPCState, sr[13]) },
    { "sr14", offsetof(CPUPPCState, sr[14]) },
    { "sr15", offsetof(CPUPPCState, sr[15]) },
    /* Too lazy to put BATs... */
    { "pvr", offsetof(CPUPPCState, spr[SPR_PVR]) },

    { "srr0", offsetof(CPUPPCState, spr[SPR_SRR0]) },
    { "srr1", offsetof(CPUPPCState, spr[SPR_SRR1]) },
    { "dar", offsetof(CPUPPCState, spr[SPR_DAR]) },
    { "dsisr", offsetof(CPUPPCState, spr[SPR_DSISR]) },
    { "cfar", offsetof(CPUPPCState, spr[SPR_CFAR]) },
    { "sprg0", offsetof(CPUPPCState, spr[SPR_SPRG0]) },
    { "sprg1", offsetof(CPUPPCState, spr[SPR_SPRG1]) },
    { "sprg2", offsetof(CPUPPCState, spr[SPR_SPRG2]) },
    { "sprg3", offsetof(CPUPPCState, spr[SPR_SPRG3]) },
    { "sprg4", offsetof(CPUPPCState, spr[SPR_SPRG4]) },
    { "sprg5", offsetof(CPUPPCState, spr[SPR_SPRG5]) },
    { "sprg6", offsetof(CPUPPCState, spr[SPR_SPRG6]) },
    { "sprg7", offsetof(CPUPPCState, spr[SPR_SPRG7]) },
    { "pid", offsetof(CPUPPCState, spr[SPR_BOOKE_PID]) },
    { "csrr0", offsetof(CPUPPCState, spr[SPR_BOOKE_CSRR0]) },
    { "csrr1", offsetof(CPUPPCState, spr[SPR_BOOKE_CSRR1]) },
    { "esr", offsetof(CPUPPCState, spr[SPR_BOOKE_ESR]) },
    { "dear", offsetof(CPUPPCState, spr[SPR_BOOKE_DEAR]) },
    { "mcsr", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSR]) },
    { "tsr", offsetof(CPUPPCState, spr[SPR_BOOKE_TSR]) },
    { "tcr", offsetof(CPUPPCState, spr[SPR_BOOKE_TCR]) },
    { "vrsave", offsetof(CPUPPCState, spr[SPR_VRSAVE]) },
    { "pir", offsetof(CPUPPCState, spr[SPR_BOOKE_PIR]) },
    { "mcsrr0", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSRR0]) },
    { "mcsrr1", offsetof(CPUPPCState, spr[SPR_BOOKE_MCSRR1]) },
    { "decar", offsetof(CPUPPCState, spr[SPR_BOOKE_DECAR]) },
    { "ivpr", offsetof(CPUPPCState, spr[SPR_BOOKE_IVPR]) },
    { "epcr", offsetof(CPUPPCState, spr[SPR_BOOKE_EPCR]) },
    { "sprg8", offsetof(CPUPPCState, spr[SPR_BOOKE_SPRG8]) },
    { "ivor0", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR0]) },
    { "ivor1", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR1]) },
    { "ivor2", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR2]) },
    { "ivor3", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR3]) },
    { "ivor4", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR4]) },
    { "ivor5", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR5]) },
    { "ivor6", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR6]) },
    { "ivor7", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR7]) },
    { "ivor8", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR8]) },
    { "ivor9", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR9]) },
    { "ivor10", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR10]) },
    { "ivor11", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR11]) },
    { "ivor12", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR12]) },
    { "ivor13", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR13]) },
    { "ivor14", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR14]) },
    { "ivor15", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR15]) },
    { "ivor32", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR32]) },
    { "ivor33", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR33]) },
    { "ivor34", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR34]) },
    { "ivor35", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR35]) },
    { "ivor36", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR36]) },
    { "ivor37", offsetof(CPUPPCState, spr[SPR_BOOKE_IVOR37]) },
    { "mas0", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS0]) },
    { "mas1", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS1]) },
    { "mas2", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS2]) },
    { "mas3", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS3]) },
    { "mas4", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS4]) },
    { "mas6", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS6]) },
    { "mas7", offsetof(CPUPPCState, spr[SPR_BOOKE_MAS7]) },
    { "mmucfg", offsetof(CPUPPCState, spr[SPR_MMUCFG]) },
    { "tlb0cfg", offsetof(CPUPPCState, spr[SPR_BOOKE_TLB0CFG]) },
    { "tlb1cfg", offsetof(CPUPPCState, spr[SPR_BOOKE_TLB1CFG]) },
    { "epr", offsetof(CPUPPCState, spr[SPR_BOOKE_EPR]) },
    { "eplc", offsetof(CPUPPCState, spr[SPR_BOOKE_EPLC]) },
    { "epsc", offsetof(CPUPPCState, spr[SPR_BOOKE_EPSC]) },
    { "svr", offsetof(CPUPPCState, spr[SPR_E500_SVR]) },
    { "mcar", offsetof(CPUPPCState, spr[SPR_Exxx_MCAR]) },
    { "pid1", offsetof(CPUPPCState, spr[SPR_BOOKE_PID1]) },
    { "pid2", offsetof(CPUPPCState, spr[SPR_BOOKE_PID2]) },
    { "hid0", offsetof(CPUPPCState, spr[SPR_HID0]) },
    { NULL },
};

const MonitorDef *target_monitor_defs(void)
{
    return monitor_defs;
}
