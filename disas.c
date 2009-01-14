/* General "disassemble this chunk" code.  Used for debugging. */
#include "config.h"
#include "dis-asm.h"
#include "elf.h"
#include <errno.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

/* Filled in by elfload.c.  Simplistic, but will do for now. */
struct syminfo *syminfos = NULL;

/* Get LENGTH bytes from info's buffer, at target address memaddr.
   Transfer them to myaddr.  */
int
buffer_read_memory(bfd_vma memaddr, bfd_byte *myaddr, int length,
                   struct disassemble_info *info)
{
    if (memaddr < info->buffer_vma
        || memaddr + length > info->buffer_vma + info->buffer_length)
        /* Out of bounds.  Use EIO because GDB uses it.  */
        return EIO;
    memcpy (myaddr, info->buffer + (memaddr - info->buffer_vma), length);
    return 0;
}

/* Get LENGTH bytes from info's buffer, at target address memaddr.
   Transfer them to myaddr.  */
static int
target_read_memory (bfd_vma memaddr,
                    bfd_byte *myaddr,
                    int length,
                    struct disassemble_info *info)
{
    int i;
    for(i = 0; i < length; i++) {
        myaddr[i] = ldub_code(memaddr + i);
    }
    return 0;
}

/* Print an error message.  We can assume that this is in response to
   an error return from buffer_read_memory.  */
void
perror_memory (int status, bfd_vma memaddr, struct disassemble_info *info)
{
  if (status != EIO)
    /* Can't happen.  */
    (*info->fprintf_func) (info->stream, "Unknown error %d\n", status);
  else
    /* Actually, address between memaddr and memaddr + len was
       out of bounds.  */
    (*info->fprintf_func) (info->stream,
			   "Address 0x%" PRIx64 " is out of bounds.\n", memaddr);
}

/* This could be in a separate file, to save miniscule amounts of space
   in statically linked executables.  */

/* Just print the address is hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */

void
generic_print_address (bfd_vma addr, struct disassemble_info *info)
{
    (*info->fprintf_func) (info->stream, "0x%" PRIx64, addr);
}

/* Just return the given address.  */

int
generic_symbol_at_address (bfd_vma addr, struct disassemble_info *info)
{
  return 1;
}

bfd_vma bfd_getl32 (const bfd_byte *addr)
{
  unsigned long v;

  v = (unsigned long) addr[0];
  v |= (unsigned long) addr[1] << 8;
  v |= (unsigned long) addr[2] << 16;
  v |= (unsigned long) addr[3] << 24;
  return (bfd_vma) v;
}

bfd_vma bfd_getb32 (const bfd_byte *addr)
{
  unsigned long v;

  v = (unsigned long) addr[0] << 24;
  v |= (unsigned long) addr[1] << 16;
  v |= (unsigned long) addr[2] << 8;
  v |= (unsigned long) addr[3];
  return (bfd_vma) v;
}

bfd_vma bfd_getl16 (const bfd_byte *addr)
{
  unsigned long v;

  v = (unsigned long) addr[0];
  v |= (unsigned long) addr[1] << 8;
  return (bfd_vma) v;
}

bfd_vma bfd_getb16 (const bfd_byte *addr)
{
  unsigned long v;

  v = (unsigned long) addr[0] << 24;
  v |= (unsigned long) addr[1] << 16;
  return (bfd_vma) v;
}

#ifdef TARGET_ARM
static int
print_insn_thumb1(bfd_vma pc, disassemble_info *info)
{
  return print_insn_arm(pc | 1, info);
}
#endif

/* Disassemble this for me please... (debugging). 'flags' has the following
   values:
    i386 - nonzero means 16 bit code
    arm  - nonzero means thumb code
    ppc  - nonzero means little endian
    other targets - unused
 */
