/* General "disassemble this chunk" code.  Used for debugging. */
#include "config.h"
#include "dis-asm.h"
#include "disas.h"
#include "elf.h"
#include <errno.h>

#include "cpu.h"
#include "exec-all.h"

/* Filled in by elfload.c.  Simplistic, but will do for now. */
unsigned int disas_num_syms;
void *disas_symtab;
const char *disas_strtab;

/* Get LENGTH bytes from info's buffer, at target address memaddr.
   Transfer them to myaddr.  */
int
buffer_read_memory (memaddr, myaddr, length, info)
     bfd_vma memaddr;
     bfd_byte *myaddr;
     int length;
     struct disassemble_info *info;
{
    if (memaddr < info->buffer_vma
        || memaddr + length > info->buffer_vma + info->buffer_length)
        /* Out of bounds.  Use EIO because GDB uses it.  */
        return EIO;
    memcpy (myaddr, info->buffer + (memaddr - info->buffer_vma), length);
    return 0;
}

#if !defined(CONFIG_USER_ONLY)
/* Get LENGTH bytes from info's buffer, at target address memaddr.
   Transfer them to myaddr.  */
static int
target_read_memory (memaddr, myaddr, length, info)
     bfd_vma memaddr;
     bfd_byte *myaddr;
     int length;
     struct disassemble_info *info;
{
    int i;
    for(i = 0; i < length; i++) {
        myaddr[i] = ldub_code((void *)((long)memaddr));
    }
    return 0;
}
#endif

/* Print an error message.  We can assume that this is in response to
   an error return from buffer_read_memory.  */
void
perror_memory (status, memaddr, info)
     int status;
     bfd_vma memaddr;
     struct disassemble_info *info;
{
  if (status != EIO)
    /* Can't happen.  */
    (*info->fprintf_func) (info->stream, "Unknown error %d\n", status);
  else
    /* Actually, address between memaddr and memaddr + len was
       out of bounds.  */
    (*info->fprintf_func) (info->stream,
			   "Address 0x%llx is out of bounds.\n", memaddr);
}

/* This could be in a separate file, to save miniscule amounts of space
   in statically linked executables.  */

/* Just print the address is hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */

void
generic_print_address (addr, info)
     bfd_vma addr;
     struct disassemble_info *info;
{
  (*info->fprintf_func) (info->stream, "0x%llx", addr);
}

/* Just return the given address.  */

int
generic_symbol_at_address (addr, info)
     bfd_vma addr;
     struct disassemble_info * info;
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

/* Disassemble this for me please... (debugging). 'flags' is only used
   for i386: non zero means 16 bit code */
void disas(FILE *out, void *code, unsigned long size, int is_host, int flags)
{
    uint8_t *pc;
    int count;
    struct disassemble_info disasm_info;
    int (*print_insn)(bfd_vma pc, disassemble_info *info);

    INIT_DISASSEMBLE_INFO(disasm_info, out, fprintf);

#if !defined(CONFIG_USER_ONLY)
    if (!is_host) {
        disasm_info.read_memory_func = target_read_memory;
    }
#endif

    disasm_info.buffer = code;
    disasm_info.buffer_vma = (unsigned long)code;
    disasm_info.buffer_length = size;

    if (is_host) {
#ifdef WORDS_BIGENDIAN
	disasm_info.endian = BFD_ENDIAN_BIG;
#else
	disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif
#ifdef __i386__
	disasm_info.mach = bfd_mach_i386_i386;
	print_insn = print_insn_i386;
#elif defined(__powerpc__)
	print_insn = print_insn_ppc;
#elif defined(__alpha__)
	print_insn = print_insn_alpha;
#elif defined(__sparc__)
	print_insn = print_insn_sparc;
#elif defined(__arm__) 
        print_insn = print_insn_arm;
#else
	fprintf(out, "Asm output not supported on this arch\n");
	return;
#endif
    } else {
#ifdef TARGET_WORDS_BIGENDIAN
	disasm_info.endian = BFD_ENDIAN_BIG;
#else
	disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(TARGET_I386)
        if (!flags)
	    disasm_info.mach = bfd_mach_i386_i386;
	else
	    disasm_info.mach = bfd_mach_i386_i8086;
	print_insn = print_insn_i386;
#elif defined(TARGET_ARM)
	print_insn = print_insn_arm;
#elif defined(TARGET_SPARC)
	print_insn = print_insn_sparc;
#else
	fprintf(out, "Asm output not supported on this arch\n");
	return;
#endif
    }

    for (pc = code; pc < (uint8_t *)code + size; pc += count) {
	fprintf(out, "0x%08lx:  ", (long)pc);
#ifdef __arm__
        /* since data are included in the code, it is better to
           display code data too */
        if (is_host) {
            fprintf(out, "%08x  ", (int)bfd_getl32((const bfd_byte *)pc));
        }
#endif
	count = print_insn((unsigned long)pc, &disasm_info);
	fprintf(out, "\n");
	if (count < 0)
	    break;
    }
}

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(void *orig_addr)
{
    unsigned int i;
    /* Hack, because we know this is x86. */
    Elf32_Sym *sym = disas_symtab;

    for (i = 0; i < disas_num_syms; i++) {
	if (sym[i].st_shndx == SHN_UNDEF
	    || sym[i].st_shndx >= SHN_LORESERVE)
	    continue;

	if (ELF_ST_TYPE(sym[i].st_info) != STT_FUNC)
	    continue;

	if ((long)orig_addr >= sym[i].st_value
	    && (long)orig_addr < sym[i].st_value + sym[i].st_size)
	    return disas_strtab + sym[i].st_name;
    }
    return "";
}
