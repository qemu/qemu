/* General "disassemble this chunk" code.  Used for debugging. */
#include "qemu/osdep.h"
#include "disas/disas-internal.h"
#include "elf.h"
#include "qemu/qemu-print.h"
#include "disas/disas.h"
#include "disas/capstone.h"
#include "hw/core/cpu.h"
#include "exec/memory.h"

/* Filled in by elfload.c.  Simplistic, but will do for now. */
struct syminfo *syminfos = NULL;

/*
 * Get LENGTH bytes from info's buffer, at host address memaddr.
 * Transfer them to myaddr.
 */
static int host_read_memory(bfd_vma memaddr, bfd_byte *myaddr, int length,
                            struct disassemble_info *info)
{
    if (memaddr < info->buffer_vma
        || memaddr + length > info->buffer_vma + info->buffer_length) {
        /* Out of bounds.  Use EIO because GDB uses it.  */
        return EIO;
    }
    memcpy (myaddr, info->buffer + (memaddr - info->buffer_vma), length);
    return 0;
}

/*
 * Get LENGTH bytes from info's buffer, at target address memaddr.
 * Transfer them to myaddr.
 */
static int target_read_memory(bfd_vma memaddr, bfd_byte *myaddr, int length,
                              struct disassemble_info *info)
{
    CPUDebug *s = container_of(info, CPUDebug, info);
    int r = cpu_memory_rw_debug(s->cpu, memaddr, myaddr, length, 0);
    return r ? EIO : 0;
}

/*
 * Print an error message.  We can assume that this is in response to
 * an error return from {host,target}_read_memory.
 */
static void perror_memory(int status, bfd_vma memaddr,
                          struct disassemble_info *info)
{
    if (status != EIO) {
        /* Can't happen.  */
        info->fprintf_func(info->stream, "Unknown error %d\n", status);
    } else {
        /* Address between memaddr and memaddr + len was out of bounds.  */
        info->fprintf_func(info->stream,
                           "Address 0x%" PRIx64 " is out of bounds.\n",
                           memaddr);
    }
}

/* Print address in hex. */
static void print_address(bfd_vma addr, struct disassemble_info *info)
{
    info->fprintf_func(info->stream, "0x%" PRIx64, addr);
}

/* Print address in hex, truncated to the width of a host virtual address. */
static void host_print_address(bfd_vma addr, struct disassemble_info *info)
{
    print_address((uintptr_t)addr, info);
}

/* Stub prevents some fruitless earching in optabs disassemblers. */
static int symbol_at_address(bfd_vma addr, struct disassemble_info *info)
{
    return 1;
}

static int print_insn_objdump(bfd_vma pc, disassemble_info *info,
                              const char *prefix)
{
    int i, n = info->buffer_length;
    g_autofree uint8_t *buf = g_malloc(n);

    if (info->read_memory_func(pc, buf, n, info) == 0) {
        for (i = 0; i < n; ++i) {
            if (i % 32 == 0) {
                info->fprintf_func(info->stream, "\n%s: ", prefix);
            }
            info->fprintf_func(info->stream, "%02x", buf[i]);
        }
    } else {
        info->fprintf_func(info->stream, "unable to read memory");
    }
    return n;
}

static int print_insn_od_host(bfd_vma pc, disassemble_info *info)
{
    return print_insn_objdump(pc, info, "OBJD-H");
}

static int print_insn_od_target(bfd_vma pc, disassemble_info *info)
{
    return print_insn_objdump(pc, info, "OBJD-T");
}

static void initialize_debug(CPUDebug *s)
{
    memset(s, 0, sizeof(*s));
    s->info.arch = bfd_arch_unknown;
    s->info.cap_arch = -1;
    s->info.cap_insn_unit = 4;
    s->info.cap_insn_split = 4;
    s->info.memory_error_func = perror_memory;
    s->info.symbol_at_address_func = symbol_at_address;
}

void disas_initialize_debug_target(CPUDebug *s, CPUState *cpu)
{
    initialize_debug(s);

    s->cpu = cpu;
    s->info.read_memory_func = target_read_memory;
    s->info.print_address_func = print_address;
    if (target_words_bigendian()) {
        s->info.endian = BFD_ENDIAN_BIG;
    } else {
        s->info.endian =  BFD_ENDIAN_LITTLE;
    }

    CPUClass *cc = CPU_GET_CLASS(cpu);
    if (cc->disas_set_info) {
        cc->disas_set_info(cpu, &s->info);
    }
}

