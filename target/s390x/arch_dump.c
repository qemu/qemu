/*
 * writing ELF notes for s390x arch
 *
 *
 * Copyright IBM Corp. 2012, 2013
 *
 *     Ekaterina Tumanova <tumanova@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "exec/cpu-all.h"
#include "sysemu/dump.h"
#include "sysemu/kvm.h"


struct S390xUserRegsStruct {
    uint64_t psw[2];
    uint64_t gprs[16];
    uint32_t acrs[16];
} QEMU_PACKED;

typedef struct S390xUserRegsStruct S390xUserRegs;

struct S390xElfPrstatusStruct {
    uint8_t pad1[32];
    uint32_t pid;
    uint8_t pad2[76];
    S390xUserRegs regs;
    uint8_t pad3[16];
} QEMU_PACKED;

typedef struct S390xElfPrstatusStruct S390xElfPrstatus;

struct S390xElfFpregsetStruct {
    uint32_t fpc;
    uint32_t pad;
    uint64_t fprs[16];
} QEMU_PACKED;

typedef struct S390xElfFpregsetStruct S390xElfFpregset;

struct S390xElfVregsLoStruct {
    uint64_t vregs[16];
} QEMU_PACKED;

typedef struct S390xElfVregsLoStruct S390xElfVregsLo;

struct S390xElfVregsHiStruct {
    uint64_t vregs[16][2];
} QEMU_PACKED;

typedef struct S390xElfVregsHiStruct S390xElfVregsHi;

struct S390xElfGSCBStruct {
    uint64_t gsregs[4];
} QEMU_PACKED;

typedef struct S390xElfGSCBStruct S390xElfGSCB;

typedef struct noteStruct {
    Elf64_Nhdr hdr;
    char name[8];
    union {
        S390xElfPrstatus prstatus;
        S390xElfFpregset fpregset;
        S390xElfVregsLo vregslo;
        S390xElfVregsHi vregshi;
        S390xElfGSCB gscb;
        uint32_t prefix;
        uint64_t timer;
        uint64_t todcmp;
        uint32_t todpreg;
        uint64_t ctrs[16];
    } contents;
} QEMU_PACKED Note;

static void s390x_write_elf64_prstatus(Note *note, S390CPU *cpu, int id)
{
    int i;
    S390xUserRegs *regs;

    note->hdr.n_type = cpu_to_be32(NT_PRSTATUS);

    regs = &(note->contents.prstatus.regs);
    regs->psw[0] = cpu_to_be64(cpu->env.psw.mask);
    regs->psw[1] = cpu_to_be64(cpu->env.psw.addr);
    for (i = 0; i <= 15; i++) {
        regs->acrs[i] = cpu_to_be32(cpu->env.aregs[i]);
        regs->gprs[i] = cpu_to_be64(cpu->env.regs[i]);
    }
    note->contents.prstatus.pid = id;
}

static void s390x_write_elf64_fpregset(Note *note, S390CPU *cpu, int id)
{
    int i;
    CPUS390XState *cs = &cpu->env;

    note->hdr.n_type = cpu_to_be32(NT_FPREGSET);
    note->contents.fpregset.fpc = cpu_to_be32(cpu->env.fpc);
    for (i = 0; i <= 15; i++) {
        note->contents.fpregset.fprs[i] = cpu_to_be64(get_freg(cs, i)->ll);
    }
}

static void s390x_write_elf64_vregslo(Note *note, S390CPU *cpu,  int id)
{
    int i;

    note->hdr.n_type = cpu_to_be32(NT_S390_VXRS_LOW);
    for (i = 0; i <= 15; i++) {
        note->contents.vregslo.vregs[i] = cpu_to_be64(cpu->env.vregs[i][1].ll);
    }
}

static void s390x_write_elf64_vregshi(Note *note, S390CPU *cpu, int id)
{
    int i;
    S390xElfVregsHi *temp_vregshi;

    temp_vregshi = &note->contents.vregshi;

    note->hdr.n_type = cpu_to_be32(NT_S390_VXRS_HIGH);
    for (i = 0; i <= 15; i++) {
        temp_vregshi->vregs[i][0] = cpu_to_be64(cpu->env.vregs[i + 16][0].ll);
        temp_vregshi->vregs[i][1] = cpu_to_be64(cpu->env.vregs[i + 16][1].ll);
    }
}

static void s390x_write_elf64_gscb(Note *note, S390CPU *cpu, int id)
{
    int i;

    note->hdr.n_type = cpu_to_be32(NT_S390_GS_CB);
    for (i = 0; i < 4; i++) {
        note->contents.gscb.gsregs[i] = cpu_to_be64(cpu->env.gscb[i]);
    }
}

static void s390x_write_elf64_timer(Note *note, S390CPU *cpu, int id)
{
    note->hdr.n_type = cpu_to_be32(NT_S390_TIMER);
    note->contents.timer = cpu_to_be64((uint64_t)(cpu->env.cputm));
}

static void s390x_write_elf64_todcmp(Note *note, S390CPU *cpu, int id)
{
    note->hdr.n_type = cpu_to_be32(NT_S390_TODCMP);
    note->contents.todcmp = cpu_to_be64((uint64_t)(cpu->env.ckc));
}

static void s390x_write_elf64_todpreg(Note *note, S390CPU *cpu, int id)
{
    note->hdr.n_type = cpu_to_be32(NT_S390_TODPREG);
    note->contents.todpreg = cpu_to_be32((uint32_t)(cpu->env.todpr));
}

static void s390x_write_elf64_ctrs(Note *note, S390CPU *cpu, int id)
{
    int i;

    note->hdr.n_type = cpu_to_be32(NT_S390_CTRS);

    for (i = 0; i <= 15; i++) {
        note->contents.ctrs[i] = cpu_to_be64(cpu->env.cregs[i]);
    }
}

static void s390x_write_elf64_prefix(Note *note, S390CPU *cpu, int id)
{
    note->hdr.n_type = cpu_to_be32(NT_S390_PREFIX);
    note->contents.prefix = cpu_to_be32((uint32_t)(cpu->env.psa));
}


typedef struct NoteFuncDescStruct {
    int contents_size;
    void (*note_contents_func)(Note *note, S390CPU *cpu, int id);
} NoteFuncDesc;

static const NoteFuncDesc note_core[] = {
    {sizeof(((Note *)0)->contents.prstatus), s390x_write_elf64_prstatus},
    {sizeof(((Note *)0)->contents.fpregset), s390x_write_elf64_fpregset},
    { 0, NULL}
};

static const NoteFuncDesc note_linux[] = {
    {sizeof(((Note *)0)->contents.prefix),   s390x_write_elf64_prefix},
    {sizeof(((Note *)0)->contents.ctrs),     s390x_write_elf64_ctrs},
    {sizeof(((Note *)0)->contents.timer),    s390x_write_elf64_timer},
    {sizeof(((Note *)0)->contents.todcmp),   s390x_write_elf64_todcmp},
    {sizeof(((Note *)0)->contents.todpreg),  s390x_write_elf64_todpreg},
    {sizeof(((Note *)0)->contents.vregslo),  s390x_write_elf64_vregslo},
    {sizeof(((Note *)0)->contents.vregshi),  s390x_write_elf64_vregshi},
    {sizeof(((Note *)0)->contents.gscb),     s390x_write_elf64_gscb},
    { 0, NULL}
};

static int s390x_write_elf64_notes(const char *note_name,
                                       WriteCoreDumpFunction f,
                                       S390CPU *cpu, int id,
                                       void *opaque,
                                       const NoteFuncDesc *funcs)
{
    Note note;
    const NoteFuncDesc *nf;
    int note_size;
    int ret = -1;

    for (nf = funcs; nf->note_contents_func; nf++) {
        memset(&note, 0, sizeof(note));
        note.hdr.n_namesz = cpu_to_be32(strlen(note_name) + 1);
        note.hdr.n_descsz = cpu_to_be32(nf->contents_size);
        strncpy(note.name, note_name, sizeof(note.name));
        (*nf->note_contents_func)(&note, cpu, id);

        note_size = sizeof(note) - sizeof(note.contents) + nf->contents_size;
        ret = f(&note, note_size, opaque);

        if (ret < 0) {
            return -1;
        }

    }

    return 0;
}


int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, void *opaque)
{
    S390CPU *cpu = S390_CPU(cs);
    int r;

    r = s390x_write_elf64_notes("CORE", f, cpu, cpuid, opaque, note_core);
    if (r) {
        return r;
    }
    return s390x_write_elf64_notes("LINUX", f, cpu, cpuid, opaque, note_linux);
}

int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks)
{
    info->d_machine = EM_S390;
    info->d_endian = ELFDATA2MSB;
    info->d_class = ELFCLASS64;

    return 0;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    int name_size = 8; /* "LINUX" or "CORE" + pad */
    size_t elf_note_size = 0;
    int note_head_size;
    const NoteFuncDesc *nf;

    assert(class == ELFCLASS64);
    assert(machine == EM_S390);

    note_head_size = sizeof(Elf64_Nhdr);

    for (nf = note_core; nf->note_contents_func; nf++) {
        elf_note_size = elf_note_size + note_head_size + name_size +
                        nf->contents_size;
    }
    for (nf = note_linux; nf->note_contents_func; nf++) {
        elf_note_size = elf_note_size + note_head_size + name_size +
                        nf->contents_size;
    }

    return (elf_note_size) * nr_cpus;
}
