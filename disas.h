#ifndef _QEMU_DISAS_H
#define _QEMU_DISAS_H

enum disas_type {
    DISAS_I386_I386,
    DISAS_I386_I8086,
    DISAS_TARGET, /* whatever host is. */
};

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size, enum disas_type type);

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(void *orig_addr);

/* Filled in by elfload.c.  Simplistic, but will do for now. */
extern unsigned int disas_num_syms;
extern void *disas_symtab;  /* FIXME: includes are a mess --RR */
extern const char *disas_strtab;
#endif /* _QEMU_DISAS_H */
