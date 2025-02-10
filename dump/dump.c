/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "elf.h"
#include "qemu/bswap.h"
#include "exec/target_page.h"
#include "monitor/monitor.h"
#include "system/dump.h"
#include "system/runstate.h"
#include "system/cpus.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-dump.h"
#include "qapi/qapi-events-dump.h"
#include "qapi/qmp/qerror.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/misc/vmcoreinfo.h"
#include "migration/blocker.h"
#include "hw/core/cpu.h"
#include "win_dump.h"
#include "qemu/range.h"

#include <zlib.h>
#ifdef CONFIG_LZO
#include <lzo/lzo1x.h>
#endif
#ifdef CONFIG_SNAPPY
#include <snappy-c.h>
#endif
#ifndef ELF_MACHINE_UNAME
#define ELF_MACHINE_UNAME "Unknown"
#endif

#define MAX_GUEST_NOTE_SIZE (1 << 20) /* 1MB should be enough */

static Error *dump_migration_blocker;

#define ELF_NOTE_SIZE(hdr_size, name_size, desc_size)   \
    ((DIV_ROUND_UP((hdr_size), 4) +                     \
      DIV_ROUND_UP((name_size), 4) +                    \
      DIV_ROUND_UP((desc_size), 4)) * 4)

static inline bool dump_is_64bit(DumpState *s)
{
    return s->dump_info.d_class == ELFCLASS64;
}

static inline bool dump_has_filter(DumpState *s)
{
    return s->filter_area_length > 0;
}

uint16_t cpu_to_dump16(DumpState *s, uint16_t val)
{
    if (s->dump_info.d_endian == ELFDATA2LSB) {
        val = cpu_to_le16(val);
    } else {
        val = cpu_to_be16(val);
    }

    return val;
}

uint32_t cpu_to_dump32(DumpState *s, uint32_t val)
{
    if (s->dump_info.d_endian == ELFDATA2LSB) {
        val = cpu_to_le32(val);
    } else {
        val = cpu_to_be32(val);
    }

    return val;
}

uint64_t cpu_to_dump64(DumpState *s, uint64_t val)
{
    if (s->dump_info.d_endian == ELFDATA2LSB) {
        val = cpu_to_le64(val);
    } else {
        val = cpu_to_be64(val);
    }

    return val;
}

static int dump_cleanup(DumpState *s)
{
    if (s->dump_info.arch_cleanup_fn) {
        s->dump_info.arch_cleanup_fn(s);
    }

    guest_phys_blocks_free(&s->guest_phys_blocks);
    memory_mapping_list_free(&s->list);
    close(s->fd);
    g_free(s->guest_note);
    g_clear_pointer(&s->string_table_buf, g_array_unref);
    s->guest_note = NULL;
    if (s->resume) {
        if (s->detached) {
            bql_lock();
        }
        vm_start();
        if (s->detached) {
            bql_unlock();
        }
    }
    migrate_del_blocker(&dump_migration_blocker);

    return 0;
}

static int fd_write_vmcore(const void *buf, size_t size, void *opaque)
{
    DumpState *s = opaque;
    size_t written_size;

    written_size = qemu_write_full(s->fd, buf, size);
    if (written_size != size) {
        return -errno;
    }

    return 0;
}

static void prepare_elf64_header(DumpState *s, Elf64_Ehdr *elf_header)
{
    /*
     * phnum in the elf header is 16 bit, if we have more segments we
     * set phnum to PN_XNUM and write the real number of segments to a
     * special section.
     */
    uint16_t phnum = MIN(s->phdr_num, PN_XNUM);

    memset(elf_header, 0, sizeof(Elf64_Ehdr));
    memcpy(elf_header, ELFMAG, SELFMAG);
    elf_header->e_ident[EI_CLASS] = ELFCLASS64;
    elf_header->e_ident[EI_DATA] = s->dump_info.d_endian;
    elf_header->e_ident[EI_VERSION] = EV_CURRENT;
    elf_header->e_type = cpu_to_dump16(s, ET_CORE);
    elf_header->e_machine = cpu_to_dump16(s, s->dump_info.d_machine);
    elf_header->e_version = cpu_to_dump32(s, EV_CURRENT);
    elf_header->e_ehsize = cpu_to_dump16(s, sizeof(elf_header));
    elf_header->e_phoff = cpu_to_dump64(s, s->phdr_offset);
    elf_header->e_phentsize = cpu_to_dump16(s, sizeof(Elf64_Phdr));
    elf_header->e_phnum = cpu_to_dump16(s, phnum);
    elf_header->e_shoff = cpu_to_dump64(s, s->shdr_offset);
    elf_header->e_shentsize = cpu_to_dump16(s, sizeof(Elf64_Shdr));
    elf_header->e_shnum = cpu_to_dump16(s, s->shdr_num);
    elf_header->e_shstrndx = cpu_to_dump16(s, s->shdr_num - 1);
}

static void prepare_elf32_header(DumpState *s, Elf32_Ehdr *elf_header)
{
    /*
     * phnum in the elf header is 16 bit, if we have more segments we
     * set phnum to PN_XNUM and write the real number of segments to a
     * special section.
     */
    uint16_t phnum = MIN(s->phdr_num, PN_XNUM);

    memset(elf_header, 0, sizeof(Elf32_Ehdr));
    memcpy(elf_header, ELFMAG, SELFMAG);
    elf_header->e_ident[EI_CLASS] = ELFCLASS32;
    elf_header->e_ident[EI_DATA] = s->dump_info.d_endian;
    elf_header->e_ident[EI_VERSION] = EV_CURRENT;
    elf_header->e_type = cpu_to_dump16(s, ET_CORE);
    elf_header->e_machine = cpu_to_dump16(s, s->dump_info.d_machine);
    elf_header->e_version = cpu_to_dump32(s, EV_CURRENT);
    elf_header->e_ehsize = cpu_to_dump16(s, sizeof(elf_header));
    elf_header->e_phoff = cpu_to_dump32(s, s->phdr_offset);
    elf_header->e_phentsize = cpu_to_dump16(s, sizeof(Elf32_Phdr));
    elf_header->e_phnum = cpu_to_dump16(s, phnum);
    elf_header->e_shoff = cpu_to_dump32(s, s->shdr_offset);
    elf_header->e_shentsize = cpu_to_dump16(s, sizeof(Elf32_Shdr));
    elf_header->e_shnum = cpu_to_dump16(s, s->shdr_num);
    elf_header->e_shstrndx = cpu_to_dump16(s, s->shdr_num - 1);
}

static void write_elf_header(DumpState *s, Error **errp)
{
    Elf32_Ehdr elf32_header;
    Elf64_Ehdr elf64_header;
    size_t header_size;
    void *header_ptr;
    int ret;

    /* The NULL header and the shstrtab are always defined */
    assert(s->shdr_num >= 2);
    if (dump_is_64bit(s)) {
        prepare_elf64_header(s, &elf64_header);
        header_size = sizeof(elf64_header);
        header_ptr = &elf64_header;
    } else {
        prepare_elf32_header(s, &elf32_header);
        header_size = sizeof(elf32_header);
        header_ptr = &elf32_header;
    }

    ret = fd_write_vmcore(header_ptr, header_size, s);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "dump: failed to write elf header");
    }
}

static void write_elf64_load(DumpState *s, MemoryMapping *memory_mapping,
                             int phdr_index, hwaddr offset,
                             hwaddr filesz, Error **errp)
{
    Elf64_Phdr phdr;
    int ret;

    memset(&phdr, 0, sizeof(Elf64_Phdr));
    phdr.p_type = cpu_to_dump32(s, PT_LOAD);
    phdr.p_offset = cpu_to_dump64(s, offset);
    phdr.p_paddr = cpu_to_dump64(s, memory_mapping->phys_addr);
    phdr.p_filesz = cpu_to_dump64(s, filesz);
    phdr.p_memsz = cpu_to_dump64(s, memory_mapping->length);
    phdr.p_vaddr = cpu_to_dump64(s, memory_mapping->virt_addr) ?: phdr.p_paddr;

    assert(memory_mapping->length >= filesz);

    ret = fd_write_vmcore(&phdr, sizeof(Elf64_Phdr), s);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "dump: failed to write program header table");
    }
}

static void write_elf32_load(DumpState *s, MemoryMapping *memory_mapping,
                             int phdr_index, hwaddr offset,
                             hwaddr filesz, Error **errp)
{
    Elf32_Phdr phdr;
    int ret;

    memset(&phdr, 0, sizeof(Elf32_Phdr));
    phdr.p_type = cpu_to_dump32(s, PT_LOAD);
    phdr.p_offset = cpu_to_dump32(s, offset);
    phdr.p_paddr = cpu_to_dump32(s, memory_mapping->phys_addr);
    phdr.p_filesz = cpu_to_dump32(s, filesz);
    phdr.p_memsz = cpu_to_dump32(s, memory_mapping->length);
    phdr.p_vaddr =
        cpu_to_dump32(s, memory_mapping->virt_addr) ?: phdr.p_paddr;

    assert(memory_mapping->length >= filesz);

    ret = fd_write_vmcore(&phdr, sizeof(Elf32_Phdr), s);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "dump: failed to write program header table");
    }
}

