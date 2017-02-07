/* General "disassemble this chunk" code.  Used for debugging. */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "disas/bfd.h"
#include "elf.h"

#include "cpu.h"
#include "disas/disas.h"

typedef struct CPUDebug {
    struct disassemble_info info;
    CPUState *cpu;
} CPUDebug;

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
    CPUDebug *s = container_of(info, CPUDebug, info);

    cpu_memory_rw_debug(s->cpu, memaddr, myaddr, length, 0);
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

/* This could be in a separate file, to save minuscule amounts of space
   in statically linked executables.  */

/* Just print the address is hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */

void
generic_print_address (bfd_vma addr, struct disassemble_info *info)
{
    (*info->fprintf_func) (info->stream, "0x%" PRIx64, addr);
}

/* Print address in hex, truncated to the width of a host virtual address. */
static void
generic_print_host_address(bfd_vma addr, struct disassemble_info *info)
{
    uint64_t mask = ~0ULL >> (64 - (sizeof(void *) * 8));
    generic_print_address(addr & mask, info);
}

/* Just return the given address.  */

int
generic_symbol_at_address (bfd_vma addr, struct disassemble_info *info)
{
  return 1;
}

bfd_vma bfd_getl64 (const bfd_byte *addr)
{
  unsigned long long v;

  v = (unsigned long long) addr[0];
  v |= (unsigned long long) addr[1] << 8;
  v |= (unsigned long long) addr[2] << 16;
  v |= (unsigned long long) addr[3] << 24;
  v |= (unsigned long long) addr[4] << 32;
  v |= (unsigned long long) addr[5] << 40;
  v |= (unsigned long long) addr[6] << 48;
  v |= (unsigned long long) addr[7] << 56;
  return (bfd_vma) v;
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

static int print_insn_objdump(bfd_vma pc, disassemble_info *info,
                              const char *prefix)
{
    int i, n = info->buffer_length;
    uint8_t *buf = g_malloc(n);

    info->read_memory_func(pc, buf, n, info);

    for (i = 0; i < n; ++i) {
        if (i % 32 == 0) {
            info->fprintf_func(info->stream, "\n%s: ", prefix);
        }
        info->fprintf_func(info->stream, "%02x", buf[i]);
    }

    g_free(buf);
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

/* Disassemble this for me please... (debugging). 'flags' has the following
   values:
    i386 - 1 means 16 bit code, 2 means 64 bit code
    ppc  - bits 0:15 specify (optionally) the machine instruction set;
           bit 16 indicates little endian.
    other targets - unused
 */
void target_disas(FILE *out, CPUState *cpu, target_ulong code,
                  target_ulong size, int flags)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    target_ulong pc;
    int count;
    CPUDebug s;

    INIT_DISASSEMBLE_INFO(s.info, out, fprintf);

    s.cpu = cpu;
    s.info.read_memory_func = target_read_memory;
    s.info.read_memory_inner_func = NULL;
    s.info.buffer_vma = code;
    s.info.buffer_length = size;
    s.info.print_address_func = generic_print_address;

#ifdef TARGET_WORDS_BIGENDIAN
    s.info.endian = BFD_ENDIAN_BIG;
#else
    s.info.endian = BFD_ENDIAN_LITTLE;
#endif

    if (cc->disas_set_info) {
        cc->disas_set_info(cpu, &s.info);
    }

#if defined(TARGET_I386)
    if (flags == 2) {
        s.info.mach = bfd_mach_x86_64;
    } else if (flags == 1) {
        s.info.mach = bfd_mach_i386_i8086;
    } else {
        s.info.mach = bfd_mach_i386_i386;
    }
    s.info.print_insn = print_insn_i386;
#elif defined(TARGET_PPC)
    if ((flags >> 16) & 1) {
        s.info.endian = BFD_ENDIAN_LITTLE;
    }
    if (flags & 0xFFFF) {
        /* If we have a precise definition of the instruction set, use it. */
        s.info.mach = flags & 0xFFFF;
    } else {
#ifdef TARGET_PPC64
        s.info.mach = bfd_mach_ppc64;
#else
        s.info.mach = bfd_mach_ppc;
#endif
    }
    s.info.disassembler_options = (char *)"any";
    s.info.print_insn = print_insn_ppc;
#endif
    if (s.info.print_insn == NULL) {
        s.info.print_insn = print_insn_od_target;
    }

    for (pc = code; size > 0; pc += count, size -= count) {
	fprintf(out, "0x" TARGET_FMT_lx ":  ", pc);
	count = s.info.print_insn(pc, &s.info);
#if 0
        {
            int i;
            uint8_t b;
            fprintf(out, " {");
            for(i = 0; i < count; i++) {
                target_read_memory(pc + i, &b, 1, &s.info);
                fprintf(out, " %02x", b);
            }
            fprintf(out, " }");
        }
#endif
	fprintf(out, "\n");
	if (count < 0)
	    break;
        if (size < count) {
            fprintf(out,
                    "Disassembler disagrees with translator over instruction "
                    "decoding\n"
                    "Please report this to qemu-devel@nongnu.org\n");
            break;
        }
    }
}

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size)
{
    uintptr_t pc;
    int count;
    CPUDebug s;
    int (*print_insn)(bfd_vma pc, disassemble_info *info) = NULL;

    INIT_DISASSEMBLE_INFO(s.info, out, fprintf);
    s.info.print_address_func = generic_print_host_address;

    s.info.buffer = code;
    s.info.buffer_vma = (uintptr_t)code;
    s.info.buffer_length = size;

#ifdef HOST_WORDS_BIGENDIAN
    s.info.endian = BFD_ENDIAN_BIG;
#else
    s.info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(CONFIG_TCG_INTERPRETER)
    print_insn = print_insn_tci;
#elif defined(__i386__)
    s.info.mach = bfd_mach_i386_i386;
    print_insn = print_insn_i386;
#elif defined(__x86_64__)
    s.info.mach = bfd_mach_x86_64;
    print_insn = print_insn_i386;
#elif defined(_ARCH_PPC)
    s.info.disassembler_options = (char *)"any";
    print_insn = print_insn_ppc;
#elif defined(__aarch64__) && defined(CONFIG_ARM_A64_DIS)
    print_insn = print_insn_arm_a64;
#elif defined(__alpha__)
    print_insn = print_insn_alpha;
#elif defined(__sparc__)
    print_insn = print_insn_sparc;
    s.info.mach = bfd_mach_sparc_v9b;
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
#elif defined(__ia64__)
    print_insn = print_insn_ia64;
#endif
    if (print_insn == NULL) {
        print_insn = print_insn_od_host;
    }
    for (pc = (uintptr_t)code; size > 0; pc += count, size -= count) {
        fprintf(out, "0x%08" PRIxPTR ":  ", pc);
        count = print_insn(pc, &s.info);
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

#include "monitor/monitor.h"

static int monitor_disas_is_physical;

static int
monitor_read_memory (bfd_vma memaddr, bfd_byte *myaddr, int length,
                     struct disassemble_info *info)
{
    CPUDebug *s = container_of(info, CPUDebug, info);

    if (monitor_disas_is_physical) {
        cpu_physical_memory_read(memaddr, myaddr, length);
    } else {
        cpu_memory_rw_debug(s->cpu, memaddr, myaddr, length, 0);
    }
    return 0;
}

/* Disassembler for the monitor.
   See target_disas for a description of flags. */
void monitor_disas(Monitor *mon, CPUState *cpu,
                   target_ulong pc, int nb_insn, int is_physical, int flags)
{
    CPUClass *cc = CPU_GET_CLASS(cpu);
    int count, i;
    CPUDebug s;

    INIT_DISASSEMBLE_INFO(s.info, (FILE *)mon, monitor_fprintf);

    s.cpu = cpu;
    monitor_disas_is_physical = is_physical;
    s.info.read_memory_func = monitor_read_memory;
    s.info.print_address_func = generic_print_address;

    s.info.buffer_vma = pc;

#ifdef TARGET_WORDS_BIGENDIAN
    s.info.endian = BFD_ENDIAN_BIG;
#else
    s.info.endian = BFD_ENDIAN_LITTLE;
#endif

    if (cc->disas_set_info) {
        cc->disas_set_info(cpu, &s.info);
    }

#if defined(TARGET_I386)
    if (flags == 2) {
        s.info.mach = bfd_mach_x86_64;
    } else if (flags == 1) {
        s.info.mach = bfd_mach_i386_i8086;
    } else {
        s.info.mach = bfd_mach_i386_i386;
    }
    s.info.print_insn = print_insn_i386;
#elif defined(TARGET_PPC)
    if (flags & 0xFFFF) {
        /* If we have a precise definition of the instruction set, use it. */
        s.info.mach = flags & 0xFFFF;
    } else {
#ifdef TARGET_PPC64
        s.info.mach = bfd_mach_ppc64;
#else
        s.info.mach = bfd_mach_ppc;
#endif
    }
    if ((flags >> 16) & 1) {
        s.info.endian = BFD_ENDIAN_LITTLE;
    }
    s.info.print_insn = print_insn_ppc;
#endif
    if (!s.info.print_insn) {
        monitor_printf(mon, "0x" TARGET_FMT_lx
                       ": Asm output not supported on this arch\n", pc);
        return;
    }

    for(i = 0; i < nb_insn; i++) {
	monitor_printf(mon, "0x" TARGET_FMT_lx ":  ", pc);
        count = s.info.print_insn(pc, &s.info);
	monitor_printf(mon, "\n");
	if (count < 0)
	    break;
        pc += count;
    }
}
#endif
