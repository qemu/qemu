/*
 * Copyright (c) 2018 Virtuozzo International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 *
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "err.h"
#include "qemu_elf.h"

#define QEMU_NOTE_NAME "QEMU"

#ifndef ROUND_UP
#define ROUND_UP(n, d) (((n) + (d) - 1) & -(0 ? (n) : (d)))
#endif

int is_system(QEMUCPUState *s)
{
    return s->gs.base >> 63;
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

static bool advance_note_offset(uint64_t *offsetp, uint64_t size, uint64_t end)
{
    uint64_t offset = *offsetp;

    if (uadd64_overflow(offset, size, &offset) || offset > UINT64_MAX - 3) {
        return false;
    }

    offset = ROUND_UP(offset, 4);

    if (offset > end) {
        return false;
    }

    *offsetp = offset;

    return true;
}

static bool init_states(QEMU_Elf *qe)
{
    Elf64_Phdr *phdr = elf64_getphdr(qe->map);
    Elf64_Nhdr *nhdr;
    GPtrArray *states;
    QEMUCPUState *state;
    uint32_t state_size;
    uint64_t offset;
    uint64_t end_offset;
    char *name;

    if (phdr[0].p_type != PT_NOTE) {
        eprintf("Failed to find PT_NOTE\n");
        return false;
    }

    qe->has_kernel_gs_base = 1;
    offset = phdr[0].p_offset;
    states = g_ptr_array_new();

    if (uadd64_overflow(offset, phdr[0].p_memsz, &end_offset) ||
        end_offset > qe->size) {
        end_offset = qe->size;
    }

    while (offset < end_offset) {
        nhdr = (void *)((uint8_t *)qe->map + offset);

        if (!advance_note_offset(&offset, sizeof(*nhdr), end_offset)) {
            break;
        }

        name = (char *)qe->map + offset;

        if (!advance_note_offset(&offset, nhdr->n_namesz, end_offset)) {
            break;
        }

        state = (void *)((uint8_t *)qe->map + offset);

        if (!advance_note_offset(&offset, nhdr->n_descsz, end_offset)) {
            break;
        }

        if (!strcmp(name, QEMU_NOTE_NAME) &&
            nhdr->n_descsz >= offsetof(QEMUCPUState, kernel_gs_base)) {
            state_size = MIN(state->size, nhdr->n_descsz);

            if (state_size < sizeof(*state)) {
                eprintf("CPU #%u: QEMU CPU state size %u doesn't match\n",
                        states->len, state_size);
                /*
                 * We assume either every QEMU CPU state has KERNEL_GS_BASE or
                 * no one has.
                 */
                qe->has_kernel_gs_base = 0;
            }
            g_ptr_array_add(states, state);
        }
    }

    printf("%u CPU states has been found\n", states->len);

    qe->state_nr = states->len;
    qe->state = (void *)g_ptr_array_free(states, FALSE);

    return true;
}

static void exit_states(QEMU_Elf *qe)
{
    g_free(qe->state);
}

static bool check_ehdr(QEMU_Elf *qe)
{
    Elf64_Ehdr *ehdr = qe->map;
    uint64_t phendoff;

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

    if (umul64_overflow(ehdr->e_phnum, sizeof(Elf64_Phdr), &phendoff) ||
        uadd64_overflow(phendoff, ehdr->e_phoff, &phendoff) ||
        phendoff > qe->size) {
        eprintf("phdrs do not fit in file\n");
        return false;
    }

    return true;
}

static bool QEMU_Elf_map(QEMU_Elf *qe, const char *filename)
{
#ifdef CONFIG_LINUX
    struct stat st;
    int fd;

    printf("Using Linux mmap\n");

    fd = open(filename, O_RDONLY, 0);
    if (fd == -1) {
        eprintf("Failed to open ELF dump file \'%s\'\n", filename);
        return false;
    }

    if (fstat(fd, &st)) {
        eprintf("Failed to get size of ELF dump file\n");
        close(fd);
        return false;
    }
    qe->size = st.st_size;

    qe->map = mmap(NULL, qe->size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_NORESERVE, fd, 0);
    if (qe->map == MAP_FAILED) {
        eprintf("Failed to map ELF file\n");
        close(fd);
        return false;
    }

    close(fd);
#else
    GError *gerr = NULL;

    printf("Using GLib mmap\n");

    qe->gmf = g_mapped_file_new(filename, TRUE, &gerr);
    if (gerr) {
        eprintf("Failed to map ELF dump file \'%s\'\n", filename);
        g_error_free(gerr);
        return false;
    }

    qe->map = g_mapped_file_get_contents(qe->gmf);
    qe->size = g_mapped_file_get_length(qe->gmf);
#endif

    return true;
}

static void QEMU_Elf_unmap(QEMU_Elf *qe)
{
#ifdef CONFIG_LINUX
    munmap(qe->map, qe->size);
#else
    g_mapped_file_unref(qe->gmf);
#endif
}

bool QEMU_Elf_init(QEMU_Elf *qe, const char *filename)
{
    if (!QEMU_Elf_map(qe, filename)) {
        return false;
    }

    if (!check_ehdr(qe)) {
        eprintf("Input file has the wrong format\n");
        QEMU_Elf_unmap(qe);
        return false;
    }

    if (!init_states(qe)) {
        eprintf("Failed to extract QEMU CPU states\n");
        QEMU_Elf_unmap(qe);
        return false;
    }

    return true;
}

void QEMU_Elf_exit(QEMU_Elf *qe)
{
    exit_states(qe);
    QEMU_Elf_unmap(qe);
}