static void prepare_elf64_phdr_note(DumpState *s, Elf64_Phdr *phdr)
{
    memset(phdr, 0, sizeof(*phdr));
    phdr->p_type = cpu_to_dump32(s, PT_NOTE);
    phdr->p_offset = cpu_to_dump64(s, s->note_offset);
    phdr->p_paddr = 0;
    phdr->p_filesz = cpu_to_dump64(s, s->note_size);
    phdr->p_memsz = cpu_to_dump64(s, s->note_size);
    phdr->p_vaddr = 0;
}

static inline int cpu_index(CPUState *cpu)
{
    return cpu->cpu_index + 1;
}

static void write_guest_note(WriteCoreDumpFunction f, DumpState *s,
                             Error **errp)
{
    int ret;

    if (s->guest_note) {
        ret = f(s->guest_note, s->guest_note_size, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to write guest note");
        }
    }
}

static void write_elf64_notes(WriteCoreDumpFunction f, DumpState *s,
                              Error **errp)
{
    CPUState *cpu;
    int ret;
    int id;

    CPU_FOREACH(cpu) {
        id = cpu_index(cpu);
        ret = cpu_write_elf64_note(f, cpu, id, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to write elf notes");
            return;
        }
    }

    CPU_FOREACH(cpu) {
        ret = cpu_write_elf64_qemunote(f, cpu, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to write CPU status");
            return;
        }
    }

    write_guest_note(f, s, errp);
}

static void prepare_elf32_phdr_note(DumpState *s, Elf32_Phdr *phdr)
{
    memset(phdr, 0, sizeof(*phdr));
    phdr->p_type = cpu_to_dump32(s, PT_NOTE);
    phdr->p_offset = cpu_to_dump32(s, s->note_offset);
    phdr->p_paddr = 0;
    phdr->p_filesz = cpu_to_dump32(s, s->note_size);
    phdr->p_memsz = cpu_to_dump32(s, s->note_size);
    phdr->p_vaddr = 0;
}

static void write_elf32_notes(WriteCoreDumpFunction f, DumpState *s,
                              Error **errp)
{
    CPUState *cpu;
    int ret;
    int id;

    CPU_FOREACH(cpu) {
        id = cpu_index(cpu);
        ret = cpu_write_elf32_note(f, cpu, id, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to write elf notes");
            return;
        }
    }

    CPU_FOREACH(cpu) {
        ret = cpu_write_elf32_qemunote(f, cpu, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to write CPU status");
            return;
        }
    }

    write_guest_note(f, s, errp);
}

static void write_elf_phdr_note(DumpState *s, Error **errp)
{
    Elf32_Phdr phdr32;
    Elf64_Phdr phdr64;
    void *phdr;
    size_t size;
    int ret;

    if (dump_is_64bit(s)) {
        prepare_elf64_phdr_note(s, &phdr64);
        size = sizeof(phdr64);
        phdr = &phdr64;
    } else {
        prepare_elf32_phdr_note(s, &phdr32);
        size = sizeof(phdr32);
        phdr = &phdr32;
    }

    ret = fd_write_vmcore(phdr, size, s);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "dump: failed to write program header table");
    }
}

static void prepare_elf_section_hdr_zero(DumpState *s)
{
    if (dump_is_64bit(s)) {
        Elf64_Shdr *shdr64 = s->elf_section_hdrs;

        shdr64->sh_info = cpu_to_dump32(s, s->phdr_num);
    } else {
        Elf32_Shdr *shdr32 = s->elf_section_hdrs;

        shdr32->sh_info = cpu_to_dump32(s, s->phdr_num);
    }
}

static void prepare_elf_section_hdr_string(DumpState *s, void *buff)
{
    uint64_t index = s->string_table_buf->len;
    const char strtab[] = ".shstrtab";
    Elf32_Shdr shdr32 = {};
    Elf64_Shdr shdr64 = {};
    int shdr_size;
    void *shdr;

    g_array_append_vals(s->string_table_buf, strtab, sizeof(strtab));
    if (dump_is_64bit(s)) {
        shdr_size = sizeof(Elf64_Shdr);
        shdr64.sh_type = SHT_STRTAB;
        shdr64.sh_offset = s->section_offset + s->elf_section_data_size;
        shdr64.sh_name = index;
        shdr64.sh_size = s->string_table_buf->len;
        shdr = &shdr64;
    } else {
        shdr_size = sizeof(Elf32_Shdr);
        shdr32.sh_type = SHT_STRTAB;
        shdr32.sh_offset = s->section_offset + s->elf_section_data_size;
        shdr32.sh_name = index;
        shdr32.sh_size = s->string_table_buf->len;
        shdr = &shdr32;
    }
    memcpy(buff, shdr, shdr_size);
}

static bool prepare_elf_section_hdrs(DumpState *s, Error **errp)
{
    size_t len, sizeof_shdr;
    void *buff_hdr;

    /*
     * Section ordering:
     * - HDR zero
     * - Arch section hdrs
     * - String table hdr
     */
    sizeof_shdr = dump_is_64bit(s) ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr);
    len = sizeof_shdr * s->shdr_num;
    s->elf_section_hdrs = g_malloc0(len);
    buff_hdr = s->elf_section_hdrs;

    /*
     * The first section header is ALWAYS a special initial section
     * header.
     *
     * The header should be 0 with one exception being that if
     * phdr_num is PN_XNUM then the sh_info field contains the real
     * number of segment entries.
     *
     * As we zero allocate the buffer we will only need to modify
     * sh_info for the PN_XNUM case.
     */
    if (s->phdr_num >= PN_XNUM) {
        prepare_elf_section_hdr_zero(s);
    }
    buff_hdr += sizeof_shdr;

    /* Add architecture defined section headers */
    if (s->dump_info.arch_sections_write_hdr_fn
        && s->shdr_num > 2) {
        buff_hdr += s->dump_info.arch_sections_write_hdr_fn(s, buff_hdr);

        if (s->shdr_num >= SHN_LORESERVE) {
            error_setg_errno(errp, EINVAL,
                             "dump: too many architecture defined sections");
            return false;
        }
    }

    /*
     * String table is the last section since strings are added via
     * arch_sections_write_hdr().
     */
    prepare_elf_section_hdr_string(s, buff_hdr);
    return true;
}

static void write_elf_section_headers(DumpState *s, Error **errp)
{
    size_t sizeof_shdr = dump_is_64bit(s) ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr);
    int ret;

    if (!prepare_elf_section_hdrs(s, errp)) {
        return;
    }

    ret = fd_write_vmcore(s->elf_section_hdrs, s->shdr_num * sizeof_shdr, s);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "dump: failed to write section headers");
    }

    g_free(s->elf_section_hdrs);
}

static void write_elf_sections(DumpState *s, Error **errp)
{
    int ret;

    if (s->elf_section_data_size) {
        /* Write architecture section data */
        ret = fd_write_vmcore(s->elf_section_data,
                              s->elf_section_data_size, s);
        if (ret < 0) {
            error_setg_errno(errp, -ret,
                             "dump: failed to write architecture section data");
            return;
        }
    }

    /* Write string table */
    ret = fd_write_vmcore(s->string_table_buf->data,
                          s->string_table_buf->len, s);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "dump: failed to write string table data");
    }
}

static void write_data(DumpState *s, void *buf, int length, Error **errp)
{
    int ret;

    ret = fd_write_vmcore(buf, length, s);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "dump: failed to save memory");
    } else {
        s->written_size += length;
    }
}

/* write the memory to vmcore. 1 page per I/O. */
static void write_memory(DumpState *s, GuestPhysBlock *block, ram_addr_t start,
                         int64_t size, Error **errp)
{
    ERRP_GUARD();
    int64_t i;

    for (i = 0; i < size / s->dump_info.page_size; i++) {
        write_data(s, block->host_addr + start + i * s->dump_info.page_size,
                   s->dump_info.page_size, errp);
        if (*errp) {
            return;
        }
    }

    if ((size % s->dump_info.page_size) != 0) {
        write_data(s, block->host_addr + start + i * s->dump_info.page_size,
                   size % s->dump_info.page_size, errp);
        if (*errp) {
            return;
        }
    }
}

