/* Support for writing ELF notes for RISC-V architectures
 *
 * Copyright (C) 2021 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "sysemu/dump.h"

/* struct user_regs_struct from arch/riscv/include/uapi/asm/ptrace.h */
struct riscv64_user_regs {
    uint64_t pc;
    uint64_t regs[31];
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct riscv64_user_regs) != 256);

/* struct elf_prstatus from include/linux/elfcore.h */
struct riscv64_elf_prstatus {
    char pad1[32]; /* 32 == offsetof(struct elf_prstatus, pr_pid) */
    uint32_t pr_pid;
    char pad2[76]; /* 76 == offsetof(struct elf_prstatus, pr_reg) -
                            offsetof(struct elf_prstatus, pr_ppid) */
    struct riscv64_user_regs pr_reg;
    char pad3[8];
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct riscv64_elf_prstatus) != 376);

struct riscv64_note {
    Elf64_Nhdr hdr;
    char name[8]; /* align_up(sizeof("CORE"), 4) */
    struct riscv64_elf_prstatus prstatus;
} QEMU_PACKED;

#define RISCV64_NOTE_HEADER_SIZE offsetof(struct riscv64_note, prstatus)
#define RISCV64_PRSTATUS_NOTE_SIZE \
            (RISCV64_NOTE_HEADER_SIZE + sizeof(struct riscv64_elf_prstatus))

static void riscv64_note_init(struct riscv64_note *note, DumpState *s,
                              const char *name, Elf64_Word namesz,
                              Elf64_Word type, Elf64_Word descsz)
{
    memset(note, 0, sizeof(*note));

    note->hdr.n_namesz = cpu_to_dump32(s, namesz);
    note->hdr.n_descsz = cpu_to_dump32(s, descsz);
    note->hdr.n_type = cpu_to_dump32(s, type);

    memcpy(note->name, name, namesz);
}

int riscv_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque)
{
    struct riscv64_note note;
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    DumpState *s = opaque;
    int ret, i = 0;
    const char name[] = "CORE";

    riscv64_note_init(&note, s, name, sizeof(name),
                      NT_PRSTATUS, sizeof(note.prstatus));

    note.prstatus.pr_pid = cpu_to_dump32(s, cpuid);

    note.prstatus.pr_reg.pc = cpu_to_dump64(s, env->pc);

    for (i = 0; i < 31; i++) {
        note.prstatus.pr_reg.regs[i] = cpu_to_dump64(s, env->gpr[i + 1]);
    }

    ret = f(&note, RISCV64_PRSTATUS_NOTE_SIZE, s);
    if (ret < 0) {
        return -1;
    }

    return ret;
}

struct riscv32_user_regs {
    uint32_t pc;
    uint32_t regs[31];
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct riscv32_user_regs) != 128);

struct riscv32_elf_prstatus {
    char pad1[24]; /* 24 == offsetof(struct elf_prstatus, pr_pid) */
    uint32_t pr_pid;
    char pad2[44]; /* 44 == offsetof(struct elf_prstatus, pr_reg) -
                            offsetof(struct elf_prstatus, pr_ppid) */
    struct riscv32_user_regs pr_reg;
    char pad3[4];
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct riscv32_elf_prstatus) != 204);

struct riscv32_note {
    Elf32_Nhdr hdr;
    char name[8]; /* align_up(sizeof("CORE"), 4) */
    struct riscv32_elf_prstatus prstatus;
} QEMU_PACKED;

#define RISCV32_NOTE_HEADER_SIZE offsetof(struct riscv32_note, prstatus)
#define RISCV32_PRSTATUS_NOTE_SIZE \
            (RISCV32_NOTE_HEADER_SIZE + sizeof(struct riscv32_elf_prstatus))

static void riscv32_note_init(struct riscv32_note *note, DumpState *s,
                              const char *name, Elf32_Word namesz,
                              Elf32_Word type, Elf32_Word descsz)
{
    memset(note, 0, sizeof(*note));

    note->hdr.n_namesz = cpu_to_dump32(s, namesz);
    note->hdr.n_descsz = cpu_to_dump32(s, descsz);
    note->hdr.n_type = cpu_to_dump32(s, type);

    memcpy(note->name, name, namesz);
}

int riscv_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                               int cpuid, void *opaque)
{
    struct riscv32_note note;
    RISCVCPU *cpu = RISCV_CPU(cs);
    CPURISCVState *env = &cpu->env;
    DumpState *s = opaque;
    int ret, i;
    const char name[] = "CORE";

    riscv32_note_init(&note, s, name, sizeof(name),
                      NT_PRSTATUS, sizeof(note.prstatus));

    note.prstatus.pr_pid = cpu_to_dump32(s, cpuid);

    note.prstatus.pr_reg.pc = cpu_to_dump32(s, env->pc);

    for (i = 0; i < 31; i++) {
        note.prstatus.pr_reg.regs[i] = cpu_to_dump32(s, env->gpr[i + 1]);
    }

    ret = f(&note, RISCV32_PRSTATUS_NOTE_SIZE, s);
    if (ret < 0) {
        return -1;
    }

    return ret;
}

int cpu_get_dump_info(ArchDumpInfo *info,
                      const GuestPhysBlockList *guest_phys_blocks)
{
    RISCVCPU *cpu;
    CPURISCVState *env;

    if (first_cpu == NULL) {
        return -1;
    }
    cpu = RISCV_CPU(first_cpu);
    env = &cpu->env;

    info->d_machine = EM_RISCV;

#if defined(TARGET_RISCV64)
    info->d_class = ELFCLASS64;
#else
    info->d_class = ELFCLASS32;
#endif

    info->d_endian = (env->mstatus & MSTATUS_UBE) != 0
                     ? ELFDATA2MSB : ELFDATA2LSB;

    return 0;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    size_t note_size;

    if (class == ELFCLASS64) {
        note_size = RISCV64_PRSTATUS_NOTE_SIZE;
    } else {
        note_size = RISCV32_PRSTATUS_NOTE_SIZE;
    }

    return note_size * nr_cpus;
}
