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
    Error **errp;
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

static int dump_init(DumpState *s, int fd, bool paging, bool has_filter,
                     int64_t begin, int64_t length, Error **errp)
{
    CPUState *cpu;
    int nr_cpus;
    Error *err = NULL;
    int ret;

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

    s->errp = errp;
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
                           int64_t begin, bool has_length, int64_t length,
                           Error **errp)
{
    const char *p;
    int fd = -1;
    DumpState *s;
    int ret;

    if (has_begin && !has_length) {
        error_set(errp, QERR_MISSING_PARAMETER, "length");
        return;
    }
    if (!has_begin && has_length) {
        error_set(errp, QERR_MISSING_PARAMETER, "begin");
        return;
    }

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

    ret = dump_init(s, fd, paging, has_begin, begin, length, errp);
    if (ret < 0) {
        g_free(s);
        return;
    }

    if (create_vmcore(s) < 0 && !error_is_set(s->errp)) {
        error_set(errp, QERR_IO_ERROR);
    }

    g_free(s);
}