static void initialize_debug_host(CPUDebug *s)
{
    initialize_debug(s);

    s->info.read_memory_func = host_read_memory;
    s->info.print_address_func = host_print_address;
#if HOST_BIG_ENDIAN
    s->info.endian = BFD_ENDIAN_BIG;
#else
    s->info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(CONFIG_TCG_INTERPRETER)
    s->info.print_insn = print_insn_tci;
#elif defined(__i386__)
    s->info.mach = bfd_mach_i386_i386;
    s->info.cap_arch = CS_ARCH_X86;
    s->info.cap_mode = CS_MODE_32;
    s->info.cap_insn_unit = 1;
    s->info.cap_insn_split = 8;
#elif defined(__x86_64__)
    s->info.mach = bfd_mach_x86_64;
    s->info.cap_arch = CS_ARCH_X86;
    s->info.cap_mode = CS_MODE_64;
    s->info.cap_insn_unit = 1;
    s->info.cap_insn_split = 8;
#elif defined(_ARCH_PPC)
    s->info.cap_arch = CS_ARCH_PPC;
# ifdef _ARCH_PPC64
    s->info.cap_mode = CS_MODE_64;
# endif
#elif defined(__riscv)
#if defined(_ILP32) || (__riscv_xlen == 32)
    s->info.print_insn = print_insn_riscv32;
#elif defined(_LP64)
    s->info.print_insn = print_insn_riscv64;
#else
#error unsupported RISC-V ABI
#endif
#elif defined(__aarch64__)
    s->info.cap_arch = CS_ARCH_ARM64;
#elif defined(__alpha__)
    s->info.print_insn = print_insn_alpha;
#elif defined(__sparc__)
    s->info.print_insn = print_insn_sparc;
    s->info.mach = bfd_mach_sparc_v9b;
#elif defined(__arm__)
    /* TCG only generates code for arm mode.  */
    s->info.cap_arch = CS_ARCH_ARM;
#elif defined(__MIPSEB__)
    s->info.print_insn = print_insn_big_mips;
#elif defined(__MIPSEL__)
    s->info.print_insn = print_insn_little_mips;
#elif defined(__m68k__)
    s->info.print_insn = print_insn_m68k;
#elif defined(__s390__)
    s->info.cap_arch = CS_ARCH_SYSZ;
    s->info.cap_insn_unit = 2;
    s->info.cap_insn_split = 6;
#elif defined(__hppa__)
    s->info.print_insn = print_insn_hppa;
#elif defined(__loongarch__)
    s->info.print_insn = print_insn_loongarch;
#endif
}

/* Disassemble this for me please... (debugging).  */
void target_disas(FILE *out, CPUState *cpu, uint64_t code, size_t size)
{
    uint64_t pc;
    int count;
    CPUDebug s;

    disas_initialize_debug_target(&s, cpu);
    s.info.fprintf_func = fprintf;
    s.info.stream = out;
    s.info.buffer_vma = code;
    s.info.buffer_length = size;

    if (s.info.cap_arch >= 0 && cap_disas_target(&s.info, code, size)) {
        return;
    }

    if (s.info.print_insn == NULL) {
        s.info.print_insn = print_insn_od_target;
    }

    for (pc = code; size > 0; pc += count, size -= count) {
        fprintf(out, "0x%08" PRIx64 ":  ", pc);
        count = s.info.print_insn(pc, &s.info);
        fprintf(out, "\n");
        if (count < 0) {
            break;
        }
        if (size < count) {
            fprintf(out,
                    "Disassembler disagrees with translator over instruction "
                    "decoding\n"
                    "Please report this to qemu-devel@nongnu.org\n");
            break;
        }
    }
}

int disas_gstring_printf(FILE *stream, const char *fmt, ...)
{
    /* We abuse the FILE parameter to pass a GString. */
    GString *s = (GString *)stream;
    int initial_len = s->len;
    va_list va;

    va_start(va, fmt);
    g_string_append_vprintf(s, fmt, va);
    va_end(va);

    return s->len - initial_len;
}

static void plugin_print_address(bfd_vma addr, struct disassemble_info *info)
{
    /* does nothing */
}


/*
 * We should only be dissembling one instruction at a time here. If
 * there is left over it usually indicates the front end has read more
 * bytes than it needed.
 */
char *plugin_disas(CPUState *cpu, uint64_t addr, size_t size)
{
    CPUDebug s;
    GString *ds = g_string_new(NULL);

    disas_initialize_debug_target(&s, cpu);
    s.info.fprintf_func = disas_gstring_printf;
    s.info.stream = (FILE *)ds;  /* abuse this slot */
    s.info.buffer_vma = addr;
    s.info.buffer_length = size;
    s.info.print_address_func = plugin_print_address;

    if (s.info.cap_arch >= 0 && cap_disas_plugin(&s.info, addr, size)) {
        ; /* done */
    } else if (s.info.print_insn) {
        s.info.print_insn(addr, &s.info);
    } else {
        ; /* cannot disassemble -- return empty string */
    }

    /* Return the buffer, freeing the GString container.  */
    return g_string_free(ds, false);
}

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, const void *code, size_t size)
{
    uintptr_t pc;
    int count;
    CPUDebug s;

    initialize_debug_host(&s);
    s.info.fprintf_func = fprintf;
    s.info.stream = out;
    s.info.buffer = code;
    s.info.buffer_vma = (uintptr_t)code;
    s.info.buffer_length = size;
    s.info.show_opcodes = true;

    if (s.info.cap_arch >= 0 && cap_disas_host(&s.info, code, size)) {
        return;
    }

    if (s.info.print_insn == NULL) {
        s.info.print_insn = print_insn_od_host;
    }
    for (pc = (uintptr_t)code; size > 0; pc += count, size -= count) {
        fprintf(out, "0x%08" PRIxPTR ":  ", pc);
        count = s.info.print_insn(pc, &s.info);
        fprintf(out, "\n");
        if (count < 0) {
            break;
        }
    }

}

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(uint64_t orig_addr)
{
    const char *symbol = "";
    struct syminfo *s;

    for (s = syminfos; s; s = s->next) {
        symbol = s->lookup_symbol(s, orig_addr);
        if (symbol[0] != '\0') {
            break;
        }
    }

    return symbol;
}
