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

#include "qemu-common.h"
#include "elf.h"
#include "cpu.h"
#include "exec/cpu-all.h"
#include "exec/hwaddr.h"
#include "monitor/monitor.h"
#include "sysemu/kvm.h"
#include "sysemu/dump.h"
#include "sysemu/sysemu.h"
#include "sysemu/memory_mapping.h"
#include "sysemu/cpus.h"
#include "qapi/error.h"
#include "qmp-commands.h"

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

static uint16_t cpu_convert_to_target16(uint16_t val, int endian)
{
    if (endian == ELFDATA2LSB) {
        val = cpu_to_le16(val);
    } else {
        val = cpu_to_be16(val);
    }

    return val;
}

static uint32_t cpu_convert_to_target32(uint32_t val, int endian)
{
    if (endian == ELFDATA2LSB) {
        val = cpu_to_le32(val);
    } else {
        val = cpu_to_be32(val);
    }

    return val;
}

static uint64_t cpu_convert_to_target64(uint64_t val, int endian)
{
    if (endian == ELFDATA2LSB) {
        val = cpu_to_le64(val);
    } else {
        val = cpu_to_be64(val);
    }

    return val;
}

typedef struct DumpState {
    GuestPhysBlockList guest_phys_blocks;
    ArchDumpInfo dump_info;
    MemoryMappingList list;
    uint16_t phdr_num;
    uint32_t sh_info;
    bool have_section;
    bool resume;
    ssize_t note_size;
    hwaddr memory_offset;
    int fd;

    GuestPhysBlock *next_block;
    ram_addr_t start;
    bool has_filter;
    int64_t begin;
    int64_t length;

    uint8_t *note_buf;          /* buffer for notes */
    size_t note_buf_offset;     /* the writing place in note_buf */
    uint32_t nr_cpus;           /* number of guest's cpu */
    size_t page_size;           /* guest's page size */
    uint32_t page_shift;        /* guest's page shift */
    uint64_t max_mapnr;         /* the biggest guest's phys-mem's number */
    size_t len_dump_bitmap;     /* the size of the place used to store
                                   dump_bitmap in vmcore */
    off_t offset_dump_bitmap;   /* offset of dump_bitmap part in vmcore */
    off_t offset_page;          /* offset of page part in vmcore */
    size_t num_dumpable;        /* number of page that can be dumped */
    uint32_t flag_compress;     /* indicate the compression format */
} DumpState;

static int dump_cleanup(DumpState *s)
{
    int ret = 0;

    guest_phys_blocks_free(&s->guest_phys_blocks);
    memory_mapping_list_free(&s->list);
    if (s->fd != -1) {
        close(s->fd);
    }
    if (s->resume) {
        vm_start();
    }

    return ret;
}

static void dump_error(DumpState *s, const char *reason)
{
    dump_cleanup(s);
}

static int fd_write_vmcore(const void *buf, size_t size, void *opaque)
{
    DumpState *s = opaque;
    size_t written_size;

    written_size = qemu_write_full(s->fd, buf, size);
    if (written_size != size) {
        return -1;
    }

    return 0;
}

static int write_elf64_header(DumpState *s)
{
    Elf64_Ehdr elf_header;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&elf_header, 0, sizeof(Elf64_Ehdr));
    memcpy(&elf_header, ELFMAG, SELFMAG);
    elf_header.e_ident[EI_CLASS] = ELFCLASS64;
    elf_header.e_ident[EI_DATA] = s->dump_info.d_endian;
    elf_header.e_ident[EI_VERSION] = EV_CURRENT;
    elf_header.e_type = cpu_convert_to_target16(ET_CORE, endian);
    elf_header.e_machine = cpu_convert_to_target16(s->dump_info.d_machine,
                                                   endian);
    elf_header.e_version = cpu_convert_to_target32(EV_CURRENT, endian);
    elf_header.e_ehsize = cpu_convert_to_target16(sizeof(elf_header), endian);
    elf_header.e_phoff = cpu_convert_to_target64(sizeof(Elf64_Ehdr), endian);
    elf_header.e_phentsize = cpu_convert_to_target16(sizeof(Elf64_Phdr),
                                                     endian);
    elf_header.e_phnum = cpu_convert_to_target16(s->phdr_num, endian);
    if (s->have_section) {
        uint64_t shoff = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) * s->sh_info;

        elf_header.e_shoff = cpu_convert_to_target64(shoff, endian);
        elf_header.e_shentsize = cpu_convert_to_target16(sizeof(Elf64_Shdr),
                                                         endian);
        elf_header.e_shnum = cpu_convert_to_target16(1, endian);
    }

    ret = fd_write_vmcore(&elf_header, sizeof(elf_header), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write elf header.\n");
        return -1;
    }

    return 0;
}

static int write_elf32_header(DumpState *s)
{
    Elf32_Ehdr elf_header;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&elf_header, 0, sizeof(Elf32_Ehdr));
    memcpy(&elf_header, ELFMAG, SELFMAG);
    elf_header.e_ident[EI_CLASS] = ELFCLASS32;
    elf_header.e_ident[EI_DATA] = endian;
    elf_header.e_ident[EI_VERSION] = EV_CURRENT;
    elf_header.e_type = cpu_convert_to_target16(ET_CORE, endian);
    elf_header.e_machine = cpu_convert_to_target16(s->dump_info.d_machine,
                                                   endian);
    elf_header.e_version = cpu_convert_to_target32(EV_CURRENT, endian);
    elf_header.e_ehsize = cpu_convert_to_target16(sizeof(elf_header), endian);
    elf_header.e_phoff = cpu_convert_to_target32(sizeof(Elf32_Ehdr), endian);
    elf_header.e_phentsize = cpu_convert_to_target16(sizeof(Elf32_Phdr),
                                                     endian);
    elf_header.e_phnum = cpu_convert_to_target16(s->phdr_num, endian);
    if (s->have_section) {
        uint32_t shoff = sizeof(Elf32_Ehdr) + sizeof(Elf32_Phdr) * s->sh_info;

        elf_header.e_shoff = cpu_convert_to_target32(shoff, endian);
        elf_header.e_shentsize = cpu_convert_to_target16(sizeof(Elf32_Shdr),
                                                         endian);
        elf_header.e_shnum = cpu_convert_to_target16(1, endian);
    }

    ret = fd_write_vmcore(&elf_header, sizeof(elf_header), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write elf header.\n");
        return -1;
    }

    return 0;
}

