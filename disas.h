#ifndef _QEMU_DISAS_H
#define _QEMU_DISAS_H

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size, int is_host, int flags);
void monitor_disas(target_ulong pc, int nb_insn, int is_physical, int flags);

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(void *orig_addr);

/* Filled in by elfload.c.  Simplistic, but will do for now. */
extern unsigned int disas_num_syms;
extern void *disas_symtab;  /* FIXME: includes are a mess --RR */
extern const char *disas_strtab;
#endif /* _QEMU_DISAS_H */
