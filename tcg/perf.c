/*
 * Linux perf perf-<pid>.map and jit-<pid>.dump integration.
 *
 * The jitdump spec can be found at [1].
 *
 * [1] https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/tools/perf/Documentation/jitdump-specification.txt
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "elf.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "qemu/timer.h"
#include "tcg/debuginfo.h"
#include "tcg/perf.h"
#include "tcg/tcg.h"

static FILE *safe_fopen_w(const char *path)
{
    int saved_errno;
    FILE *f;
    int fd;

    /* Delete the old file, if any. */
    unlink(path);

    /* Avoid symlink attacks by using O_CREAT | O_EXCL. */
    fd = open(path, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        return NULL;
    }

    /* Convert fd to FILE*. */
    f = fdopen(fd, "w");
    if (f == NULL) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return NULL;
    }

    return f;
}

static FILE *perfmap;

void perf_enable_perfmap(void)
{
    char map_file[32];

    snprintf(map_file, sizeof(map_file), "/tmp/perf-%d.map", getpid());
    perfmap = safe_fopen_w(map_file);
    if (perfmap == NULL) {
        warn_report("Could not open %s: %s, proceeding without perfmap",
                    map_file, strerror(errno));
    }
}

/* Get PC and size of code JITed for guest instruction #INSN. */
static void get_host_pc_size(uintptr_t *host_pc, uint16_t *host_size,
                             const void *start, size_t insn)
{
    uint16_t start_off = insn ? tcg_ctx->gen_insn_end_off[insn - 1] : 0;

    if (host_pc) {
        *host_pc = (uintptr_t)start + start_off;
    }
    if (host_size) {
        *host_size = tcg_ctx->gen_insn_end_off[insn] - start_off;
    }
}

static const char *pretty_symbol(const struct debuginfo_query *q, size_t *len)
{
    static __thread char buf[64];
    int tmp;

    if (!q->symbol) {
        tmp = snprintf(buf, sizeof(buf), "guest-0x%"PRIx64, q->address);
        if (len) {
            *len = MIN(tmp + 1, sizeof(buf));
        }
        return buf;
    }

    if (!q->offset) {
        if (len) {
            *len = strlen(q->symbol) + 1;
        }
        return q->symbol;
    }

    tmp = snprintf(buf, sizeof(buf), "%s+0x%"PRIx64, q->symbol, q->offset);
    if (len) {
        *len = MIN(tmp + 1, sizeof(buf));
    }
    return buf;
}

static void write_perfmap_entry(const void *start, size_t insn,
                                const struct debuginfo_query *q)
{
    uint16_t host_size;
    uintptr_t host_pc;

    get_host_pc_size(&host_pc, &host_size, start, insn);
    fprintf(perfmap, "%"PRIxPTR" %"PRIx16" %s\n",
            host_pc, host_size, pretty_symbol(q, NULL));
}

static FILE *jitdump;
static size_t perf_marker_size;
static void *perf_marker = MAP_FAILED;

#define JITHEADER_MAGIC 0x4A695444
#define JITHEADER_VERSION 1

struct jitheader {
    uint32_t magic;
    uint32_t version;
    uint32_t total_size;
    uint32_t elf_mach;
    uint32_t pad1;
    uint32_t pid;
    uint64_t timestamp;
    uint64_t flags;
};

enum jit_record_type {
    JIT_CODE_LOAD = 0,
    JIT_CODE_DEBUG_INFO = 2,
};

struct jr_prefix {
    uint32_t id;
    uint32_t total_size;
    uint64_t timestamp;
};

struct jr_code_load {
    struct jr_prefix p;

    uint32_t pid;
    uint32_t tid;
    uint64_t vma;
    uint64_t code_addr;
    uint64_t code_size;
    uint64_t code_index;
};

struct debug_entry {
    uint64_t addr;
    int lineno;
    int discrim;
    const char name[];
};

struct jr_code_debug_info {
    struct jr_prefix p;

    uint64_t code_addr;
    uint64_t nr_entry;
    struct debug_entry entries[];
};

static uint32_t get_e_machine(void)
{
    Elf64_Ehdr elf_header;
    FILE *exe;
    size_t n;

    QEMU_BUILD_BUG_ON(offsetof(Elf32_Ehdr, e_machine) !=
                      offsetof(Elf64_Ehdr, e_machine));

    exe = fopen("/proc/self/exe", "r");
    if (exe == NULL) {
        return EM_NONE;
    }

    n = fread(&elf_header, sizeof(elf_header), 1, exe);
    fclose(exe);
    if (n != 1) {
        return EM_NONE;
    }

    return elf_header.e_machine;
}

void perf_enable_jitdump(void)
{
    struct jitheader header;
    char jitdump_file[32];

    if (!use_rt_clock) {
        warn_report("CLOCK_MONOTONIC is not available, proceeding without jitdump");
        return;
    }

    snprintf(jitdump_file, sizeof(jitdump_file), "jit-%d.dump", getpid());
    jitdump = safe_fopen_w(jitdump_file);
    if (jitdump == NULL) {
        warn_report("Could not open %s: %s, proceeding without jitdump",
                    jitdump_file, strerror(errno));
        return;
    }

    /*
     * `perf inject` will see that the mapped file name in the corresponding
     * PERF_RECORD_MMAP or PERF_RECORD_MMAP2 event is of the form jit-%d.dump
     * and will process it as a jitdump file.
     */
    perf_marker_size = qemu_real_host_page_size();
    perf_marker = mmap(NULL, perf_marker_size, PROT_READ | PROT_EXEC,
                       MAP_PRIVATE, fileno(jitdump), 0);
    if (perf_marker == MAP_FAILED) {
        warn_report("Could not map %s: %s, proceeding without jitdump",
                    jitdump_file, strerror(errno));
        fclose(jitdump);
        jitdump = NULL;
        return;
    }

    header.magic = JITHEADER_MAGIC;
    header.version = JITHEADER_VERSION;
    header.total_size = sizeof(header);
    header.elf_mach = get_e_machine();
    header.pad1 = 0;
    header.pid = getpid();
    header.timestamp = get_clock();
    header.flags = 0;
    fwrite(&header, sizeof(header), 1, jitdump);
}

