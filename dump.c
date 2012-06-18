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
#include "cpu-all.h"
#include "targphys.h"
#include "monitor.h"
#include "kvm.h"
#include "dump.h"
#include "sysemu.h"
#include "memory_mapping.h"
#include "error.h"
#include "qmp-commands.h"
#include "gdbstub.h"

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
    ArchDumpInfo dump_info;
    MemoryMappingList list;
    uint16_t phdr_num;
    uint32_t sh_info;
    bool have_section;
    bool resume;
    size_t note_size;
    target_phys_addr_t memory_offset;
    int fd;

    RAMBlock *block;
    ram_addr_t start;
    bool has_filter;
    int64_t begin;
    int64_t length;
    Error **errp;
} DumpState;

static int dump_cleanup(DumpState *s)
{
    int ret = 0;

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

static int fd_write_vmcore(void *buf, size_t size, void *opaque)
{
    DumpState *s = opaque;
    int fd = s->fd;
    size_t writen_size;

    /* The fd may be passed from user, and it can be non-blocked */
    while (size) {
        writen_size = qemu_write_full(fd, buf, size);
        if (writen_size != size && errno != EAGAIN) {
            return -1;
        }

        buf += writen_size;
        size -= writen_size;
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
                            int phdr_index, target_phys_addr_t offset)
{
    Elf64_Phdr phdr;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&phdr, 0, sizeof(Elf64_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_LOAD, endian);
    phdr.p_offset = cpu_convert_to_target64(offset, endian);
    phdr.p_paddr = cpu_convert_to_target64(memory_mapping->phys_addr, endian);
    if (offset == -1) {
        /* When the memory is not stored into vmcore, offset will be -1 */
        phdr.p_filesz = 0;
    } else {
        phdr.p_filesz = cpu_convert_to_target64(memory_mapping->length, endian);
    }
    phdr.p_memsz = cpu_convert_to_target64(memory_mapping->length, endian);
    phdr.p_vaddr = cpu_convert_to_target64(memory_mapping->virt_addr, endian);

    ret = fd_write_vmcore(&phdr, sizeof(Elf64_Phdr), s);
    if (ret < 0) {
        dump_error(s, "dump: failed to write program header table.\n");
        return -1;
    }

    return 0;
}

static int write_elf32_load(DumpState *s, MemoryMapping *memory_mapping,
                            int phdr_index, target_phys_addr_t offset)
{
    Elf32_Phdr phdr;
    int ret;
    int endian = s->dump_info.d_endian;

    memset(&phdr, 0, sizeof(Elf32_Phdr));
    phdr.p_type = cpu_convert_to_target32(PT_LOAD, endian);
    phdr.p_offset = cpu_convert_to_target32(offset, endian);
    phdr.p_paddr = cpu_convert_to_target32(memory_mapping->phys_addr, endian);
    if (offset == -1) {
        /* When the memory is not stored into vmcore, offset will be -1 */
        phdr.p_filesz = 0;
    } else {
        phdr.p_filesz = cpu_convert_to_target32(memory_mapping->length, endian);
    }
    phdr.p_memsz = cpu_convert_to_target32(memory_mapping->length, endian);
    phdr.p_vaddr = cpu_convert_to_target32(memory_mapping->virt_addr, endian);

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
    target_phys_addr_t begin = s->memory_offset - s->note_size;
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

static int write_elf64_notes(DumpState *s)
{
    CPUArchState *env;
    int ret;
    int id;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        id = cpu_index(env);
        ret = cpu_write_elf64_note(fd_write_vmcore, env, id, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write elf notes.\n");
            return -1;
        }
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        ret = cpu_write_elf64_qemunote(fd_write_vmcore, env, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write CPU status.\n");
            return -1;
        }
    }

    return 0;
}

static int write_elf32_note(DumpState *s)
{
    target_phys_addr_t begin = s->memory_offset - s->note_size;
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

static int write_elf32_notes(DumpState *s)
{
    CPUArchState *env;
    int ret;
    int id;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        id = cpu_index(env);
        ret = cpu_write_elf32_note(fd_write_vmcore, env, id, s);
        if (ret < 0) {
            dump_error(s, "dump: failed to write elf notes.\n");
            return -1;
        }
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        ret = cpu_write_elf32_qemunote(fd_write_vmcore, env, s);
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
static int write_memory(DumpState *s, RAMBlock *block, ram_addr_t start,
                        int64_t size)
{
    int64_t i;
    int ret;

    for (i = 0; i < size / TARGET_PAGE_SIZE; i++) {
        ret = write_data(s, block->host + start + i * TARGET_PAGE_SIZE,
                         TARGET_PAGE_SIZE);
        if (ret < 0) {
            return ret;
        }
    }

    if ((size % TARGET_PAGE_SIZE) != 0) {
        ret = write_data(s, block->host + start + i * TARGET_PAGE_SIZE,
                         size % TARGET_PAGE_SIZE);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

/* get the memory's offset in the vmcore */
static target_phys_addr_t get_offset(target_phys_addr_t phys_addr,
                                     DumpState *s)
{
    RAMBlock *block;
    target_phys_addr_t offset = s->memory_offset;
    int64_t size_in_block, start;

    if (s->has_filter) {
        if (phys_addr < s->begin || phys_addr >= s->begin + s->length) {
            return -1;
        }
    }

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (s->has_filter) {
            if (block->offset >= s->begin + s->length ||
                block->offset + block->length <= s->begin) {
                /* This block is out of the range */
                continue;
            }

            if (s->begin <= block->offset) {
                start = block->offset;
            } else {
                start = s->begin;
            }

            size_in_block = block->length - (start - block->offset);
            if (s->begin + s->length < block->offset + block->length) {
                size_in_block -= block->offset + block->length -
                                 (s->begin + s->length);
            }
        } else {
            start = block->offset;
            size_in_block = block->length;
        }

        if (phys_addr >= start && phys_addr < start + size_in_block) {
            return phys_addr - start + offset;
        }

        offset += size_in_block;
    }

    return -1;
}

static int write_elf_loads(DumpState *s)
{
    target_phys_addr_t offset;
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
        offset = get_offset(memory_mapping->phys_addr, s);
        if (s->dump_info.d_class == ELFCLASS64) {
            ret = write_elf64_load(s, memory_mapping, phdr_index++, offset);
        } else {
            ret = write_elf32_load(s, memory_mapping, phdr_index++, offset);
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
        if (write_elf64_notes(s) < 0) {
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
        if (write_elf32_notes(s) < 0) {
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

static int get_next_block(DumpState *s, RAMBlock *block)
{
    while (1) {
        block = QLIST_NEXT(block, next);
        if (!block) {
            /* no more block */
            return 1;
        }

        s->start = 0;
        s->block = block;
        if (s->has_filter) {
            if (block->offset >= s->begin + s->length ||
                block->offset + block->length <= s->begin) {
                /* This block is out of the range */
                continue;
            }

            if (s->begin > block->offset) {
                s->start = s->begin - block->offset;
            }
        }

        return 0;
    }
}

/* write all memory to vmcore */
static int dump_iterate(DumpState *s)
{
    RAMBlock *block;
    int64_t size;
    int ret;

    while (1) {
        block = s->block;

        size = block->length;
        if (s->has_filter) {
            size -= s->start;
            if (s->begin + s->length < block->offset + block->length) {
                size -= block->offset + block->length - (s->begin + s->length);
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

static ram_addr_t get_start_block(DumpState *s)
{
    RAMBlock *block;

    if (!s->has_filter) {
        s->block = QLIST_FIRST(&ram_list.blocks);
        return 0;
    }

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (block->offset >= s->begin + s->length ||
            block->offset + block->length <= s->begin) {
            /* This block is out of the range */
            continue;
        }

        s->block = block;
        if (s->begin > block->offset) {
            s->start = s->begin - block->offset;
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
    CPUArchState *env;
    int nr_cpus;
    int ret;

    if (runstate_is_running()) {
        vm_stop(RUN_STATE_SAVE_VM);
        s->resume = true;
    } else {
        s->resume = false;
    }

    s->errp = errp;
    s->fd = fd;
    s->has_filter = has_filter;
    s->begin = begin;
    s->length = length;
    s->start = get_start_block(s);
    if (s->start == -1) {
        error_set(errp, QERR_INVALID_PARAMETER, "begin");
        goto cleanup;
    }

    /*
     * get dump info: endian, class and architecture.
     * If the target architecture is not supported, cpu_get_dump_info() will
     * return -1.
     *
     * if we use kvm, we should synchronize the register before we get dump
     * info.
     */
    nr_cpus = 0;
    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        cpu_synchronize_state(env);
        nr_cpus++;
    }

    ret = cpu_get_dump_info(&s->dump_info);
    if (ret < 0) {
        error_set(errp, QERR_UNSUPPORTED);
        goto cleanup;
    }

    s->note_size = cpu_get_note_size(s->dump_info.d_class,
                                     s->dump_info.d_machine, nr_cpus);
    if (ret < 0) {
        error_set(errp, QERR_UNSUPPORTED);
        goto cleanup;
    }

    /* get memory mapping */
    memory_mapping_list_init(&s->list);
    if (paging) {
        qemu_get_guest_memory_mapping(&s->list);
    } else {
        qemu_get_guest_simple_memory_mapping(&s->list);
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
        fd = monitor_get_fd(cur_mon, p);
        if (fd == -1) {
            error_set(errp, QERR_FD_NOT_FOUND, p);
            return;
        }
    }
#endif

    if  (strstart(file, "file:", &p)) {
        fd = qemu_open(p, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR);
        if (fd < 0) {
            error_set(errp, QERR_OPEN_FILE_FAILED, p);
            return;
        }
    }

    if (fd == -1) {
        error_set(errp, QERR_INVALID_PARAMETER, "protocol");
        return;
    }

    s = g_malloc(sizeof(DumpState));

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
