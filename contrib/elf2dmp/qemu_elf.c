/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "err.h"
#include "qemu_elf.h"

#define QEMU_NOTE_NAME "QEMU"

#ifndef ROUND_UP
#define ROUND_UP(n, d) (((n) + (d) - 1) & -(0 ? (n) : (d)))
#endif

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif

#define ELF_NOTE_SIZE(hdr_size, name_size, desc_size)   \
    ((DIV_ROUND_UP((hdr_size), 4) +                     \
      DIV_ROUND_UP((name_size), 4) +                    \
      DIV_ROUND_UP((desc_size), 4)) * 4)

int is_system(QEMUCPUState *s)
{
    return s->gs.base >> 63;
}

static char *nhdr_get_name(Elf64_Nhdr *nhdr)
{
    return (char *)nhdr + ROUND_UP(sizeof(*nhdr), 4);
}

static void *nhdr_get_desc(Elf64_Nhdr *nhdr)
{
    return nhdr_get_name(nhdr) + ROUND_UP(nhdr->n_namesz, 4);
}

static Elf64_Nhdr *nhdr_get_next(Elf64_Nhdr *nhdr)
{
    return (void *)((uint8_t *)nhdr + ELF_NOTE_SIZE(sizeof(*nhdr),
                nhdr->n_namesz, nhdr->n_descsz));
}

Elf64_Phdr *elf64_getphdr(void *map)
{
    Elf64_Ehdr *ehdr = map;
    Elf64_Phdr *phdr = (void *)((uint8_t *)map + ehdr->e_phoff);

    return phdr;
}

Elf64_Half elf_getphdrnum(void *map)
{
    Elf64_Ehdr *ehdr = map;

    return ehdr->e_phnum;
}

static int init_states(QEMU_Elf *qe)
{
    Elf64_Phdr *phdr = elf64_getphdr(qe->map);
    Elf64_Nhdr *start = (void *)((uint8_t *)qe->map + phdr[0].p_offset);
    Elf64_Nhdr *end = (void *)((uint8_t *)start + phdr[0].p_memsz);
    Elf64_Nhdr *nhdr;
    size_t cpu_nr = 0;

    if (phdr[0].p_type != PT_NOTE) {
        eprintf("Failed to find PT_NOTE\n");
        return 1;
    }

    qe->has_kernel_gs_base = 1;

    for (nhdr = start; nhdr < end; nhdr = nhdr_get_next(nhdr)) {
        if (!strcmp(nhdr_get_name(nhdr), QEMU_NOTE_NAME)) {
            QEMUCPUState *state = nhdr_get_desc(nhdr);

            if (state->size < sizeof(*state)) {
                eprintf("CPU #%zu: QEMU CPU state size %u doesn't match\n",
                        cpu_nr, state->size);
                /*
                 * We assume either every QEMU CPU state has KERNEL_GS_BASE or
                 * no one has.
                 */
                qe->has_kernel_gs_base = 0;
            }
            cpu_nr++;
        }
    }

    printf("%zu CPU states has been found\n", cpu_nr);

    qe->state = malloc(sizeof(*qe->state) * cpu_nr);
    if (!qe->state) {
        return 1;
    }

    cpu_nr = 0;

    for (nhdr = start; nhdr < end; nhdr = nhdr_get_next(nhdr)) {
        if (!strcmp(nhdr_get_name(nhdr), QEMU_NOTE_NAME)) {
            qe->state[cpu_nr] = nhdr_get_desc(nhdr);
            cpu_nr++;
        }
    }

    qe->state_nr = cpu_nr;

    return 0;
}

static void exit_states(QEMU_Elf *qe)
{
    free(qe->state);
}

static bool check_ehdr(QEMU_Elf *qe)
{
    Elf64_Ehdr *ehdr = qe->map;

    if (sizeof(Elf64_Ehdr) > qe->size) {
        eprintf("Invalid input dump file size\n");
        return false;
    }

    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
        eprintf("Invalid ELF signature, input file is not ELF\n");
        return false;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
            ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        eprintf("Invalid ELF class or byte order, must be 64-bit LE\n");
        return false;
    }

    if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
        eprintf("Invalid ELF version\n");
        return false;
    }

    if (ehdr->e_machine != EM_X86_64) {
        eprintf("Invalid input dump architecture, only x86_64 is supported\n");
        return false;
    }

    if (ehdr->e_type != ET_CORE) {
        eprintf("Invalid ELF type, must be core file\n");
        return false;
    }

    /*
     * ELF dump file must contain one PT_NOTE and at least one PT_LOAD to
     * restore physical address space.
     */
    if (ehdr->e_phnum < 2) {
        eprintf("Invalid number of ELF program headers\n");
        return false;
    }

    return true;
}

int QEMU_Elf_init(QEMU_Elf *qe, const char *filename)
{
    GError *gerr = NULL;
    int err = 0;

    qe->gmf = g_mapped_file_new(filename, TRUE, &gerr);
    if (gerr) {
        eprintf("Failed to map ELF dump file \'%s\'\n", filename);
        g_error_free(gerr);
        return 1;
    }

    qe->map = g_mapped_file_get_contents(qe->gmf);
    qe->size = g_mapped_file_get_length(qe->gmf);

    if (!check_ehdr(qe)) {
        eprintf("Input file has the wrong format\n");
        err = 1;
        goto out_unmap;
    }

    if (init_states(qe)) {
        eprintf("Failed to extract QEMU CPU states\n");
        err = 1;
        goto out_unmap;
    }

    return 0;

out_unmap:
    g_mapped_file_unref(qe->gmf);

    return err;
}

void QEMU_Elf_exit(QEMU_Elf *qe)
{
    exit_states(qe);
    g_mapped_file_unref(qe->gmf);
}
