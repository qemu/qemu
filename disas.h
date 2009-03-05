#ifndef _QEMU_DISAS_H
#define _QEMU_DISAS_H

#include "qemu-common.h"

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, void *code, unsigned long size);
void target_disas(FILE *out, target_ulong code, target_ulong size, int flags);

/* The usual mess... FIXME: Remove this condition once dyngen-exec.h is gone */
#ifndef __DYNGEN_EXEC_H__
void monitor_disas(Monitor *mon, CPUState *env,
                   target_ulong pc, int nb_insn, int is_physical, int flags);
#endif

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(target_ulong orig_addr);

struct syminfo;
struct elf32_sym;
struct elf64_sym;

typedef const char *(*lookup_symbol_t)(struct syminfo *s, target_ulong orig_addr);

struct syminfo {
    lookup_symbol_t lookup_symbol;
    unsigned int disas_num_syms;
    union {
      struct elf32_sym *elf32;
      struct elf64_sym *elf64;
    } disas_symtab;
    const char *disas_strtab;
    struct syminfo *next;
};

/* Filled in by elfload.c.  Simplistic, but will do for now. */
extern struct syminfo *syminfos;

#endif /* _QEMU_DISAS_H */