void target_disas(FILE *out, target_ulong code, target_ulong size, int flags)
{
    target_ulong pc;
    int count;
    struct disassemble_info disasm_info;
    int (*print_insn)(bfd_vma pc, disassemble_info *info);

    INIT_DISASSEMBLE_INFO(disasm_info, out, fprintf);

    disasm_info.read_memory_func = target_read_memory;
    disasm_info.buffer_vma = code;
    disasm_info.buffer_length = size;

#ifdef TARGET_WORDS_BIGENDIAN
    disasm_info.endian = BFD_ENDIAN_BIG;
#else
    disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(TARGET_I386)
    if (flags == 2)
        disasm_info.mach = bfd_mach_x86_64;
    else if (flags == 1)
        disasm_info.mach = bfd_mach_i386_i8086;
    else
        disasm_info.mach = bfd_mach_i386_i386;
    print_insn = print_insn_i386;
#elif defined(TARGET_ARM)
    if (flags)
	print_insn = print_insn_thumb1;
    else
	print_insn = print_insn_arm;
#elif defined(TARGET_SPARC)
    print_insn = print_insn_sparc;
#ifdef TARGET_SPARC64
    disasm_info.mach = bfd_mach_sparc_v9b;
#endif
#elif defined(TARGET_PPC)
    if (flags >> 16)
        disasm_info.endian = BFD_ENDIAN_LITTLE;
    if (flags & 0xFFFF) {
        /* If we have a precise definitions of the instructions set, use it */
        disasm_info.mach = flags & 0xFFFF;
    } else {
#ifdef TARGET_PPC64
        disasm_info.mach = bfd_mach_ppc64;
#else
        disasm_info.mach = bfd_mach_ppc;
#endif
    }
    print_insn = print_insn_ppc;
#elif defined(TARGET_M68K)
    print_insn = print_insn_m68k;
#elif defined(TARGET_MIPS)
#ifdef TARGET_WORDS_BIGENDIAN
    print_insn = print_insn_big_mips;
#else
    print_insn = print_insn_little_mips;
#endif
#elif defined(TARGET_SH4)
    disasm_info.mach = bfd_mach_sh4;
    print_insn = print_insn_sh;
#elif defined(TARGET_ALPHA)
    disasm_info.mach = bfd_mach_alpha;
    print_insn = print_insn_alpha;
#elif defined(TARGET_CRIS)
    disasm_info.mach = bfd_mach_cris_v32;
    print_insn = print_insn_crisv32;
#else
    fprintf(out, "0x" TARGET_FMT_lx
	    ": Asm output not supported on this arch\n", code);
    return;
#endif

    for (pc = code; pc < code + size; pc += count) {
	fprintf(out, "0x" TARGET_FMT_lx ":  ", pc);
	count = print_insn(pc, &disasm_info);
#if 0
        {
            int i;
            uint8_t b;
            fprintf(out, " {");
            for(i = 0; i < count; i++) {
                target_read_memory(pc + i, &b, 1, &disasm_info);
                fprintf(out, " %02x", b);
            }
            fprintf(out, " }");
        }
#endif
	fprintf(out, "\n");
	if (count < 0)
	    break;
    }
}

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size)
{
    unsigned long pc;
    int count;
    struct disassemble_info disasm_info;
    int (*print_insn)(bfd_vma pc, disassemble_info *info);

    INIT_DISASSEMBLE_INFO(disasm_info, out, fprintf);

    disasm_info.buffer = code;
    disasm_info.buffer_vma = (unsigned long)code;
    disasm_info.buffer_length = size;

#ifdef WORDS_BIGENDIAN
    disasm_info.endian = BFD_ENDIAN_BIG;
#else
    disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(__i386__)
    disasm_info.mach = bfd_mach_i386_i386;
    print_insn = print_insn_i386;
#elif defined(__x86_64__)
    disasm_info.mach = bfd_mach_x86_64;
    print_insn = print_insn_i386;
#elif defined(_ARCH_PPC)
    print_insn = print_insn_ppc;
#elif defined(__alpha__)
    print_insn = print_insn_alpha;
#elif defined(__sparc__)
    print_insn = print_insn_sparc;
#if defined(__sparc_v8plus__) || defined(__sparc_v8plusa__) || defined(__sparc_v9__)
    disasm_info.mach = bfd_mach_sparc_v9b;
#endif
#elif defined(__arm__)
    print_insn = print_insn_arm;
#elif defined(__MIPSEB__)
    print_insn = print_insn_big_mips;
#elif defined(__MIPSEL__)
    print_insn = print_insn_little_mips;
#elif defined(__m68k__)
    print_insn = print_insn_m68k;
#elif defined(__s390__)
    print_insn = print_insn_s390;
#elif defined(__hppa__)
    print_insn = print_insn_hppa;
#else
    fprintf(out, "0x%lx: Asm output not supported on this arch\n",
	    (long) code);
    return;
#endif
    for (pc = (unsigned long)code; pc < (unsigned long)code + size; pc += count) {
	fprintf(out, "0x%08lx:  ", pc);
#ifdef __arm__
        /* since data is included in the code, it is better to
           display code data too */
        fprintf(out, "%08x  ", (int)bfd_getl32((const bfd_byte *)pc));
#endif
	count = print_insn(pc, &disasm_info);
	fprintf(out, "\n");
	if (count < 0)
	    break;
    }
}

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(target_ulong orig_addr)
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