/* get the memory's offset and size in the vmcore */
static void get_offset_range(hwaddr phys_addr,
                             ram_addr_t mapping_length,
                             DumpState *s,
                             hwaddr *p_offset,
                             hwaddr *p_filesz)
{
    GuestPhysBlock *block;
    hwaddr offset = s->memory_offset;
    int64_t size_in_block, start;

    /* When the memory is not stored into vmcore, offset will be -1 */
    *p_offset = -1;
    *p_filesz = 0;

    if (dump_has_filter(s)) {
        if (phys_addr < s->filter_area_begin ||
            phys_addr >= s->filter_area_begin + s->filter_area_length) {
            return;
        }
    }

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        if (dump_has_filter(s)) {
            if (!ranges_overlap(block->target_start,
                                block->target_end - block->target_start,
                                s->filter_area_begin,
                                s->filter_area_length)) {
                /* This block is out of the range */
                continue;
            }

            if (s->filter_area_begin <= block->target_start) {
                start = block->target_start;
            } else {
                start = s->filter_area_begin;
            }

            size_in_block = block->target_end - start;
            if (s->filter_area_begin + s->filter_area_length < block->target_end) {
                size_in_block -= block->target_end - (s->filter_area_begin + s->filter_area_length);
            }
        } else {
            start = block->target_start;
            size_in_block = block->target_end - block->target_start;
        }

        if (phys_addr >= start && phys_addr < start + size_in_block) {
            *p_offset = phys_addr - start + offset;

            /* The offset range mapped from the vmcore file must not spill over
             * the GuestPhysBlock, clamp it. The rest of the mapping will be
             * zero-filled in memory at load time; see
             * <http://refspecs.linuxbase.org/elf/gabi4+/ch5.pheader.html>.
             */
            *p_filesz = phys_addr + mapping_length <= start + size_in_block ?
                        mapping_length :
                        size_in_block - (phys_addr - start);
            return;
        }

        offset += size_in_block;
    }
}

static void write_elf_phdr_loads(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    hwaddr offset, filesz;
    MemoryMapping *memory_mapping;
    uint32_t phdr_index = 1;

    QTAILQ_FOREACH(memory_mapping, &s->list.head, next) {
        get_offset_range(memory_mapping->phys_addr,
                         memory_mapping->length,
                         s, &offset, &filesz);
        if (dump_is_64bit(s)) {
            write_elf64_load(s, memory_mapping, phdr_index++, offset,
                             filesz, errp);
        } else {
            write_elf32_load(s, memory_mapping, phdr_index++, offset,
                             filesz, errp);
        }

        if (*errp) {
            return;
        }

        if (phdr_index >= s->phdr_num) {
            break;
        }
    }
}

static void write_elf_notes(DumpState *s, Error **errp)
{
    if (dump_is_64bit(s)) {
        write_elf64_notes(fd_write_vmcore, s, errp);
    } else {
        write_elf32_notes(fd_write_vmcore, s, errp);
    }
}

/* write elf header, PT_NOTE and elf note to vmcore. */
static void dump_begin(DumpState *s, Error **errp)
{
    ERRP_GUARD();

    /*
     * the vmcore's format is:
     *   --------------
     *   |  elf header |
     *   --------------
     *   |  sctn_hdr   |
     *   --------------
     *   |  PT_NOTE    |
     *   --------------
     *   |  PT_LOAD    |
     *   --------------
     *   |  ......     |
     *   --------------
     *   |  PT_LOAD    |
     *   --------------
     *   |  elf note   |
     *   --------------
     *   |  memory     |
     *   --------------
     *
     * we only know where the memory is saved after we write elf note into
     * vmcore.
     */

    /* write elf header to vmcore */
    write_elf_header(s, errp);
    if (*errp) {
        return;
    }

    /* write section headers to vmcore */
    write_elf_section_headers(s, errp);
    if (*errp) {
        return;
    }

    /* write PT_NOTE to vmcore */
    write_elf_phdr_note(s, errp);
    if (*errp) {
        return;
    }

    /* write all PT_LOADs to vmcore */
    write_elf_phdr_loads(s, errp);
    if (*errp) {
        return;
    }

    /* write notes to vmcore */
    write_elf_notes(s, errp);
}

int64_t dump_filtered_memblock_size(GuestPhysBlock *block,
                                    int64_t filter_area_start,
                                    int64_t filter_area_length)
{
    int64_t size, left, right;

    /* No filter, return full size */
    if (!filter_area_length) {
        return block->target_end - block->target_start;
    }

    /* calculate the overlapped region. */
    left = MAX(filter_area_start, block->target_start);
    right = MIN(filter_area_start + filter_area_length, block->target_end);
    size = right - left;
    size = size > 0 ? size : 0;

    return size;
}

int64_t dump_filtered_memblock_start(GuestPhysBlock *block,
                                     int64_t filter_area_start,
                                     int64_t filter_area_length)
{
    if (filter_area_length) {
        /* return -1 if the block is not within filter area */
        if (!ranges_overlap(block->target_start,
                            block->target_end - block->target_start,
                            filter_area_start, filter_area_length)) {
            return -1;
        }

        if (filter_area_start > block->target_start) {
            return filter_area_start - block->target_start;
        }
    }

    return 0;
}

/* write all memory to vmcore */
static void dump_iterate(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    GuestPhysBlock *block;
    int64_t memblock_size, memblock_start;

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        memblock_start = dump_filtered_memblock_start(block, s->filter_area_begin, s->filter_area_length);
        if (memblock_start == -1) {
            continue;
        }

        memblock_size = dump_filtered_memblock_size(block, s->filter_area_begin, s->filter_area_length);

        /* Write the memory to file */
        write_memory(s, block, memblock_start, memblock_size, errp);
        if (*errp) {
            return;
        }
    }
}

static void dump_end(DumpState *s, Error **errp)
{
    int rc;

    if (s->elf_section_data_size) {
        s->elf_section_data = g_malloc0(s->elf_section_data_size);
    }

    /* Adds the architecture defined section data to s->elf_section_data  */
    if (s->dump_info.arch_sections_write_fn &&
        s->elf_section_data_size) {
        rc = s->dump_info.arch_sections_write_fn(s, s->elf_section_data);
        if (rc) {
            error_setg_errno(errp, rc,
                             "dump: failed to get arch section data");
            g_free(s->elf_section_data);
            return;
        }
    }

    /* write sections to vmcore */
    write_elf_sections(s, errp);
}

static void create_vmcore(DumpState *s, Error **errp)
{
    ERRP_GUARD();

    dump_begin(s, errp);
    if (*errp) {
        return;
    }

    /* Iterate over memory and dump it to file */
    dump_iterate(s, errp);
    if (*errp) {
        return;
    }

    /* Write the section data */
    dump_end(s, errp);
}

static int write_start_flat_header(DumpState *s)
{
    MakedumpfileHeader *mh;
    int ret = 0;

    if (s->kdump_raw) {
        return 0;
    }

    QEMU_BUILD_BUG_ON(sizeof *mh > MAX_SIZE_MDF_HEADER);
    mh = g_malloc0(MAX_SIZE_MDF_HEADER);

    memcpy(mh->signature, MAKEDUMPFILE_SIGNATURE,
           MIN(sizeof mh->signature, sizeof MAKEDUMPFILE_SIGNATURE));

    mh->type = cpu_to_be64(TYPE_FLAT_HEADER);
    mh->version = cpu_to_be64(VERSION_FLAT_HEADER);

    size_t written_size;
    written_size = qemu_write_full(s->fd, mh, MAX_SIZE_MDF_HEADER);
    if (written_size != MAX_SIZE_MDF_HEADER) {
        ret = -1;
    }

    g_free(mh);
    return ret;
}

static int write_end_flat_header(DumpState *s)
{
    MakedumpfileDataHeader mdh;

    if (s->kdump_raw) {
        return 0;
    }

    mdh.offset = END_FLAG_FLAT_HEADER;
    mdh.buf_size = END_FLAG_FLAT_HEADER;

    size_t written_size;
    written_size = qemu_write_full(s->fd, &mdh, sizeof(mdh));
    if (written_size != sizeof(mdh)) {
        return -1;
    }

    return 0;
}

static int write_buffer(DumpState *s, off_t offset, const void *buf, size_t size)
{
    size_t written_size;
    MakedumpfileDataHeader mdh;
    off_t seek_loc;

    if (s->kdump_raw) {
        seek_loc = lseek(s->fd, offset, SEEK_SET);
        if (seek_loc == (off_t) -1) {
            return -1;
        }
    } else {
        mdh.offset = cpu_to_be64(offset);
        mdh.buf_size = cpu_to_be64(size);

        written_size = qemu_write_full(s->fd, &mdh, sizeof(mdh));
        if (written_size != sizeof(mdh)) {
            return -1;
        }
    }

    written_size = qemu_write_full(s->fd, buf, size);
    if (written_size != size) {
        return -1;
    }

    return 0;
}

static int buf_write_note(const void *buf, size_t size, void *opaque)
{
    DumpState *s = opaque;

    /* note_buf is not enough */
    if (s->note_buf_offset + size > s->note_size) {
        return -1;
    }

    memcpy(s->note_buf + s->note_buf_offset, buf, size);

    s->note_buf_offset += size;

    return 0;
}

