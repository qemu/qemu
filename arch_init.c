/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/mman.h>
#endif
#include "config.h"
#include "monitor.h"
#include "sysemu.h"
#include "arch_init.h"
#include "audio/audio.h"
#include "hw/pc.h"
#include "hw/pci.h"
#include "hw/audiodev.h"
#include "kvm.h"
#include "migration.h"
#include "net.h"
#include "gdbstub.h"
#include "hw/smbios.h"
#include "exec-memory.h"
#include "hw/pcspk.h"

#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 15;
#endif

const char arch_config_name[] = CONFIG_QEMU_CONFDIR "/target-" TARGET_ARCH ".conf";

#if defined(TARGET_ALPHA)
#define QEMU_ARCH QEMU_ARCH_ALPHA
#elif defined(TARGET_ARM)
#define QEMU_ARCH QEMU_ARCH_ARM
#elif defined(TARGET_CRIS)
#define QEMU_ARCH QEMU_ARCH_CRIS
#elif defined(TARGET_I386)
#define QEMU_ARCH QEMU_ARCH_I386
#elif defined(TARGET_M68K)
#define QEMU_ARCH QEMU_ARCH_M68K
#elif defined(TARGET_LM32)
#define QEMU_ARCH QEMU_ARCH_LM32
#elif defined(TARGET_MICROBLAZE)
#define QEMU_ARCH QEMU_ARCH_MICROBLAZE
#elif defined(TARGET_MIPS)
#define QEMU_ARCH QEMU_ARCH_MIPS
#elif defined(TARGET_PPC)
#define QEMU_ARCH QEMU_ARCH_PPC
#elif defined(TARGET_S390X)
#define QEMU_ARCH QEMU_ARCH_S390X
#elif defined(TARGET_SH4)
#define QEMU_ARCH QEMU_ARCH_SH4
#elif defined(TARGET_SPARC)
#define QEMU_ARCH QEMU_ARCH_SPARC
#elif defined(TARGET_XTENSA)
#define QEMU_ARCH QEMU_ARCH_XTENSA
#endif

const uint32_t arch_type = QEMU_ARCH;

/***********************************************************/
/* ram save/restore */

#define RAM_SAVE_FLAG_FULL     0x01 /* Obsolete, not used anymore */
#define RAM_SAVE_FLAG_COMPRESS 0x02
#define RAM_SAVE_FLAG_MEM_SIZE 0x04
#define RAM_SAVE_FLAG_PAGE     0x08
#define RAM_SAVE_FLAG_EOS      0x10
#define RAM_SAVE_FLAG_CONTINUE 0x20

#ifdef __ALTIVEC__
#include <altivec.h>
#define VECTYPE        vector unsigned char
#define SPLAT(p)       vec_splat(vec_ld(0, p), 0)
#define ALL_EQ(v1, v2) vec_all_eq(v1, v2)
#elif defined __SSE2__
#include <emmintrin.h>
#define VECTYPE        __m128i
#define SPLAT(p)       _mm_set1_epi8(*(p))
#define ALL_EQ(v1, v2) (_mm_movemask_epi8(_mm_cmpeq_epi8(v1, v2)) == 0xFFFF)
#else
#define VECTYPE        unsigned long
#define SPLAT(p)       (*(p) * (~0UL / 255))
#define ALL_EQ(v1, v2) ((v1) == (v2))
#endif

static int is_dup_page(uint8_t *page)
{
    VECTYPE *p = (VECTYPE *)page;
    VECTYPE val = SPLAT(page);
    int i;

    for (i = 0; i < TARGET_PAGE_SIZE / sizeof(VECTYPE); i++) {
        if (!ALL_EQ(val, p[i])) {
            return 0;
        }
    }

    return 1;
}

static RAMBlock *last_block;
static ram_addr_t last_offset;

static int ram_save_block(QEMUFile *f)
{
    RAMBlock *block = last_block;
    ram_addr_t offset = last_offset;
    int bytes_sent = 0;
    MemoryRegion *mr;

    if (!block)
        block = QLIST_FIRST(&ram_list.blocks);

    do {
        mr = block->mr;
        if (memory_region_get_dirty(mr, offset, TARGET_PAGE_SIZE,
                                    DIRTY_MEMORY_MIGRATION)) {
            uint8_t *p;
            int cont = (block == last_block) ? RAM_SAVE_FLAG_CONTINUE : 0;

            memory_region_reset_dirty(mr, offset, TARGET_PAGE_SIZE,
                                      DIRTY_MEMORY_MIGRATION);

            p = memory_region_get_ram_ptr(mr) + offset;

            if (is_dup_page(p)) {
                qemu_put_be64(f, offset | cont | RAM_SAVE_FLAG_COMPRESS);
                if (!cont) {
                    qemu_put_byte(f, strlen(block->idstr));
                    qemu_put_buffer(f, (uint8_t *)block->idstr,
                                    strlen(block->idstr));
                }
                qemu_put_byte(f, *p);
                bytes_sent = 1;
            } else {
                qemu_put_be64(f, offset | cont | RAM_SAVE_FLAG_PAGE);
                if (!cont) {
                    qemu_put_byte(f, strlen(block->idstr));
                    qemu_put_buffer(f, (uint8_t *)block->idstr,
                                    strlen(block->idstr));
                }
                qemu_put_buffer(f, p, TARGET_PAGE_SIZE);
                bytes_sent = TARGET_PAGE_SIZE;
            }

            break;
        }

        offset += TARGET_PAGE_SIZE;
        if (offset >= block->length) {
            offset = 0;
            block = QLIST_NEXT(block, next);
            if (!block)
                block = QLIST_FIRST(&ram_list.blocks);
        }
    } while (block != last_block || offset != last_offset);

    last_block = block;
    last_offset = offset;

    return bytes_sent;
}