void perf_report_prologue(const void *start, size_t size)
{
    if (perfmap) {
        fprintf(perfmap, "%"PRIxPTR" %zx tcg-prologue-buffer\n",
                (uintptr_t)start, size);
    }
}

/* Write a JIT_CODE_DEBUG_INFO jitdump entry. */
static void write_jr_code_debug_info(const void *start,
                                     const struct debuginfo_query *q,
                                     size_t icount)
{
    struct jr_code_debug_info rec;
    struct debug_entry ent;
    uintptr_t host_pc;
    int insn;

    /* Write the header. */
    rec.p.id = JIT_CODE_DEBUG_INFO;
    rec.p.total_size = sizeof(rec) + sizeof(ent) + 1;
    rec.p.timestamp = get_clock();
    rec.code_addr = (uintptr_t)start;
    rec.nr_entry = 1;
    for (insn = 0; insn < icount; insn++) {
        if (q[insn].file) {
            rec.p.total_size += sizeof(ent) + strlen(q[insn].file) + 1;
            rec.nr_entry++;
        }
    }
    fwrite(&rec, sizeof(rec), 1, jitdump);

    /* Write the main debug entries. */
    for (insn = 0; insn < icount; insn++) {
        if (q[insn].file) {
            get_host_pc_size(&host_pc, NULL, start, insn);
            ent.addr = host_pc;
            ent.lineno = q[insn].line;
            ent.discrim = 0;
            fwrite(&ent, sizeof(ent), 1, jitdump);
            fwrite(q[insn].file, strlen(q[insn].file) + 1, 1, jitdump);
        }
    }

    /* Write the trailing debug_entry. */
    ent.addr = (uintptr_t)start + tcg_ctx->gen_insn_end_off[icount - 1];
    ent.lineno = 0;
    ent.discrim = 0;
    fwrite(&ent, sizeof(ent), 1, jitdump);
    fwrite("", 1, 1, jitdump);
}

/* Write a JIT_CODE_LOAD jitdump entry. */
static void write_jr_code_load(const void *start, uint16_t host_size,
                               const struct debuginfo_query *q)
{
    static uint64_t code_index;
    struct jr_code_load rec;
    const char *symbol;
    size_t symbol_size;

    symbol = pretty_symbol(q, &symbol_size);
    rec.p.id = JIT_CODE_LOAD;
    rec.p.total_size = sizeof(rec) + symbol_size + host_size;
    rec.p.timestamp = get_clock();
    rec.pid = getpid();
    rec.tid = qemu_get_thread_id();
    rec.vma = (uintptr_t)start;
    rec.code_addr = (uintptr_t)start;
    rec.code_size = host_size;
    rec.code_index = code_index++;
    fwrite(&rec, sizeof(rec), 1, jitdump);
    fwrite(symbol, symbol_size, 1, jitdump);
    fwrite(start, host_size, 1, jitdump);
}

void perf_report_code(uint64_t guest_pc, TranslationBlock *tb,
                      const void *start)
{
    struct debuginfo_query *q;
    size_t insn;
    uint64_t *gen_insn_data;

    if (!perfmap && !jitdump) {
        return;
    }

    q = g_try_malloc0_n(tb->icount, sizeof(*q));
    if (!q) {
        return;
    }

    debuginfo_lock();

    /* Query debuginfo for each guest instruction. */
    gen_insn_data = tcg_ctx->gen_insn_data;

    for (insn = 0; insn < tb->icount; insn++) {
        /* FIXME: This replicates the restore_state_to_opc() logic. */
        q[insn].address = gen_insn_data[insn * INSN_START_WORDS + 0];
        if (tb_cflags(tb) & CF_PCREL) {
            q[insn].address |= guest_pc & TARGET_PAGE_MASK;
        }
        q[insn].flags = DEBUGINFO_SYMBOL | (jitdump ? DEBUGINFO_LINE : 0);
    }
    debuginfo_query(q, tb->icount);

    /* Emit perfmap entries if needed. */
    if (perfmap) {
        flockfile(perfmap);
        for (insn = 0; insn < tb->icount; insn++) {
            write_perfmap_entry(start, insn, &q[insn]);
        }
        funlockfile(perfmap);
    }

    /* Emit jitdump entries if needed. */
    if (jitdump) {
        flockfile(jitdump);
        write_jr_code_debug_info(start, q, tb->icount);
        write_jr_code_load(start, tcg_ctx->gen_insn_end_off[tb->icount - 1],
                           q);
        funlockfile(jitdump);
    }

    debuginfo_unlock();
    g_free(q);
}

void perf_exit(void)
{
    if (perfmap) {
        fclose(perfmap);
        perfmap = NULL;
    }

    if (perf_marker != MAP_FAILED) {
        munmap(perf_marker, perf_marker_size);
        perf_marker = MAP_FAILED;
    }

    if (jitdump) {
        fclose(jitdump);
        jitdump = NULL;
    }
}