/*
 * This function retrieves various sizes from an elf header.
 *
 * @note has to be a valid ELF note. The return sizes are unmodified
 * (not padded or rounded up to be multiple of 4).
 */
static void get_note_sizes(DumpState *s, const void *note,
                           uint64_t *note_head_size,
                           uint64_t *name_size,
                           uint64_t *desc_size)
{
    uint64_t note_head_sz;
    uint64_t name_sz;
    uint64_t desc_sz;

    if (dump_is_64bit(s)) {
        const Elf64_Nhdr *hdr = note;
        note_head_sz = sizeof(Elf64_Nhdr);
        name_sz = cpu_to_dump64(s, hdr->n_namesz);
        desc_sz = cpu_to_dump64(s, hdr->n_descsz);
    } else {
        const Elf32_Nhdr *hdr = note;
        note_head_sz = sizeof(Elf32_Nhdr);
        name_sz = cpu_to_dump32(s, hdr->n_namesz);
        desc_sz = cpu_to_dump32(s, hdr->n_descsz);
    }

    if (note_head_size) {
        *note_head_size = note_head_sz;
    }
    if (name_size) {
        *name_size = name_sz;
    }
    if (desc_size) {
        *desc_size = desc_sz;
    }
}

static bool note_name_equal(DumpState *s,
                            const uint8_t *note, const char *name)
{
    int len = strlen(name) + 1;
    uint64_t head_size, name_size;

    get_note_sizes(s, note, &head_size, &name_size, NULL);
    head_size = ROUND_UP(head_size, 4);

    return name_size == len && memcmp(note + head_size, name, len) == 0;
}

/* write common header, sub header and elf note to vmcore */
static void create_header32(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    DiskDumpHeader32 *dh = NULL;
    KdumpSubHeader32 *kh = NULL;
    size_t size;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t status = 0;
    uint64_t offset_note;

    /* write common header, the version of kdump-compressed format is 6th */
    size = sizeof(DiskDumpHeader32);
    dh = g_malloc0(size);

    memcpy(dh->signature, KDUMP_SIGNATURE, SIG_LEN);
    dh->header_version = cpu_to_dump32(s, 6);
    block_size = s->dump_info.page_size;
    dh->block_size = cpu_to_dump32(s, block_size);
    sub_hdr_size = sizeof(struct KdumpSubHeader32) + s->note_size;
    sub_hdr_size = DIV_ROUND_UP(sub_hdr_size, block_size);
    dh->sub_hdr_size = cpu_to_dump32(s, sub_hdr_size);
    /* dh->max_mapnr may be truncated, full 64bit is in kh.max_mapnr_64 */
    dh->max_mapnr = cpu_to_dump32(s, MIN(s->max_mapnr, UINT_MAX));
    dh->nr_cpus = cpu_to_dump32(s, s->nr_cpus);
    bitmap_blocks = DIV_ROUND_UP(s->len_dump_bitmap, block_size) * 2;
    dh->bitmap_blocks = cpu_to_dump32(s, bitmap_blocks);
    strncpy(dh->utsname.machine, ELF_MACHINE_UNAME, sizeof(dh->utsname.machine));

    if (s->flag_compress & DUMP_DH_COMPRESSED_ZLIB) {
        status |= DUMP_DH_COMPRESSED_ZLIB;
    }
#ifdef CONFIG_LZO
    if (s->flag_compress & DUMP_DH_COMPRESSED_LZO) {
        status |= DUMP_DH_COMPRESSED_LZO;
    }
#endif
#ifdef CONFIG_SNAPPY
    if (s->flag_compress & DUMP_DH_COMPRESSED_SNAPPY) {
        status |= DUMP_DH_COMPRESSED_SNAPPY;
    }
#endif
    dh->status = cpu_to_dump32(s, status);

    if (write_buffer(s, 0, dh, size) < 0) {
        error_setg(errp, "dump: failed to write disk dump header");
        goto out;
    }

    /* write sub header */
    size = sizeof(KdumpSubHeader32);
    kh = g_malloc0(size);

    /* 64bit max_mapnr_64 */
    kh->max_mapnr_64 = cpu_to_dump64(s, s->max_mapnr);
    kh->phys_base = cpu_to_dump32(s, s->dump_info.phys_base);
    kh->dump_level = cpu_to_dump32(s, DUMP_LEVEL);

    offset_note = DISKDUMP_HEADER_BLOCKS * block_size + size;
    if (s->guest_note &&
        note_name_equal(s, s->guest_note, "VMCOREINFO")) {
        uint64_t hsize, name_size, size_vmcoreinfo_desc, offset_vmcoreinfo;

        get_note_sizes(s, s->guest_note,
                       &hsize, &name_size, &size_vmcoreinfo_desc);
        offset_vmcoreinfo = offset_note + s->note_size - s->guest_note_size +
            (DIV_ROUND_UP(hsize, 4) + DIV_ROUND_UP(name_size, 4)) * 4;
        kh->offset_vmcoreinfo = cpu_to_dump64(s, offset_vmcoreinfo);
        kh->size_vmcoreinfo = cpu_to_dump32(s, size_vmcoreinfo_desc);
    }

    kh->offset_note = cpu_to_dump64(s, offset_note);
    kh->note_size = cpu_to_dump32(s, s->note_size);

    if (write_buffer(s, DISKDUMP_HEADER_BLOCKS *
                     block_size, kh, size) < 0) {
        error_setg(errp, "dump: failed to write kdump sub header");
        goto out;
    }

    /* write note */
    s->note_buf = g_malloc0(s->note_size);
    s->note_buf_offset = 0;

    /* use s->note_buf to store notes temporarily */
    write_elf32_notes(buf_write_note, s, errp);
    if (*errp) {
        goto out;
    }
    if (write_buffer(s, offset_note, s->note_buf,
                     s->note_size) < 0) {
        error_setg(errp, "dump: failed to write notes");
        goto out;
    }

    /* get offset of dump_bitmap */
    s->offset_dump_bitmap = (DISKDUMP_HEADER_BLOCKS + sub_hdr_size) *
                             block_size;

    /* get offset of page */
    s->offset_page = (DISKDUMP_HEADER_BLOCKS + sub_hdr_size + bitmap_blocks) *
                     block_size;

out:
    g_free(dh);
    g_free(kh);
    g_free(s->note_buf);
}

/* write common header, sub header and elf note to vmcore */
static void create_header64(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    DiskDumpHeader64 *dh = NULL;
    KdumpSubHeader64 *kh = NULL;
    size_t size;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t status = 0;
    uint64_t offset_note;

    /* write common header, the version of kdump-compressed format is 6th */
    size = sizeof(DiskDumpHeader64);
    dh = g_malloc0(size);

    memcpy(dh->signature, KDUMP_SIGNATURE, SIG_LEN);
    dh->header_version = cpu_to_dump32(s, 6);
    block_size = s->dump_info.page_size;
    dh->block_size = cpu_to_dump32(s, block_size);
    sub_hdr_size = sizeof(struct KdumpSubHeader64) + s->note_size;
    sub_hdr_size = DIV_ROUND_UP(sub_hdr_size, block_size);
    dh->sub_hdr_size = cpu_to_dump32(s, sub_hdr_size);
    /* dh->max_mapnr may be truncated, full 64bit is in kh.max_mapnr_64 */
    dh->max_mapnr = cpu_to_dump32(s, MIN(s->max_mapnr, UINT_MAX));
    dh->nr_cpus = cpu_to_dump32(s, s->nr_cpus);
    bitmap_blocks = DIV_ROUND_UP(s->len_dump_bitmap, block_size) * 2;
    dh->bitmap_blocks = cpu_to_dump32(s, bitmap_blocks);
    strncpy(dh->utsname.machine, ELF_MACHINE_UNAME, sizeof(dh->utsname.machine));

    if (s->flag_compress & DUMP_DH_COMPRESSED_ZLIB) {
        status |= DUMP_DH_COMPRESSED_ZLIB;
    }
#ifdef CONFIG_LZO
    if (s->flag_compress & DUMP_DH_COMPRESSED_LZO) {
        status |= DUMP_DH_COMPRESSED_LZO;
    }
#endif
#ifdef CONFIG_SNAPPY
    if (s->flag_compress & DUMP_DH_COMPRESSED_SNAPPY) {
        status |= DUMP_DH_COMPRESSED_SNAPPY;
    }
#endif
    dh->status = cpu_to_dump32(s, status);

    if (write_buffer(s, 0, dh, size) < 0) {
        error_setg(errp, "dump: failed to write disk dump header");
        goto out;
    }

    /* write sub header */
    size = sizeof(KdumpSubHeader64);
    kh = g_malloc0(size);

    /* 64bit max_mapnr_64 */
    kh->max_mapnr_64 = cpu_to_dump64(s, s->max_mapnr);
    kh->phys_base = cpu_to_dump64(s, s->dump_info.phys_base);
    kh->dump_level = cpu_to_dump32(s, DUMP_LEVEL);

    offset_note = DISKDUMP_HEADER_BLOCKS * block_size + size;
    if (s->guest_note &&
        note_name_equal(s, s->guest_note, "VMCOREINFO")) {
        uint64_t hsize, name_size, size_vmcoreinfo_desc, offset_vmcoreinfo;

        get_note_sizes(s, s->guest_note,
                       &hsize, &name_size, &size_vmcoreinfo_desc);
        offset_vmcoreinfo = offset_note + s->note_size - s->guest_note_size +
            (DIV_ROUND_UP(hsize, 4) + DIV_ROUND_UP(name_size, 4)) * 4;
        kh->offset_vmcoreinfo = cpu_to_dump64(s, offset_vmcoreinfo);
        kh->size_vmcoreinfo = cpu_to_dump64(s, size_vmcoreinfo_desc);
    }

    kh->offset_note = cpu_to_dump64(s, offset_note);
    kh->note_size = cpu_to_dump64(s, s->note_size);

    if (write_buffer(s, DISKDUMP_HEADER_BLOCKS *
                     block_size, kh, size) < 0) {
        error_setg(errp, "dump: failed to write kdump sub header");
        goto out;
    }

    /* write note */
    s->note_buf = g_malloc0(s->note_size);
    s->note_buf_offset = 0;

    /* use s->note_buf to store notes temporarily */
    write_elf64_notes(buf_write_note, s, errp);
    if (*errp) {
        goto out;
    }

    if (write_buffer(s, offset_note, s->note_buf,
                     s->note_size) < 0) {
        error_setg(errp, "dump: failed to write notes");
        goto out;
    }

    /* get offset of dump_bitmap */
    s->offset_dump_bitmap = (DISKDUMP_HEADER_BLOCKS + sub_hdr_size) *
                             block_size;

    /* get offset of page */
    s->offset_page = (DISKDUMP_HEADER_BLOCKS + sub_hdr_size + bitmap_blocks) *
                     block_size;

out:
    g_free(dh);
    g_free(kh);
    g_free(s->note_buf);
}