static uint64_t bytes_transferred;

static ram_addr_t ram_save_remaining(void)
{
    RAMBlock *block;
    ram_addr_t count = 0;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        ram_addr_t addr;
        for (addr = 0; addr < block->length; addr += TARGET_PAGE_SIZE) {
            if (memory_region_get_dirty(block->mr, addr, TARGET_PAGE_SIZE,
                                        DIRTY_MEMORY_MIGRATION)) {
                count++;
            }
        }
    }

    return count;
}

uint64_t ram_bytes_remaining(void)
{
    return ram_save_remaining() * TARGET_PAGE_SIZE;
}

uint64_t ram_bytes_transferred(void)
{
    return bytes_transferred;
}

uint64_t ram_bytes_total(void)
{
    RAMBlock *block;
    uint64_t total = 0;

    QLIST_FOREACH(block, &ram_list.blocks, next)
        total += block->length;

    return total;
}

static int block_compar(const void *a, const void *b)
{
    RAMBlock * const *ablock = a;
    RAMBlock * const *bblock = b;

    return strcmp((*ablock)->idstr, (*bblock)->idstr);
}

static void sort_ram_list(void)
{
    RAMBlock *block, *nblock, **blocks;
    int n;
    n = 0;
    QLIST_FOREACH(block, &ram_list.blocks, next) {
        ++n;
    }
    blocks = g_malloc(n * sizeof *blocks);
    n = 0;
    QLIST_FOREACH_SAFE(block, &ram_list.blocks, next, nblock) {
        blocks[n++] = block;
        QLIST_REMOVE(block, next);
    }
    qsort(blocks, n, sizeof *blocks, block_compar);
    while (--n >= 0) {
        QLIST_INSERT_HEAD(&ram_list.blocks, blocks[n], next);
    }
    g_free(blocks);
}

int ram_save_live(QEMUFile *f, int stage, void *opaque)
{
    ram_addr_t addr;
    uint64_t bytes_transferred_last;
    double bwidth = 0;
    uint64_t expected_time = 0;
    int ret;

    if (stage < 0) {
        memory_global_dirty_log_stop();
        return 0;
    }

    memory_global_sync_dirty_bitmap(get_system_memory());

    if (stage == 1) {
        RAMBlock *block;
        bytes_transferred = 0;
        last_block = NULL;
        last_offset = 0;
        sort_ram_list();

        /* Make sure all dirty bits are set */
        QLIST_FOREACH(block, &ram_list.blocks, next) {
            for (addr = 0; addr < block->length; addr += TARGET_PAGE_SIZE) {
                if (!memory_region_get_dirty(block->mr, addr, TARGET_PAGE_SIZE,
                                             DIRTY_MEMORY_MIGRATION)) {
                    memory_region_set_dirty(block->mr, addr, TARGET_PAGE_SIZE);
                }
            }
        }

        memory_global_dirty_log_start();

        qemu_put_be64(f, ram_bytes_total() | RAM_SAVE_FLAG_MEM_SIZE);

        QLIST_FOREACH(block, &ram_list.blocks, next) {
            qemu_put_byte(f, strlen(block->idstr));
            qemu_put_buffer(f, (uint8_t *)block->idstr, strlen(block->idstr));
            qemu_put_be64(f, block->length);
        }
    }

    bytes_transferred_last = bytes_transferred;
    bwidth = qemu_get_clock_ns(rt_clock);

    while ((ret = qemu_file_rate_limit(f)) == 0) {
        int bytes_sent;

        bytes_sent = ram_save_block(f);
        bytes_transferred += bytes_sent;
        if (bytes_sent == 0) { /* no more blocks */
            break;
        }
    }

    if (ret < 0) {
        return ret;
    }

    bwidth = qemu_get_clock_ns(rt_clock) - bwidth;
    bwidth = (bytes_transferred - bytes_transferred_last) / bwidth;

    /* if we haven't transferred anything this round, force expected_time to a
     * a very high value, but without crashing */
    if (bwidth == 0) {
        bwidth = 0.000001;
    }

    /* try transferring iterative blocks of memory */
    if (stage == 3) {
        int bytes_sent;

        /* flush all remaining blocks regardless of rate limiting */
        while ((bytes_sent = ram_save_block(f)) != 0) {
            bytes_transferred += bytes_sent;
        }
        memory_global_dirty_log_stop();
    }

    qemu_put_be64(f, RAM_SAVE_FLAG_EOS);

    expected_time = ram_save_remaining() * TARGET_PAGE_SIZE / bwidth;

    return (stage == 2) && (expected_time <= migrate_max_downtime());
}