static int write_elf64_load(DumpState *s, MemoryMapping *memory_mapping,
                            int phdr_index, hwaddr offset,
                            hwaddr filesz)
{
    Elf64_Phdr phdr;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&phdr, 0, sizeof(Elf64_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_LOAD, endian);
    phdr.p_offset = cpu_convert_to_target64(offset, endian);
    phdr.p_paddr = cpu_convert_to_target64(memory_mapping->phys_addr, endian);
    phdr.p_filesz = cpu_convert_to_target64(filesz, endian);
    phdr.p_memsz = cpu_convert_to_target64(memory_mapping->length, endian);
    phdr.p_vaddr = cpu_convert_to_target64(memory_mapping->virt_addr, endian);

    assert(memory_mapping->length >= filesz);

    ret = fd_write_vmcore(&phdr, sizeof(Elf64_Phdr), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write program header table.\n");
        return -1;
    }

    return 0;
}

static int write_elf32_load(DumpState *s, MemoryMapping *memory_mapping,
                            int phdr_index, hwaddr offset,
                            hwaddr filesz)
{
    Elf32_Phdr phdr;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&phdr, 0, sizeof(Elf32_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_LOAD, endian);
    phdr.p_offset = cpu_convert_to_target32(offset, endian);
    phdr.p_paddr = cpu_convert_to_target32(memory_mapping->phys_addr, endian);
    phdr.p_filesz = cpu_convert_to_target32(filesz, endian);
    phdr.p_memsz = cpu_convert_to_target32(memory_mapping->length, endian);
    phdr.p_vaddr = cpu_convert_to_target32(memory_mapping->virt_addr, endian);

    assert(memory_mapping->length >= filesz);

    ret = fd_write_vmcore(&phdr, sizeof(Elf32_Phdr), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write program header table.\n");
        return -1;
    }

    return 0;
}