static void write_dump_header(DumpState *s, Error **errp)
{
    if (dump_is_64bit(s)) {
        create_header64(s, errp);
    } else {
        create_header32(s, errp);
    }
}

static size_t dump_bitmap_get_bufsize(DumpState *s)
{
    return s->dump_info.page_size;
}

/*
 * set dump_bitmap sequencely. the bit before last_pfn is not allowed to be
 * rewritten, so if need to set the first bit, set last_pfn and pfn to 0.
 * set_dump_bitmap will always leave the recently set bit un-sync. And setting
 * (last bit + sizeof(buf) * 8) to 0 will do flushing the content in buf into
 * vmcore, ie. synchronizing un-sync bit into vmcore.
 */
static int set_dump_bitmap(uint64_t last_pfn, uint64_t pfn, bool value,
                           uint8_t *buf, DumpState *s)
{
    off_t old_offset, new_offset;
    off_t offset_bitmap1, offset_bitmap2;
    uint32_t byte, bit;
    size_t bitmap_bufsize = dump_bitmap_get_bufsize(s);
    size_t bits_per_buf = bitmap_bufsize * CHAR_BIT;

    /* should not set the previous place */
    assert(last_pfn <= pfn);

    /*
     * if the bit needed to be set is not cached in buf, flush the data in buf
     * to vmcore firstly.
     * making new_offset be bigger than old_offset can also sync remained data
     * into vmcore.
     */
    old_offset = bitmap_bufsize * (last_pfn / bits_per_buf);
    new_offset = bitmap_bufsize * (pfn / bits_per_buf);

    while (old_offset < new_offset) {
        /* calculate the offset and write dump_bitmap */
        offset_bitmap1 = s->offset_dump_bitmap + old_offset;
        if (write_buffer(s, offset_bitmap1, buf,
                         bitmap_bufsize) < 0) {
            return -1;
        }

        /* dump level 1 is chosen, so 1st and 2nd bitmap are same */
        offset_bitmap2 = s->offset_dump_bitmap + s->len_dump_bitmap +
                         old_offset;
        if (write_buffer(s, offset_bitmap2, buf,
                         bitmap_bufsize) < 0) {
            return -1;
        }

        memset(buf, 0, bitmap_bufsize);
        old_offset += bitmap_bufsize;
    }

    /* get the exact place of the bit in the buf, and set it */
    byte = (pfn % bits_per_buf) / CHAR_BIT;
    bit = (pfn % bits_per_buf) % CHAR_BIT;
    if (value) {
        buf[byte] |= 1u << bit;
    } else {
        buf[byte] &= ~(1u << bit);
    }

    return 0;
}

static uint64_t dump_paddr_to_pfn(DumpState *s, uint64_t addr)
{
    int target_page_shift = ctz32(s->dump_info.page_size);

    return (addr >> target_page_shift) - ARCH_PFN_OFFSET;
}

static uint64_t dump_pfn_to_paddr(DumpState *s, uint64_t pfn)
{
    int target_page_shift = ctz32(s->dump_info.page_size);

    return (pfn + ARCH_PFN_OFFSET) << target_page_shift;
}

/*
 * Return the page frame number and the page content in *bufptr. bufptr can be
 * NULL. If not NULL, *bufptr must contains a target page size of pre-allocated
 * memory. This is not necessarily the memory returned.
 */
static bool get_next_page(GuestPhysBlock **blockptr, uint64_t *pfnptr,
                          uint8_t **bufptr, DumpState *s)
{
    GuestPhysBlock *block = *blockptr;
    uint32_t page_size = s->dump_info.page_size;
    uint8_t *buf = NULL, *hbuf;
    hwaddr addr;

    /* block == NULL means the start of the iteration */
    if (!block) {
        block = QTAILQ_FIRST(&s->guest_phys_blocks.head);
        *blockptr = block;
        addr = block->target_start;
        *pfnptr = dump_paddr_to_pfn(s, addr);
    } else {
        *pfnptr += 1;
        addr = dump_pfn_to_paddr(s, *pfnptr);
    }
    assert(block != NULL);

    while (1) {
        if (addr >= block->target_start && addr < block->target_end) {
            size_t n = MIN(block->target_end - addr, page_size - addr % page_size);
            hbuf = block->host_addr + (addr - block->target_start);
            if (!buf) {
                if (n == page_size) {
                    /* this is a whole target page, go for it */
                    assert(addr % page_size == 0);
                    buf = hbuf;
                    break;
                } else if (bufptr) {
                    assert(*bufptr);
                    buf = *bufptr;
                    memset(buf, 0, page_size);
                } else {
                    return true;
                }
            }

            memcpy(buf + addr % page_size, hbuf, n);
            addr += n;
            if (addr % page_size == 0 || addr >= block->target_end) {
                /* we filled up the page or the current block is finished */
                break;
            }
        } else {
            /* the next page is in the next block */
            *blockptr = block = QTAILQ_NEXT(block, next);
            if (!block) {
                break;
            }

            addr = block->target_start;
            /* are we still in the same page? */
            if (dump_paddr_to_pfn(s, addr) != *pfnptr) {
                if (buf) {
                    /* no, but we already filled something earlier, return it */
                    break;
                } else {
                    /* else continue from there */
                    *pfnptr = dump_paddr_to_pfn(s, addr);
                }
            }
        }
    }

    if (bufptr) {
        *bufptr = buf;
    }

    return buf != NULL;
}

static void write_dump_bitmap(DumpState *s, Error **errp)
{
    int ret = 0;
    uint64_t last_pfn, pfn;
    void *dump_bitmap_buf;
    size_t num_dumpable;
    GuestPhysBlock *block_iter = NULL;
    size_t bitmap_bufsize = dump_bitmap_get_bufsize(s);
    size_t bits_per_buf = bitmap_bufsize * CHAR_BIT;

    /* dump_bitmap_buf is used to store dump_bitmap temporarily */
    dump_bitmap_buf = g_malloc0(bitmap_bufsize);

    num_dumpable = 0;
    last_pfn = 0;

    /*
     * exam memory page by page, and set the bit in dump_bitmap corresponded
     * to the existing page.
     */
    while (get_next_page(&block_iter, &pfn, NULL, s)) {
        ret = set_dump_bitmap(last_pfn, pfn, true, dump_bitmap_buf, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to set dump_bitmap");
            goto out;
        }

        last_pfn = pfn;
        num_dumpable++;
    }

    /*
     * set_dump_bitmap will always leave the recently set bit un-sync. Here we
     * set the remaining bits from last_pfn to the end of the bitmap buffer to
     * 0. With those set, the un-sync bit will be synchronized into the vmcore.
     */
    if (num_dumpable > 0) {
        ret = set_dump_bitmap(last_pfn, last_pfn + bits_per_buf, false,
                              dump_bitmap_buf, s);
        if (ret < 0) {
            error_setg(errp, "dump: failed to sync dump_bitmap");
            goto out;
        }
    }

    /* number of dumpable pages that will be dumped later */
    s->num_dumpable = num_dumpable;

out:
    g_free(dump_bitmap_buf);
}