#if !defined(CONFIG_USER_ONLY)

void term_vprintf(const char *fmt, va_list ap);
void term_printf(const char *fmt, ...);

static int monitor_disas_is_physical;
static CPUState *monitor_disas_env;

static int
monitor_read_memory (bfd_vma memaddr, bfd_byte *myaddr, int length,
                     struct disassemble_info *info)
{
    if (monitor_disas_is_physical) {
        cpu_physical_memory_rw(memaddr, myaddr, length, 0);
    } else {
        cpu_memory_rw_debug(monitor_disas_env, memaddr,myaddr, length, 0);
    }
    return 0;
}

static int monitor_fprintf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    term_vprintf(fmt, ap);
    va_end(ap);
    return 0;
}

void monitor_disas(CPUState *env,
                   target_ulong pc, int nb_insn, int is_physical, int flags)
{
    int count, i;
    struct disassemble_info disasm_info;
    int (*print_insn)(bfd_vma pc, disassemble_info *info);

    INIT_DISASSEMBLE_INFO(disasm_info, NULL, monitor_fprintf);

    monitor_disas_env = env;
    monitor_disas_is_physical = is_physical;
    disasm_info.read_memory_func = monitor_read_memory;

    disasm_info.buffer_vma = pc;

#ifdef TARGET_WORDS_BIGENDIAN
    disasm_info.endian = BFD_ENDIAN_BIG;
#else
    disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(TARGET_I386)
    if (flags == 2)
        disasm_info.mach = bfd_mach_x86_64;
    else if (flags == 1)
        disasm_info.mach = bfd_mach_i386_i8086;
    else
        disasm_info.mach = bfd_mach_i386_i386;
    print_insn = print_insn_i386;
#elif defined(TARGET_ARM)
    print_insn = print_insn_arm;
#elif defined(TARGET_ALPHA)
    print_insn = print_insn_alpha;
#elif defined(TARGET_SPARC)
    print_insn = print_insn_sparc;
#ifdef TARGET_SPARC64
    disasm_info.mach = bfd_mach_sparc_v9b;
#endif
#elif defined(TARGET_PPC)
#ifdef TARGET_PPC64
    disasm_info.mach = bfd_mach_ppc64;
#else
    disasm_info.mach = bfd_mach_ppc;
#endif
    print_insn = print_insn_ppc;
#elif defined(TARGET_M68K)
    print_insn = print_insn_m68k;
#elif defined(TARGET_MIPS)
#ifdef TARGET_WORDS_BIGENDIAN
    print_insn = print_insn_big_mips;
#else
    print_insn = print_insn_little_mips;
#endif
#else
    term_printf("0x" TARGET_FMT_lx
		": Asm output not supported on this arch\n", pc);
    return;
#endif

    for(i = 0; i < nb_insn; i++) {
	term_printf("0x" TARGET_FMT_lx ":  ", pc);
	count = print_insn(pc, &disasm_info);
	term_printf("\n");
	if (count < 0)
	    break;
        pc += count;
    }
}
#endif