static int write_elf64_note(DumpState *s)
{
    Elf64_Phdr phdr;
    int endian = s->dump_info.d_endian;
    hwaddr begin = s->memory_offset - s->note_size;
    int ret;

    memset(&phdr, 0, sizeof(Elf64_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_NOTE, endian);
    phdr.p_offset = cpu_convert_to_target64(begin, endian);
    phdr.p_paddr = 0;
    phdr.p_filesz = cpu_convert_to_target64(s->note_size, endian);
    phdr.p_memsz = cpu_convert_to_target64(s->note_size, endian);
    phdr.p_vaddr = 0;

    ret = fd_write_vmcore(&phdr, sizeof(Elf64_Phdr), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write program header table.\n");
        return -1;
    }

    return 0;
}

static inline int cpu_index(CPUState *cpu)
{
    return cpu->cpu_index + 1;
}

static int write_elf64_notes(WriteCoreDumpFunction f, DumpState *s)
{
    CPUState *cpu;
    int ret;
    int id;

    CPU_FOREACH(cpu) {
        id = cpu_index(cpu);
        ret = cpu_write_elf64_note(f, cpu, id, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write elf notes.\n");
            return -1;
        }
    }

    CPU_FOREACH(cpu) {
        ret = cpu_write_elf64_qemunote(f, cpu, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write CPU status.\n");
            return -1;
        }
    }

    return 0;
}

static int write_elf32_note(DumpState *s)
{
    hwaddr begin = s->memory_offset - s->note_size;
    Elf32_Phdr phdr;
    int endian = s->dump_info.d_endian;
    int ret;

    memset(&phdr, 0, sizeof(Elf32_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_NOTE, endian);
    phdr.p_offset = cpu_convert_to_target32(begin, endian);
    phdr.p_paddr = 0;
    phdr.p_filesz = cpu_convert_to_target32(s->note_size, endian);
    phdr.p_memsz = cpu_convert_to_target32(s->note_size, endian);
    phdr.p_vaddr = 0;

    ret = fd_write_vmcore(&phdr, sizeof(Elf32_Phdr), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write program header table.\n");
        return -1;
    }

    return 0;
}

static int write_elf32_notes(WriteCoreDumpFunction f, DumpState *s)
{
    CPUState *cpu;
    int ret;
    int id;

    CPU_FOREACH(cpu) {
        id = cpu_index(cpu);
        ret = cpu_write_elf32_note(f, cpu, id, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write elf notes.\n");
            return -1;
        }
    }

    CPU_FOREACH(cpu) {
        ret = cpu_write_elf32_qemunote(f, cpu, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write CPU status.\n");
            return -1;
        }
    }

    return 0;
}

static int write_elf_section(DumpState *s, int type)
{
    Elf32_Shdr shdr32;
    Elf64_Shdr shdr64;
    int endian = s->dump_info.d_endian;
    int shdr_size;
    void *shdr;
    int ret;

    if (type == 0) {
        shdr_size = sizeof(Elf32_Shdr);
        memset(&shdr32, 0, shdr_size);
        shdr32.sh_info = cpu_convert_to_target32(s->sh_info, endian);
        shdr = &shdr32;
    } else {
        shdr_size = sizeof(Elf64_Shdr);
        memset(&shdr64, 0, shdr_size);
        shdr64.sh_info = cpu_convert_to_target32(s->sh_info, endian);
        shdr = &shdr64;
    }

    ret = fd_write_vmcore(&shdr, shdr_size, s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write section header table.\n");
        return -1;
    }

    return 0;
}

static int write_data(DumpState *s, void *buf, int length)
{
    int ret;

    ret = fd_write_vmcore(buf, length, s);
    if (ret < 0) {
        dump_error(s, "dump: failed to save memory.\n");
        return -1;
    }

    return 0;
}

/* write the memroy to vmcore. 1 page per I/O. */
static int write_memory(DumpState *s, GuestPhysBlock *block, ram_addr_t start,
                        int64_t size)
{
    int64_t i;
    int ret;

    for (i = 0; i < size / TARGET_PAGE_SIZE; i++) {
        ret = write_data(s, block->host_addr + start + i * TARGET_PAGE_SIZE,
                         TARGET_PAGE_SIZE);
        if (ret < 0) {
            return ret;
        }
    }

    if ((size % TARGET_PAGE_SIZE) != 0) {
        ret = write_data(s, block->host_addr + start + i * TARGET_PAGE_SIZE,
                         size % TARGET_PAGE_SIZE);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
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

    if (s->has_filter) {
        if (phys_addr < s->begin || phys_addr >= s->begin + s->length) {
            return;
        }
    }

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        if (s->has_filter) {
            if (block->target_start >= s->begin + s->length ||
                block->target_end <= s->begin) {
                /* This block is out of the range */
                continue;
            }

            if (s->begin <= block->target_start) {
                start = block->target_start;
            } else {
                start = s->begin;
            }

            size_in_block = block->target_end - start;
            if (s->begin + s->length < block->target_end) {
                size_in_block -= block->target_end - (s->begin + s->length);
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

static int write_elf_loads(DumpState *s)
{
    hwaddr offset, filesz;
    MemoryMapping *memory_mapping;
    uint32_t phdr_index = 1;
    int ret;
    uint32_t max_index;

    if (s->have_section) {
        max_index = s->sh_info;
    } else {
        max_index = s->phdr_num;
    }

    QTAILQ_FOREACH(memory_mapping, &s->list.head, next) {
        get_offset_range(memory_mapping->phys_addr,
                         memory_mapping->length,
                         s, &offset, &filesz);
        if (s->dump_info.d_class == ELFCLASS64) {
            ret = write_elf64_load(s, memory_mapping, phdr_index++, offset,
                                   filesz);
        } else {
            ret = write_elf32_load(s, memory_mapping, phdr_index++, offset,
                                   filesz);
        }

        if (ret < 0) {
            return -1;
        }

        if (phdr_index >= max_index) {
            break;
        }
    }

    return 0;
}

/* write elf header, PT_NOTE and elf note to vmcore. */
static int dump_begin(DumpState *s)
{
    int ret;

    /*
     * the vmcore's format is:
     *   --------------
     *   |  elf header |
     *   --------------
     *   |  PT_NOTE    |
     *   --------------
     *   |  PT_LOAD    |
     *   --------------
     *   |  ......     |
     *   --------------
     *   |  PT_LOAD    |
     *   --------------
     *   |  sec_hdr    |
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
    if (s->dump_info.d_class == ELFCLASS64) {
        ret = write_elf64_header(s);
    } else {
        ret = write_elf32_header(s);
    }
    if (ret < 0) {
        return -1;
    }

    if (s->dump_info.d_class == ELFCLASS64) {
        /* write PT_NOTE to vmcore */
        if (write_elf64_note(s) < 0) {
            return -1;
        }

        /* write all PT_LOAD to vmcore */
        if (write_elf_loads(s) < 0) {
            return -1;
        }

        /* write section to vmcore */
        if (s->have_section) {
            if (write_elf_section(s, 1) < 0) {
                return -1;
            }
        }

        /* write notes to vmcore */
        if (write_elf64_notes(fd_write_vmcore, s) < 0) {
            return -1;
        }

    } else {
        /* write PT_NOTE to vmcore */
        if (write_elf32_note(s) < 0) {
            return -1;
        }

        /* write all PT_LOAD to vmcore */
        if (write_elf_loads(s) < 0) {
            return -1;
        }

        /* write section to vmcore */
        if (s->have_section) {
            if (write_elf_section(s, 0) < 0) {
                return -1;
            }
        }

        /* write notes to vmcore */
        if (write_elf32_notes(fd_write_vmcore, s) < 0) {
            return -1;
        }
    }

    return 0;
}

/* write PT_LOAD to vmcore */
static int dump_completed(DumpState *s)
{
    dump_cleanup(s);
    return 0;
}

static int get_next_block(DumpState *s, GuestPhysBlock *block)
{
    while (1) {
        block = QTAILQ_NEXT(block, next);
        if (!block) {
            /* no more block */
            return 1;
        }

        s->start = 0;
        s->next_block = block;
        if (s->has_filter) {
            if (block->target_start >= s->begin + s->length ||
                block->target_end <= s->begin) {
                /* This block is out of the range */
                continue;
            }

            if (s->begin > block->target_start) {
                s->start = s->begin - block->target_start;
            }
        }

        return 0;
    }
}

/* write all memory to vmcore */
static int dump_iterate(DumpState *s)
{
    GuestPhysBlock *block;
    int64_t size;
    int ret;

    while (1) {
        block = s->next_block;

        size = block->target_end - block->target_start;
        if (s->has_filter) {
            size -= s->start;
            if (s->begin + s->length < block->target_end) {
                size -= block->target_end - (s->begin + s->length);
            }
        }
        ret = write_memory(s, block, s->start, size);
        if (ret == -1) {
            return ret;
        }

        ret = get_next_block(s, block);
        if (ret == 1) {
            dump_completed(s);
            return 0;
        }
    }
}

static int create_vmcore(DumpState *s)
{
    int ret;

    ret = dump_begin(s);
    if (ret < 0) {
        return -1;
    }

    ret = dump_iterate(s);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

static int write_start_flat_header(int fd)
{
    uint8_t *buf;
    MakedumpfileHeader mh;
    int ret = 0;

    memset(&mh, 0, sizeof(mh));
    strncpy(mh.signature, MAKEDUMPFILE_SIGNATURE,
            strlen(MAKEDUMPFILE_SIGNATURE));

    mh.type = cpu_to_be64(TYPE_FLAT_HEADER);
    mh.version = cpu_to_be64(VERSION_FLAT_HEADER);

    buf = g_malloc0(MAX_SIZE_MDF_HEADER);
    memcpy(buf, &mh, sizeof(mh));

    size_t written_size;
    written_size = qemu_write_full(fd, buf, MAX_SIZE_MDF_HEADER);
    if (written_size != MAX_SIZE_MDF_HEADER) {
        ret = -1;
    }

    g_free(buf);
    return ret;
}

static int write_end_flat_header(int fd)
{
    MakedumpfileDataHeader mdh;

    mdh.offset = END_FLAG_FLAT_HEADER;
    mdh.buf_size = END_FLAG_FLAT_HEADER;

    size_t written_size;
    written_size = qemu_write_full(fd, &mdh, sizeof(mdh));
    if (written_size != sizeof(mdh)) {
        return -1;
    }

    return 0;
}

static int write_buffer(int fd, off_t offset, const void *buf, size_t size)
{
    size_t written_size;
    MakedumpfileDataHeader mdh;

    mdh.offset = cpu_to_be64(offset);
    mdh.buf_size = cpu_to_be64(size);

    written_size = qemu_write_full(fd, &mdh, sizeof(mdh));
    if (written_size != sizeof(mdh)) {
        return -1;
    }

    written_size = qemu_write_full(fd, buf, size);
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

/* write common header, sub header and elf note to vmcore */
static int create_header32(DumpState *s)
{
    int ret = 0;
    DiskDumpHeader32 *dh = NULL;
    KdumpSubHeader32 *kh = NULL;
    size_t size;
    int endian = s->dump_info.d_endian;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t status = 0;
    uint64_t offset_note;

    /* write common header, the version of kdump-compressed format is 6th */
    size = sizeof(DiskDumpHeader32);
    dh = g_malloc0(size);

    strncpy(dh->signature, KDUMP_SIGNATURE, strlen(KDUMP_SIGNATURE));
    dh->header_version = cpu_convert_to_target32(6, endian);
    block_size = s->page_size;
    dh->block_size = cpu_convert_to_target32(block_size, endian);
    sub_hdr_size = sizeof(struct KdumpSubHeader32) + s->note_size;
    sub_hdr_size = DIV_ROUND_UP(sub_hdr_size, block_size);
    dh->sub_hdr_size = cpu_convert_to_target32(sub_hdr_size, endian);
    /* dh->max_mapnr may be truncated, full 64bit is in kh.max_mapnr_64 */
    dh->max_mapnr = cpu_convert_to_target32(MIN(s->max_mapnr, UINT_MAX),
                                            endian);
    dh->nr_cpus = cpu_convert_to_target32(s->nr_cpus, endian);
    bitmap_blocks = DIV_ROUND_UP(s->len_dump_bitmap, block_size) * 2;
    dh->bitmap_blocks = cpu_convert_to_target32(bitmap_blocks, endian);
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
    dh->status = cpu_convert_to_target32(status, endian);

    if (write_buffer(s->fd, 0, dh, size) < 0) {
        dump_error(s, "dump: failed to write disk dump header.\n");
        ret = -1;
        goto out;
    }

    /* write sub header */
    size = sizeof(KdumpSubHeader32);
    kh = g_malloc0(size);

    /* 64bit max_mapnr_64 */
    kh->max_mapnr_64 = cpu_convert_to_target64(s->max_mapnr, endian);
    kh->phys_base = cpu_convert_to_target32(PHYS_BASE, endian);
    kh->dump_level = cpu_convert_to_target32(DUMP_LEVEL, endian);

    offset_note = DISKDUMP_HEADER_BLOCKS * block_size + size;
    kh->offset_note = cpu_convert_to_target64(offset_note, endian);
    kh->note_size = cpu_convert_to_target32(s->note_size, endian);

    if (write_buffer(s->fd, DISKDUMP_HEADER_BLOCKS *
                     block_size, kh, size) < 0) {
        dump_error(s, "dump: failed to write kdump sub header.\n");
        ret = -1;
        goto out;
    }

    /* write note */
    s->note_buf = g_malloc0(s->note_size);
    s->note_buf_offset = 0;

    /* use s->note_buf to store notes temporarily */
    if (write_elf32_notes(buf_write_note, s) < 0) {
        ret = -1;
        goto out;
    }

    if (write_buffer(s->fd, offset_note, s->note_buf,
                     s->note_size) < 0) {
        dump_error(s, "dump: failed to write notes");
        ret = -1;
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

    return ret;
}

/* write common header, sub header and elf note to vmcore */
static int create_header64(DumpState *s)
{
    int ret = 0;
    DiskDumpHeader64 *dh = NULL;
    KdumpSubHeader64 *kh = NULL;
    size_t size;
    int endian = s->dump_info.d_endian;
    uint32_t block_size;
    uint32_t sub_hdr_size;
    uint32_t bitmap_blocks;
    uint32_t status = 0;
    uint64_t offset_note;

    /* write common header, the version of kdump-compressed format is 6th */
    size = sizeof(DiskDumpHeader64);
    dh = g_malloc0(size);

    strncpy(dh->signature, KDUMP_SIGNATURE, strlen(KDUMP_SIGNATURE));
    dh->header_version = cpu_convert_to_target32(6, endian);
    block_size = s->page_size;
    dh->block_size = cpu_convert_to_target32(block_size, endian);
    sub_hdr_size = sizeof(struct KdumpSubHeader64) + s->note_size;
    sub_hdr_size = DIV_ROUND_UP(sub_hdr_size, block_size);
    dh->sub_hdr_size = cpu_convert_to_target32(sub_hdr_size, endian);
    /* dh->max_mapnr may be truncated, full 64bit is in kh.max_mapnr_64 */
    dh->max_mapnr = cpu_convert_to_target32(MIN(s->max_mapnr, UINT_MAX),
                                            endian);
    dh->nr_cpus = cpu_convert_to_target32(s->nr_cpus, endian);
    bitmap_blocks = DIV_ROUND_UP(s->len_dump_bitmap, block_size) * 2;
    dh->bitmap_blocks = cpu_convert_to_target32(bitmap_blocks, endian);
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
    dh->status = cpu_convert_to_target32(status, endian);

    if (write_buffer(s->fd, 0, dh, size) < 0) {
        dump_error(s, "dump: failed to write disk dump header.\n");
        ret = -1;
        goto out;
    }

    /* write sub header */
    size = sizeof(KdumpSubHeader64);
    kh = g_malloc0(size);

    /* 64bit max_mapnr_64 */
    kh->max_mapnr_64 = cpu_convert_to_target64(s->max_mapnr, endian);
    kh->phys_base = cpu_convert_to_target64(PHYS_BASE, endian);
    kh->dump_level = cpu_convert_to_target32(DUMP_LEVEL, endian);

    offset_note = DISKDUMP_HEADER_BLOCKS * block_size + size;
    kh->offset_note = cpu_convert_to_target64(offset_note, endian);
    kh->note_size = cpu_convert_to_target64(s->note_size, endian);

    if (write_buffer(s->fd, DISKDUMP_HEADER_BLOCKS *
                     block_size, kh, size) < 0) {
        dump_error(s, "dump: failed to write kdump sub header.\n");
        ret = -1;
        goto out;
    }

    /* write note */
    s->note_buf = g_malloc0(s->note_size);
    s->note_buf_offset = 0;

    /* use s->note_buf to store notes temporarily */
    if (write_elf64_notes(buf_write_note, s) < 0) {
        ret = -1;
        goto out;
    }

    if (write_buffer(s->fd, offset_note, s->note_buf,
                     s->note_size) < 0) {
        dump_error(s, "dump: failed to write notes");
        ret = -1;
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

    return ret;
}

static int write_dump_header(DumpState *s)
{
    if (s->dump_info.d_machine == EM_386) {
        return create_header32(s);
    } else {
        return create_header64(s);
    }
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

    /* should not set the previous place */
    assert(last_pfn <= pfn);

    /*
     * if the bit needed to be set is not cached in buf, flush the data in buf
     * to vmcore firstly.
     * making new_offset be bigger than old_offset can also sync remained data
     * into vmcore.
     */
    old_offset = BUFSIZE_BITMAP * (last_pfn / PFN_BUFBITMAP);
    new_offset = BUFSIZE_BITMAP * (pfn / PFN_BUFBITMAP);

    while (old_offset < new_offset) {
        /* calculate the offset and write dump_bitmap */
        offset_bitmap1 = s->offset_dump_bitmap + old_offset;
        if (write_buffer(s->fd, offset_bitmap1, buf,
                         BUFSIZE_BITMAP) < 0) {
            return -1;
        }

        /* dump level 1 is chosen, so 1st and 2nd bitmap are same */
        offset_bitmap2 = s->offset_dump_bitmap + s->len_dump_bitmap +
                         old_offset;
        if (write_buffer(s->fd, offset_bitmap2, buf,
                         BUFSIZE_BITMAP) < 0) {
            return -1;
        }

        memset(buf, 0, BUFSIZE_BITMAP);
        old_offset += BUFSIZE_BITMAP;
    }

    /* get the exact place of the bit in the buf, and set it */
    byte = (pfn % PFN_BUFBITMAP) / CHAR_BIT;
    bit = (pfn % PFN_BUFBITMAP) % CHAR_BIT;
    if (value) {
        buf[byte] |= 1u << bit;
    } else {
        buf[byte] &= ~(1u << bit);
    }

    return 0;
}

/*
 * exam every page and return the page frame number and the address of the page.
 * bufptr can be NULL. note: the blocks here is supposed to reflect guest-phys
 * blocks, so block->target_start and block->target_end should be interal
 * multiples of the target page size.
 */
static bool get_next_page(GuestPhysBlock **blockptr, uint64_t *pfnptr,
                          uint8_t **bufptr, DumpState *s)
{
    GuestPhysBlock *block = *blockptr;
    hwaddr addr;
    uint8_t *buf;

    /* block == NULL means the start of the iteration */
    if (!block) {
        block = QTAILQ_FIRST(&s->guest_phys_blocks.head);
        *blockptr = block;
        assert(block->target_start % s->page_size == 0);
        assert(block->target_end % s->page_size == 0);
        *pfnptr = paddr_to_pfn(block->target_start, s->page_shift);
        if (bufptr) {
            *bufptr = block->host_addr;
        }
        return true;
    }

    *pfnptr = *pfnptr + 1;
    addr = pfn_to_paddr(*pfnptr, s->page_shift);

    if ((addr >= block->target_start) &&
        (addr + s->page_size <= block->target_end)) {
        buf = block->host_addr + (addr - block->target_start);
    } else {
        /* the next page is in the next block */
        block = QTAILQ_NEXT(block, next);
        *blockptr = block;
        if (!block) {
            return false;
        }
        assert(block->target_start % s->page_size == 0);
        assert(block->target_end % s->page_size == 0);
        *pfnptr = paddr_to_pfn(block->target_start, s->page_shift);
        buf = block->host_addr;
    }

    if (bufptr) {
        *bufptr = buf;
    }

    return true;
}

static int write_dump_bitmap(DumpState *s)
{
    int ret = 0;
    uint64_t last_pfn, pfn;
    void *dump_bitmap_buf;
    size_t num_dumpable;
    GuestPhysBlock *block_iter = NULL;

    /* dump_bitmap_buf is used to store dump_bitmap temporarily */
    dump_bitmap_buf = g_malloc0(BUFSIZE_BITMAP);

    num_dumpable = 0;
    last_pfn = 0;

    /*
     * exam memory page by page, and set the bit in dump_bitmap corresponded
     * to the existing page.
     */
    while (get_next_page(&block_iter, &pfn, NULL, s)) {
        ret = set_dump_bitmap(last_pfn, pfn, true, dump_bitmap_buf, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to set dump_bitmap.\n");
            ret = -1;
            goto out;
        }

        last_pfn = pfn;
        num_dumpable++;
    }

    /*
     * set_dump_bitmap will always leave the recently set bit un-sync. Here we
     * set last_pfn + PFN_BUFBITMAP to 0 and those set but un-sync bit will be
     * synchronized into vmcore.
     */
    if (num_dumpable > 0) {
        ret = set_dump_bitmap(last_pfn, last_pfn + PFN_BUFBITMAP, false,
                              dump_bitmap_buf, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to sync dump_bitmap.\n");
            ret = -1;
            goto out;
        }
    }

    /* number of dumpable pages that will be dumped later */
    s->num_dumpable = num_dumpable;

out:
    g_free(dump_bitmap_buf);

    return ret;
}

static void prepare_data_cache(DataCache *data_cache, DumpState *s,
                               off_t offset)
{
    data_cache->fd = s->fd;
    data_cache->data_size = 0;
    data_cache->buf_size = BUFSIZE_DATA_CACHE;
    data_cache->buf = g_malloc0(BUFSIZE_DATA_CACHE);
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
     * write the data in dc->buf to dc->fd and reset dc->buf
     */
    if ((!flag_sync && dc->data_size + size > dc->buf_size) ||
        (flag_sync && dc->data_size > 0)) {
        if (write_buffer(dc->fd, dc->offset, dc->buf, dc->data_size) < 0) {
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
    size_t len_buf_out_zlib, len_buf_out_lzo, len_buf_out_snappy;
    size_t len_buf_out;

    /* init buf_out */
    len_buf_out_zlib = len_buf_out_lzo = len_buf_out_snappy = 0;

    /* buf size for zlib */
    len_buf_out_zlib = compressBound(page_size);

    /* buf size for lzo */
#ifdef CONFIG_LZO
    if (flag_compress & DUMP_DH_COMPRESSED_LZO) {
        if (lzo_init() != LZO_E_OK) {
            /* return 0 to indicate lzo is unavailable */
            return 0;
        }
    }

    /*
     * LZO will expand incompressible data by a little amount. please check the
     * following URL to see the expansion calculation:
     * http://www.oberhumer.com/opensource/lzo/lzofaq.php
     */
    len_buf_out_lzo = page_size + page_size / 16 + 64 + 3;
#endif

#ifdef CONFIG_SNAPPY
    /* buf size for snappy */
    len_buf_out_snappy = snappy_max_compressed_length(page_size);
#endif

    /* get the biggest that can store all kinds of compressed page */
    len_buf_out = MAX(len_buf_out_zlib,
                      MAX(len_buf_out_lzo, len_buf_out_snappy));

    return len_buf_out;
}

/*
 * check if the page is all 0
 */
static inline bool is_zero_page(const uint8_t *buf, size_t page_size)
{
    return buffer_is_zero(buf, page_size);
}

static int write_dump_pages(DumpState *s)
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
    int endian = s->dump_info.d_endian;
    GuestPhysBlock *block_iter = NULL;
    uint64_t pfn_iter;

    /* get offset of page_desc and page_data in dump file */
    offset_desc = s->offset_page;
    offset_data = offset_desc + sizeof(PageDescriptor) * s->num_dumpable;

    prepare_data_cache(&page_desc, s, offset_desc);
    prepare_data_cache(&page_data, s, offset_data);

    /* prepare buffer to store compressed data */
    len_buf_out = get_len_buf_out(s->page_size, s->flag_compress);
    if (len_buf_out == 0) {
        dump_error(s, "dump: failed to get length of output buffer.\n");
        goto out;
    }

#ifdef CONFIG_LZO
    wrkmem = g_malloc(LZO1X_1_MEM_COMPRESS);
#endif

    buf_out = g_malloc(len_buf_out);

    /*
     * init zero page's page_desc and page_data, because every zero page
     * uses the same page_data
     */
    pd_zero.size = cpu_convert_to_target32(s->page_size, endian);
    pd_zero.flags = cpu_convert_to_target32(0, endian);
    pd_zero.offset = cpu_convert_to_target64(offset_data, endian);
    pd_zero.page_flags = cpu_convert_to_target64(0, endian);
    buf = g_malloc0(s->page_size);
    ret = write_cache(&page_data, buf, s->page_size, false);
    g_free(buf);
    if (ret < 0) {
        dump_error(s, "dump: failed to write page data(zero page).\n");
        goto out;
    }

    offset_data += s->page_size;

    /*
     * dump memory to vmcore page by page. zero page will all be resided in the
     * first page of page section
     */
    while (get_next_page(&block_iter, &pfn_iter, &buf, s)) {
        /* check zero page */
        if (is_zero_page(buf, s->page_size)) {
            ret = write_cache(&page_desc, &pd_zero, sizeof(PageDescriptor),
                              false);
            if (ret < 0) {
                dump_error(s, "dump: failed to write page desc.\n");
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
                    (compress2(buf_out, (uLongf *)&size_out, buf, s->page_size,
                    Z_BEST_SPEED) == Z_OK) && (size_out < s->page_size)) {
                pd.flags = cpu_convert_to_target32(DUMP_DH_COMPRESSED_ZLIB,
                                                   endian);
                pd.size  = cpu_convert_to_target32(size_out, endian);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    dump_error(s, "dump: failed to write page data.\n");
                    goto out;
                }
#ifdef CONFIG_LZO
            } else if ((s->flag_compress & DUMP_DH_COMPRESSED_LZO) &&
                    (lzo1x_1_compress(buf, s->page_size, buf_out,
                    (lzo_uint *)&size_out, wrkmem) == LZO_E_OK) &&
                    (size_out < s->page_size)) {
                pd.flags = cpu_convert_to_target32(DUMP_DH_COMPRESSED_LZO,
                                                   endian);
                pd.size  = cpu_convert_to_target32(size_out, endian);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    dump_error(s, "dump: failed to write page data.\n");
                    goto out;
                }
#endif
#ifdef CONFIG_SNAPPY
            } else if ((s->flag_compress & DUMP_DH_COMPRESSED_SNAPPY) &&
                    (snappy_compress((char *)buf, s->page_size,
                    (char *)buf_out, &size_out) == SNAPPY_OK) &&
                    (size_out < s->page_size)) {
                pd.flags = cpu_convert_to_target32(
                                        DUMP_DH_COMPRESSED_SNAPPY, endian);
                pd.size  = cpu_convert_to_target32(size_out, endian);

                ret = write_cache(&page_data, buf_out, size_out, false);
                if (ret < 0) {
                    dump_error(s, "dump: failed to write page data.\n");
                    goto out;
                }
#endif
            } else {
                /*
                 * fall back to save in plaintext, size_out should be
                 * assigned to s->page_size
                 */
                pd.flags = cpu_convert_to_target32(0, endian);
                size_out = s->page_size;
                pd.size = cpu_convert_to_target32(size_out, endian);

                ret = write_cache(&page_data, buf, s->page_size, false);
                if (ret < 0) {
                    dump_error(s, "dump: failed to write page data.\n");
                    goto out;
                }
            }

            /* get and write page desc here */
            pd.page_flags = cpu_convert_to_target64(0, endian);
            pd.offset = cpu_convert_to_target64(offset_data, endian);
            offset_data += size_out;

            ret = write_cache(&page_desc, &pd, sizeof(PageDescriptor), false);
            if (ret < 0) {
                dump_error(s, "dump: failed to write page desc.\n");
                goto out;
            }
        }
    }

    ret = write_cache(&page_desc, NULL, 0, true);
    if (ret < 0) {
        dump_error(s, "dump: failed to sync cache for page_desc.\n");
        goto out;
    }
    ret = write_cache(&page_data, NULL, 0, true);
    if (ret < 0) {
        dump_error(s, "dump: failed to sync cache for page_data.\n");
        goto out;
    }

out:
    free_data_cache(&page_desc);
    free_data_cache(&page_data);

#ifdef CONFIG_LZO
    g_free(wrkmem);
#endif

    g_free(buf_out);

    return ret;
}

static int create_kdump_vmcore(DumpState *s)
{
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

    ret = write_start_flat_header(s->fd);
    if (ret < 0) {
        dump_error(s, "dump: failed to write start flat header.\n");
        return -1;
    }

    ret = write_dump_header(s);
    if (ret < 0) {
        return -1;
    }

    ret = write_dump_bitmap(s);
    if (ret < 0) {
        return -1;
    }

    ret = write_dump_pages(s);
    if (ret < 0) {
        return -1;
    }

    ret = write_end_flat_header(s->fd);
    if (ret < 0) {
        dump_error(s, "dump: failed to write end flat header.\n");
        return -1;
    }

    dump_completed(s);

    return 0;
}

static ram_addr_t get_start_block(DumpState *s)
{
    GuestPhysBlock *block;

    if (!s->has_filter) {
        s->next_block = QTAILQ_FIRST(&s->guest_phys_blocks.head);
        return 0;
    }

    QTAILQ_FOREACH(block, &s->guest_phys_blocks.head, next) {
        if (block->target_start >= s->begin + s->length ||
            block->target_end <= s->begin) {
            /* This block is out of the range */
            continue;
        }

        s->next_block = block;
        if (s->begin > block->target_start) {
            s->start = s->begin - block->target_start;
        } else {
            s->start = 0;
        }
        return s->start;
    }

    return -1;
}

static void get_max_mapnr(DumpState *s)
{
    GuestPhysBlock *last_block;

    last_block = QTAILQ_LAST(&s->guest_phys_blocks.head, GuestPhysBlockHead);
    s->max_mapnr = paddr_to_pfn(last_block->target_end, s->page_shift);
}

static int dump_init(DumpState *s, int fd, bool has_format,
                     DumpGuestMemoryFormat format, bool paging, bool has_filter,
                     int64_t begin, int64_t length, Error **errp)
{
    CPUState *cpu;
    int nr_cpus;
    Error *err = NULL;
    int ret;

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
    s->has_filter = has_filter;
    s->begin = begin;
    s->length = length;

    guest_phys_blocks_init(&s->guest_phys_blocks);
    guest_phys_blocks_append(&s->guest_phys_blocks);

    s->start = get_start_block(s);
    if (s->start == -1) {
        error_set(errp, QERR_INVALID_PARAMETER, "begin");
        goto cleanup;
    }

    /* get dump info: endian, class and architecture.
     * If the target architecture is not supported, cpu_get_dump_info() will
     * return -1.
     */
    ret = cpu_get_dump_info(&s->dump_info, &s->guest_phys_blocks);
    if (ret < 0) {
        error_set(errp, QERR_UNSUPPORTED);
        goto cleanup;
    }

    s->note_size = cpu_get_note_size(s->dump_info.d_class,
                                     s->dump_info.d_machine, nr_cpus);
    if (s->note_size < 0) {
        error_set(errp, QERR_UNSUPPORTED);
        goto cleanup;
    }

    /* get memory mapping */
    memory_mapping_list_init(&s->list);
    if (paging) {
        qemu_get_guest_memory_mapping(&s->list, &s->guest_phys_blocks, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            goto cleanup;
        }
    } else {
        qemu_get_guest_simple_memory_mapping(&s->list, &s->guest_phys_blocks);
    }

    s->nr_cpus = nr_cpus;
    s->page_size = TARGET_PAGE_SIZE;
    s->page_shift = ffs(s->page_size) - 1;

    get_max_mapnr(s);

    uint64_t tmp;
    tmp = DIV_ROUND_UP(DIV_ROUND_UP(s->max_mapnr, CHAR_BIT), s->page_size);
    s->len_dump_bitmap = tmp * s->page_size;

    /* init for kdump-compressed format */
    if (has_format && format != DUMP_GUEST_MEMORY_FORMAT_ELF) {
        switch (format) {
        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB:
            s->flag_compress = DUMP_DH_COMPRESSED_ZLIB;
            break;

        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO:
            s->flag_compress = DUMP_DH_COMPRESSED_LZO;
            break;

        case DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY:
            s->flag_compress = DUMP_DH_COMPRESSED_SNAPPY;
            break;

        default:
            s->flag_compress = 0;
        }

        return 0;
    }

    if (s->has_filter) {
        memory_mapping_filter(&s->list, s->begin, s->length);
    }

    /*
     * calculate phdr_num
     *
     * the type of ehdr->e_phnum is uint16_t, so we should avoid overflow
     */
    s->phdr_num = 1; /* PT_NOTE */
    if (s->list.num < UINT16_MAX - 2) {
        s->phdr_num += s->list.num;
        s->have_section = false;
    } else {
        s->have_section = true;
        s->phdr_num = PN_XNUM;
        s->sh_info = 1; /* PT_NOTE */

        /* the type of shdr->sh_info is uint32_t, so we should avoid overflow */
        if (s->list.num <= UINT32_MAX - 1) {
            s->sh_info += s->list.num;
        } else {
            s->sh_info = UINT32_MAX;
        }
    }

    if (s->dump_info.d_class == ELFCLASS64) {
        if (s->have_section) {
            s->memory_offset = sizeof(Elf64_Ehdr) +
                               sizeof(Elf64_Phdr) * s->sh_info +
                               sizeof(Elf64_Shdr) + s->note_size;
        } else {
            s->memory_offset = sizeof(Elf64_Ehdr) +
                               sizeof(Elf64_Phdr) * s->phdr_num + s->note_size;
        }
    } else {
        if (s->have_section) {
            s->memory_offset = sizeof(Elf32_Ehdr) +
                               sizeof(Elf32_Phdr) * s->sh_info +
                               sizeof(Elf32_Shdr) + s->note_size;
        } else {
            s->memory_offset = sizeof(Elf32_Ehdr) +
                               sizeof(Elf32_Phdr) * s->phdr_num + s->note_size;
        }
    }

    return 0;

cleanup:
    guest_phys_blocks_free(&s->guest_phys_blocks);

    if (s->resume) {
        vm_start();
    }

    return -1;
}

void qmp_dump_guest_memory(bool paging, const char *file, bool has_begin,
                           int64_t begin, bool has_length,
                           int64_t length, bool has_format,
                           DumpGuestMemoryFormat format, Error **errp)
{
    const char *p;
    int fd = -1;
    DumpState *s;
    int ret;

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
        error_set(errp, QERR_MISSING_PARAMETER, "length");
        return;
    }
    if (!has_begin && has_length) {
        error_set(errp, QERR_MISSING_PARAMETER, "begin");
        return;
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

#if !defined(WIN32)
    if (strstart(file, "fd:", &p)) {
        fd = monitor_get_fd(cur_mon, p, errp);
        if (fd == -1) {
            return;
        }
    }
#endif

    if  (strstart(file, "file:", &p)) {
        fd = qemu_open(p, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR);
        if (fd < 0) {
            error_setg_file_open(errp, errno, p);
            return;
        }
    }

    if (fd == -1) {
        error_set(errp, QERR_INVALID_PARAMETER, "protocol");
        return;
    }

    s = g_malloc0(sizeof(DumpState));

    ret = dump_init(s, fd, has_format, format, paging, has_begin,
                    begin, length, errp);
    if (ret < 0) {
        g_free(s);
        return;
    }

    if (has_format && format != DUMP_GUEST_MEMORY_FORMAT_ELF) {
        if (create_kdump_vmcore(s) < 0) {
            error_set(errp, QERR_IO_ERROR);
        }
    } else {
        if (create_vmcore(s) < 0) {
            error_set(errp, QERR_IO_ERROR);
        }
    }

    g_free(s);
}

DumpGuestMemoryCapability *qmp_query_dump_guest_memory_capability(Error **errp)
{
    DumpGuestMemoryFormatList *item;
    DumpGuestMemoryCapability *cap =
                                  g_malloc0(sizeof(DumpGuestMemoryCapability));

    /* elf is always available */
    item = g_malloc0(sizeof(DumpGuestMemoryFormatList));
    cap->formats = item;
    item->value = DUMP_GUEST_MEMORY_FORMAT_ELF;

    /* kdump-zlib is always available */
    item->next = g_malloc0(sizeof(DumpGuestMemoryFormatList));
    item = item->next;
    item->value = DUMP_GUEST_MEMORY_FORMAT_KDUMP_ZLIB;

    /* add new item if kdump-lzo is available */
#ifdef CONFIG_LZO
    item->next = g_malloc0(sizeof(DumpGuestMemoryFormatList));
    item = item->next;
    item->value = DUMP_GUEST_MEMORY_FORMAT_KDUMP_LZO;
#endif

    /* add new item if kdump-snappy is available */
#ifdef CONFIG_SNAPPY
    item->next = g_malloc0(sizeof(DumpGuestMemoryFormatList));
    item = item->next;
    item->value = DUMP_GUEST_MEMORY_FORMAT_KDUMP_SNAPPY;
#endif

    return cap;
}
