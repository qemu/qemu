/* General "disassemble this chunk" code.  Used for debugging. */
#include "dis-asm.h"
#include "disas.h"
#include "elf.h"

/* Filled in by elfload.c.  Simplistic, but will do for now. */
unsigned int disas_num_syms;
void *disas_symtab;
const char *disas_strtab;

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size, enum disas_type type)
{
    uint8_t *pc;
    int count;
    struct disassemble_info disasm_info;
    int (*print_insn)(bfd_vma pc, disassemble_info *info);

    INIT_DISASSEMBLE_INFO(disasm_info, out, fprintf);

    disasm_info.buffer = code;
    disasm_info.buffer_vma = (unsigned long)code;
    disasm_info.buffer_length = size;

    if (type == DISAS_TARGET) {
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
#else
	fprintf(out, "Asm output not supported on this arch\n");
	return;
#endif
    } else {
	/* Currently only source supported in x86. */
	disasm_info.endian = BFD_ENDIAN_LITTLE;
	if (type == DISAS_I386_I386)
	    disasm_info.mach = bfd_mach_i386_i386;
	else
	    disasm_info.mach = bfd_mach_i386_i8086;
	print_insn = print_insn_i386;
    }

    for (pc = code; pc < (uint8_t *)code + size; pc += count) {
	fprintf(out, "0x%08lx:  ", (long)pc);
	count = print_insn((long)pc, &disasm_info);
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