static void prepare_data_cache(DataCache *data_cache, DumpState *s,
                               off_t offset)
{
    data_cache->state = s;
    data_cache->data_size = 0;
    data_cache->buf_size = 4 * dump_bitmap_get_bufsize(s);
    data_cache->buf = g_malloc0(data_cache->buf_size);
    data_cache->offset = offset;
}

static int write_cache(DataCache *dc, const void *buf, size_t size,
                       bool flag_sync)
{
    /*
     * dc->buf_size should not be less than size, otherwise dc will never be
     * enough
     */
    assert(size <= dc->buf_size);

    /*
     * if flag_sync is set, synchronize data in dc->buf into vmcore.
     * otherwise check if the space is enough for caching data in buf, if not,
     * write the data in dc->buf to dc->state->fd and reset dc->buf
     */
    if ((!flag_sync && dc->data_size + size > dc->buf_size) ||
        (flag_sync && dc->data_size > 0)) {
        if (write_buffer(dc->state, dc->offset, dc->buf, dc->data_size) < 0) {
            return -1;
        }

        dc->offset += dc->data_size;
        dc->data_size = 0;
    }

    if (!flag_sync) {
        memcpy(dc->buf + dc->data_size, buf, size);
        dc->data_size += size;
    }

    return 0;
}

static void free_data_cache(DataCache *data_cache)
{
    g_free(data_cache->buf);
}

static size_t get_len_buf_out(size_t page_size, uint32_t flag_compress)
{
    switch (flag_compress) {
    case DUMP_DH_COMPRESSED_ZLIB:
        return compressBound(page_size);

    case DUMP_DH_COMPRESSED_LZO:
        /*
         * LZO will expand incompressible data by a little amount. Please check
         * the following URL to see the expansion calculation:
         * http://www.oberhumer.com/opensource/lzo/lzofaq.php
         */
        return page_size + page_size / 16 + 64 + 3;

#ifdef CONFIG_SNAPPY
    case DUMP_DH_COMPRESSED_SNAPPY:
        return snappy_max_compressed_length(page_size);
#endif
    }
    return 0;
}

static void write_dump_pages(DumpState *s, Error **errp)
{
    int ret = 0;
    DataCache page_desc, page_data;
    size_t len_buf_out, size_out;
#ifdef CONFIG_LZO
    lzo_bytep wrkmem = NULL;
#endif
    uint8_t *buf_out = NULL;
    off_t offset_desc, offset_data;
    PageDescriptor pd, pd_zero;
    uint8_t *buf;
    GuestPhysBlock *block_iter = NULL;
    uint64_t pfn_iter;
    g_autofree uint8_t *page = NULL;

    /* get offset of page_desc and page_data in dump file */
    offset_desc = s->offset_page;
    offset_data = offset_desc + sizeof(PageDescriptor) * s->num_dumpable;

    prepare_data_cache(&page_desc, s, offset_desc);
    prepare_data_cache(&page_data, s, offset_data);

    /* prepare buffer to store compressed data */
    len_buf_out = get_len_buf_out(s->dump_info.page_size, s->flag_compress);
    assert(len_buf_out != 0);

#ifdef CONFIG_LZO
    wrkmem = g_malloc(LZO1X_1_MEM_COMPRESS);
#endif

    buf_out = g_malloc(len_buf_out);

    /*
     * init zero page's page_desc and page_data, because every zero page
     * uses the same page_data
     */
    pd_zero.size = cpu_to_dump32(s, s->dump_info.page_size);
    pd_zero.flags = cpu_to_dump32(s, 0);
    pd_zero.offset = cpu_to_dump64(s, offset_data);
    pd_zero.page_flags = cpu_to_dump64(s, 0);
    buf = g_malloc0(s->dump_info.page_size);
    ret = write_cache(&page_data, buf, s->dump_info.page_size, false);
    g_free(buf);
    if (ret < 0) {
        error_setg(errp, "dump: failed to write page data (zero page)");
        goto out;
    }

    offset_data += s->dump_info.page_size;
    page = g_malloc(s->dump_info.page_size);

    /*
     * dump memory to vmcore page by page. zero page will all be resided in the
     * first page of page section
     */
    for (buf = page; get_next_page(&block_iter, &pfn_iter, &buf, s); buf = page) {
        /* check zero page */
        if (buffer_is_zero(buf, s->dump_info.page_size)) {
            ret = write_cache(&page_desc, &pd_zero, sizeof(PageDescriptor),
                              false);
            if (ret < 0) {
                error_setg(errp, "dump: failed to write page desc");
                goto out;
            }
        } else {
            /*
             * not zero page, then:
             * 1. compress the page
             * 2. write the compressed page into the cache of page_data
             * 3. get page desc of the compressed page and write it into the
             *    cache of page_desc
             *
             * only one compression format will be used here, for
             * s->flag_compress is set. But when compression fails to work,
             * we fall back to save in plaintext.
             */
             size_out = len_buf_out;
             if ((s->flag_compress & DUMP_DH_COMPRESSED_ZLIB) &&
                    (compress2(buf_out, (uLongf *)&size_out, buf,
                               s->dump_info.page_size, Z_BEST_SPEED) == Z_OK) &&
                    (size_out < s->dump_info.page_size)) {
                pd.flags = cpu_to_dump32(s, DUMP_DH_COMPRESSED_ZLIB);
                pd.size  = cpu_to_dump32(s, size_out);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    error_setg(errp, "dump: failed to write page data");
                    goto out;
                }
#ifdef CONFIG_LZO
            } else if ((s->flag_compress & DUMP_DH_COMPRESSED_LZO) &&
                    (lzo1x_1_compress(buf, s->dump_info.page_size, buf_out,
                    (lzo_uint *)&size_out, wrkmem) == LZO_E_OK) &&
                    (size_out < s->dump_info.page_size)) {
                pd.flags = cpu_to_dump32(s, DUMP_DH_COMPRESSED_LZO);
                pd.size  = cpu_to_dump32(s, size_out);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    error_setg(errp, "dump: failed to write page data");
                    goto out;
                }
#endif
#ifdef CONFIG_SNAPPY
            } else if ((s->flag_compress & DUMP_DH_COMPRESSED_SNAPPY) &&
                    (snappy_compress((char *)buf, s->dump_info.page_size,
                    (char *)buf_out, &size_out) == SNAPPY_OK) &&
                    (size_out < s->dump_info.page_size)) {
                pd.flags = cpu_to_dump32(s, DUMP_DH_COMPRESSED_SNAPPY);
                pd.size  = cpu_to_dump32(s, size_out);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    error_setg(errp, "dump: failed to write page data");
                    goto out;
                }
#endif
            } else {
                /*
                 * fall back to save in plaintext, size_out should be
                 * assigned the target's page size
                 */
                pd.flags = cpu_to_dump32(s, 0);
                size_out = s->dump_info.page_size;
                pd.size = cpu_to_dump32(s, size_out);

                ret = write_cache(&page_data, buf,
                                  s->dump_info.page_size, false);
                if (ret < 0) {
                    error_setg(errp, "dump: failed to write page data");
                    goto out;
                }
            }

            /* get and write page desc here */
            pd.page_flags = cpu_to_dump64(s, 0);
            pd.offset = cpu_to_dump64(s, offset_data);
            offset_data += size_out;

            ret = write_cache(&page_desc, &pd, sizeof(PageDescriptor), false);
            if (ret < 0) {
                error_setg(errp, "dump: failed to write page desc");
                goto out;
            }
        }
        s->written_size += s->dump_info.page_size;
    }

    ret = write_cache(&page_desc, NULL, 0, true);
    if (ret < 0) {
        error_setg(errp, "dump: failed to sync cache for page_desc");
        goto out;
    }
    ret = write_cache(&page_data, NULL, 0, true);
    if (ret < 0) {
        error_setg(errp, "dump: failed to sync cache for page_data");
        goto out;
    }

out:
    free_data_cache(&page_desc);
    free_data_cache(&page_data);

#ifdef CONFIG_LZO
    g_free(wrkmem);
#endif

    g_free(buf_out);
}