static inline void *host_from_stream_offset(QEMUFile *f,
                                            ram_addr_t offset,
                                            int flags)
{
    static RAMBlock *block = NULL;
    char id[256];
    uint8_t len;

    if (flags & RAM_SAVE_FLAG_CONTINUE) {
        if (!block) {
            fprintf(stderr, "Ack, bad migration stream!\n");
            return NULL;
        }

        return memory_region_get_ram_ptr(block->mr) + offset;
    }

    len = qemu_get_byte(f);
    qemu_get_buffer(f, (uint8_t *)id, len);
    id[len] = 0;

    QLIST_FOREACH(block, &ram_list.blocks, next) {
        if (!strncmp(id, block->idstr, sizeof(id)))
            return memory_region_get_ram_ptr(block->mr) + offset;
    }

    fprintf(stderr, "Can't find block %s!\n", id);
    return NULL;
}

int ram_load(QEMUFile *f, void *opaque, int version_id)
{
    ram_addr_t addr;
    int flags;
    int error;

    if (version_id < 4 || version_id > 4) {
        return -EINVAL;
    }

    do {
        addr = qemu_get_be64(f);

        flags = addr & ~TARGET_PAGE_MASK;
        addr &= TARGET_PAGE_MASK;

        if (flags & RAM_SAVE_FLAG_MEM_SIZE) {
            if (version_id == 4) {
                /* Synchronize RAM block list */
                char id[256];
                ram_addr_t length;
                ram_addr_t total_ram_bytes = addr;

                while (total_ram_bytes) {
                    RAMBlock *block;
                    uint8_t len;

                    len = qemu_get_byte(f);
                    qemu_get_buffer(f, (uint8_t *)id, len);
                    id[len] = 0;
                    length = qemu_get_be64(f);

                    QLIST_FOREACH(block, &ram_list.blocks, next) {
                        if (!strncmp(id, block->idstr, sizeof(id))) {
                            if (block->length != length)
                                return -EINVAL;
                            break;
                        }
                    }

                    if (!block) {
                        fprintf(stderr, "Unknown ramblock \"%s\", cannot "
                                "accept migration\n", id);
                        return -EINVAL;
                    }

                    total_ram_bytes -= length;
                }
            }
        }

        if (flags & RAM_SAVE_FLAG_COMPRESS) {
            void *host;
            uint8_t ch;

            host = host_from_stream_offset(f, addr, flags);
            if (!host) {
                return -EINVAL;
            }

            ch = qemu_get_byte(f);
            memset(host, ch, TARGET_PAGE_SIZE);
#ifndef _WIN32
            if (ch == 0 &&
                (!kvm_enabled() || kvm_has_sync_mmu())) {
                qemu_madvise(host, TARGET_PAGE_SIZE, QEMU_MADV_DONTNEED);
            }
#endif
        } else if (flags & RAM_SAVE_FLAG_PAGE) {
            void *host;

            host = host_from_stream_offset(f, addr, flags);

            qemu_get_buffer(f, host, TARGET_PAGE_SIZE);
        }
        error = qemu_file_get_error(f);
        if (error) {
            return error;
        }
    } while (!(flags & RAM_SAVE_FLAG_EOS));

    return 0;
}

#ifdef HAS_AUDIO
struct soundhw {
    const char *name;
    const char *descr;
    int enabled;
    int isa;
    union {
        int (*init_isa) (ISABus *bus);
        int (*init_pci) (PCIBus *bus);
    } init;
};

