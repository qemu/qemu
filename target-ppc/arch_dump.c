/*
 * writing ELF notes for ppc64 arch
 *
 *
 * Copyright IBM, Corp. 2013
 *
 * Authors:
 * Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "exec/cpu-all.h"
#include "sysemu/dump.h"
#include "sysemu/kvm.h"

struct PPC64UserRegStruct {
    uint64_t gpr[32];
    uint64_t nip;
    uint64_t msr;
    uint64_t orig_gpr3;
    uint64_t ctr;
    uint64_t link;
    uint64_t xer;
    uint64_t ccr;
    uint64_t softe;
    uint64_t trap;
    uint64_t dar;
    uint64_t dsisr;
    uint64_t result;
} QEMU_PACKED;

struct PPC64ElfPrstatus {
    char pad1[112];
    struct PPC64UserRegStruct pr_reg;
    uint64_t pad2[4];
} QEMU_PACKED;


struct PPC64ElfFpregset {
    uint64_t fpr[32];
    uint64_t fpscr;
}  QEMU_PACKED;


struct PPC64ElfVmxregset {
    ppc_avr_t avr[32];
    ppc_avr_t vscr;
    union {
        ppc_avr_t unused;
        uint32_t value;
    } vrsave;
}  QEMU_PACKED;

struct PPC64ElfVsxregset {
    uint64_t vsr[32];
}  QEMU_PACKED;

struct PPC64ElfSperegset {
    uint32_t evr[32];
    uint64_t spe_acc;
    uint32_t spe_fscr;
}  QEMU_PACKED;

typedef struct noteStruct {
    Elf64_Nhdr hdr;
    char name[5];
    char pad3[3];
    union {
        struct PPC64ElfPrstatus  prstatus;
        struct PPC64ElfFpregset  fpregset;
        struct PPC64ElfVmxregset vmxregset;
        struct PPC64ElfVsxregset vsxregset;
        struct PPC64ElfSperegset speregset;
    } contents;
} QEMU_PACKED Note;

typedef struct NoteFuncArg {
    Note note;
    DumpState *state;
} NoteFuncArg;

static void ppc64_write_elf64_prstatus(NoteFuncArg *arg, PowerPCCPU *cpu)
{
    int i;
    uint64_t cr;
    struct PPC64ElfPrstatus *prstatus;
    struct PPC64UserRegStruct *reg;
    Note *note = &arg->note;
    DumpState *s = arg->state;

    note->hdr.n_type = cpu_to_dump32(s, NT_PRSTATUS);

    prstatus = &note->contents.prstatus;
    memset(prstatus, 0, sizeof(*prstatus));
    reg = &prstatus->pr_reg;

    for (i = 0; i < 32; i++) {
        reg->gpr[i] = cpu_to_dump64(s, cpu->env.gpr[i]);
    }
    reg->nip = cpu_to_dump64(s, cpu->env.nip);
    reg->msr = cpu_to_dump64(s, cpu->env.msr);
    reg->ctr = cpu_to_dump64(s, cpu->env.ctr);
    reg->link = cpu_to_dump64(s, cpu->env.lr);
    reg->xer = cpu_to_dump64(s, cpu_read_xer(&cpu->env));

    cr = 0;
    for (i = 0; i < 8; i++) {
        cr |= (cpu->env.crf[i] & 15) << (4 * (7 - i));
    }
    reg->ccr = cpu_to_dump64(s, cr);
}

static void ppc64_write_elf64_fpregset(NoteFuncArg *arg, PowerPCCPU *cpu)
{
    int i;
    struct PPC64ElfFpregset  *fpregset;
    Note *note = &arg->note;
    DumpState *s = arg->state;

    note->hdr.n_type = cpu_to_dump32(s, NT_PRFPREG);

    fpregset = &note->contents.fpregset;
    memset(fpregset, 0, sizeof(*fpregset));

    for (i = 0; i < 32; i++) {
        fpregset->fpr[i] = cpu_to_dump64(s, cpu->env.fpr[i]);
    }
    fpregset->fpscr = cpu_to_dump64(s, cpu->env.fpscr);
}

static void ppc64_write_elf64_vmxregset(NoteFuncArg *arg, PowerPCCPU *cpu)
{
    int i;
    struct PPC64ElfVmxregset *vmxregset;
    Note *note = &arg->note;
    DumpState *s = arg->state;

    note->hdr.n_type = cpu_to_dump32(s, NT_PPC_VMX);
    vmxregset = &note->contents.vmxregset;
    memset(vmxregset, 0, sizeof(*vmxregset));

    for (i = 0; i < 32; i++) {
        bool needs_byteswap;

#ifdef HOST_WORDS_BIGENDIAN
        needs_byteswap = s->dump_info.d_endian == ELFDATA2LSB;
#else
        needs_byteswap = s->dump_info.d_endian == ELFDATA2MSB;
#endif

        if (needs_byteswap) {
            vmxregset->avr[i].u64[0] = bswap64(cpu->env.avr[i].u64[1]);
            vmxregset->avr[i].u64[1] = bswap64(cpu->env.avr[i].u64[0]);
        } else {
            vmxregset->avr[i].u64[0] = cpu->env.avr[i].u64[0];
            vmxregset->avr[i].u64[1] = cpu->env.avr[i].u64[1];
        }
    }
    vmxregset->vscr.u32[3] = cpu_to_dump32(s, cpu->env.vscr);
}
static void ppc64_write_elf64_vsxregset(NoteFuncArg *arg, PowerPCCPU *cpu)
{
    int i;
    struct PPC64ElfVsxregset *vsxregset;
    Note *note = &arg->note;
    DumpState *s = arg->state;

    note->hdr.n_type = cpu_to_dump32(s, NT_PPC_VSX);
    vsxregset = &note->contents.vsxregset;
    memset(vsxregset, 0, sizeof(*vsxregset));

    for (i = 0; i < 32; i++) {
        vsxregset->vsr[i] = cpu_to_dump64(s, cpu->env.vsr[i]);
    }
}
static void ppc64_write_elf64_speregset(NoteFuncArg *arg, PowerPCCPU *cpu)
{
    struct PPC64ElfSperegset *speregset;
    Note *note = &arg->note;
    DumpState *s = arg->state;

    note->hdr.n_type = cpu_to_dump32(s, NT_PPC_SPE);
    speregset = &note->contents.speregset;
    memset(speregset, 0, sizeof(*speregset));

    speregset->spe_acc = cpu_to_dump64(s, cpu->env.spe_acc);
    speregset->spe_fscr = cpu_to_dump32(s, cpu->env.spe_fscr);
}

static const struct NoteFuncDescStruct {
    int contents_size;
    void (*note_contents_func)(NoteFuncArg *arg, PowerPCCPU *cpu);
} note_func[] = {
    {sizeof(((Note *)0)->contents.prstatus),  ppc64_write_elf64_prstatus},
    {sizeof(((Note *)0)->contents.fpregset),  ppc64_write_elf64_fpregset},
    {sizeof(((Note *)0)->contents.vmxregset), ppc64_write_elf64_vmxregset},
    {sizeof(((Note *)0)->contents.vsxregset), ppc64_write_elf64_vsxregset},
    {sizeof(((Note *)0)->contents.speregset), ppc64_write_elf64_speregset},
    { 0, NULL}
};

typedef struct NoteFuncDescStruct NoteFuncDesc;

int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks)
{
    PowerPCCPU *cpu = POWERPC_CPU(first_cpu);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    info->d_machine = EM_PPC64;
    info->d_class = ELFCLASS64;
    if ((*pcc->interrupts_big_endian)(cpu)) {
        info->d_endian = ELFDATA2MSB;
    } else {
        info->d_endian = ELFDATA2LSB;
    }

    return 0;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    int name_size = 8; /* "CORE" or "QEMU" rounded */
    size_t elf_note_size = 0;
    int note_head_size;
    const NoteFuncDesc *nf;

    if (class != ELFCLASS64) {
        return -1;
    }
    assert(machine == EM_PPC64);

    note_head_size = sizeof(Elf64_Nhdr);

    for (nf = note_func; nf->note_contents_func; nf++) {
        elf_note_size = elf_note_size + note_head_size + name_size +
                        nf->contents_size;
    }

    return (elf_note_size) * nr_cpus;
}

static int ppc64_write_all_elf64_notes(const char *note_name,
                                       WriteCoreDumpFunction f,
                                       PowerPCCPU *cpu, int id,
                                       void *opaque)
{
    NoteFuncArg arg = { .state = opaque };
    int ret = -1;
    int note_size;
    const NoteFuncDesc *nf;

    for (nf = note_func; nf->note_contents_func; nf++) {
        arg.note.hdr.n_namesz = cpu_to_dump32(opaque, sizeof(arg.note.name));
        arg.note.hdr.n_descsz = cpu_to_dump32(opaque, nf->contents_size);
        strncpy(arg.note.name, note_name, sizeof(arg.note.name));

        (*nf->note_contents_func)(&arg, cpu);

        note_size =
            sizeof(arg.note) - sizeof(arg.note.contents) + nf->contents_size;
        ret = f(&arg.note, note_size, opaque);
        if (ret < 0) {
            return -1;
        }
    }
    return 0;
}

int ppc64_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    return ppc64_write_all_elf64_notes("CORE", f, cpu, cpuid, opaque);
}