static void create_kdump_vmcore(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    int ret;

    /*
     * the kdump-compressed format is:
     *                                               File offset
     *  +------------------------------------------+ 0x0
     *  |    main header (struct disk_dump_header) |
     *  |------------------------------------------+ block 1
     *  |    sub header (struct kdump_sub_header)  |
     *  |------------------------------------------+ block 2
     *  |            1st-dump_bitmap               |
     *  |------------------------------------------+ block 2 + X blocks
     *  |            2nd-dump_bitmap               | (aligned by block)
     *  |------------------------------------------+ block 2 + 2 * X blocks
     *  |  page desc for pfn 0 (struct page_desc)  | (aligned by block)
     *  |  page desc for pfn 1 (struct page_desc)  |
     *  |                    :                     |
     *  |------------------------------------------| (not aligned by block)
     *  |         page data (pfn 0)                |
     *  |         page data (pfn 1)                |
     *  |                    :                     |
     *  +------------------------------------------+
     */

    ret = write_start_flat_header(s);
    if (ret < 0) {
        error_setg(errp, "dump: failed to write start flat header");
        return;
    }

    write_dump_header(s, errp);
    if (*errp) {
        return;
    }

    write_dump_bitmap(s, errp);
    if (*errp) {
        return;
    }

    write_dump_pages(s, errp);
    if (*errp) {
        return;
    }

    ret = write_end_flat_header(s);
    if (ret < 0) {
        error_setg(errp, "dump: failed to write end flat header");
        return;
    }
}

static void get_max_mapnr(DumpState *s)
{
    GuestPhysBlock *last_block;

    last_block = QTAILQ_LAST(&s->guest_phys_blocks.head);
    s->max_mapnr = dump_paddr_to_pfn(s, last_block->target_end);
}

static DumpState dump_state_global = { .status = DUMP_STATUS_NONE };

static void dump_state_prepare(DumpState *s)
{
    /* zero the struct, setting status to active */
    *s = (DumpState) { .status = DUMP_STATUS_ACTIVE };
}

bool qemu_system_dump_in_progress(void)
{
    DumpState *state = &dump_state_global;
    return (qatomic_read(&state->status) == DUMP_STATUS_ACTIVE);
}

/*
 * calculate total size of memory to be dumped (taking filter into
 * account.)
 */
static int64_t dump_calculate_size(DumpState *s)
{
    GuestPhysBlock *block;
    int64_t total = 0;

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        total += dump_filtered_memblock_size(block,
                                             s->filter_area_begin,
                                             s->filter_area_length);
    }

    return total;
}

static void vmcoreinfo_update_phys_base(DumpState *s)
{
    uint64_t size, note_head_size, name_size, phys_base;
    char **lines;
    uint8_t *vmci;
    size_t i;

    if (!note_name_equal(s, s->guest_note, "VMCOREINFO")) {
        return;
    }

    get_note_sizes(s, s->guest_note, &note_head_size, &name_size, &size);
    note_head_size = ROUND_UP(note_head_size, 4);

    vmci = s->guest_note + note_head_size + ROUND_UP(name_size, 4);
    *(vmci + size) = '\0';

    lines = g_strsplit((char *)vmci, "\n", -1);
    for (i = 0; lines[i]; i++) {
        const char *prefix = NULL;

        if (s->dump_info.d_machine == EM_X86_64) {
            prefix = "NUMBER(phys_base)=";
        } else if (s->dump_info.d_machine == EM_AARCH64) {
            prefix = "NUMBER(PHYS_OFFSET)=";
        }

        if (prefix && g_str_has_prefix(lines[i], prefix)) {
            if (qemu_strtou64(lines[i] + strlen(prefix), NULL, 16,
                              &phys_base) < 0) {
                warn_report("Failed to read %s", prefix);
            } else {
                s->dump_info.phys_base = phys_base;
            }
            break;
        }
    }

    g_strfreev(lines);
}

static void dump_init(DumpState *s, int fd, bool has_format,
                      DumpGuestMemoryFormat format, bool paging, bool has_filter,
                      int64_t begin, int64_t length, bool kdump_raw,
                      Error **errp)
{
    ERRP_GUARD();
    VMCoreInfoState *vmci = vmcoreinfo_find();
    CPUState *cpu;
    int nr_cpus;
    int ret;

    s->has_format = has_format;
    s->format = format;
    s->written_size = 0;
    s->kdump_raw = kdump_raw;

    /* kdump-compressed is conflict with paging and filter */
    if (has_format && format != DUMP_GUEST_MEMORY_FORMAT_ELF) {
        assert(!paging && !has_filter);
    }

    if (runstate_is_running()) {
        vm_stop(RUN_STATE_SAVE_VM);
        s->resume = true;
    } else {
        s->resume = false;
    }

    /* If we use KVM, we should synchronize the registers before we get dump
     * info or physmap info.
     */
    cpu_synchronize_all_states();
    nr_cpus = 0;
    CPU_FOREACH(cpu) {
        nr_cpus++;
    }

    s->fd = fd;
    if (has_filter && !length) {
        error_setg(errp, "parameter 'length' expects a non-zero size");
        goto cleanup;
    }
    s->filter_area_begin = begin;
    s->filter_area_length = length;

    /* First index is 0, it's the special null name */
    s->string_table_buf = g_array_new(FALSE, TRUE, 1);
    /*
     * Allocate the null name, due to the clearing option set to true
     * it will be 0.
     */
    g_array_set_size(s->string_table_buf, 1);

    memory_mapping_list_init(&s->list);

    guest_phys_blocks_init(&s->guest_phys_blocks);
    guest_phys_blocks_append(&s->guest_phys_blocks);
    s->total_size = dump_calculate_size(s);
#ifdef DEBUG_DUMP_GUEST_MEMORY
    fprintf(stderr, "DUMP: total memory to dump: %lu\n", s->total_size);
#endif

    /* it does not make sense to dump non-existent memory */
    if (!s->total_size) {
        error_setg(errp, "dump: no guest memory to dump");
        goto cleanup;
    }

    /* get dump info: endian, class and architecture.
     * If the target architecture is not supported, cpu_get_dump_info() will
     * return -1.
     */
    ret = cpu_get_dump_info(&s->dump_info, &s->guest_phys_blocks);
    if (ret < 0) {
        error_setg(errp,
                   "dumping guest memory is not supported on this target");
        goto cleanup;
    }

    if (!s->dump_info.page_size) {
        s->dump_info.page_size = qemu_target_page_size();
    }

    s->note_size = cpu_get_note_size(s->dump_info.d_class,
                                     s->dump_info.d_machine, nr_cpus);
    assert(s->note_size >= 0);

    /*
     * The goal of this block is to (a) update the previously guessed
     * phys_base, (b) copy the guest note out of the guest.
     * Failure to do so is not fatal for dumping.
     */
    if (vmci) {
        uint64_t addr, note_head_size, name_size, desc_size;
        uint32_t size;
        uint16_t guest_format;

        note_head_size = dump_is_64bit(s) ?
            sizeof(Elf64_Nhdr) : sizeof(Elf32_Nhdr);

        guest_format = le16_to_cpu(vmci->vmcoreinfo.guest_format);
        size = le32_to_cpu(vmci->vmcoreinfo.size);
        addr = le64_to_cpu(vmci->vmcoreinfo.paddr);
        if (!vmci->has_vmcoreinfo) {
            warn_report("guest note is not present");
        } else if (size < note_head_size || size > MAX_GUEST_NOTE_SIZE) {
            warn_report("guest note size is invalid: %" PRIu32, size);
        } else if (guest_format != FW_CFG_VMCOREINFO_FORMAT_ELF) {
            warn_report("guest note format is unsupported: %" PRIu16, guest_format);
        } else {
            s->guest_note = g_malloc(size + 1); /* +1 for adding \0 */
            cpu_physical_memory_read(addr, s->guest_note, size);

            get_note_sizes(s, s->guest_note, NULL, &name_size, &desc_size);
            s->guest_note_size = ELF_NOTE_SIZE(note_head_size, name_size,
                                               desc_size);
            if (name_size > MAX_GUEST_NOTE_SIZE ||
                desc_size > MAX_GUEST_NOTE_SIZE ||
                s->guest_note_size > size) {
                warn_report("Invalid guest note header");
                g_free(s->guest_note);
                s->guest_note = NULL;
            } else {
                vmcoreinfo_update_phys_base(s);
                s->note_size += s->guest_note_size;
            }
        }
    }

    /* get memory mapping */
    if (paging) {
        qemu_get_guest_memory_mapping(&s->list, &s->guest_phys_blocks, errp);
        if (*errp) {
            goto cleanup;
        }
    } else {
        qemu_get_guest_simple_memory_mapping(&s->list, &s->guest_phys_blocks);
    }

    s->nr_cpus = nr_cpus;

    get_max_mapnr(s);

    uint64_t tmp;
    tmp = DIV_ROUND_UP(DIV_ROUND_UP(s->max_mapnr, CHAR_BIT),
                       s->dump_info.page_size);
    s->len_dump_bitmap = tmp * s->dump_info.page_size;

    /* init for kdump-compressed format */
    if (has_format && format != DUMP_GUEST_MEMORY_FORMAT_ELF) {
        switch (format) {
        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB:
            s->flag_compress = DUMP_DH_COMPRESSED_ZLIB;
            break;

        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO:
#ifdef CONFIG_LZO
            if (lzo_init() != LZO_E_OK) {
                error_setg(errp, "failed to initialize the LZO library");
                goto cleanup;
            }
#endif
            s->flag_compress = DUMP_DH_COMPRESSED_LZO;
            break;

        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY:
            s->flag_compress = DUMP_DH_COMPRESSED_SNAPPY;
            break;

        default:
            s->flag_compress = 0;
        }

        return;
    }

    if (dump_has_filter(s)) {
        memory_mapping_filter(&s->list, s->filter_area_begin, s->filter_area_length);
    }

    /*
     * The first section header is always a special one in which most
     * fields are 0. The section header string table is also always
     * set.
     */
    s->shdr_num = 2;

    /*
     * Adds the number of architecture sections to shdr_num and sets
     * elf_section_data_size so we know the offsets and sizes of all
     * parts.
     */
    if (s->dump_info.arch_sections_add_fn) {
        s->dump_info.arch_sections_add_fn(s);
    }

    /*
     * calculate shdr_num so we know the offsets and sizes of all
     * parts.
     * Calculate phdr_num
     *
     * The absolute maximum amount of phdrs is UINT32_MAX - 1 as
     * sh_info is 32 bit. There's special handling once we go over
     * UINT16_MAX - 1 but that is handled in the ehdr and section
     * code.
     */
    s->phdr_num = 1; /* Reserve PT_NOTE */
    if (s->list.num <= UINT32_MAX - 1) {
        s->phdr_num += s->list.num;
    } else {
        s->phdr_num = UINT32_MAX;
    }

    /*
     * Now that the number of section and program headers is known we
     * can calculate the offsets of the headers and data.
     */
    if (dump_is_64bit(s)) {
        s->shdr_offset = sizeof(Elf64_Ehdr);
        s->phdr_offset = s->shdr_offset + sizeof(Elf64_Shdr) * s->shdr_num;
        s->note_offset = s->phdr_offset + sizeof(Elf64_Phdr) * s->phdr_num;
    } else {
        s->shdr_offset = sizeof(Elf32_Ehdr);
        s->phdr_offset = s->shdr_offset + sizeof(Elf32_Shdr) * s->shdr_num;
        s->note_offset = s->phdr_offset + sizeof(Elf32_Phdr) * s->phdr_num;
    }
    s->memory_offset = s->note_offset + s->note_size;
    s->section_offset = s->memory_offset + s->total_size;

    return;

cleanup:
    dump_cleanup(s);
}