static struct soundhw soundhw[] = {
#ifdef HAS_AUDIO_CHOICE
#if defined(TARGET_I386) || defined(TARGET_MIPS)
    {
        "pcspk",
        "PC speaker",
        0,
        1,
        { .init_isa = pcspk_audio_init }
    },
#endif

#ifdef CONFIG_SB16
    {
        "sb16",
        "Creative Sound Blaster 16",
        0,
        1,
        { .init_isa = SB16_init }
    },
#endif

#ifdef CONFIG_CS4231A
    {
        "cs4231a",
        "CS4231A",
        0,
        1,
        { .init_isa = cs4231a_init }
    },
#endif

#ifdef CONFIG_ADLIB
    {
        "adlib",
#ifdef HAS_YMF262
        "Yamaha YMF262 (OPL3)",
#else
        "Yamaha YM3812 (OPL2)",
#endif
        0,
        1,
        { .init_isa = Adlib_init }
    },
#endif

#ifdef CONFIG_GUS
    {
        "gus",
        "Gravis Ultrasound GF1",
        0,
        1,
        { .init_isa = GUS_init }
    },
#endif

#ifdef CONFIG_AC97
    {
        "ac97",
        "Intel 82801AA AC97 Audio",
        0,
        0,
        { .init_pci = ac97_init }
    },
#endif

#ifdef CONFIG_ES1370
    {
        "es1370",
        "ENSONIQ AudioPCI ES1370",
        0,
        0,
        { .init_pci = es1370_init }
    },
#endif

#ifdef CONFIG_HDA
    {
        "hda",
        "Intel HD Audio",
        0,
        0,
        { .init_pci = intel_hda_and_codec_init }
    },
#endif

#endif /* HAS_AUDIO_CHOICE */

    { NULL, NULL, 0, 0, { NULL } }
};

void select_soundhw(const char *optarg)
{
    struct soundhw *c;

    if (*optarg == '?') {
    show_valid_cards:

        printf("Valid sound card names (comma separated):\n");
        for (c = soundhw; c->name; ++c) {
            printf ("%-11s %s\n", c->name, c->descr);
        }
        printf("\n-soundhw all will enable all of the above\n");
        exit(*optarg != '?');
    }
    else {
        size_t l;
        const char *p;
        char *e;
        int bad_card = 0;

        if (!strcmp(optarg, "all")) {
            for (c = soundhw; c->name; ++c) {
                c->enabled = 1;
            }
            return;
        }

        p = optarg;
        while (*p) {
            e = strchr(p, ',');
            l = !e ? strlen(p) : (size_t) (e - p);

            for (c = soundhw; c->name; ++c) {
                if (!strncmp(c->name, p, l) && !c->name[l]) {
                    c->enabled = 1;
                    break;
                }
            }

            if (!c->name) {
                if (l > 80) {
                    fprintf(stderr,
                            "Unknown sound card name (too big to show)\n");
                }
                else {
                    fprintf(stderr, "Unknown sound card name `%.*s'\n",
                            (int) l, p);
                }
                bad_card = 1;
            }
            p += l + (e != NULL);
        }

        if (bad_card) {
            goto show_valid_cards;
        }
    }
}

void audio_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    struct soundhw *c;

    for (c = soundhw; c->name; ++c) {
        if (c->enabled) {
            if (c->isa) {
                if (isa_bus) {
                    c->init.init_isa(isa_bus);
                }
            } else {
                if (pci_bus) {
                    c->init.init_pci(pci_bus);
                }
            }
        }
    }
}
#else
void select_soundhw(const char *optarg)
{
}
void audio_init(ISABus *isa_bus, PCIBus *pci_bus)
{
}
#endif

int qemu_uuid_parse(const char *str, uint8_t *uuid)
{
    int ret;

    if (strlen(str) != 36) {
        return -1;
    }

    ret = sscanf(str, UUID_FMT, &uuid[0], &uuid[1], &uuid[2], &uuid[3],
                 &uuid[4], &uuid[5], &uuid[6], &uuid[7], &uuid[8], &uuid[9],
                 &uuid[10], &uuid[11], &uuid[12], &uuid[13], &uuid[14],
                 &uuid[15]);

    if (ret != 16) {
        return -1;
    }
#ifdef TARGET_I386
    smbios_add_field(1, offsetof(struct smbios_type_1, uuid), 16, uuid);
#endif
    return 0;
}

void do_acpitable_option(const char *optarg)
{
#ifdef TARGET_I386
    if (acpi_table_add(optarg) < 0) {
        fprintf(stderr, "Wrong acpi table provided\n");
        exit(1);
    }
#endif
}

void do_smbios_option(const char *optarg)
{
#ifdef TARGET_I386
    if (smbios_entry_add(optarg) < 0) {
        fprintf(stderr, "Wrong smbios provided\n");
        exit(1);
    }
#endif
}

void cpudef_init(void)
{
#if defined(cpudef_setup)
    cpudef_setup(); /* parse cpu definitions in target config file */
#endif
}

int audio_available(void)
{
#ifdef HAS_AUDIO
    return 1;
#else
    return 0;
#endif
}

int tcg_available(void)
{
    return 1;
}

int kvm_available(void)
{
#ifdef CONFIG_KVM
    return 1;
#else
    return 0;
#endif
}

int xen_available(void)
{
#ifdef CONFIG_XEN
    return 1;
#else
    return 0;
#endif
}
