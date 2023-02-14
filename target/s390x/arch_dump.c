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
#include "qemu/units.h"
#include "cpu.h"
#include "s390x-internal.h"
#include "elf.h"
#include "sysemu/dump.h"
#include "hw/s390x/pv.h"
#include "kvm/kvm_s390x.h"

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
        uint8_t dynamic[1];  /*
                              * Would be a flexible array member, if
                              * that was legal inside a union. Real
                              * size comes from PV info interface.
                              */
    } contents;
} QEMU_PACKED Note;

static bool pv_dump_initialized;

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
        note->contents.fpregset.fprs[i] = cpu_to_be64(*get_freg(cs, i));
    }
}

static void s390x_write_elf64_vregslo(Note *note, S390CPU *cpu,  int id)
{
    int i;

    note->hdr.n_type = cpu_to_be32(NT_S390_VXRS_LOW);
    for (i = 0; i <= 15; i++) {
        note->contents.vregslo.vregs[i] = cpu_to_be64(cpu->env.vregs[i][1]);
    }
}

static void s390x_write_elf64_vregshi(Note *note, S390CPU *cpu, int id)
{
    int i;
    S390xElfVregsHi *temp_vregshi;

    temp_vregshi = &note->contents.vregshi;

    note->hdr.n_type = cpu_to_be32(NT_S390_VXRS_HIGH);
    for (i = 0; i <= 15; i++) {
        temp_vregshi->vregs[i][0] = cpu_to_be64(cpu->env.vregs[i + 16][0]);
        temp_vregshi->vregs[i][1] = cpu_to_be64(cpu->env.vregs[i + 16][1]);
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

static void s390x_write_elf64_pv(Note *note, S390CPU *cpu, int id)
{
    note->hdr.n_type = cpu_to_be32(NT_S390_PV_CPU_DATA);
    if (!pv_dump_initialized) {
        return;
    }
    kvm_s390_dump_cpu(cpu, &note->contents.dynamic);
}

typedef struct NoteFuncDescStruct {
    int contents_size;
    uint64_t (*note_size_func)(void); /* NULL for non-dynamic sized contents */
    void (*note_contents_func)(Note *note, S390CPU *cpu, int id);
    bool pvonly;
} NoteFuncDesc;

static const NoteFuncDesc note_core[] = {
    {sizeof_field(Note, contents.prstatus), NULL, s390x_write_elf64_prstatus, false},
    {sizeof_field(Note, contents.fpregset), NULL, s390x_write_elf64_fpregset, false},
    { 0, NULL, NULL, false}
};

static const NoteFuncDesc note_linux[] = {
    {sizeof_field(Note, contents.prefix),   NULL, s390x_write_elf64_prefix,  false},
    {sizeof_field(Note, contents.ctrs),     NULL, s390x_write_elf64_ctrs,    false},
    {sizeof_field(Note, contents.timer),    NULL, s390x_write_elf64_timer,   false},
    {sizeof_field(Note, contents.todcmp),   NULL, s390x_write_elf64_todcmp,  false},
    {sizeof_field(Note, contents.todpreg),  NULL, s390x_write_elf64_todpreg, false},
    {sizeof_field(Note, contents.vregslo),  NULL, s390x_write_elf64_vregslo, false},
    {sizeof_field(Note, contents.vregshi),  NULL, s390x_write_elf64_vregshi, false},
    {sizeof_field(Note, contents.gscb),     NULL, s390x_write_elf64_gscb,    false},
    {0, kvm_s390_pv_dmp_get_size_cpu,       s390x_write_elf64_pv, true},
    { 0, NULL, NULL, false}
};

static int s390x_write_elf64_notes(const char *note_name,
                                       WriteCoreDumpFunction f,
                                       S390CPU *cpu, int id,
                                       DumpState *s,
                                       const NoteFuncDesc *funcs)
{
    Note note, *notep;
    const NoteFuncDesc *nf;
    int note_size, content_size;
    int ret = -1;

    assert(strlen(note_name) < sizeof(note.name));

    for (nf = funcs; nf->note_contents_func; nf++) {
        notep = &note;
        if (nf->pvonly && !s390_is_pv()) {
            continue;
        }

        content_size = nf->note_size_func ? nf->note_size_func() : nf->contents_size;
        note_size = sizeof(note) - sizeof(notep->contents) + content_size;

        /* Notes with dynamic sizes need to allocate a note */
        if (nf->note_size_func) {
            notep = g_malloc(note_size);
        }

        memset(notep, 0, note_size);

        /* Setup note header data */
        notep->hdr.n_descsz = cpu_to_be32(content_size);
        notep->hdr.n_namesz = cpu_to_be32(strlen(note_name) + 1);
        g_strlcpy(notep->name, note_name, sizeof(notep->name));

        /* Get contents and write them out */
        (*nf->note_contents_func)(notep, cpu, id);
        ret = f(notep, note_size, s);

        if (nf->note_size_func) {
            g_free(notep);
        }

        if (ret < 0) {
            return -1;
        }

    }

    return 0;
}


int s390_cpu_write_elf64_note(WriteCoreDumpFunction f, CPUState *cs,
                              int cpuid, DumpState *s)
{
    S390CPU *cpu = S390_CPU(cs);
    int r;

    r = s390x_write_elf64_notes("CORE", f, cpu, cpuid, s, note_core);
    if (r) {
        return r;
    }
    return s390x_write_elf64_notes("LINUX", f, cpu, cpuid, s, note_linux);
}

/* PV dump section size functions */
static uint64_t get_mem_state_size_from_len(uint64_t len)
{
    return (len / (MiB)) * kvm_s390_pv_dmp_get_size_mem_state();
}

static uint64_t get_size_mem_state(DumpState *s)
{
    return get_mem_state_size_from_len(s->total_size);
}

static uint64_t get_size_completion_data(DumpState *s)
{
    return kvm_s390_pv_dmp_get_size_completion_data();
}

/* PV dump section data functions*/
static int get_data_completion(DumpState *s, uint8_t *buff)
{
    int rc;

    if (!pv_dump_initialized) {
        return 0;
    }
    rc = kvm_s390_dump_completion_data(buff);
    if (!rc) {
            pv_dump_initialized = false;
    }
    return rc;
}

static int get_mem_state(DumpState *s, uint8_t *buff)
{
    int64_t memblock_size, memblock_start;
    GuestPhysBlock *block;
    uint64_t off;
    int rc;

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        memblock_start = dump_filtered_memblock_start(block, s->filter_area_begin,
                                                      s->filter_area_length);
        if (memblock_start == -1) {
            continue;
        }

        memblock_size = dump_filtered_memblock_size(block, s->filter_area_begin,
                                                    s->filter_area_length);

        off = get_mem_state_size_from_len(block->target_start);

        rc = kvm_s390_dump_mem_state(block->target_start,
                                     get_mem_state_size_from_len(memblock_size),
                                     buff + off);
        if (rc) {
            return rc;
        }
    }

    return 0;
}

static struct sections {
    uint64_t (*sections_size_func)(DumpState *s);
    int (*sections_contents_func)(DumpState *s, uint8_t *buff);
    char sctn_str[12];
} sections[] = {
    { get_size_mem_state, get_mem_state, "pv_mem_meta"},
    { get_size_completion_data, get_data_completion, "pv_compl"},
    {NULL , NULL, ""}
};

static uint64_t arch_sections_write_hdr(DumpState *s, uint8_t *buff)
{
    Elf64_Shdr *shdr = (void *)buff;
    struct sections *sctn = sections;
    uint64_t off = s->section_offset;

    if (!pv_dump_initialized) {
        return 0;
    }

    for (; sctn->sections_size_func; off += shdr->sh_size, sctn++, shdr++) {
        memset(shdr, 0, sizeof(*shdr));
        shdr->sh_type = SHT_PROGBITS;
        shdr->sh_offset = off;
        shdr->sh_size = sctn->sections_size_func(s);
        shdr->sh_name = s->string_table_buf->len;
        g_array_append_vals(s->string_table_buf, sctn->sctn_str, sizeof(sctn->sctn_str));
    }

    return (uintptr_t)shdr - (uintptr_t)buff;
}


/* Add arch specific number of sections and their respective sizes */
static void arch_sections_add(DumpState *s)
{
    struct sections *sctn = sections;

    /*
     * We only do a PV dump if we are running a PV guest, KVM supports
     * the dump API and we got valid dump length information.
     */
    if (!s390_is_pv() || !kvm_s390_get_protected_dump() ||
        !kvm_s390_pv_info_basic_valid()) {
        return;
    }

    /*
     * Start the UV dump process by doing the initialize dump call via
     * KVM as the proxy.
     */
    if (!kvm_s390_dump_init()) {
        pv_dump_initialized = true;
    } else {
        /*
         * Dump init failed, maybe the guest owner disabled dumping.
         * We'll continue the non-PV dump process since this is no
         * reason to crash qemu.
         */
        return;
    }

    for (; sctn->sections_size_func; sctn++) {
        s->shdr_num += 1;
        s->elf_section_data_size += sctn->sections_size_func(s);
    }
}

/*
 * After the PV dump has been initialized, the CPU data has been
 * fetched and memory has been dumped, we need to grab the tweak data
 * and the completion data.
 */
static int arch_sections_write(DumpState *s, uint8_t *buff)
{
    struct sections *sctn = sections;
    int rc;

    if (!pv_dump_initialized) {
        return -EINVAL;
    }

    for (; sctn->sections_size_func; sctn++) {
        rc = sctn->sections_contents_func(s, buff);
        buff += sctn->sections_size_func(s);
        if (rc) {
            return rc;
        }
    }
    return 0;
}

int cpu_get_dump_info(ArchDumpInfo *info,
                      const struct GuestPhysBlockList *guest_phys_blocks)
{
    info->d_machine = EM_S390;
    info->d_endian = ELFDATA2MSB;
    info->d_class = ELFCLASS64;
    /*
     * This is evaluated for each dump so we can freely switch
     * between PV and non-PV.
     */
    if (s390_is_pv() && kvm_s390_get_protected_dump() &&
        kvm_s390_pv_info_basic_valid()) {
        info->arch_sections_add_fn = *arch_sections_add;
        info->arch_sections_write_hdr_fn = *arch_sections_write_hdr;
        info->arch_sections_write_fn = *arch_sections_write;
    } else {
        info->arch_sections_add_fn = NULL;
        info->arch_sections_write_hdr_fn = NULL;
        info->arch_sections_write_fn = NULL;
    }
    return 0;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    int name_size = 8; /* "LINUX" or "CORE" + pad */
    size_t elf_note_size = 0;
    int note_head_size, content_size;
    const NoteFuncDesc *nf;

    assert(class == ELFCLASS64);
    assert(machine == EM_S390);

    note_head_size = sizeof(Elf64_Nhdr);

    for (nf = note_core; nf->note_contents_func; nf++) {
        elf_note_size = elf_note_size + note_head_size + name_size + nf->contents_size;
    }
    for (nf = note_linux; nf->note_contents_func; nf++) {
        if (nf->pvonly && !s390_is_pv()) {
            continue;
        }
        content_size = nf->contents_size ? nf->contents_size : nf->note_size_func();
        elf_note_size = elf_note_size + note_head_size + name_size +
                        content_size;
    }

    return (elf_note_size) * nr_cpus;
}