/* this operation might be time consuming. */
static void dump_process(DumpState *s, Error **errp)
{
    ERRP_GUARD();
    DumpQueryResult *result = NULL;

    if (s->has_format && s->format == DUMP_GUEST_MEMORY_FORMAT_WIN_DMP) {
        create_win_dump(s, errp);
    } else if (s->has_format && s->format != DUMP_GUEST_MEMORY_FORMAT_ELF) {
        create_kdump_vmcore(s, errp);
    } else {
        create_vmcore(s, errp);
    }

    /* make sure status is written after written_size updates */
    smp_wmb();
    qatomic_set(&s->status,
               (*errp ? DUMP_STATUS_FAILED : DUMP_STATUS_COMPLETED));

    /* send DUMP_COMPLETED message (unconditionally) */
    result = qmp_query_dump(NULL);
    /* should never fail */
    assert(result);
    qapi_event_send_dump_completed(result,
                                   *errp ? error_get_pretty(*errp) : NULL);
    qapi_free_DumpQueryResult(result);

    dump_cleanup(s);
}

static void *dump_thread(void *data)
{
    DumpState *s = (DumpState *)data;
    dump_process(s, NULL);
    return NULL;
}

DumpQueryResult *qmp_query_dump(Error **errp)
{
    DumpQueryResult *result = g_new(DumpQueryResult, 1);
    DumpState *state = &dump_state_global;
    result->status = qatomic_read(&state->status);
    /* make sure we are reading status and written_size in order */
    smp_rmb();
    result->completed = state->written_size;
    result->total = state->total_size;
    return result;
}

void qmp_dump_guest_memory(bool paging, const char *protocol,
                           bool has_detach, bool detach,
                           bool has_begin, int64_t begin,
                           bool has_length, int64_t length,
                           bool has_format, DumpGuestMemoryFormat format,
                           Error **errp)
{
    ERRP_GUARD();
    const char *p;
    int fd;
    DumpState *s;
    bool detach_p = false;
    bool kdump_raw = false;

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        error_setg(errp, "Dump not allowed during incoming migration.");
        return;
    }

    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (qemu_system_dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    /*
     * externally, we represent kdump-raw-* as separate formats, but internally
     * they are handled the same, except for the "raw" flag
     */
    if (has_format) {
        switch (format) {
        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_ZLIB:
            format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB;
            kdump_raw = true;
            break;
        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_LZO:
            format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO;
            kdump_raw = true;
            break;
        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_SNAPPY:
            format = DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY;
            kdump_raw = true;
            break;
        default:
            break;
        }
    }

    /*
     * kdump-compressed format need the whole memory dumped, so paging or
     * filter is not supported here.
     */
    if ((has_format && format != DUMP_GUEST_MEMORY_FORMAT_ELF) &&
        (paging || has_begin || has_length)) {
        error_setg(errp, "kdump-compressed format doesn't support paging or "
                         "filter");
        return;
    }
    if (has_begin && !has_length) {
        error_setg(errp, QERR_MISSING_PARAMETER, "length");
        return;
    }
    if (!has_begin && has_length) {
        error_setg(errp, QERR_MISSING_PARAMETER, "begin");
        return;
    }
    if (has_detach) {
        detach_p = detach;
    }

    /* check whether lzo/snappy is supported */
#ifndef CONFIG_LZO
    if (has_format && format == DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO) {
        error_setg(errp, "kdump-lzo is not available now");
        return;
    }
#endif

#ifndef CONFIG_SNAPPY
    if (has_format && format == DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY) {
        error_setg(errp, "kdump-snappy is not available now");
        return;
    }
#endif

    if (has_format && format == DUMP_GUEST_MEMORY_FORMAT_WIN_DMP
        && !win_dump_available(errp)) {
        return;
    }

    if (strstart(protocol, "fd:", &p)) {
        fd = monitor_get_fd(monitor_cur(), p, errp);
        if (fd == -1) {
            return;
        }
    } else if  (strstart(protocol, "file:", &p)) {
        fd = qemu_create(p, O_WRONLY | O_TRUNC | O_BINARY, S_IRUSR, errp);
        if (fd < 0) {
            return;
        }
    } else {
        error_setg(errp,
                   "parameter 'protocol' must start with 'file:' or 'fd:'");
        return;
    }
    if (kdump_raw && lseek(fd, 0, SEEK_CUR) == (off_t) -1) {
        close(fd);
        error_setg(errp, "kdump-raw formats require a seekable file");
        return;
    }

    if (!dump_migration_blocker) {
        error_setg(&dump_migration_blocker,
                   "Live migration disabled: dump-guest-memory in progress");
    }

    /*
     * Allows even for -only-migratable, but forbid migration during the
     * process of dump guest memory.
     */
    if (migrate_add_blocker_internal(&dump_migration_blocker, errp)) {
        /* Remember to release the fd before passing it over to dump state */
        close(fd);
        return;
    }

    s = &dump_state_global;
    dump_state_prepare(s);

    dump_init(s, fd, has_format, format, paging, has_begin,
              begin, length, kdump_raw, errp);
    if (*errp) {
        qatomic_set(&s->status, DUMP_STATUS_FAILED);
        return;
    }

    if (detach_p) {
        /* detached dump */
        s->detached = true;
        qemu_thread_create(&s->dump_thread, "dump_thread", dump_thread,
                           s, QEMU_THREAD_DETACHED);
    } else {
        /* sync dump */
        dump_process(s, errp);
    }
}

DumpGuestMemoryCapability *qmp_query_dump_guest_memory_capability(Error **errp)
{
    DumpGuestMemoryCapability *cap =
                                  g_new0(DumpGuestMemoryCapability, 1);
    DumpGuestMemoryFormatList **tail = &cap->formats;

    /* elf is always available */
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_ELF);

    /* kdump-zlib is always available */
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB);
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_ZLIB);

    /* add new item if kdump-lzo is available */
#ifdef CONFIG_LZO
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO);
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_LZO);
#endif

    /* add new item if kdump-snappy is available */
#ifdef CONFIG_SNAPPY
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY);
    QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_KDUMP_RAW_SNAPPY);
#endif

    if (win_dump_available(NULL)) {
        QAPI_LIST_APPEND(tail, DUMP_GUEST_MEMORY_FORMAT_WIN_DMP);
    }

    return cap;
}
