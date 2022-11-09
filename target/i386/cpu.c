/*
 *  i386 CPUID, CPU class, definitions, models
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qemu/qemu-print.h"
#include "qemu/hw-version.h"
#include "cpu.h"
#include "tcg/helper-tcg.h"
#include "sysemu/reset.h"
#include "sysemu/hvf.h"
#include "kvm/kvm_i386.h"
#include "sev.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qapi-commands-machine-target.h"
#include "standard-headers/asm-x86/kvm_para.h"
#include "hw/qdev-properties.h"
#include "hw/i386/topology.h"
#ifndef CONFIG_USER_ONLY
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/i386/sgx-epc.h"
#endif

#include "disas/capstone.h"
#include "cpu-internal.h"

/* Helpers for building CPUID[2] descriptors: */

struct CPUID2CacheDescriptorInfo {
    enum CacheType type;
    int level;
    int size;
    int line_size;
    int associativity;
};

/*
 * Known CPUID 2 cache descriptors.
 * From Intel SDM Volume 2A, CPUID instruction
 */
struct CPUID2CacheDescriptorInfo cpuid2_cache_descriptors[] = {
    [0x06] = { .level = 1, .type = INSTRUCTION_CACHE, .size =   8 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x08] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  16 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x09] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  32 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x0A] = { .level = 1, .type = DATA_CACHE,        .size =   8 * KiB,
               .associativity = 2,  .line_size = 32, },
    [0x0C] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x0D] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x0E] = { .level = 1, .type = DATA_CACHE,        .size =  24 * KiB,
               .associativity = 6,  .line_size = 64, },
    [0x1D] = { .level = 2, .type = UNIFIED_CACHE,     .size = 128 * KiB,
               .associativity = 2,  .line_size = 64, },
    [0x21] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 8,  .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x22, 0x23 are not included
    */
    [0x24] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 16, .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x25, 0x20 are not included
    */
    [0x2C] = { .level = 1, .type = DATA_CACHE,        .size =  32 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x30] = { .level = 1, .type = INSTRUCTION_CACHE, .size =  32 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x41] = { .level = 2, .type = UNIFIED_CACHE,     .size = 128 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x42] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x43] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 32, },
    [0x44] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 32, },
    [0x45] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 4,  .line_size = 32, },
    [0x46] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0x47] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0x48] = { .level = 2, .type = UNIFIED_CACHE,     .size =   3 * MiB,
               .associativity = 12, .line_size = 64, },
    /* Descriptor 0x49 depends on CPU family/model, so it is not included */
    [0x4A] = { .level = 3, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 12, .line_size = 64, },
    [0x4B] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 16, .line_size = 64, },
    [0x4C] = { .level = 3, .type = UNIFIED_CACHE,     .size =  12 * MiB,
               .associativity = 12, .line_size = 64, },
    [0x4D] = { .level = 3, .type = UNIFIED_CACHE,     .size =  16 * MiB,
               .associativity = 16, .line_size = 64, },
    [0x4E] = { .level = 2, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 24, .line_size = 64, },
    [0x60] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x66] = { .level = 1, .type = DATA_CACHE,        .size =   8 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x67] = { .level = 1, .type = DATA_CACHE,        .size =  16 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x68] = { .level = 1, .type = DATA_CACHE,        .size =  32 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x78] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 64, },
    /* lines per sector is not supported cpuid2_cache_descriptor(),
    * so descriptors 0x79, 0x7A, 0x7B, 0x7C are not included.
    */
    [0x7D] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0x7F] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 2,  .line_size = 64, },
    [0x80] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 8,  .line_size = 64, },
    [0x82] = { .level = 2, .type = UNIFIED_CACHE,     .size = 256 * KiB,
               .associativity = 8,  .line_size = 32, },
    [0x83] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 8,  .line_size = 32, },
    [0x84] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 32, },
    [0x85] = { .level = 2, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 32, },
    [0x86] = { .level = 2, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0x87] = { .level = 2, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD0] = { .level = 3, .type = UNIFIED_CACHE,     .size = 512 * KiB,
               .associativity = 4,  .line_size = 64, },
    [0xD1] = { .level = 3, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0xD2] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 4,  .line_size = 64, },
    [0xD6] = { .level = 3, .type = UNIFIED_CACHE,     .size =   1 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD7] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xD8] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 8,  .line_size = 64, },
    [0xDC] = { .level = 3, .type = UNIFIED_CACHE,     .size = 1.5 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xDD] = { .level = 3, .type = UNIFIED_CACHE,     .size =   3 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xDE] = { .level = 3, .type = UNIFIED_CACHE,     .size =   6 * MiB,
               .associativity = 12, .line_size = 64, },
    [0xE2] = { .level = 3, .type = UNIFIED_CACHE,     .size =   2 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xE3] = { .level = 3, .type = UNIFIED_CACHE,     .size =   4 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xE4] = { .level = 3, .type = UNIFIED_CACHE,     .size =   8 * MiB,
               .associativity = 16, .line_size = 64, },
    [0xEA] = { .level = 3, .type = UNIFIED_CACHE,     .size =  12 * MiB,
               .associativity = 24, .line_size = 64, },
    [0xEB] = { .level = 3, .type = UNIFIED_CACHE,     .size =  18 * MiB,
               .associativity = 24, .line_size = 64, },
    [0xEC] = { .level = 3, .type = UNIFIED_CACHE,     .size =  24 * MiB,
               .associativity = 24, .line_size = 64, },
};

/*
 * "CPUID leaf 2 does not report cache descriptor information,
 * use CPUID leaf 4 to query cache parameters"
 */
#define CACHE_DESCRIPTOR_UNAVAILABLE 0xFF

/*
 * Return a CPUID 2 cache descriptor for a given cache.
 * If no known descriptor is found, return CACHE_DESCRIPTOR_UNAVAILABLE
 */
static uint8_t cpuid2_cache_descriptor(CPUCacheInfo *cache)
{
    int i;

    assert(cache->size > 0);
    assert(cache->level > 0);
    assert(cache->line_size > 0);
    assert(cache->associativity > 0);
    for (i = 0; i < ARRAY_SIZE(cpuid2_cache_descriptors); i++) {
        struct CPUID2CacheDescriptorInfo *d = &cpuid2_cache_descriptors[i];
        if (d->level == cache->level && d->type == cache->type &&
            d->size == cache->size && d->line_size == cache->line_size &&
            d->associativity == cache->associativity) {
                return i;
            }
    }

    return CACHE_DESCRIPTOR_UNAVAILABLE;
}

/* CPUID Leaf 4 constants: */

/* EAX: */
#define CACHE_TYPE_D    1
#define CACHE_TYPE_I    2
#define CACHE_TYPE_UNIFIED   3

#define CACHE_LEVEL(l)        (l << 5)

#define CACHE_SELF_INIT_LEVEL (1 << 8)

/* EDX: */
#define CACHE_NO_INVD_SHARING   (1 << 0)
#define CACHE_INCLUSIVE       (1 << 1)
#define CACHE_COMPLEX_IDX     (1 << 2)

/* Encode CacheType for CPUID[4].EAX */
#define CACHE_TYPE(t) (((t) == DATA_CACHE) ? CACHE_TYPE_D : \
                       ((t) == INSTRUCTION_CACHE) ? CACHE_TYPE_I : \
                       ((t) == UNIFIED_CACHE) ? CACHE_TYPE_UNIFIED : \
                       0 /* Invalid value */)


/* Encode cache info for CPUID[4] */
static void encode_cache_cpuid4(CPUCacheInfo *cache,
                                int num_apic_ids, int num_cores,
                                uint32_t *eax, uint32_t *ebx,
                                uint32_t *ecx, uint32_t *edx)
{
    assert(cache->size == cache->line_size * cache->associativity *
                          cache->partitions * cache->sets);

    assert(num_apic_ids > 0);
    *eax = CACHE_TYPE(cache->type) |
           CACHE_LEVEL(cache->level) |
           (cache->self_init ? CACHE_SELF_INIT_LEVEL : 0) |
           ((num_cores - 1) << 26) |
           ((num_apic_ids - 1) << 14);

    assert(cache->line_size > 0);
    assert(cache->partitions > 0);
    assert(cache->associativity > 0);
    /* We don't implement fully-associative caches */
    assert(cache->associativity < cache->sets);
    *ebx = (cache->line_size - 1) |
           ((cache->partitions - 1) << 12) |
           ((cache->associativity - 1) << 22);

    assert(cache->sets > 0);
    *ecx = cache->sets - 1;

    *edx = (cache->no_invd_sharing ? CACHE_NO_INVD_SHARING : 0) |
           (cache->inclusive ? CACHE_INCLUSIVE : 0) |
           (cache->complex_indexing ? CACHE_COMPLEX_IDX : 0);
}

/* Encode cache info for CPUID[0x80000005].ECX or CPUID[0x80000005].EDX */
static uint32_t encode_cache_cpuid80000005(CPUCacheInfo *cache)
{
    assert(cache->size % 1024 == 0);
    assert(cache->lines_per_tag > 0);
    assert(cache->associativity > 0);
    assert(cache->line_size > 0);
    return ((cache->size / 1024) << 24) | (cache->associativity << 16) |
           (cache->lines_per_tag << 8) | (cache->line_size);
}

#define ASSOC_FULL 0xFF

/* AMD associativity encoding used on CPUID Leaf 0x80000006: */
#define AMD_ENC_ASSOC(a) (a <=   1 ? a   : \
                          a ==   2 ? 0x2 : \
                          a ==   4 ? 0x4 : \
                          a ==   8 ? 0x6 : \
                          a ==  16 ? 0x8 : \
                          a ==  32 ? 0xA : \
                          a ==  48 ? 0xB : \
                          a ==  64 ? 0xC : \
                          a ==  96 ? 0xD : \
                          a == 128 ? 0xE : \
                          a == ASSOC_FULL ? 0xF : \
                          0 /* invalid value */)

/*
 * Encode cache info for CPUID[0x80000006].ECX and CPUID[0x80000006].EDX
 * @l3 can be NULL.
 */
static void encode_cache_cpuid80000006(CPUCacheInfo *l2,
                                       CPUCacheInfo *l3,
                                       uint32_t *ecx, uint32_t *edx)
{
    assert(l2->size % 1024 == 0);
    assert(l2->associativity > 0);
    assert(l2->lines_per_tag > 0);
    assert(l2->line_size > 0);
    *ecx = ((l2->size / 1024) << 16) |
           (AMD_ENC_ASSOC(l2->associativity) << 12) |
           (l2->lines_per_tag << 8) | (l2->line_size);

    if (l3) {
        assert(l3->size % (512 * 1024) == 0);
        assert(l3->associativity > 0);
        assert(l3->lines_per_tag > 0);
        assert(l3->line_size > 0);
        *edx = ((l3->size / (512 * 1024)) << 18) |
               (AMD_ENC_ASSOC(l3->associativity) << 12) |
               (l3->lines_per_tag << 8) | (l3->line_size);
    } else {
        *edx = 0;
    }
}

/* Encode cache info for CPUID[8000001D] */
static void encode_cache_cpuid8000001d(CPUCacheInfo *cache,
                                       X86CPUTopoInfo *topo_info,
                                       uint32_t *eax, uint32_t *ebx,
                                       uint32_t *ecx, uint32_t *edx)
{
    uint32_t l3_threads;
    assert(cache->size == cache->line_size * cache->associativity *
                          cache->partitions * cache->sets);

    *eax = CACHE_TYPE(cache->type) | CACHE_LEVEL(cache->level) |
               (cache->self_init ? CACHE_SELF_INIT_LEVEL : 0);

    /* L3 is shared among multiple cores */
    if (cache->level == 3) {
        l3_threads = topo_info->cores_per_die * topo_info->threads_per_core;
        *eax |= (l3_threads - 1) << 14;
    } else {
        *eax |= ((topo_info->threads_per_core - 1) << 14);
    }

    assert(cache->line_size > 0);
    assert(cache->partitions > 0);
    assert(cache->associativity > 0);
    /* We don't implement fully-associative caches */
    assert(cache->associativity < cache->sets);
    *ebx = (cache->line_size - 1) |
           ((cache->partitions - 1) << 12) |
           ((cache->associativity - 1) << 22);

    assert(cache->sets > 0);
    *ecx = cache->sets - 1;

    *edx = (cache->no_invd_sharing ? CACHE_NO_INVD_SHARING : 0) |
           (cache->inclusive ? CACHE_INCLUSIVE : 0) |
           (cache->complex_indexing ? CACHE_COMPLEX_IDX : 0);
}

/* Encode cache info for CPUID[8000001E] */
static void encode_topo_cpuid8000001e(X86CPU *cpu, X86CPUTopoInfo *topo_info,
                                      uint32_t *eax, uint32_t *ebx,
                                      uint32_t *ecx, uint32_t *edx)
{
    X86CPUTopoIDs topo_ids;

    x86_topo_ids_from_apicid(cpu->apic_id, topo_info, &topo_ids);

    *eax = cpu->apic_id;

    /*
     * CPUID_Fn8000001E_EBX [Core Identifiers] (CoreId)
     * Read-only. Reset: 0000_XXXXh.
     * See Core::X86::Cpuid::ExtApicId.
     * Core::X86::Cpuid::CoreId_lthree[1:0]_core[3:0]_thread[1:0];
     * Bits Description
     * 31:16 Reserved.
     * 15:8 ThreadsPerCore: threads per core. Read-only. Reset: XXh.
     *      The number of threads per core is ThreadsPerCore+1.
     *  7:0 CoreId: core ID. Read-only. Reset: XXh.
     *
     *  NOTE: CoreId is already part of apic_id. Just use it. We can
     *  use all the 8 bits to represent the core_id here.
     */
    *ebx = ((topo_info->threads_per_core - 1) << 8) | (topo_ids.core_id & 0xFF);

    /*
     * CPUID_Fn8000001E_ECX [Node Identifiers] (NodeId)
     * Read-only. Reset: 0000_0XXXh.
     * Core::X86::Cpuid::NodeId_lthree[1:0]_core[3:0]_thread[1:0];
     * Bits Description
     * 31:11 Reserved.
     * 10:8 NodesPerProcessor: Node per processor. Read-only. Reset: XXXb.
     *      ValidValues:
     *      Value Description
     *      000b  1 node per processor.
     *      001b  2 nodes per processor.
     *      010b Reserved.
     *      011b 4 nodes per processor.
     *      111b-100b Reserved.
     *  7:0 NodeId: Node ID. Read-only. Reset: XXh.
     *
     * NOTE: Hardware reserves 3 bits for number of nodes per processor.
     * But users can create more nodes than the actual hardware can
     * support. To genaralize we can use all the upper 8 bits for nodes.
     * NodeId is combination of node and socket_id which is already decoded
     * in apic_id. Just use it by shifting.
     */
    *ecx = ((topo_info->dies_per_pkg - 1) << 8) |
           ((cpu->apic_id >> apicid_die_offset(topo_info)) & 0xFF);

    *edx = 0;
}

/*
 * Definitions of the hardcoded cache entries we expose:
 * These are legacy cache values. If there is a need to change any
 * of these values please use builtin_x86_defs
 */

/* L1 data cache: */
static CPUCacheInfo legacy_l1d_cache = {
    .type = DATA_CACHE,
    .level = 1,
    .size = 32 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 8,
    .sets = 64,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l1d_cache_amd = {
    .type = DATA_CACHE,
    .level = 1,
    .size = 64 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 2,
    .sets = 512,
    .partitions = 1,
    .lines_per_tag = 1,
    .no_invd_sharing = true,
};

/* L1 instruction cache: */
static CPUCacheInfo legacy_l1i_cache = {
    .type = INSTRUCTION_CACHE,
    .level = 1,
    .size = 32 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 8,
    .sets = 64,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 0x80000005 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l1i_cache_amd = {
    .type = INSTRUCTION_CACHE,
    .level = 1,
    .size = 64 * KiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 2,
    .sets = 512,
    .partitions = 1,
    .lines_per_tag = 1,
    .no_invd_sharing = true,
};

/* Level 2 unified cache: */
static CPUCacheInfo legacy_l2_cache = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 4 * MiB,
    .self_init = 1,
    .line_size = 64,
    .associativity = 16,
    .sets = 4096,
    .partitions = 1,
    .no_invd_sharing = true,
};

/*FIXME: CPUID leaf 2 descriptor is inconsistent with CPUID leaf 4 */
static CPUCacheInfo legacy_l2_cache_cpuid2 = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 2 * MiB,
    .line_size = 64,
    .associativity = 8,
};


/*FIXME: CPUID leaf 0x80000006 is inconsistent with leaves 2 & 4 */
static CPUCacheInfo legacy_l2_cache_amd = {
    .type = UNIFIED_CACHE,
    .level = 2,
    .size = 512 * KiB,
    .line_size = 64,
    .lines_per_tag = 1,
    .associativity = 16,
    .sets = 512,
    .partitions = 1,
};

/* Level 3 unified cache: */
static CPUCacheInfo legacy_l3_cache = {
    .type = UNIFIED_CACHE,
    .level = 3,
    .size = 16 * MiB,
    .line_size = 64,
    .associativity = 16,
    .sets = 16384,
    .partitions = 1,
    .lines_per_tag = 1,
    .self_init = true,
    .inclusive = true,
    .complex_indexing = true,
};

/* TLB definitions: */

#define L1_DTLB_2M_ASSOC       1
#define L1_DTLB_2M_ENTRIES   255
#define L1_DTLB_4K_ASSOC       1
#define L1_DTLB_4K_ENTRIES   255

#define L1_ITLB_2M_ASSOC       1
#define L1_ITLB_2M_ENTRIES   255
#define L1_ITLB_4K_ASSOC       1
#define L1_ITLB_4K_ENTRIES   255

#define L2_DTLB_2M_ASSOC       0 /* disabled */
#define L2_DTLB_2M_ENTRIES     0 /* disabled */
#define L2_DTLB_4K_ASSOC       4
#define L2_DTLB_4K_ENTRIES   512

#define L2_ITLB_2M_ASSOC       0 /* disabled */
#define L2_ITLB_2M_ENTRIES     0 /* disabled */
#define L2_ITLB_4K_ASSOC       4
#define L2_ITLB_4K_ENTRIES   512

/* CPUID Leaf 0x14 constants: */
#define INTEL_PT_MAX_SUBLEAF     0x1
/*
 * bit[00]: IA32_RTIT_CTL.CR3 filter can be set to 1 and IA32_RTIT_CR3_MATCH
 *          MSR can be accessed;
 * bit[01]: Support Configurable PSB and Cycle-Accurate Mode;
 * bit[02]: Support IP Filtering, TraceStop filtering, and preservation
 *          of Intel PT MSRs across warm reset;
 * bit[03]: Support MTC timing packet and suppression of COFI-based packets;
 */
#define INTEL_PT_MINIMAL_EBX     0xf
/*
 * bit[00]: Tracing can be enabled with IA32_RTIT_CTL.ToPA = 1 and
 *          IA32_RTIT_OUTPUT_BASE and IA32_RTIT_OUTPUT_MASK_PTRS MSRs can be
 *          accessed;
 * bit[01]: ToPA tables can hold any number of output entries, up to the
 *          maximum allowed by the MaskOrTableOffset field of
 *          IA32_RTIT_OUTPUT_MASK_PTRS;
 * bit[02]: Support Single-Range Output scheme;
 */
#define INTEL_PT_MINIMAL_ECX     0x7
/* generated packets which contain IP payloads have LIP values */
#define INTEL_PT_IP_LIP          (1 << 31)
#define INTEL_PT_ADDR_RANGES_NUM 0x2 /* Number of configurable address ranges */
#define INTEL_PT_ADDR_RANGES_NUM_MASK 0x3
#define INTEL_PT_MTC_BITMAP      (0x0249 << 16) /* Support ART(0,3,6,9) */
#define INTEL_PT_CYCLE_BITMAP    0x1fff         /* Support 0,2^(0~11) */
#define INTEL_PT_PSB_BITMAP      (0x003f << 16) /* Support 2K,4K,8K,16K,32K,64K */

/* CPUID Leaf 0x1D constants: */
#define INTEL_AMX_TILE_MAX_SUBLEAF     0x1
#define INTEL_AMX_TOTAL_TILE_BYTES     0x2000
#define INTEL_AMX_BYTES_PER_TILE       0x400
#define INTEL_AMX_BYTES_PER_ROW        0x40
#define INTEL_AMX_TILE_MAX_NAMES       0x8
#define INTEL_AMX_TILE_MAX_ROWS        0x10

/* CPUID Leaf 0x1E constants: */
#define INTEL_AMX_TMUL_MAX_K           0x10
#define INTEL_AMX_TMUL_MAX_N           0x40

void x86_cpu_vendor_words2str(char *dst, uint32_t vendor1,
                              uint32_t vendor2, uint32_t vendor3)
{
    int i;
    for (i = 0; i < 4; i++) {
        dst[i] = vendor1 >> (8 * i);
        dst[i + 4] = vendor2 >> (8 * i);
        dst[i + 8] = vendor3 >> (8 * i);
    }
    dst[CPUID_VENDOR_SZ] = '\0';
}

#define I486_FEATURES (CPUID_FP87 | CPUID_VME | CPUID_PSE)
#define PENTIUM_FEATURES (I486_FEATURES | CPUID_DE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_MMX | CPUID_APIC)
#define PENTIUM2_FEATURES (PENTIUM_FEATURES | CPUID_PAE | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_FXSR)
#define PENTIUM3_FEATURES (PENTIUM2_FEATURES | CPUID_SSE)
#define PPRO_FEATURES (CPUID_FP87 | CPUID_DE | CPUID_PSE | CPUID_TSC | \
          CPUID_MSR | CPUID_MCE | CPUID_CX8 | CPUID_PGE | CPUID_CMOV | \
          CPUID_PAT | CPUID_FXSR | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | \
          CPUID_PAE | CPUID_SEP | CPUID_APIC)

#define TCG_FEATURES (CPUID_FP87 | CPUID_PSE | CPUID_TSC | CPUID_MSR | \
          CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC | CPUID_SEP | \
          CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV | CPUID_PAT | \
          CPUID_PSE36 | CPUID_CLFLUSH | CPUID_ACPI | CPUID_MMX | \
          CPUID_FXSR | CPUID_SSE | CPUID_SSE2 | CPUID_SS | CPUID_DE)
          /* partly implemented:
          CPUID_MTRR, CPUID_MCA, CPUID_CLFLUSH (needed for Win64) */
          /* missing:
          CPUID_VME, CPUID_DTS, CPUID_SS, CPUID_HT, CPUID_TM, CPUID_PBE */
#define TCG_EXT_FEATURES (CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | \
          CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | \
          CPUID_EXT_SSE41 | CPUID_EXT_SSE42 | CPUID_EXT_POPCNT | \
          CPUID_EXT_XSAVE | /* CPUID_EXT_OSXSAVE is dynamic */   \
          CPUID_EXT_MOVBE | CPUID_EXT_AES | CPUID_EXT_HYPERVISOR | \
          CPUID_EXT_RDRAND | CPUID_EXT_AVX | CPUID_EXT_F16C | \
          CPUID_EXT_FMA)
          /* missing:
          CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_VMX, CPUID_EXT_SMX,
          CPUID_EXT_EST, CPUID_EXT_TM2, CPUID_EXT_CID,
          CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_PCID, CPUID_EXT_DCA,
          CPUID_EXT_X2APIC, CPUID_EXT_TSC_DEADLINE_TIMER */

#ifdef TARGET_X86_64
#define TCG_EXT2_X86_64_FEATURES (CPUID_EXT2_SYSCALL | CPUID_EXT2_LM)
#else
#define TCG_EXT2_X86_64_FEATURES 0
#endif

#define TCG_EXT2_FEATURES ((TCG_FEATURES & CPUID_EXT2_AMD_ALIASES) | \
          CPUID_EXT2_NX | CPUID_EXT2_MMXEXT | CPUID_EXT2_RDTSCP | \
          CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_PDPE1GB | \
          TCG_EXT2_X86_64_FEATURES)
#define TCG_EXT3_FEATURES (CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM | \
          CPUID_EXT3_CR8LEG | CPUID_EXT3_ABM | CPUID_EXT3_SSE4A)
#define TCG_EXT4_FEATURES 0
#define TCG_SVM_FEATURES (CPUID_SVM_NPT | CPUID_SVM_VGIF | \
          CPUID_SVM_SVME_ADDR_CHK)
#define TCG_KVM_FEATURES 0
#define TCG_7_0_EBX_FEATURES (CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_SMAP | \
          CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ADX | \
          CPUID_7_0_EBX_PCOMMIT | CPUID_7_0_EBX_CLFLUSHOPT |            \
          CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_FSGSBASE | \
          CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_AVX2)
          /* missing:
          CPUID_7_0_EBX_HLE
          CPUID_7_0_EBX_INVPCID, CPUID_7_0_EBX_RTM,
          CPUID_7_0_EBX_RDSEED */
#define TCG_7_0_ECX_FEATURES (CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU | \
          /* CPUID_7_0_ECX_OSPKE is dynamic */ \
          CPUID_7_0_ECX_LA57 | CPUID_7_0_ECX_PKS | CPUID_7_0_ECX_VAES)
#define TCG_7_0_EDX_FEATURES 0
#define TCG_7_1_EAX_FEATURES 0
#define TCG_APM_FEATURES 0
#define TCG_6_EAX_FEATURES CPUID_6_EAX_ARAT
#define TCG_XSAVE_FEATURES (CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XGETBV1)
          /* missing:
          CPUID_XSAVE_XSAVEC, CPUID_XSAVE_XSAVES */
#define TCG_14_0_ECX_FEATURES 0
#define TCG_SGX_12_0_EAX_FEATURES 0
#define TCG_SGX_12_0_EBX_FEATURES 0
#define TCG_SGX_12_1_EAX_FEATURES 0

FeatureWordInfo feature_word_info[FEATURE_WORDS] = {
    [FEAT_1_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "fpu", "vme", "de", "pse",
            "tsc", "msr", "pae", "mce",
            "cx8", "apic", NULL, "sep",
            "mtrr", "pge", "mca", "cmov",
            "pat", "pse36", "pn" /* Intel psn */, "clflush" /* Intel clfsh */,
            NULL, "ds" /* Intel dts */, "acpi", "mmx",
            "fxsr", "sse", "sse2", "ss",
            "ht" /* Intel htt */, "tm", "ia64", "pbe",
        },
        .cpuid = {.eax = 1, .reg = R_EDX, },
        .tcg_features = TCG_FEATURES,
    },
    [FEAT_1_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "pni" /* Intel,AMD sse3 */, "pclmulqdq", "dtes64", "monitor",
            "ds-cpl", "vmx", "smx", "est",
            "tm2", "ssse3", "cid", NULL,
            "fma", "cx16", "xtpr", "pdcm",
            NULL, "pcid", "dca", "sse4.1",
            "sse4.2", "x2apic", "movbe", "popcnt",
            "tsc-deadline", "aes", "xsave", NULL /* osxsave */,
            "avx", "f16c", "rdrand", "hypervisor",
        },
        .cpuid = { .eax = 1, .reg = R_ECX, },
        .tcg_features = TCG_EXT_FEATURES,
    },
    /* Feature names that are already defined on feature_name[] but
     * are set on CPUID[8000_0001].EDX on AMD CPUs don't have their
     * names on feat_names below. They are copied automatically
     * to features[FEAT_8000_0001_EDX] if and only if CPU vendor is AMD.
     */
    [FEAT_8000_0001_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL /* fpu */, NULL /* vme */, NULL /* de */, NULL /* pse */,
            NULL /* tsc */, NULL /* msr */, NULL /* pae */, NULL /* mce */,
            NULL /* cx8 */, NULL /* apic */, NULL, "syscall",
            NULL /* mtrr */, NULL /* pge */, NULL /* mca */, NULL /* cmov */,
            NULL /* pat */, NULL /* pse36 */, NULL, NULL /* Linux mp */,
            "nx", NULL, "mmxext", NULL /* mmx */,
            NULL /* fxsr */, "fxsr-opt", "pdpe1gb", "rdtscp",
            NULL, "lm", "3dnowext", "3dnow",
        },
        .cpuid = { .eax = 0x80000001, .reg = R_EDX, },
        .tcg_features = TCG_EXT2_FEATURES,
    },
    [FEAT_8000_0001_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "lahf-lm", "cmp-legacy", "svm", "extapic",
            "cr8legacy", "abm", "sse4a", "misalignsse",
            "3dnowprefetch", "osvw", "ibs", "xop",
            "skinit", "wdt", NULL, "lwp",
            "fma4", "tce", NULL, "nodeid-msr",
            NULL, "tbm", "topoext", "perfctr-core",
            "perfctr-nb", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000001, .reg = R_ECX, },
        .tcg_features = TCG_EXT3_FEATURES,
        /*
         * TOPOEXT is always allowed but can't be enabled blindly by
         * "-cpu host", as it requires consistent cache topology info
         * to be provided so it doesn't confuse guests.
         */
        .no_autoenable_flags = CPUID_EXT3_TOPOEXT,
    },
    [FEAT_C000_0001_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "xstore", "xstore-en",
            NULL, NULL, "xcrypt", "xcrypt-en",
            "ace2", "ace2-en", "phe", "phe-en",
            "pmm", "pmm-en", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0xC0000001, .reg = R_EDX, },
        .tcg_features = TCG_EXT4_FEATURES,
    },
    [FEAT_KVM] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "kvmclock", "kvm-nopiodelay", "kvm-mmu", "kvmclock",
            "kvm-asyncpf", "kvm-steal-time", "kvm-pv-eoi", "kvm-pv-unhalt",
            NULL, "kvm-pv-tlb-flush", NULL, "kvm-pv-ipi",
            "kvm-poll-control", "kvm-pv-sched-yield", "kvm-asyncpf-int", "kvm-msi-ext-dest-id",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "kvmclock-stable-bit", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = KVM_CPUID_FEATURES, .reg = R_EAX, },
        .tcg_features = TCG_KVM_FEATURES,
    },
    [FEAT_KVM_HINTS] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "kvm-hint-dedicated", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = KVM_CPUID_FEATURES, .reg = R_EDX, },
        .tcg_features = TCG_KVM_FEATURES,
        /*
         * KVM hints aren't auto-enabled by -cpu host, they need to be
         * explicitly enabled in the command-line.
         */
        .no_autoenable_flags = ~0U,
    },
    [FEAT_SVM] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "npt", "lbrv", "svm-lock", "nrip-save",
            "tsc-scale", "vmcb-clean",  "flushbyasid", "decodeassists",
            NULL, NULL, "pause-filter", NULL,
            "pfthreshold", "avic", NULL, "v-vmsave-vmload",
            "vgif", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "svme-addr-chk", NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x8000000A, .reg = R_EDX, },
        .tcg_features = TCG_SVM_FEATURES,
    },
    [FEAT_7_0_EBX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "fsgsbase", "tsc-adjust", "sgx", "bmi1",
            "hle", "avx2", NULL, "smep",
            "bmi2", "erms", "invpcid", "rtm",
            NULL, NULL, "mpx", NULL,
            "avx512f", "avx512dq", "rdseed", "adx",
            "smap", "avx512ifma", "pcommit", "clflushopt",
            "clwb", "intel-pt", "avx512pf", "avx512er",
            "avx512cd", "sha-ni", "avx512bw", "avx512vl",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EBX,
        },
        .tcg_features = TCG_7_0_EBX_FEATURES,
    },
    [FEAT_7_0_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, "avx512vbmi", "umip", "pku",
            NULL /* ospke */, "waitpkg", "avx512vbmi2", NULL,
            "gfni", "vaes", "vpclmulqdq", "avx512vnni",
            "avx512bitalg", NULL, "avx512-vpopcntdq", NULL,
            "la57", NULL, NULL, NULL,
            NULL, NULL, "rdpid", NULL,
            "bus-lock-detect", "cldemote", NULL, "movdiri",
            "movdir64b", NULL, "sgxlc", "pks",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_ECX,
        },
        .tcg_features = TCG_7_0_ECX_FEATURES,
    },
    [FEAT_7_0_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "avx512-4vnniw", "avx512-4fmaps",
            "fsrm", NULL, NULL, NULL,
            "avx512-vp2intersect", NULL, "md-clear", NULL,
            NULL, NULL, "serialize", NULL,
            "tsx-ldtrk", NULL, NULL /* pconfig */, "arch-lbr",
            NULL, NULL, "amx-bf16", "avx512-fp16",
            "amx-tile", "amx-int8", "spec-ctrl", "stibp",
            NULL, "arch-capabilities", "core-capability", "ssbd",
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EDX,
        },
        .tcg_features = TCG_7_0_EDX_FEATURES,
    },
    [FEAT_7_1_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            "avx-vnni", "avx512-bf16", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 7,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EAX,
        },
        .tcg_features = TCG_7_1_EAX_FEATURES,
    },
    [FEAT_8000_0007_EDX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "invtsc", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000007, .reg = R_EDX, },
        .tcg_features = TCG_APM_FEATURES,
        .unmigratable_flags = CPUID_APM_INVTSC,
    },
    [FEAT_8000_0008_EBX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "clzero", NULL, "xsaveerptr", NULL,
            NULL, NULL, NULL, NULL,
            NULL, "wbnoinvd", NULL, NULL,
            "ibpb", NULL, "ibrs", "amd-stibp",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "amd-ssbd", "virt-ssbd", "amd-no-ssb", NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 0x80000008, .reg = R_EBX, },
        .tcg_features = 0,
        .unmigratable_flags = 0,
    },
    [FEAT_XSAVE] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "xsaveopt", "xsavec", "xgetbv1", "xsaves",
            "xfd", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0xd,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EAX,
        },
        .tcg_features = TCG_XSAVE_FEATURES,
    },
    [FEAT_XSAVE_XSS_LO] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true,
            .ecx = 1,
            .reg = R_ECX,
        },
    },
    [FEAT_XSAVE_XSS_HI] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true,
            .ecx = 1,
            .reg = R_EDX
        },
    },
    [FEAT_6_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "arat", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = { .eax = 6, .reg = R_EAX, },
        .tcg_features = TCG_6_EAX_FEATURES,
    },
    [FEAT_XSAVE_XCR0_LO] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EAX,
        },
        .tcg_features = ~0U,
        .migratable_flags = XSTATE_FP_MASK | XSTATE_SSE_MASK |
            XSTATE_YMM_MASK | XSTATE_BNDREGS_MASK | XSTATE_BNDCSR_MASK |
            XSTATE_OPMASK_MASK | XSTATE_ZMM_Hi256_MASK | XSTATE_Hi16_ZMM_MASK |
            XSTATE_PKRU_MASK,
    },
    [FEAT_XSAVE_XCR0_HI] = {
        .type = CPUID_FEATURE_WORD,
        .cpuid = {
            .eax = 0xD,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EDX,
        },
        .tcg_features = ~0U,
    },
    /*Below are MSR exposed features*/
    [FEAT_ARCH_CAPABILITIES] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "rdctl-no", "ibrs-all", "rsba", "skip-l1dfl-vmentry",
            "ssb-no", "mds-no", "pschange-mc-no", "tsx-ctrl",
            "taa-no", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_ARCH_CAPABILITIES,
        },
    },
    [FEAT_CORE_CAPABILITY] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, "split-lock-detect", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_CORE_CAPABILITY,
        },
    },
    [FEAT_PERF_CAPABILITIES] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, "full-width-write", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_PERF_CAPABILITIES,
        },
    },

    [FEAT_VMX_PROCBASED_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "vmx-vintr-pending", "vmx-tsc-offset",
            NULL, NULL, NULL, "vmx-hlt-exit",
            NULL, "vmx-invlpg-exit", "vmx-mwait-exit", "vmx-rdpmc-exit",
            "vmx-rdtsc-exit", NULL, NULL, "vmx-cr3-load-noexit",
            "vmx-cr3-store-noexit", NULL, NULL, "vmx-cr8-load-exit",
            "vmx-cr8-store-exit", "vmx-flexpriority", "vmx-vnmi-pending", "vmx-movdr-exit",
            "vmx-io-exit", "vmx-io-bitmap", NULL, "vmx-mtf",
            "vmx-msr-bitmap", "vmx-monitor-exit", "vmx-pause-exit", "vmx-secondary-ctls",
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
        }
    },

    [FEAT_VMX_SECONDARY_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-apicv-xapic", "vmx-ept", "vmx-desc-exit", "vmx-rdtscp-exit",
            "vmx-apicv-x2apic", "vmx-vpid", "vmx-wbinvd-exit", "vmx-unrestricted-guest",
            "vmx-apicv-register", "vmx-apicv-vid", "vmx-ple", "vmx-rdrand-exit",
            "vmx-invpcid-exit", "vmx-vmfunc", "vmx-shadow-vmcs", "vmx-encls-exit",
            "vmx-rdseed-exit", "vmx-pml", NULL, NULL,
            "vmx-xsaves", NULL, NULL, NULL,
            NULL, "vmx-tsc-scaling", NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_PROCBASED_CTLS2,
        }
    },

    [FEAT_VMX_PINBASED_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-intr-exit", NULL, NULL, "vmx-nmi-exit",
            NULL, "vmx-vnmi", "vmx-preemption-timer", "vmx-posted-intr",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_PINBASED_CTLS,
        }
    },

    [FEAT_VMX_EXIT_CTLS] = {
        .type = MSR_FEATURE_WORD,
        /*
         * VMX_VM_EXIT_HOST_ADDR_SPACE_SIZE is copied from
         * the LM CPUID bit.
         */
        .feat_names = {
            NULL, NULL, "vmx-exit-nosave-debugctl", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL /* vmx-exit-host-addr-space-size */, NULL, NULL,
            "vmx-exit-load-perf-global-ctrl", NULL, NULL, "vmx-exit-ack-intr",
            NULL, NULL, "vmx-exit-save-pat", "vmx-exit-load-pat",
            "vmx-exit-save-efer", "vmx-exit-load-efer",
                "vmx-exit-save-preemption-timer", "vmx-exit-clear-bndcfgs",
            NULL, "vmx-exit-clear-rtit-ctl", NULL, NULL,
            NULL, "vmx-exit-load-pkrs", NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_EXIT_CTLS,
        }
    },

    [FEAT_VMX_ENTRY_CTLS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, "vmx-entry-noload-debugctl", NULL,
            NULL, NULL, NULL, NULL,
            NULL, "vmx-entry-ia32e-mode", NULL, NULL,
            NULL, "vmx-entry-load-perf-global-ctrl", "vmx-entry-load-pat", "vmx-entry-load-efer",
            "vmx-entry-load-bndcfgs", NULL, "vmx-entry-load-rtit-ctl", NULL,
            NULL, NULL, "vmx-entry-load-pkrs", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_TRUE_ENTRY_CTLS,
        }
    },

    [FEAT_VMX_MISC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, "vmx-store-lma", "vmx-activity-hlt", "vmx-activity-shutdown",
            "vmx-activity-wait-sipi", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, "vmx-vmwrite-vmexit-fields", "vmx-zero-len-inject", NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_MISC,
        }
    },

    [FEAT_VMX_EPT_VPID_CAPS] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            "vmx-ept-execonly", NULL, NULL, NULL,
            NULL, NULL, "vmx-page-walk-4", "vmx-page-walk-5",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "vmx-ept-2mb", "vmx-ept-1gb", NULL, NULL,
            "vmx-invept", "vmx-eptad", "vmx-ept-advanced-exitinfo", NULL,
            NULL, "vmx-invept-single-context", "vmx-invept-all-context", NULL,
            NULL, NULL, NULL, NULL,
            "vmx-invvpid", NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            "vmx-invvpid-single-addr", "vmx-invept-single-context",
                "vmx-invvpid-all-context", "vmx-invept-single-context-noglobals",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .msr = {
            .index = MSR_IA32_VMX_EPT_VPID_CAP,
        }
    },

    [FEAT_VMX_BASIC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            [54] = "vmx-ins-outs",
            [55] = "vmx-true-ctls",
        },
        .msr = {
            .index = MSR_IA32_VMX_BASIC,
        },
        /* Just to be safe - we don't support setting the MSEG version field.  */
        .no_autoenable_flags = MSR_VMX_BASIC_DUAL_MONITOR,
    },

    [FEAT_VMX_VMFUNC] = {
        .type = MSR_FEATURE_WORD,
        .feat_names = {
            [0] = "vmx-eptp-switching",
        },
        .msr = {
            .index = MSR_IA32_VMX_VMFUNC,
        }
    },

    [FEAT_14_0_ECX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, "intel-pt-lip",
        },
        .cpuid = {
            .eax = 0x14,
            .needs_ecx = true, .ecx = 0,
            .reg = R_ECX,
        },
        .tcg_features = TCG_14_0_ECX_FEATURES,
     },

    [FEAT_SGX_12_0_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "sgx1", "sgx2", NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, "sgx-edeccssa",
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0x12,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EAX,
        },
        .tcg_features = TCG_SGX_12_0_EAX_FEATURES,
    },

    [FEAT_SGX_12_0_EBX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            "sgx-exinfo" , NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0x12,
            .needs_ecx = true, .ecx = 0,
            .reg = R_EBX,
        },
        .tcg_features = TCG_SGX_12_0_EBX_FEATURES,
    },

    [FEAT_SGX_12_1_EAX] = {
        .type = CPUID_FEATURE_WORD,
        .feat_names = {
            NULL, "sgx-debug", "sgx-mode64", NULL,
            "sgx-provisionkey", "sgx-tokenkey", NULL, "sgx-kss",
            NULL, NULL, "sgx-aex-notify", NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL,
        },
        .cpuid = {
            .eax = 0x12,
            .needs_ecx = true, .ecx = 1,
            .reg = R_EAX,
        },
        .tcg_features = TCG_SGX_12_1_EAX_FEATURES,
    },
};

typedef struct FeatureMask {
    FeatureWord index;
    uint64_t mask;
} FeatureMask;

typedef struct FeatureDep {
    FeatureMask from, to;
} FeatureDep;

static FeatureDep feature_dependencies[] = {
    {
        .from = { FEAT_7_0_EDX,             CPUID_7_0_EDX_ARCH_CAPABILITIES },
        .to = { FEAT_ARCH_CAPABILITIES,     ~0ull },
    },
    {
        .from = { FEAT_7_0_EDX,             CPUID_7_0_EDX_CORE_CAPABILITY },
        .to = { FEAT_CORE_CAPABILITY,       ~0ull },
    },
    {
        .from = { FEAT_1_ECX,             CPUID_EXT_PDCM },
        .to = { FEAT_PERF_CAPABILITIES,       ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_PROCBASED_CTLS,    ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_PINBASED_CTLS,     ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_EXIT_CTLS,         ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_ENTRY_CTLS,        ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_MISC,              ~0ull },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_VMX },
        .to = { FEAT_VMX_BASIC,             ~0ull },
    },
    {
        .from = { FEAT_8000_0001_EDX,       CPUID_EXT2_LM },
        .to = { FEAT_VMX_ENTRY_CTLS,        VMX_VM_ENTRY_IA32E_MODE },
    },
    {
        .from = { FEAT_VMX_PROCBASED_CTLS,  VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS },
        .to = { FEAT_VMX_SECONDARY_CTLS,    ~0ull },
    },
    {
        .from = { FEAT_XSAVE,               CPUID_XSAVE_XSAVES },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_XSAVES },
    },
    {
        .from = { FEAT_1_ECX,               CPUID_EXT_RDRAND },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_RDRAND_EXITING },
    },
    {
        .from = { FEAT_7_0_EBX,             CPUID_7_0_EBX_INVPCID },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_ENABLE_INVPCID },
    },
    {
        .from = { FEAT_7_0_EBX,             CPUID_7_0_EBX_MPX },
        .to = { FEAT_VMX_EXIT_CTLS,         VMX_VM_EXIT_CLEAR_BNDCFGS },
    },
    {
        .from = { FEAT_7_0_EBX,             CPUID_7_0_EBX_MPX },
        .to = { FEAT_VMX_ENTRY_CTLS,        VMX_VM_ENTRY_LOAD_BNDCFGS },
    },
    {
        .from = { FEAT_7_0_EBX,             CPUID_7_0_EBX_RDSEED },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_RDSEED_EXITING },
    },
    {
        .from = { FEAT_7_0_EBX,             CPUID_7_0_EBX_INTEL_PT },
        .to = { FEAT_14_0_ECX,              ~0ull },
    },
    {
        .from = { FEAT_8000_0001_EDX,       CPUID_EXT2_RDTSCP },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_RDTSCP },
    },
    {
        .from = { FEAT_VMX_SECONDARY_CTLS,  VMX_SECONDARY_EXEC_ENABLE_EPT },
        .to = { FEAT_VMX_EPT_VPID_CAPS,     0xffffffffull },
    },
    {
        .from = { FEAT_VMX_SECONDARY_CTLS,  VMX_SECONDARY_EXEC_ENABLE_EPT },
        .to = { FEAT_VMX_SECONDARY_CTLS,    VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST },
    },
    {
        .from = { FEAT_VMX_SECONDARY_CTLS,  VMX_SECONDARY_EXEC_ENABLE_VPID },
        .to = { FEAT_VMX_EPT_VPID_CAPS,     0xffffffffull << 32 },
    },
    {
        .from = { FEAT_VMX_SECONDARY_CTLS,  VMX_SECONDARY_EXEC_ENABLE_VMFUNC },
        .to = { FEAT_VMX_VMFUNC,            ~0ull },
    },
    {
        .from = { FEAT_8000_0001_ECX,       CPUID_EXT3_SVM },
        .to = { FEAT_SVM,                   ~0ull },
    },
};

typedef struct X86RegisterInfo32 {
    /* Name of register */
    const char *name;
    /* QAPI enum value register */
    X86CPURegister32 qapi_enum;
} X86RegisterInfo32;

#define REGISTER(reg) \
    [R_##reg] = { .name = #reg, .qapi_enum = X86_CPU_REGISTER32_##reg }
static const X86RegisterInfo32 x86_reg_info_32[CPU_NB_REGS32] = {
    REGISTER(EAX),
    REGISTER(ECX),
    REGISTER(EDX),
    REGISTER(EBX),
    REGISTER(ESP),
    REGISTER(EBP),
    REGISTER(ESI),
    REGISTER(EDI),
};
#undef REGISTER

/* CPUID feature bits available in XSS */
#define CPUID_XSTATE_XSS_MASK    (XSTATE_ARCH_LBR_MASK)

ExtSaveArea x86_ext_save_areas[XSAVE_STATE_AREA_COUNT] = {
    [XSTATE_FP_BIT] = {
        /* x87 FP state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_SSE_BIT] = {
        /* SSE state component is always enabled if XSAVE is supported */
        .feature = FEAT_1_ECX, .bits = CPUID_EXT_XSAVE,
        .size = sizeof(X86LegacyXSaveArea) + sizeof(X86XSaveHeader),
    },
    [XSTATE_YMM_BIT] =
          { .feature = FEAT_1_ECX, .bits = CPUID_EXT_AVX,
            .size = sizeof(XSaveAVX) },
    [XSTATE_BNDREGS_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .size = sizeof(XSaveBNDREG)  },
    [XSTATE_BNDCSR_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_MPX,
            .size = sizeof(XSaveBNDCSR)  },
    [XSTATE_OPMASK_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .size = sizeof(XSaveOpmask) },
    [XSTATE_ZMM_Hi256_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .size = sizeof(XSaveZMM_Hi256) },
    [XSTATE_Hi16_ZMM_BIT] =
          { .feature = FEAT_7_0_EBX, .bits = CPUID_7_0_EBX_AVX512F,
            .size = sizeof(XSaveHi16_ZMM) },
    [XSTATE_PKRU_BIT] =
          { .feature = FEAT_7_0_ECX, .bits = CPUID_7_0_ECX_PKU,
            .size = sizeof(XSavePKRU) },
    [XSTATE_ARCH_LBR_BIT] = {
            .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_ARCH_LBR,
            .offset = 0 /*supervisor mode component, offset = 0 */,
            .size = sizeof(XSavesArchLBR) },
    [XSTATE_XTILE_CFG_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .size = sizeof(XSaveXTILECFG),
    },
    [XSTATE_XTILE_DATA_BIT] = {
        .feature = FEAT_7_0_EDX, .bits = CPUID_7_0_EDX_AMX_TILE,
        .size = sizeof(XSaveXTILEDATA)
    },
};

uint32_t xsave_area_size(uint64_t mask, bool compacted)
{
    uint64_t ret = x86_ext_save_areas[0].size;
    const ExtSaveArea *esa;
    uint32_t offset = 0;
    int i;

    for (i = 2; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        esa = &x86_ext_save_areas[i];
        if ((mask >> i) & 1) {
            offset = compacted ? ret : esa->offset;
            ret = MAX(ret, offset + esa->size);
        }
    }
    return ret;
}

static inline bool accel_uses_host_cpuid(void)
{
    return kvm_enabled() || hvf_enabled();
}

static inline uint64_t x86_cpu_xsave_xcr0_components(X86CPU *cpu)
{
    return ((uint64_t)cpu->env.features[FEAT_XSAVE_XCR0_HI]) << 32 |
           cpu->env.features[FEAT_XSAVE_XCR0_LO];
}

/* Return name of 32-bit register, from a R_* constant */
static const char *get_register_name_32(unsigned int reg)
{
    if (reg >= CPU_NB_REGS32) {
        return NULL;
    }
    return x86_reg_info_32[reg].name;
}

static inline uint64_t x86_cpu_xsave_xss_components(X86CPU *cpu)
{
    return ((uint64_t)cpu->env.features[FEAT_XSAVE_XSS_HI]) << 32 |
           cpu->env.features[FEAT_XSAVE_XSS_LO];
}

/*
 * Returns the set of feature flags that are supported and migratable by
 * QEMU, for a given FeatureWord.
 */
static uint64_t x86_cpu_get_migratable_flags(FeatureWord w)
{
    FeatureWordInfo *wi = &feature_word_info[w];
    uint64_t r = 0;
    int i;

    for (i = 0; i < 64; i++) {
        uint64_t f = 1ULL << i;

        /* If the feature name is known, it is implicitly considered migratable,
         * unless it is explicitly set in unmigratable_flags */
        if ((wi->migratable_flags & f) ||
            (wi->feat_names[i] && !(wi->unmigratable_flags & f))) {
            r |= f;
        }
    }
    return r;
}

void host_cpuid(uint32_t function, uint32_t count,
                uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t vec[4];

#ifdef __x86_64__
    asm volatile("cpuid"
                 : "=a"(vec[0]), "=b"(vec[1]),
                   "=c"(vec[2]), "=d"(vec[3])
                 : "0"(function), "c"(count) : "cc");
#elif defined(__i386__)
    asm volatile("pusha \n\t"
                 "cpuid \n\t"
                 "mov %%eax, 0(%2) \n\t"
                 "mov %%ebx, 4(%2) \n\t"
                 "mov %%ecx, 8(%2) \n\t"
                 "mov %%edx, 12(%2) \n\t"
                 "popa"
                 : : "a"(function), "c"(count), "S"(vec)
                 : "memory", "cc");
#else
    abort();
#endif

    if (eax)
        *eax = vec[0];
    if (ebx)
        *ebx = vec[1];
    if (ecx)
        *ecx = vec[2];
    if (edx)
        *edx = vec[3];
}

/* CPU class name definitions: */

/* Return type name for a given CPU model name
 * Caller is responsible for freeing the returned string.
 */
static char *x86_cpu_type_name(const char *model_name)
{
    return g_strdup_printf(X86_CPU_TYPE_NAME("%s"), model_name);
}

static ObjectClass *x86_cpu_class_by_name(const char *cpu_model)
{
    g_autofree char *typename = x86_cpu_type_name(cpu_model);
    return object_class_by_name(typename);
}

static char *x86_cpu_class_get_model_name(X86CPUClass *cc)
{
    const char *class_name = object_class_get_name(OBJECT_CLASS(cc));
    assert(g_str_has_suffix(class_name, X86_CPU_TYPE_SUFFIX));
    return g_strndup(class_name,
                     strlen(class_name) - strlen(X86_CPU_TYPE_SUFFIX));
}

typedef struct X86CPUVersionDefinition {
    X86CPUVersion version;
    const char *alias;
    const char *note;
    PropValue *props;
} X86CPUVersionDefinition;

/* Base definition for a CPU model */
typedef struct X86CPUDefinition {
    const char *name;
    uint32_t level;
    uint32_t xlevel;
    /* vendor is zero-terminated, 12 character ASCII string */
    char vendor[CPUID_VENDOR_SZ + 1];
    int family;
    int model;
    int stepping;
    FeatureWordArray features;
    const char *model_id;
    const CPUCaches *const cache_info;
    /*
     * Definitions for alternative versions of CPU model.
     * List is terminated by item with version == 0.
     * If NULL, version 1 will be registered automatically.
     */
    const X86CPUVersionDefinition *versions;
    const char *deprecation_note;
} X86CPUDefinition;

/* Reference to a specific CPU model version */
struct X86CPUModel {
    /* Base CPU definition */
    const X86CPUDefinition *cpudef;
    /* CPU model version */
    X86CPUVersion version;
    const char *note;
    /*
     * If true, this is an alias CPU model.
     * This matters only for "-cpu help" and query-cpu-definitions
     */
    bool is_alias;
};

/* Get full model name for CPU version */
static char *x86_cpu_versioned_model_name(const X86CPUDefinition *cpudef,
                                          X86CPUVersion version)
{
    assert(version > 0);
    return g_strdup_printf("%s-v%d", cpudef->name, (int)version);
}

static const X86CPUVersionDefinition *
x86_cpu_def_get_versions(const X86CPUDefinition *def)
{
    /* When X86CPUDefinition::versions is NULL, we register only v1 */
    static const X86CPUVersionDefinition default_version_list[] = {
        { 1 },
        { /* end of list */ }
    };

    return def->versions ?: default_version_list;
}

static const CPUCaches epyc_cache_info = {
    .l1d_cache = &(CPUCacheInfo) {
        .type = DATA_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l1i_cache = &(CPUCacheInfo) {
        .type = INSTRUCTION_CACHE,
        .level = 1,
        .size = 64 * KiB,
        .line_size = 64,
        .associativity = 4,
        .partitions = 1,
        .sets = 256,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l2_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 2,
        .size = 512 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 1024,
        .lines_per_tag = 1,
    },
    .l3_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 3,
        .size = 8 * MiB,
        .line_size = 64,
        .associativity = 16,
        .partitions = 1,
        .sets = 8192,
        .lines_per_tag = 1,
        .self_init = true,
        .inclusive = true,
        .complex_indexing = true,
    },
};

static const CPUCaches epyc_rome_cache_info = {
    .l1d_cache = &(CPUCacheInfo) {
        .type = DATA_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l1i_cache = &(CPUCacheInfo) {
        .type = INSTRUCTION_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l2_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 2,
        .size = 512 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 1024,
        .lines_per_tag = 1,
    },
    .l3_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 3,
        .size = 16 * MiB,
        .line_size = 64,
        .associativity = 16,
        .partitions = 1,
        .sets = 16384,
        .lines_per_tag = 1,
        .self_init = true,
        .inclusive = true,
        .complex_indexing = true,
    },
};

static const CPUCaches epyc_milan_cache_info = {
    .l1d_cache = &(CPUCacheInfo) {
        .type = DATA_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l1i_cache = &(CPUCacheInfo) {
        .type = INSTRUCTION_CACHE,
        .level = 1,
        .size = 32 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 64,
        .lines_per_tag = 1,
        .self_init = 1,
        .no_invd_sharing = true,
    },
    .l2_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 2,
        .size = 512 * KiB,
        .line_size = 64,
        .associativity = 8,
        .partitions = 1,
        .sets = 1024,
        .lines_per_tag = 1,
    },
    .l3_cache = &(CPUCacheInfo) {
        .type = UNIFIED_CACHE,
        .level = 3,
        .size = 32 * MiB,
        .line_size = 64,
        .associativity = 16,
        .partitions = 1,
        .sets = 32768,
        .lines_per_tag = 1,
        .self_init = true,
        .inclusive = true,
        .complex_indexing = true,
    },
};

/* The following VMX features are not supported by KVM and are left out in the
 * CPU definitions:
 *
 *  Dual-monitor support (all processors)
 *  Entry to SMM
 *  Deactivate dual-monitor treatment
 *  Number of CR3-target values
 *  Shutdown activity state
 *  Wait-for-SIPI activity state
 *  PAUSE-loop exiting (Westmere and newer)
 *  EPT-violation #VE (Broadwell and newer)
 *  Inject event with insn length=0 (Skylake and newer)
 *  Conceal non-root operation from PT
 *  Conceal VM exits from PT
 *  Conceal VM entries from PT
 *  Enable ENCLS exiting
 *  Mode-based execute control (XS/XU)
 s  TSC scaling (Skylake Server and newer)
 *  GPA translation for PT (IceLake and newer)
 *  User wait and pause
 *  ENCLV exiting
 *  Load IA32_RTIT_CTL
 *  Clear IA32_RTIT_CTL
 *  Advanced VM-exit information for EPT violations
 *  Sub-page write permissions
 *  PT in VMX operation
 */

static const X86CPUDefinition builtin_x86_defs[] = {
    {
        .name = "qemu64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 107,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM,
        .xlevel = 0x8000000A,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "phenom",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_CX16 |
            CPUID_EXT_POPCNT,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX |
            CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT | CPUID_EXT2_MMXEXT |
            CPUID_EXT2_FFXSR | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP,
        /* Missing: CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS */
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_SVM |
            CPUID_EXT3_ABM | CPUID_EXT3_SSE4A,
        /* Missing: CPUID_SVM_LBRV */
        .features[FEAT_SVM] =
            CPUID_SVM_NPT,
        .xlevel = 0x8000001A,
        .model_id = "AMD Phenom(tm) 9550 Quad-Core Processor"
    },
    {
        .name = "core2duo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 11,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36 | CPUID_VME | CPUID_ACPI | CPUID_SS,
        /* Missing: CPUID_EXT_DTES64, CPUID_EXT_DSCPL, CPUID_EXT_EST,
         * CPUID_EXT_TM2, CPUID_EXT_XTPR, CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_CX16,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Core(TM)2 Duo CPU     T7700  @ 2.40GHz",
    },
    {
        .name = "kvm64",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        /* Missing: CPUID_HT */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA |
            CPUID_PSE36,
        /* Missing: CPUID_EXT_POPCNT, CPUID_EXT_MONITOR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_CX16,
        /* Missing: CPUID_EXT2_PDPE1GB, CPUID_EXT2_RDTSCP */
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        /* Missing: CPUID_EXT3_LAHF_LM, CPUID_EXT3_CMP_LEG, CPUID_EXT3_EXTAPIC,
                    CPUID_EXT3_CR8LEG, CPUID_EXT3_ABM, CPUID_EXT3_SSE4A,
                    CPUID_EXT3_MISALIGNSSE, CPUID_EXT3_3DNOWPREFETCH,
                    CPUID_EXT3_OSVW, CPUID_EXT3_IBS, CPUID_EXT3_SVM */
        .features[FEAT_8000_0001_ECX] =
            0,
        /* VMX features from Cedar Mill/Prescott */
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Common KVM processor"
    },
    {
        .name = "qemu32",
        .level = 4,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 6,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .xlevel = 0x80000004,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "kvm32",
        .level = 5,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_PSE36,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_ECX] =
            0,
        /* VMX features from Yonah */
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_MOV_DR_EXITING | VMX_CPU_BASED_UNCOND_IO_EXITING |
             VMX_CPU_BASED_USE_IO_BITMAPS | VMX_CPU_BASED_MONITOR_EXITING |
             VMX_CPU_BASED_PAUSE_EXITING | VMX_CPU_BASED_USE_MSR_BITMAPS,
        .xlevel = 0x80000008,
        .model_id = "Common 32-bit KVM processor"
    },
    {
        .name = "coreduo",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 14,
        .stepping = 8,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_VME |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_ACPI |
            CPUID_SS,
        /* Missing: CPUID_EXT_EST, CPUID_EXT_TM2 , CPUID_EXT_XTPR,
         * CPUID_EXT_PDCM, CPUID_EXT_VMX */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_MOV_DR_EXITING | VMX_CPU_BASED_UNCOND_IO_EXITING |
             VMX_CPU_BASED_USE_IO_BITMAPS | VMX_CPU_BASED_MONITOR_EXITING |
             VMX_CPU_BASED_PAUSE_EXITING | VMX_CPU_BASED_USE_MSR_BITMAPS,
        .xlevel = 0x80000008,
        .model_id = "Genuine Intel(R) CPU           T2600  @ 2.16GHz",
    },
    {
        .name = "486",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 4,
        .model = 8,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            I486_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium",
        .level = 1,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 5,
        .model = 4,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium2",
        .level = 2,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 5,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            PENTIUM2_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "pentium3",
        .level = 3,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 7,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PENTIUM3_FEATURES,
        .xlevel = 0,
        .model_id = "",
    },
    {
        .name = "athlon",
        .level = 2,
        .vendor = CPUID_VENDOR_AMD,
        .family = 6,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            PPRO_FEATURES | CPUID_PSE36 | CPUID_VME | CPUID_MTRR |
            CPUID_MCA,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_MMXEXT | CPUID_EXT2_3DNOW | CPUID_EXT2_3DNOWEXT,
        .xlevel = 0x80000008,
        .model_id = "QEMU Virtual CPU version " QEMU_HW_VERSION,
    },
    {
        .name = "n270",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 28,
        .stepping = 2,
        /* Missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
        .features[FEAT_1_EDX] =
            PPRO_FEATURES |
            CPUID_MTRR | CPUID_CLFLUSH | CPUID_MCA | CPUID_VME |
            CPUID_ACPI | CPUID_SS,
            /* Some CPUs got no CPUID_SEP */
        /* Missing: CPUID_EXT_DSCPL, CPUID_EXT_EST, CPUID_EXT_TM2,
         * CPUID_EXT_XTPR */
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_MONITOR | CPUID_EXT_SSSE3 |
            CPUID_EXT_MOVBE,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "Intel(R) Atom(TM) CPU N270   @ 1.60GHz",
    },
    {
        .name = "Conroe",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 15,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES,
        .xlevel = 0x80000008,
        .model_id = "Intel Celeron_4x0 (Conroe/Merom Class Core 2)",
    },
    {
        .name = "Penryn",
        .level = 10,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 23,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL,
        .features[FEAT_VMX_EXIT_CTLS] = VMX_VM_EXIT_ACK_INTR_ON_EXIT |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core 2 Duo P9xxx (Penryn Class Core 2)",
    },
    {
        .name = "Nehalem",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 26,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID,
        .xlevel = 0x80000008,
        .model_id = "Intel Core i7 9xx (Nehalem Class Core i7)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Nehalem-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core i7 9xx (Nehalem Core i7, IBRS update)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Westmere",
        .level = 11,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 44,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_SYSCALL | CPUID_EXT2_NX,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST,
        .xlevel = 0x80000008,
        .model_id = "Westmere E56xx/L56xx/X56xx (Nehalem-C)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Westmere-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Westmere E56xx/L56xx/X56xx (IBRS update)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "SandyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 42,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E312xx (Sandy Bridge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "SandyBridge-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon E312xx (Sandy Bridge, IBRS update)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "IvyBridge",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 58,
        .stepping = 9,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_POPCNT |
            CPUID_EXT_X2APIC | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3 | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_ERMS,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon E3-12xx v2 (Ivy Bridge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "IvyBridge-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon E3-12xx v2 (Ivy Bridge, IBRS)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Haswell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 60,
        .stepping = 4,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Haswell)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Haswell-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "stepping", "1" },
                    { "model-id", "Intel Core Processor (Haswell, no TSX)", },
                    { /* end of list */ }
                },
            },
            {
                .version = 3,
                .alias = "Haswell-IBRS",
                .props = (PropValue[]) {
                    /* Restore TSX features removed by -v2 above */
                    { "hle", "on" },
                    { "rtm", "on" },
                    /*
                     * Haswell and Haswell-IBRS had stepping=4 in
                     * QEMU 4.0 and older
                     */
                    { "stepping", "4" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Haswell, IBRS)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 4,
                .alias = "Haswell-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    /* spec-ctrl was already enabled by -v3 above */
                    { "stepping", "1" },
                    { "model-id",
                      "Intel Core Processor (Haswell, no TSX, IBRS)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Broadwell",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 61,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Broadwell)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Broadwell-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id", "Intel Core Processor (Broadwell, no TSX)", },
                    { /* end of list */ }
                },
            },
            {
                .version = 3,
                .alias = "Broadwell-IBRS",
                .props = (PropValue[]) {
                    /* Restore TSX features removed by -v2 above */
                    { "hle", "on" },
                    { "rtm", "on" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Broadwell, IBRS)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 4,
                .alias = "Broadwell-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    /* spec-ctrl was already enabled by -v3 above */
                    { "model-id",
                      "Intel Core Processor (Broadwell, no TSX, IBRS)" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Skylake-Client",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 94,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP,
        /* XSAVES is added in version 4 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Core Processor (Skylake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Skylake-Client-IBRS",
                .props = (PropValue[]) {
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Core Processor (Skylake, IBRS)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 3,
                .alias = "Skylake-Client-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id",
                      "Intel Core Processor (Skylake, IBRS, no TSX)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 4,
                .note = "IBRS, XSAVES, no TSX",
                .props = (PropValue[]) {
                    { "xsaves", "on" },
                    { "vmx-xsaves", "on" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Skylake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 4,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU,
        /* XSAVES is added in version 5 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Skylake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "Skylake-Server-IBRS",
                .props = (PropValue[]) {
                    /* clflushopt was not added to Skylake-Server-IBRS */
                    /* TODO: add -v3 including clflushopt */
                    { "clflushopt", "off" },
                    { "spec-ctrl", "on" },
                    { "model-id",
                      "Intel Xeon Processor (Skylake, IBRS)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 3,
                .alias = "Skylake-Server-noTSX-IBRS",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { "model-id",
                      "Intel Xeon Processor (Skylake, IBRS, no TSX)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 4,
                .props = (PropValue[]) {
                    { "vmx-eptp-switching", "on" },
                    { /* end of list */ }
                }
            },
            {
                .version = 5,
                .note = "IBRS, XSAVES, EPT switching, no TSX",
                .props = (PropValue[]) {
                    { "xsaves", "on" },
                    { "vmx-xsaves", "on" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Cascadelake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 6,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512VNNI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* XSAVES is added in version 5 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Cascadelake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            { .version = 2,
              .note = "ARCH_CAPABILITIES",
              .props = (PropValue[]) {
                  { "arch-capabilities", "on" },
                  { "rdctl-no", "on" },
                  { "ibrs-all", "on" },
                  { "skip-l1dfl-vmentry", "on" },
                  { "mds-no", "on" },
                  { /* end of list */ }
              },
            },
            { .version = 3,
              .alias = "Cascadelake-Server-noTSX",
              .note = "ARCH_CAPABILITIES, no TSX",
              .props = (PropValue[]) {
                  { "hle", "off" },
                  { "rtm", "off" },
                  { /* end of list */ }
              },
            },
            { .version = 4,
              .note = "ARCH_CAPABILITIES, no TSX",
              .props = (PropValue[]) {
                  { "vmx-eptp-switching", "on" },
                  { /* end of list */ }
              },
            },
            { .version = 5,
              .note = "ARCH_CAPABILITIES, EPT switching, XSAVES, no TSX",
              .props = (PropValue[]) {
                  { "xsaves", "on" },
                  { "vmx-xsaves", "on" },
                  { /* end of list */ }
              },
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Cooperlake",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 85,
        .stepping = 10,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512VNNI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_STIBP |
            CPUID_7_0_EDX_SPEC_CTRL_SSBD | CPUID_7_0_EDX_ARCH_CAPABILITIES,
        .features[FEAT_ARCH_CAPABILITIES] =
            MSR_ARCH_CAP_RDCL_NO | MSR_ARCH_CAP_IBRS_ALL |
            MSR_ARCH_CAP_SKIP_L1DFL_VMENTRY | MSR_ARCH_CAP_MDS_NO |
            MSR_ARCH_CAP_PSCHANGE_MC_NO | MSR_ARCH_CAP_TAA_NO,
        .features[FEAT_7_1_EAX] =
            CPUID_7_1_EAX_AVX512_BF16,
        /* XSAVES is added in version 2 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Cooperlake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            { .version = 2,
              .note = "XSAVES",
              .props = (PropValue[]) {
                  { "xsaves", "on" },
                  { "vmx-xsaves", "on" },
                  { /* end of list */ }
              },
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Icelake-Server",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 134,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_PCID | CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_WBNOINVD,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 |
            CPUID_7_0_EBX_HLE | CPUID_7_0_EBX_AVX2 | CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS | CPUID_7_0_EBX_INVPCID |
            CPUID_7_0_EBX_RTM | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_AVX512F | CPUID_7_0_EBX_AVX512DQ |
            CPUID_7_0_EBX_AVX512BW | CPUID_7_0_EBX_AVX512CD |
            CPUID_7_0_EBX_AVX512VL | CPUID_7_0_EBX_CLFLUSHOPT,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_AVX512_VBMI | CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_PKU |
            CPUID_7_0_ECX_AVX512_VBMI2 | CPUID_7_0_ECX_GFNI |
            CPUID_7_0_ECX_VAES | CPUID_7_0_ECX_VPCLMULQDQ |
            CPUID_7_0_ECX_AVX512VNNI | CPUID_7_0_ECX_AVX512BITALG |
            CPUID_7_0_ECX_AVX512_VPOPCNTDQ | CPUID_7_0_ECX_LA57,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* XSAVES is added in version 5 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        /* Missing: Mode-based execute control (XS/XU), processor tracing, TSC scaling */
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Processor (Icelake)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .note = "no TSX",
                .alias = "Icelake-Server-noTSX",
                .props = (PropValue[]) {
                    { "hle", "off" },
                    { "rtm", "off" },
                    { /* end of list */ }
                },
            },
            {
                .version = 3,
                .props = (PropValue[]) {
                    { "arch-capabilities", "on" },
                    { "rdctl-no", "on" },
                    { "ibrs-all", "on" },
                    { "skip-l1dfl-vmentry", "on" },
                    { "mds-no", "on" },
                    { "pschange-mc-no", "on" },
                    { "taa-no", "on" },
                    { /* end of list */ }
                },
            },
            {
                .version = 4,
                .props = (PropValue[]) {
                    { "sha-ni", "on" },
                    { "avx512ifma", "on" },
                    { "rdpid", "on" },
                    { "fsrm", "on" },
                    { "vmx-rdseed-exit", "on" },
                    { "vmx-pml", "on" },
                    { "vmx-eptp-switching", "on" },
                    { "model", "106" },
                    { /* end of list */ }
                },
            },
            {
                .version = 5,
                .note = "XSAVES",
                .props = (PropValue[]) {
                    { "xsaves", "on" },
                    { "vmx-xsaves", "on" },
                    { /* end of list */ }
                },
            },
            {
                .version = 6,
                .note = "5-level EPT",
                .props = (PropValue[]) {
                    { "vmx-page-walk-5", "on" },
                    { /* end of list */ }
                },
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Denverton",
        .level = 21,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 95,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE | CPUID_TSC |
            CPUID_MSR | CPUID_PAE | CPUID_MCE | CPUID_CX8 | CPUID_APIC |
            CPUID_SEP | CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
            CPUID_PAT | CPUID_PSE36 | CPUID_CLFLUSH | CPUID_MMX | CPUID_FXSR |
            CPUID_SSE | CPUID_SSE2,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_MONITOR |
            CPUID_EXT_SSSE3 | CPUID_EXT_CX16 | CPUID_EXT_SSE41 |
            CPUID_EXT_SSE42 | CPUID_EXT_X2APIC | CPUID_EXT_MOVBE |
            CPUID_EXT_POPCNT | CPUID_EXT_TSC_DEADLINE_TIMER |
            CPUID_EXT_AES | CPUID_EXT_XSAVE | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_SYSCALL | CPUID_EXT2_NX | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_RDTSCP | CPUID_EXT2_LM,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_MPX | CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_SMAP |
            CPUID_7_0_EBX_CLFLUSHOPT | CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL | CPUID_7_0_EDX_ARCH_CAPABILITIES |
            CPUID_7_0_EDX_SPEC_CTRL_SSBD,
        /* XSAVES is added in version 3 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC | CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_ARCH_CAPABILITIES] =
            MSR_ARCH_CAP_RDCL_NO | MSR_ARCH_CAP_SKIP_L1DFL_VMENTRY,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Atom Processor (Denverton)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .note = "no MPX, no MONITOR",
                .props = (PropValue[]) {
                    { "monitor", "off" },
                    { "mpx", "off" },
                    { /* end of list */ },
                },
            },
            {
                .version = 3,
                .note = "XSAVES, no MPX, no MONITOR",
                .props = (PropValue[]) {
                    { "xsaves", "on" },
                    { "vmx-xsaves", "on" },
                    { /* end of list */ },
                },
            },
            { /* end of list */ },
        },
    },
    {
        .name = "Snowridge",
        .level = 27,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 134,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            /* missing: CPUID_PN CPUID_IA64 */
            /* missing: CPUID_DTS, CPUID_HT, CPUID_TM, CPUID_PBE */
            CPUID_FP87 | CPUID_VME | CPUID_DE | CPUID_PSE |
            CPUID_TSC | CPUID_MSR | CPUID_PAE | CPUID_MCE |
            CPUID_CX8 | CPUID_APIC | CPUID_SEP |
            CPUID_MTRR | CPUID_PGE | CPUID_MCA | CPUID_CMOV |
            CPUID_PAT | CPUID_PSE36 | CPUID_CLFLUSH |
            CPUID_MMX |
            CPUID_FXSR | CPUID_SSE | CPUID_SSE2,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_MONITOR |
            CPUID_EXT_SSSE3 |
            CPUID_EXT_CX16 |
            CPUID_EXT_SSE41 |
            CPUID_EXT_SSE42 | CPUID_EXT_X2APIC | CPUID_EXT_MOVBE |
            CPUID_EXT_POPCNT |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_AES | CPUID_EXT_XSAVE |
            CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_SYSCALL |
            CPUID_EXT2_NX |
            CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_LM,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE |
            CPUID_7_0_EBX_SMEP |
            CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_MPX |  /* missing bits 13, 15 */
            CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_CLWB |
            CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_UMIP |
            /* missing bit 5 */
            CPUID_7_0_ECX_GFNI |
            CPUID_7_0_ECX_MOVDIRI | CPUID_7_0_ECX_CLDEMOTE |
            CPUID_7_0_ECX_MOVDIR64B,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_SPEC_CTRL |
            CPUID_7_0_EDX_ARCH_CAPABILITIES | CPUID_7_0_EDX_SPEC_CTRL_SSBD |
            CPUID_7_0_EDX_CORE_CAPABILITY,
        .features[FEAT_CORE_CAPABILITY] =
            MSR_CORE_CAP_SPLIT_LOCK_DETECT,
        /* XSAVES is added in version 3 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_VMX_BASIC] = MSR_VMX_BASIC_INS_OUTS |
             MSR_VMX_BASIC_TRUE_CTLS,
        .features[FEAT_VMX_ENTRY_CTLS] = VMX_VM_ENTRY_IA32E_MODE |
             VMX_VM_ENTRY_LOAD_IA32_PERF_GLOBAL_CTRL | VMX_VM_ENTRY_LOAD_IA32_PAT |
             VMX_VM_ENTRY_LOAD_DEBUG_CONTROLS | VMX_VM_ENTRY_LOAD_IA32_EFER,
        .features[FEAT_VMX_EPT_VPID_CAPS] = MSR_VMX_EPT_EXECONLY |
             MSR_VMX_EPT_PAGE_WALK_LENGTH_4 | MSR_VMX_EPT_WB | MSR_VMX_EPT_2MB |
             MSR_VMX_EPT_1GB | MSR_VMX_EPT_INVEPT |
             MSR_VMX_EPT_INVEPT_SINGLE_CONTEXT | MSR_VMX_EPT_INVEPT_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID | MSR_VMX_EPT_INVVPID_SINGLE_ADDR |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT | MSR_VMX_EPT_INVVPID_ALL_CONTEXT |
             MSR_VMX_EPT_INVVPID_SINGLE_CONTEXT_NOGLOBALS | MSR_VMX_EPT_AD_BITS,
        .features[FEAT_VMX_EXIT_CTLS] =
             VMX_VM_EXIT_ACK_INTR_ON_EXIT | VMX_VM_EXIT_SAVE_DEBUG_CONTROLS |
             VMX_VM_EXIT_LOAD_IA32_PERF_GLOBAL_CTRL |
             VMX_VM_EXIT_LOAD_IA32_PAT | VMX_VM_EXIT_LOAD_IA32_EFER |
             VMX_VM_EXIT_SAVE_IA32_PAT | VMX_VM_EXIT_SAVE_IA32_EFER |
             VMX_VM_EXIT_SAVE_VMX_PREEMPTION_TIMER,
        .features[FEAT_VMX_MISC] = MSR_VMX_MISC_ACTIVITY_HLT |
             MSR_VMX_MISC_STORE_LMA | MSR_VMX_MISC_VMWRITE_VMEXIT,
        .features[FEAT_VMX_PINBASED_CTLS] = VMX_PIN_BASED_EXT_INTR_MASK |
             VMX_PIN_BASED_NMI_EXITING | VMX_PIN_BASED_VIRTUAL_NMIS |
             VMX_PIN_BASED_VMX_PREEMPTION_TIMER | VMX_PIN_BASED_POSTED_INTR,
        .features[FEAT_VMX_PROCBASED_CTLS] = VMX_CPU_BASED_VIRTUAL_INTR_PENDING |
             VMX_CPU_BASED_USE_TSC_OFFSETING | VMX_CPU_BASED_HLT_EXITING |
             VMX_CPU_BASED_INVLPG_EXITING | VMX_CPU_BASED_MWAIT_EXITING |
             VMX_CPU_BASED_RDPMC_EXITING | VMX_CPU_BASED_RDTSC_EXITING |
             VMX_CPU_BASED_CR8_LOAD_EXITING | VMX_CPU_BASED_CR8_STORE_EXITING |
             VMX_CPU_BASED_TPR_SHADOW | VMX_CPU_BASED_MOV_DR_EXITING |
             VMX_CPU_BASED_UNCOND_IO_EXITING | VMX_CPU_BASED_USE_IO_BITMAPS |
             VMX_CPU_BASED_MONITOR_EXITING | VMX_CPU_BASED_PAUSE_EXITING |
             VMX_CPU_BASED_VIRTUAL_NMI_PENDING | VMX_CPU_BASED_USE_MSR_BITMAPS |
             VMX_CPU_BASED_CR3_LOAD_EXITING | VMX_CPU_BASED_CR3_STORE_EXITING |
             VMX_CPU_BASED_MONITOR_TRAP_FLAG |
             VMX_CPU_BASED_ACTIVATE_SECONDARY_CONTROLS,
        .features[FEAT_VMX_SECONDARY_CTLS] =
             VMX_SECONDARY_EXEC_VIRTUALIZE_APIC_ACCESSES |
             VMX_SECONDARY_EXEC_WBINVD_EXITING | VMX_SECONDARY_EXEC_ENABLE_EPT |
             VMX_SECONDARY_EXEC_DESC | VMX_SECONDARY_EXEC_RDTSCP |
             VMX_SECONDARY_EXEC_VIRTUALIZE_X2APIC_MODE |
             VMX_SECONDARY_EXEC_ENABLE_VPID | VMX_SECONDARY_EXEC_UNRESTRICTED_GUEST |
             VMX_SECONDARY_EXEC_APIC_REGISTER_VIRT |
             VMX_SECONDARY_EXEC_VIRTUAL_INTR_DELIVERY |
             VMX_SECONDARY_EXEC_RDRAND_EXITING | VMX_SECONDARY_EXEC_ENABLE_INVPCID |
             VMX_SECONDARY_EXEC_ENABLE_VMFUNC | VMX_SECONDARY_EXEC_SHADOW_VMCS |
             VMX_SECONDARY_EXEC_RDSEED_EXITING | VMX_SECONDARY_EXEC_ENABLE_PML,
        .features[FEAT_VMX_VMFUNC] = MSR_VMX_VMFUNC_EPT_SWITCHING,
        .xlevel = 0x80000008,
        .model_id = "Intel Atom Processor (SnowRidge)",
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .props = (PropValue[]) {
                    { "mpx", "off" },
                    { "model-id", "Intel Atom Processor (Snowridge, no MPX)" },
                    { /* end of list */ },
                },
            },
            {
                .version = 3,
                .note = "XSAVES, no MPX",
                .props = (PropValue[]) {
                    { "xsaves", "on" },
                    { "vmx-xsaves", "on" },
                    { /* end of list */ },
                },
            },
            {
                .version = 4,
                .note = "no split lock detect, no core-capability",
                .props = (PropValue[]) {
                    { "split-lock-detect", "off" },
                    { "core-capability", "off" },
                    { /* end of list */ },
                },
            },
            { /* end of list */ },
        },
    },
    {
        .name = "KnightsMill",
        .level = 0xd,
        .vendor = CPUID_VENDOR_INTEL,
        .family = 6,
        .model = 133,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SS | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR |
            CPUID_MMX | CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV |
            CPUID_MCA | CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC |
            CPUID_CX8 | CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC |
            CPUID_PSE | CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_X2APIC | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_SSSE3 |
            CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_TSC_DEADLINE_TIMER | CPUID_EXT_FMA | CPUID_EXT_MOVBE |
            CPUID_EXT_F16C | CPUID_EXT_RDRAND,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_RDTSCP |
            CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_ABM | CPUID_EXT3_LAHF_LM | CPUID_EXT3_3DNOWPREFETCH,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_RDSEED | CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_AVX512F |
            CPUID_7_0_EBX_AVX512CD | CPUID_7_0_EBX_AVX512PF |
            CPUID_7_0_EBX_AVX512ER,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_AVX512_VPOPCNTDQ,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_AVX512_4VNNIW | CPUID_7_0_EDX_AVX512_4FMAPS,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .xlevel = 0x80000008,
        .model_id = "Intel Xeon Phi Processor (Knights Mill)",
    },
    {
        .name = "Opteron_G1",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 240 (Gen 1 Class Opteron)",
    },
    {
        .name = "Opteron_G2",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 15,
        .model = 6,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_CX16 | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 22xx (Gen 2 Class Opteron)",
    },
    {
        .name = "Opteron_G3",
        .level = 5,
        .vendor = CPUID_VENDOR_AMD,
        .family = 16,
        .model = 2,
        .stepping = 3,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_POPCNT | CPUID_EXT_CX16 | CPUID_EXT_MONITOR |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_NX | CPUID_EXT2_SYSCALL |
            CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A |
            CPUID_EXT3_ABM | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM,
        .xlevel = 0x80000008,
        .model_id = "AMD Opteron 23xx (Gen 3 Class Opteron)",
    },
    {
        .name = "Opteron_G4",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_AVX | CPUID_EXT_XSAVE | CPUID_EXT_AES |
            CPUID_EXT_POPCNT | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ |
            CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL | CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        /* no xsaveopt! */
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 62xx class CPU",
    },
    {
        .name = "Opteron_G5",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 21,
        .model = 2,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_VME | CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX |
            CPUID_CLFLUSH | CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA |
            CPUID_PGE | CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 |
            CPUID_MCE | CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE |
            CPUID_DE | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_F16C | CPUID_EXT_AVX | CPUID_EXT_XSAVE |
            CPUID_EXT_AES | CPUID_EXT_POPCNT | CPUID_EXT_SSE42 |
            CPUID_EXT_SSE41 | CPUID_EXT_CX16 | CPUID_EXT_FMA |
            CPUID_EXT_SSSE3 | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_PDPE1GB | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL | CPUID_EXT2_RDTSCP,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_TBM | CPUID_EXT3_FMA4 | CPUID_EXT3_XOP |
            CPUID_EXT3_3DNOWPREFETCH | CPUID_EXT3_MISALIGNSSE |
            CPUID_EXT3_SSE4A | CPUID_EXT3_ABM | CPUID_EXT3_SVM |
            CPUID_EXT3_LAHF_LM,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        /* no xsaveopt! */
        .xlevel = 0x8000001A,
        .model_id = "AMD Opteron 63xx class CPU",
    },
    {
        .name = "EPYC",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 23,
        .model = 1,
        .stepping = 2,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_AES |  CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_SHA_NI,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "AMD EPYC Processor",
        .cache_info = &epyc_cache_info,
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .alias = "EPYC-IBPB",
                .props = (PropValue[]) {
                    { "ibpb", "on" },
                    { "model-id",
                      "AMD EPYC Processor (with IBPB)" },
                    { /* end of list */ }
                }
            },
            {
                .version = 3,
                .props = (PropValue[]) {
                    { "ibpb", "on" },
                    { "perfctr-core", "on" },
                    { "clzero", "on" },
                    { "xsaveerptr", "on" },
                    { "xsaves", "on" },
                    { "model-id",
                      "AMD EPYC Processor" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "Dhyana",
        .level = 0xd,
        .vendor = CPUID_VENDOR_HYGON,
        .family = 24,
        .model = 0,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_IBPB,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT,
        /* XSAVES is added in version 2 */
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "Hygon Dhyana Processor",
        .cache_info = &epyc_cache_info,
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            { .version = 2,
              .note = "XSAVES",
              .props = (PropValue[]) {
                  { "xsaves", "on" },
                  { /* end of list */ }
              },
            },
            { /* end of list */ }
        }
    },
    {
        .name = "EPYC-Rome",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 23,
        .model = 49,
        .stepping = 0,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_AES |  CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT | CPUID_EXT3_PERFCORE,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_CLZERO | CPUID_8000_0008_EBX_XSAVEERPTR |
            CPUID_8000_0008_EBX_WBNOINVD | CPUID_8000_0008_EBX_IBPB |
            CPUID_8000_0008_EBX_STIBP,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_SHA_NI | CPUID_7_0_EBX_CLWB,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_RDPID,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1 | CPUID_XSAVE_XSAVES,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE,
        .xlevel = 0x8000001E,
        .model_id = "AMD EPYC-Rome Processor",
        .cache_info = &epyc_rome_cache_info,
        .versions = (X86CPUVersionDefinition[]) {
            { .version = 1 },
            {
                .version = 2,
                .props = (PropValue[]) {
                    { "ibrs", "on" },
                    { "amd-ssbd", "on" },
                    { /* end of list */ }
                }
            },
            { /* end of list */ }
        }
    },
    {
        .name = "EPYC-Milan",
        .level = 0xd,
        .vendor = CPUID_VENDOR_AMD,
        .family = 25,
        .model = 1,
        .stepping = 1,
        .features[FEAT_1_EDX] =
            CPUID_SSE2 | CPUID_SSE | CPUID_FXSR | CPUID_MMX | CPUID_CLFLUSH |
            CPUID_PSE36 | CPUID_PAT | CPUID_CMOV | CPUID_MCA | CPUID_PGE |
            CPUID_MTRR | CPUID_SEP | CPUID_APIC | CPUID_CX8 | CPUID_MCE |
            CPUID_PAE | CPUID_MSR | CPUID_TSC | CPUID_PSE | CPUID_DE |
            CPUID_VME | CPUID_FP87,
        .features[FEAT_1_ECX] =
            CPUID_EXT_RDRAND | CPUID_EXT_F16C | CPUID_EXT_AVX |
            CPUID_EXT_XSAVE | CPUID_EXT_AES |  CPUID_EXT_POPCNT |
            CPUID_EXT_MOVBE | CPUID_EXT_SSE42 | CPUID_EXT_SSE41 |
            CPUID_EXT_CX16 | CPUID_EXT_FMA | CPUID_EXT_SSSE3 |
            CPUID_EXT_MONITOR | CPUID_EXT_PCLMULQDQ | CPUID_EXT_SSE3 |
            CPUID_EXT_PCID,
        .features[FEAT_8000_0001_EDX] =
            CPUID_EXT2_LM | CPUID_EXT2_RDTSCP | CPUID_EXT2_PDPE1GB |
            CPUID_EXT2_FFXSR | CPUID_EXT2_MMXEXT | CPUID_EXT2_NX |
            CPUID_EXT2_SYSCALL,
        .features[FEAT_8000_0001_ECX] =
            CPUID_EXT3_OSVW | CPUID_EXT3_3DNOWPREFETCH |
            CPUID_EXT3_MISALIGNSSE | CPUID_EXT3_SSE4A | CPUID_EXT3_ABM |
            CPUID_EXT3_CR8LEG | CPUID_EXT3_SVM | CPUID_EXT3_LAHF_LM |
            CPUID_EXT3_TOPOEXT | CPUID_EXT3_PERFCORE,
        .features[FEAT_8000_0008_EBX] =
            CPUID_8000_0008_EBX_CLZERO | CPUID_8000_0008_EBX_XSAVEERPTR |
            CPUID_8000_0008_EBX_WBNOINVD | CPUID_8000_0008_EBX_IBPB |
            CPUID_8000_0008_EBX_IBRS | CPUID_8000_0008_EBX_STIBP |
            CPUID_8000_0008_EBX_AMD_SSBD,
        .features[FEAT_7_0_EBX] =
            CPUID_7_0_EBX_FSGSBASE | CPUID_7_0_EBX_BMI1 | CPUID_7_0_EBX_AVX2 |
            CPUID_7_0_EBX_SMEP | CPUID_7_0_EBX_BMI2 | CPUID_7_0_EBX_RDSEED |
            CPUID_7_0_EBX_ADX | CPUID_7_0_EBX_SMAP | CPUID_7_0_EBX_CLFLUSHOPT |
            CPUID_7_0_EBX_SHA_NI | CPUID_7_0_EBX_CLWB | CPUID_7_0_EBX_ERMS |
            CPUID_7_0_EBX_INVPCID,
        .features[FEAT_7_0_ECX] =
            CPUID_7_0_ECX_UMIP | CPUID_7_0_ECX_RDPID | CPUID_7_0_ECX_PKU,
        .features[FEAT_7_0_EDX] =
            CPUID_7_0_EDX_FSRM,
        .features[FEAT_XSAVE] =
            CPUID_XSAVE_XSAVEOPT | CPUID_XSAVE_XSAVEC |
            CPUID_XSAVE_XGETBV1 | CPUID_XSAVE_XSAVES,
        .features[FEAT_6_EAX] =
            CPUID_6_EAX_ARAT,
        .features[FEAT_SVM] =
            CPUID_SVM_NPT | CPUID_SVM_NRIPSAVE | CPUID_SVM_SVME_ADDR_CHK,
        .xlevel = 0x8000001E,
        .model_id = "AMD EPYC-Milan Processor",
        .cache_info = &epyc_milan_cache_info,
    },
};

/*
 * We resolve CPU model aliases using -v1 when using "-machine
 * none", but this is just for compatibility while libvirt isn't
 * adapted to resolve CPU model versions before creating VMs.
 * See "Runnability guarantee of CPU models" at
 * docs/about/deprecated.rst.
 */
X86CPUVersion default_cpu_version = 1;

void x86_cpu_set_default_version(X86CPUVersion version)
{
    /* Translating CPU_VERSION_AUTO to CPU_VERSION_AUTO doesn't make sense */
    assert(version != CPU_VERSION_AUTO);
    default_cpu_version = version;
}

static X86CPUVersion x86_cpu_model_last_version(const X86CPUModel *model)
{
    int v = 0;
    const X86CPUVersionDefinition *vdef =
        x86_cpu_def_get_versions(model->cpudef);
    while (vdef->version) {
        v = vdef->version;
        vdef++;
    }
    return v;
}

/* Return the actual version being used for a specific CPU model */
static X86CPUVersion x86_cpu_model_resolve_version(const X86CPUModel *model)
{
    X86CPUVersion v = model->version;
    if (v == CPU_VERSION_AUTO) {
        v = default_cpu_version;
    }
    if (v == CPU_VERSION_LATEST) {
        return x86_cpu_model_last_version(model);
    }
    return v;
}

static Property max_x86_cpu_properties[] = {
    DEFINE_PROP_BOOL("migratable", X86CPU, migratable, true),
    DEFINE_PROP_BOOL("host-cache-info", X86CPU, cache_info_passthrough, false),
    DEFINE_PROP_END_OF_LIST()
};

static void max_x86_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->ordering = 9;

    xcc->model_description =
        "Enables all features supported by the accelerator in the current host";

    device_class_set_props(dc, max_x86_cpu_properties);
}

static void max_x86_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);

    /* We can't fill the features array here because we don't know yet if
     * "migratable" is true or false.
     */
    cpu->max_features = true;
    object_property_set_bool(OBJECT(cpu), "pmu", true, &error_abort);

    /*
     * these defaults are used for TCG and all other accelerators
     * besides KVM and HVF, which overwrite these values
     */
    object_property_set_str(OBJECT(cpu), "vendor", CPUID_VENDOR_AMD,
                            &error_abort);
#ifdef TARGET_X86_64
    object_property_set_int(OBJECT(cpu), "family", 15, &error_abort);
    object_property_set_int(OBJECT(cpu), "model", 107, &error_abort);
    object_property_set_int(OBJECT(cpu), "stepping", 1, &error_abort);
#else
    object_property_set_int(OBJECT(cpu), "family", 6, &error_abort);
    object_property_set_int(OBJECT(cpu), "model", 6, &error_abort);
    object_property_set_int(OBJECT(cpu), "stepping", 3, &error_abort);
#endif
    object_property_set_str(OBJECT(cpu), "model-id",
                            "QEMU TCG CPU version " QEMU_HW_VERSION,
                            &error_abort);
}

static const TypeInfo max_x86_cpu_type_info = {
    .name = X86_CPU_TYPE_NAME("max"),
    .parent = TYPE_X86_CPU,
    .instance_init = max_x86_cpu_initfn,
    .class_init = max_x86_cpu_class_init,
};

static char *feature_word_description(FeatureWordInfo *f, uint32_t bit)
{
    assert(f->type == CPUID_FEATURE_WORD || f->type == MSR_FEATURE_WORD);

    switch (f->type) {
    case CPUID_FEATURE_WORD:
        {
            const char *reg = get_register_name_32(f->cpuid.reg);
            assert(reg);
            return g_strdup_printf("CPUID.%02XH:%s",
                                   f->cpuid.eax, reg);
        }
    case MSR_FEATURE_WORD:
        return g_strdup_printf("MSR(%02XH)",
                               f->msr.index);
    }

    return NULL;
}

static bool x86_cpu_have_filtered_features(X86CPU *cpu)
{
    FeatureWord w;

    for (w = 0; w < FEATURE_WORDS; w++) {
        if (cpu->filtered_features[w]) {
            return true;
        }
    }

    return false;
}

static void mark_unavailable_features(X86CPU *cpu, FeatureWord w, uint64_t mask,
                                      const char *verbose_prefix)
{
    CPUX86State *env = &cpu->env;
    FeatureWordInfo *f = &feature_word_info[w];
    int i;

    if (!cpu->force_features) {
        env->features[w] &= ~mask;
    }
    cpu->filtered_features[w] |= mask;

    if (!verbose_prefix) {
        return;
    }

    for (i = 0; i < 64; ++i) {
        if ((1ULL << i) & mask) {
            g_autofree char *feat_word_str = feature_word_description(f, i);
            warn_report("%s: %s%s%s [bit %d]",
                        verbose_prefix,
                        feat_word_str,
                        f->feat_names[i] ? "." : "",
                        f->feat_names[i] ? f->feat_names[i] : "", i);
        }
    }
}

static void x86_cpuid_version_get_family(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 8) & 0xf;
    if (value == 0xf) {
        value += (env->cpuid_version >> 20) & 0xff;
    }
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_family(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff + 0xf;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xff00f00;
    if (value > 0x0f) {
        env->cpuid_version |= 0xf00 | ((value - 0x0f) << 20);
    } else {
        env->cpuid_version |= value << 8;
    }
}

static void x86_cpuid_version_get_model(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = (env->cpuid_version >> 4) & 0xf;
    value |= ((env->cpuid_version >> 16) & 0xf) << 4;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_model(Object *obj, Visitor *v,
                                        const char *name, void *opaque,
                                        Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xff;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf00f0;
    env->cpuid_version |= ((value & 0xf) << 4) | ((value >> 4) << 16);
}

static void x86_cpuid_version_get_stepping(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int64_t value;

    value = env->cpuid_version & 0xf;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_version_set_stepping(Object *obj, Visitor *v,
                                           const char *name, void *opaque,
                                           Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    const int64_t min = 0;
    const int64_t max = 0xf;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    env->cpuid_version &= ~0xf;
    env->cpuid_version |= value & 0xf;
}

static char *x86_cpuid_get_vendor(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;

    value = g_malloc(CPUID_VENDOR_SZ + 1);
    x86_cpu_vendor_words2str(value, env->cpuid_vendor1, env->cpuid_vendor2,
                             env->cpuid_vendor3);
    return value;
}

static void x86_cpuid_set_vendor(Object *obj, const char *value,
                                 Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int i;

    if (strlen(value) != CPUID_VENDOR_SZ) {
        error_setg(errp, QERR_PROPERTY_VALUE_BAD, "", "vendor", value);
        return;
    }

    env->cpuid_vendor1 = 0;
    env->cpuid_vendor2 = 0;
    env->cpuid_vendor3 = 0;
    for (i = 0; i < 4; i++) {
        env->cpuid_vendor1 |= ((uint8_t)value[i    ]) << (8 * i);
        env->cpuid_vendor2 |= ((uint8_t)value[i + 4]) << (8 * i);
        env->cpuid_vendor3 |= ((uint8_t)value[i + 8]) << (8 * i);
    }
}

static char *x86_cpuid_get_model_id(Object *obj, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    char *value;
    int i;

    value = g_malloc(48 + 1);
    for (i = 0; i < 48; i++) {
        value[i] = env->cpuid_model[i >> 2] >> (8 * (i & 3));
    }
    value[48] = '\0';
    return value;
}

static void x86_cpuid_set_model_id(Object *obj, const char *model_id,
                                   Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    CPUX86State *env = &cpu->env;
    int c, len, i;

    if (model_id == NULL) {
        model_id = "";
    }
    len = strlen(model_id);
    memset(env->cpuid_model, 0, 48);
    for (i = 0; i < 48; i++) {
        if (i >= len) {
            c = '\0';
        } else {
            c = (uint8_t)model_id[i];
        }
        env->cpuid_model[i >> 2] |= c << (8 * (i & 3));
    }
}

static void x86_cpuid_get_tsc_freq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    int64_t value;

    value = cpu->env.tsc_khz * 1000;
    visit_type_int(v, name, &value, errp);
}

static void x86_cpuid_set_tsc_freq(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    const int64_t min = 0;
    const int64_t max = INT64_MAX;
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
        return;
    }
    if (value < min || value > max) {
        error_setg(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE, "",
                   name ? name : "null", value, min, max);
        return;
    }

    cpu->env.tsc_khz = cpu->env.user_tsc_khz = value / 1000;
}

/* Generic getter for "feature-words" and "filtered-features" properties */
static void x86_cpu_get_feature_words(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    uint64_t *array = (uint64_t *)opaque;
    FeatureWord w;
    X86CPUFeatureWordInfo word_infos[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList list_entries[FEATURE_WORDS] = { };
    X86CPUFeatureWordInfoList *list = NULL;

    for (w = 0; w < FEATURE_WORDS; w++) {
        FeatureWordInfo *wi = &feature_word_info[w];
        /*
                * We didn't have MSR features when "feature-words" was
                *  introduced. Therefore skipped other type entries.
                */
        if (wi->type != CPUID_FEATURE_WORD) {
            continue;
        }
        X86CPUFeatureWordInfo *qwi = &word_infos[w];
        qwi->cpuid_input_eax = wi->cpuid.eax;
        qwi->has_cpuid_input_ecx = wi->cpuid.needs_ecx;
        qwi->cpuid_input_ecx = wi->cpuid.ecx;
        qwi->cpuid_register = x86_reg_info_32[wi->cpuid.reg].qapi_enum;
        qwi->features = array[w];

        /* List will be in reverse order, but order shouldn't matter */
        list_entries[w].next = list;
        list_entries[w].value = &word_infos[w];
        list = &list_entries[w];
    }

    visit_type_X86CPUFeatureWordInfoList(v, "feature-words", &list, errp);
}

/* Convert all '_' in a feature string option name to '-', to make feature
 * name conform to QOM property naming rule, which uses '-' instead of '_'.
 */
static inline void feat2prop(char *s)
{
    while ((s = strchr(s, '_'))) {
        *s = '-';
    }
}

/* Return the feature property name for a feature flag bit */
static const char *x86_cpu_feature_name(FeatureWord w, int bitnr)
{
    const char *name;
    /* XSAVE components are automatically enabled by other features,
     * so return the original feature name instead
     */
    if (w == FEAT_XSAVE_XCR0_LO || w == FEAT_XSAVE_XCR0_HI) {
        int comp = (w == FEAT_XSAVE_XCR0_HI) ? bitnr + 32 : bitnr;

        if (comp < ARRAY_SIZE(x86_ext_save_areas) &&
            x86_ext_save_areas[comp].bits) {
            w = x86_ext_save_areas[comp].feature;
            bitnr = ctz32(x86_ext_save_areas[comp].bits);
        }
    }

    assert(bitnr < 64);
    assert(w < FEATURE_WORDS);
    name = feature_word_info[w].feat_names[bitnr];
    assert(bitnr < 32 || !(name && feature_word_info[w].type == CPUID_FEATURE_WORD));
    return name;
}

/* Compatibily hack to maintain legacy +-feat semantic,
 * where +-feat overwrites any feature set by
 * feat=on|feat even if the later is parsed after +-feat
 * (i.e. "-x2apic,x2apic=on" will result in x2apic disabled)
 */
static GList *plus_features, *minus_features;

static gint compare_string(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(a, b);
}

/* Parse "+feature,-feature,feature=foo" CPU feature string
 */
static void x86_cpu_parse_featurestr(const char *typename, char *features,
                                     Error **errp)
{
    char *featurestr; /* Single 'key=value" string being parsed */
    static bool cpu_globals_initialized;
    bool ambiguous = false;

    if (cpu_globals_initialized) {
        return;
    }
    cpu_globals_initialized = true;

    if (!features) {
        return;
    }

    for (featurestr = strtok(features, ",");
         featurestr;
         featurestr = strtok(NULL, ",")) {
        const char *name;
        const char *val = NULL;
        char *eq = NULL;
        char num[32];
        GlobalProperty *prop;

        /* Compatibility syntax: */
        if (featurestr[0] == '+') {
            plus_features = g_list_append(plus_features,
                                          g_strdup(featurestr + 1));
            continue;
        } else if (featurestr[0] == '-') {
            minus_features = g_list_append(minus_features,
                                           g_strdup(featurestr + 1));
            continue;
        }

        eq = strchr(featurestr, '=');
        if (eq) {
            *eq++ = 0;
            val = eq;
        } else {
            val = "on";
        }

        feat2prop(featurestr);
        name = featurestr;

        if (g_list_find_custom(plus_features, name, compare_string)) {
            warn_report("Ambiguous CPU model string. "
                        "Don't mix both \"+%s\" and \"%s=%s\"",
                        name, name, val);
            ambiguous = true;
        }
        if (g_list_find_custom(minus_features, name, compare_string)) {
            warn_report("Ambiguous CPU model string. "
                        "Don't mix both \"-%s\" and \"%s=%s\"",
                        name, name, val);
            ambiguous = true;
        }

        /* Special case: */
        if (!strcmp(name, "tsc-freq")) {
            int ret;
            uint64_t tsc_freq;

            ret = qemu_strtosz_metric(val, NULL, &tsc_freq);
            if (ret < 0 || tsc_freq > INT64_MAX) {
                error_setg(errp, "bad numerical value %s", val);
                return;
            }
            snprintf(num, sizeof(num), "%" PRId64, tsc_freq);
            val = num;
            name = "tsc-frequency";
        }

        prop = g_new0(typeof(*prop), 1);
        prop->driver = typename;
        prop->property = g_strdup(name);
        prop->value = g_strdup(val);
        qdev_prop_register_global(prop);
    }

    if (ambiguous) {
        warn_report("Compatibility of ambiguous CPU model "
                    "strings won't be kept on future QEMU versions");
    }
}

static void x86_cpu_filter_features(X86CPU *cpu, bool verbose);

/* Build a list with the name of all features on a feature word array */
static void x86_cpu_list_feature_names(FeatureWordArray features,
                                       strList **list)
{
    strList **tail = list;
    FeatureWord w;

    for (w = 0; w < FEATURE_WORDS; w++) {
        uint64_t filtered = features[w];
        int i;
        for (i = 0; i < 64; i++) {
            if (filtered & (1ULL << i)) {
                QAPI_LIST_APPEND(tail, g_strdup(x86_cpu_feature_name(w, i)));
            }
        }
    }
}

static void x86_cpu_get_unavailable_features(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    X86CPU *xc = X86_CPU(obj);
    strList *result = NULL;

    x86_cpu_list_feature_names(xc->filtered_features, &result);
    visit_type_strList(v, "unavailable-features", &result, errp);
}

/* Check for missing features that may prevent the CPU class from
 * running using the current machine and accelerator.
 */
static void x86_cpu_class_check_missing_features(X86CPUClass *xcc,
                                                 strList **list)
{
    strList **tail = list;
    X86CPU *xc;
    Error *err = NULL;

    if (xcc->host_cpuid_required && !accel_uses_host_cpuid()) {
        QAPI_LIST_APPEND(tail, g_strdup("kvm"));
        return;
    }

    xc = X86_CPU(object_new_with_class(OBJECT_CLASS(xcc)));

    x86_cpu_expand_features(xc, &err);
    if (err) {
        /* Errors at x86_cpu_expand_features should never happen,
         * but in case it does, just report the model as not
         * runnable at all using the "type" property.
         */
        QAPI_LIST_APPEND(tail, g_strdup("type"));
        error_free(err);
    }

    x86_cpu_filter_features(xc, false);

    x86_cpu_list_feature_names(xc->filtered_features, tail);

    object_unref(OBJECT(xc));
}

/* Print all cpuid feature names in featureset
 */
static void listflags(GList *features)
{
    size_t len = 0;
    GList *tmp;

    for (tmp = features; tmp; tmp = tmp->next) {
        const char *name = tmp->data;
        if ((len + strlen(name) + 1) >= 75) {
            qemu_printf("\n");
            len = 0;
        }
        qemu_printf("%s%s", len == 0 ? "  " : " ", name);
        len += strlen(name) + 1;
    }
    qemu_printf("\n");
}

/* Sort alphabetically by type name, respecting X86CPUClass::ordering. */
static gint x86_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    X86CPUClass *cc_a = X86_CPU_CLASS(class_a);
    X86CPUClass *cc_b = X86_CPU_CLASS(class_b);
    int ret;

    if (cc_a->ordering != cc_b->ordering) {
        ret = cc_a->ordering - cc_b->ordering;
    } else {
        g_autofree char *name_a = x86_cpu_class_get_model_name(cc_a);
        g_autofree char *name_b = x86_cpu_class_get_model_name(cc_b);
        ret = strcmp(name_a, name_b);
    }
    return ret;
}

static GSList *get_sorted_cpu_model_list(void)
{
    GSList *list = object_class_get_list(TYPE_X86_CPU, false);
    list = g_slist_sort(list, x86_cpu_list_compare);
    return list;
}

static char *x86_cpu_class_get_model_id(X86CPUClass *xc)
{
    Object *obj = object_new_with_class(OBJECT_CLASS(xc));
    char *r = object_property_get_str(obj, "model-id", &error_abort);
    object_unref(obj);
    return r;
}

static char *x86_cpu_class_get_alias_of(X86CPUClass *cc)
{
    X86CPUVersion version;

    if (!cc->model || !cc->model->is_alias) {
        return NULL;
    }
    version = x86_cpu_model_resolve_version(cc->model);
    if (version <= 0) {
        return NULL;
    }
    return x86_cpu_versioned_model_name(cc->model->cpudef, version);
}

static void x86_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    X86CPUClass *cc = X86_CPU_CLASS(oc);
    g_autofree char *name = x86_cpu_class_get_model_name(cc);
    g_autofree char *desc = g_strdup(cc->model_description);
    g_autofree char *alias_of = x86_cpu_class_get_alias_of(cc);
    g_autofree char *model_id = x86_cpu_class_get_model_id(cc);

    if (!desc && alias_of) {
        if (cc->model && cc->model->version == CPU_VERSION_AUTO) {
            desc = g_strdup("(alias configured by machine type)");
        } else {
            desc = g_strdup_printf("(alias of %s)", alias_of);
        }
    }
    if (!desc && cc->model && cc->model->note) {
        desc = g_strdup_printf("%s [%s]", model_id, cc->model->note);
    }
    if (!desc) {
        desc = g_strdup_printf("%s", model_id);
    }

    if (cc->model && cc->model->cpudef->deprecation_note) {
        g_autofree char *olddesc = desc;
        desc = g_strdup_printf("%s (deprecated)", olddesc);
    }

    qemu_printf("x86 %-20s  %s\n", name, desc);
}

/* list available CPU models and flags */
void x86_cpu_list(void)
{
    int i, j;
    GSList *list;
    GList *names = NULL;

    qemu_printf("Available CPUs:\n");
    list = get_sorted_cpu_model_list();
    g_slist_foreach(list, x86_cpu_list_entry, NULL);
    g_slist_free(list);

    names = NULL;
    for (i = 0; i < ARRAY_SIZE(feature_word_info); i++) {
        FeatureWordInfo *fw = &feature_word_info[i];
        for (j = 0; j < 64; j++) {
            if (fw->feat_names[j]) {
                names = g_list_append(names, (gpointer)fw->feat_names[j]);
            }
        }
    }

    names = g_list_sort(names, (GCompareFunc)strcmp);

    qemu_printf("\nRecognized CPUID flags:\n");
    listflags(names);
    qemu_printf("\n");
    g_list_free(names);
}

static void x86_cpu_definition_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    X86CPUClass *cc = X86_CPU_CLASS(oc);
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = x86_cpu_class_get_model_name(cc);
    x86_cpu_class_check_missing_features(cc, &info->unavailable_features);
    info->has_unavailable_features = true;
    info->q_typename = g_strdup(object_class_get_name(oc));
    info->migration_safe = cc->migration_safe;
    info->has_migration_safe = true;
    info->q_static = cc->static_model;
    if (cc->model && cc->model->cpudef->deprecation_note) {
        info->deprecated = true;
    } else {
        info->deprecated = false;
    }
    /*
     * Old machine types won't report aliases, so that alias translation
     * doesn't break compatibility with previous QEMU versions.
     */
    if (default_cpu_version != CPU_VERSION_LEGACY) {
        info->alias_of = x86_cpu_class_get_alias_of(cc);
    }

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list = get_sorted_cpu_model_list();
    g_slist_foreach(list, x86_cpu_definition_entry, &cpu_list);
    g_slist_free(list);
    return cpu_list;
}

uint64_t x86_cpu_get_supported_feature_word(FeatureWord w,
                                            bool migratable_only)
{
    FeatureWordInfo *wi = &feature_word_info[w];
    uint64_t r = 0;

    if (kvm_enabled()) {
        switch (wi->type) {
        case CPUID_FEATURE_WORD:
            r = kvm_arch_get_supported_cpuid(kvm_state, wi->cpuid.eax,
                                                        wi->cpuid.ecx,
                                                        wi->cpuid.reg);
            break;
        case MSR_FEATURE_WORD:
            r = kvm_arch_get_supported_msr_feature(kvm_state,
                        wi->msr.index);
            break;
        }
    } else if (hvf_enabled()) {
        if (wi->type != CPUID_FEATURE_WORD) {
            return 0;
        }
        r = hvf_get_supported_cpuid(wi->cpuid.eax,
                                    wi->cpuid.ecx,
                                    wi->cpuid.reg);
    } else if (tcg_enabled()) {
        r = wi->tcg_features;
    } else {
        return ~0;
    }
#ifndef TARGET_X86_64
    if (w == FEAT_8000_0001_EDX) {
        r &= ~CPUID_EXT2_LM;
    }
#endif
    if (migratable_only) {
        r &= x86_cpu_get_migratable_flags(w);
    }
    return r;
}

static void x86_cpu_get_supported_cpuid(uint32_t func, uint32_t index,
                                        uint32_t *eax, uint32_t *ebx,
                                        uint32_t *ecx, uint32_t *edx)
{
    if (kvm_enabled()) {
        *eax = kvm_arch_get_supported_cpuid(kvm_state, func, index, R_EAX);
        *ebx = kvm_arch_get_supported_cpuid(kvm_state, func, index, R_EBX);
        *ecx = kvm_arch_get_supported_cpuid(kvm_state, func, index, R_ECX);
        *edx = kvm_arch_get_supported_cpuid(kvm_state, func, index, R_EDX);
    } else if (hvf_enabled()) {
        *eax = hvf_get_supported_cpuid(func, index, R_EAX);
        *ebx = hvf_get_supported_cpuid(func, index, R_EBX);
        *ecx = hvf_get_supported_cpuid(func, index, R_ECX);
        *edx = hvf_get_supported_cpuid(func, index, R_EDX);
    } else {
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
    }
}

static void x86_cpu_get_cache_cpuid(uint32_t func, uint32_t index,
                                    uint32_t *eax, uint32_t *ebx,
                                    uint32_t *ecx, uint32_t *edx)
{
    uint32_t level, unused;

    /* Only return valid host leaves.  */
    switch (func) {
    case 2:
    case 4:
        host_cpuid(0, 0, &level, &unused, &unused, &unused);
        break;
    case 0x80000005:
    case 0x80000006:
    case 0x8000001d:
        host_cpuid(0x80000000, 0, &level, &unused, &unused, &unused);
        break;
    default:
        return;
    }

    if (func > level) {
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
    } else {
        host_cpuid(func, index, eax, ebx, ecx, edx);
    }
}

/*
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 */
void x86_cpu_apply_props(X86CPU *cpu, PropValue *props)
{
    PropValue *pv;
    for (pv = props; pv->prop; pv++) {
        if (!pv->value) {
            continue;
        }
        object_property_parse(OBJECT(cpu), pv->prop, pv->value,
                              &error_abort);
    }
}

/*
 * Apply properties for the CPU model version specified in model.
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 */

static void x86_cpu_apply_version_props(X86CPU *cpu, X86CPUModel *model)
{
    const X86CPUVersionDefinition *vdef;
    X86CPUVersion version = x86_cpu_model_resolve_version(model);

    if (version == CPU_VERSION_LEGACY) {
        return;
    }

    for (vdef = x86_cpu_def_get_versions(model->cpudef); vdef->version; vdef++) {
        PropValue *p;

        for (p = vdef->props; p && p->prop; p++) {
            object_property_parse(OBJECT(cpu), p->prop, p->value,
                                  &error_abort);
        }

        if (vdef->version == version) {
            break;
        }
    }

    /*
     * If we reached the end of the list, version number was invalid
     */
    assert(vdef->version == version);
}

/*
 * Load data from X86CPUDefinition into a X86CPU object.
 * Only for builtin_x86_defs models initialized with x86_register_cpudef_types.
 */
static void x86_cpu_load_model(X86CPU *cpu, X86CPUModel *model)
{
    const X86CPUDefinition *def = model->cpudef;
    CPUX86State *env = &cpu->env;
    FeatureWord w;

    /*NOTE: any property set by this function should be returned by
     * x86_cpu_static_props(), so static expansion of
     * query-cpu-model-expansion is always complete.
     */

    /* CPU models only set _minimum_ values for level/xlevel: */
    object_property_set_uint(OBJECT(cpu), "min-level", def->level,
                             &error_abort);
    object_property_set_uint(OBJECT(cpu), "min-xlevel", def->xlevel,
                             &error_abort);

    object_property_set_int(OBJECT(cpu), "family", def->family, &error_abort);
    object_property_set_int(OBJECT(cpu), "model", def->model, &error_abort);
    object_property_set_int(OBJECT(cpu), "stepping", def->stepping,
                            &error_abort);
    object_property_set_str(OBJECT(cpu), "model-id", def->model_id,
                            &error_abort);
    for (w = 0; w < FEATURE_WORDS; w++) {
        env->features[w] = def->features[w];
    }

    /* legacy-cache defaults to 'off' if CPU model provides cache info */
    cpu->legacy_cache = !def->cache_info;

    env->features[FEAT_1_ECX] |= CPUID_EXT_HYPERVISOR;

    /* sysenter isn't supported in compatibility mode on AMD,
     * syscall isn't supported in compatibility mode on Intel.
     * Normally we advertise the actual CPU vendor, but you can
     * override this using the 'vendor' property if you want to use
     * KVM's sysenter/syscall emulation in compatibility mode and
     * when doing cross vendor migration
     */

    /*
     * vendor property is set here but then overloaded with the
     * host cpu vendor for KVM and HVF.
     */
    object_property_set_str(OBJECT(cpu), "vendor", def->vendor, &error_abort);

    x86_cpu_apply_version_props(cpu, model);

    /*
     * Properties in versioned CPU model are not user specified features.
     * We can simply clear env->user_features here since it will be filled later
     * in x86_cpu_expand_features() based on plus_features and minus_features.
     */
    memset(&env->user_features, 0, sizeof(env->user_features));
}

static gchar *x86_gdb_arch_name(CPUState *cs)
{
#ifdef TARGET_X86_64
    return g_strdup("i386:x86-64");
#else
    return g_strdup("i386");
#endif
}

static void x86_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    X86CPUModel *model = data;
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    xcc->model = model;
    xcc->migration_safe = true;
    cc->deprecation_note = model->cpudef->deprecation_note;
}

static void x86_register_cpu_model_type(const char *name, X86CPUModel *model)
{
    g_autofree char *typename = x86_cpu_type_name(name);
    TypeInfo ti = {
        .name = typename,
        .parent = TYPE_X86_CPU,
        .class_init = x86_cpu_cpudef_class_init,
        .class_data = model,
    };

    type_register(&ti);
}


/*
 * register builtin_x86_defs;
 * "max", "base" and subclasses ("host") are not registered here.
 * See x86_cpu_register_types for all model registrations.
 */
static void x86_register_cpudef_types(const X86CPUDefinition *def)
{
    X86CPUModel *m;
    const X86CPUVersionDefinition *vdef;

    /* AMD aliases are handled at runtime based on CPUID vendor, so
     * they shouldn't be set on the CPU model table.
     */
    assert(!(def->features[FEAT_8000_0001_EDX] & CPUID_EXT2_AMD_ALIASES));
    /* catch mistakes instead of silently truncating model_id when too long */
    assert(def->model_id && strlen(def->model_id) <= 48);

    /* Unversioned model: */
    m = g_new0(X86CPUModel, 1);
    m->cpudef = def;
    m->version = CPU_VERSION_AUTO;
    m->is_alias = true;
    x86_register_cpu_model_type(def->name, m);

    /* Versioned models: */

    for (vdef = x86_cpu_def_get_versions(def); vdef->version; vdef++) {
        X86CPUModel *m = g_new0(X86CPUModel, 1);
        g_autofree char *name =
            x86_cpu_versioned_model_name(def, vdef->version);
        m->cpudef = def;
        m->version = vdef->version;
        m->note = vdef->note;
        x86_register_cpu_model_type(name, m);

        if (vdef->alias) {
            X86CPUModel *am = g_new0(X86CPUModel, 1);
            am->cpudef = def;
            am->version = vdef->version;
            am->is_alias = true;
            x86_register_cpu_model_type(vdef->alias, am);
        }
    }

}

uint32_t cpu_x86_virtual_addr_width(CPUX86State *env)
{
    if  (env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_LA57) {
        return 57; /* 57 bits virtual */
    } else {
        return 48; /* 48 bits virtual */
    }
}

void cpu_x86_cpuid(CPUX86State *env, uint32_t index, uint32_t count,
                   uint32_t *eax, uint32_t *ebx,
                   uint32_t *ecx, uint32_t *edx)
{
    X86CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    uint32_t die_offset;
    uint32_t limit;
    uint32_t signature[3];
    X86CPUTopoInfo topo_info;

    topo_info.dies_per_pkg = env->nr_dies;
    topo_info.cores_per_die = cs->nr_cores;
    topo_info.threads_per_core = cs->nr_threads;

    /* Calculate & apply limits for different index ranges */
    if (index >= 0xC0000000) {
        limit = env->cpuid_xlevel2;
    } else if (index >= 0x80000000) {
        limit = env->cpuid_xlevel;
    } else if (index >= 0x40000000) {
        limit = 0x40000001;
    } else {
        limit = env->cpuid_level;
    }

    if (index > limit) {
        /* Intel documentation states that invalid EAX input will
         * return the same information as EAX=cpuid_level
         * (Intel SDM Vol. 2A - Instruction Set Reference - CPUID)
         */
        index = env->cpuid_level;
    }

    switch(index) {
    case 0:
        *eax = env->cpuid_level;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 1:
        *eax = env->cpuid_version;
        *ebx = (cpu->apic_id << 24) |
               8 << 8; /* CLFLUSH size in quad words, Linux wants it. */
        *ecx = env->features[FEAT_1_ECX];
        if ((*ecx & CPUID_EXT_XSAVE) && (env->cr[4] & CR4_OSXSAVE_MASK)) {
            *ecx |= CPUID_EXT_OSXSAVE;
        }
        *edx = env->features[FEAT_1_EDX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            *ebx |= (cs->nr_cores * cs->nr_threads) << 16;
            *edx |= CPUID_HT;
        }
        if (!cpu->enable_pmu) {
            *ecx &= ~CPUID_EXT_PDCM;
        }
        break;
    case 2:
        /* cache info: needed for Pentium Pro compatibility */
        if (cpu->cache_info_passthrough) {
            x86_cpu_get_cache_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        } else if (cpu->vendor_cpuid_only && IS_AMD_CPU(env)) {
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }
        *eax = 1; /* Number of CPUID[EAX=2] calls required */
        *ebx = 0;
        if (!cpu->enable_l3_cache) {
            *ecx = 0;
        } else {
            *ecx = cpuid2_cache_descriptor(env->cache_info_cpuid2.l3_cache);
        }
        *edx = (cpuid2_cache_descriptor(env->cache_info_cpuid2.l1d_cache) << 16) |
               (cpuid2_cache_descriptor(env->cache_info_cpuid2.l1i_cache) <<  8) |
               (cpuid2_cache_descriptor(env->cache_info_cpuid2.l2_cache));
        break;
    case 4:
        /* cache info: needed for Core compatibility */
        if (cpu->cache_info_passthrough) {
            x86_cpu_get_cache_cpuid(index, count, eax, ebx, ecx, edx);
            /*
             * QEMU has its own number of cores/logical cpus,
             * set 24..14, 31..26 bit to configured values
             */
            if (*eax & 31) {
                int host_vcpus_per_cache = 1 + ((*eax & 0x3FFC000) >> 14);
                int vcpus_per_socket = env->nr_dies * cs->nr_cores *
                                       cs->nr_threads;
                if (cs->nr_cores > 1) {
                    *eax &= ~0xFC000000;
                    *eax |= (pow2ceil(cs->nr_cores) - 1) << 26;
                }
                if (host_vcpus_per_cache > vcpus_per_socket) {
                    *eax &= ~0x3FFC000;
                    *eax |= (pow2ceil(vcpus_per_socket) - 1) << 14;
                }
            }
        } else if (cpu->vendor_cpuid_only && IS_AMD_CPU(env)) {
            *eax = *ebx = *ecx = *edx = 0;
        } else {
            *eax = 0;
            switch (count) {
            case 0: /* L1 dcache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l1d_cache,
                                    1, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 1: /* L1 icache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l1i_cache,
                                    1, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 2: /* L2 cache info */
                encode_cache_cpuid4(env->cache_info_cpuid4.l2_cache,
                                    cs->nr_threads, cs->nr_cores,
                                    eax, ebx, ecx, edx);
                break;
            case 3: /* L3 cache info */
                die_offset = apicid_die_offset(&topo_info);
                if (cpu->enable_l3_cache) {
                    encode_cache_cpuid4(env->cache_info_cpuid4.l3_cache,
                                        (1 << die_offset), cs->nr_cores,
                                        eax, ebx, ecx, edx);
                    break;
                }
                /* fall through */
            default: /* end of info */
                *eax = *ebx = *ecx = *edx = 0;
                break;
            }
        }
        break;
    case 5:
        /* MONITOR/MWAIT Leaf */
        *eax = cpu->mwait.eax; /* Smallest monitor-line size in bytes */
        *ebx = cpu->mwait.ebx; /* Largest monitor-line size in bytes */
        *ecx = cpu->mwait.ecx; /* flags */
        *edx = cpu->mwait.edx; /* mwait substates */
        break;
    case 6:
        /* Thermal and Power Leaf */
        *eax = env->features[FEAT_6_EAX];
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 7:
        /* Structured Extended Feature Flags Enumeration Leaf */
        if (count == 0) {
            /* Maximum ECX value for sub-leaves */
            *eax = env->cpuid_level_func7;
            *ebx = env->features[FEAT_7_0_EBX]; /* Feature flags */
            *ecx = env->features[FEAT_7_0_ECX]; /* Feature flags */
            if ((*ecx & CPUID_7_0_ECX_PKU) && env->cr[4] & CR4_PKE_MASK) {
                *ecx |= CPUID_7_0_ECX_OSPKE;
            }
            *edx = env->features[FEAT_7_0_EDX]; /* Feature flags */

            /*
             * SGX cannot be emulated in software.  If hardware does not
             * support enabling SGX and/or SGX flexible launch control,
             * then we need to update the VM's CPUID values accordingly.
             */
            if ((*ebx & CPUID_7_0_EBX_SGX) &&
                (!kvm_enabled() ||
                 !(kvm_arch_get_supported_cpuid(cs->kvm_state, 0x7, 0, R_EBX) &
                    CPUID_7_0_EBX_SGX))) {
                *ebx &= ~CPUID_7_0_EBX_SGX;
            }

            if ((*ecx & CPUID_7_0_ECX_SGX_LC) &&
                (!(*ebx & CPUID_7_0_EBX_SGX) || !kvm_enabled() ||
                 !(kvm_arch_get_supported_cpuid(cs->kvm_state, 0x7, 0, R_ECX) &
                    CPUID_7_0_ECX_SGX_LC))) {
                *ecx &= ~CPUID_7_0_ECX_SGX_LC;
            }
        } else if (count == 1) {
            *eax = env->features[FEAT_7_1_EAX];
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 9:
        /* Direct Cache Access Information Leaf */
        *eax = 0; /* Bits 0-31 in DCA_CAP MSR */
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xA:
        /* Architectural Performance Monitoring Leaf */
        if (accel_uses_host_cpuid() && cpu->enable_pmu) {
            x86_cpu_get_supported_cpuid(0xA, count, eax, ebx, ecx, edx);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xB:
        /* Extended Topology Enumeration Leaf */
        if (!cpu->enable_cpuid_0xb) {
                *eax = *ebx = *ecx = *edx = 0;
                break;
        }

        *ecx = count & 0xff;
        *edx = cpu->apic_id;

        switch (count) {
        case 0:
            *eax = apicid_core_offset(&topo_info);
            *ebx = cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_SMT;
            break;
        case 1:
            *eax = apicid_pkg_offset(&topo_info);
            *ebx = cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_CORE;
            break;
        default:
            *eax = 0;
            *ebx = 0;
            *ecx |= CPUID_TOPOLOGY_LEVEL_INVALID;
        }

        assert(!(*eax & ~0x1f));
        *ebx &= 0xffff; /* The count doesn't need to be reliable. */
        break;
    case 0x1C:
        if (accel_uses_host_cpuid() && cpu->enable_pmu &&
            (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR)) {
            x86_cpu_get_supported_cpuid(0x1C, 0, eax, ebx, ecx, edx);
            *edx = 0;
        }
        break;
    case 0x1F:
        /* V2 Extended Topology Enumeration Leaf */
        if (env->nr_dies < 2) {
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }

        *ecx = count & 0xff;
        *edx = cpu->apic_id;
        switch (count) {
        case 0:
            *eax = apicid_core_offset(&topo_info);
            *ebx = cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_SMT;
            break;
        case 1:
            *eax = apicid_die_offset(&topo_info);
            *ebx = cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_CORE;
            break;
        case 2:
            *eax = apicid_pkg_offset(&topo_info);
            *ebx = env->nr_dies * cs->nr_cores * cs->nr_threads;
            *ecx |= CPUID_TOPOLOGY_LEVEL_DIE;
            break;
        default:
            *eax = 0;
            *ebx = 0;
            *ecx |= CPUID_TOPOLOGY_LEVEL_INVALID;
        }
        assert(!(*eax & ~0x1f));
        *ebx &= 0xffff; /* The count doesn't need to be reliable. */
        break;
    case 0xD: {
        /* Processor Extended State */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
            break;
        }

        if (count == 0) {
            *ecx = xsave_area_size(x86_cpu_xsave_xcr0_components(cpu), false);
            *eax = env->features[FEAT_XSAVE_XCR0_LO];
            *edx = env->features[FEAT_XSAVE_XCR0_HI];
            /*
             * The initial value of xcr0 and ebx == 0, On host without kvm
             * commit 412a3c41(e.g., CentOS 6), the ebx's value always == 0
             * even through guest update xcr0, this will crash some legacy guest
             * (e.g., CentOS 6), So set ebx == ecx to workaroud it.
             */
            *ebx = kvm_enabled() ? *ecx : xsave_area_size(env->xcr0, false);
        } else if (count == 1) {
            uint64_t xstate = x86_cpu_xsave_xcr0_components(cpu) |
                              x86_cpu_xsave_xss_components(cpu);

            *eax = env->features[FEAT_XSAVE];
            *ebx = xsave_area_size(xstate, true);
            *ecx = env->features[FEAT_XSAVE_XSS_LO];
            *edx = env->features[FEAT_XSAVE_XSS_HI];
            if (kvm_enabled() && cpu->enable_pmu &&
                (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR) &&
                (*eax & CPUID_XSAVE_XSAVES)) {
                *ecx |= XSTATE_ARCH_LBR_MASK;
            } else {
                *ecx &= ~XSTATE_ARCH_LBR_MASK;
            }
        } else if (count == 0xf &&
                   accel_uses_host_cpuid() && cpu->enable_pmu &&
                   (env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_ARCH_LBR)) {
            x86_cpu_get_supported_cpuid(0xD, count, eax, ebx, ecx, edx);
        } else if (count < ARRAY_SIZE(x86_ext_save_areas)) {
            const ExtSaveArea *esa = &x86_ext_save_areas[count];

            if (x86_cpu_xsave_xcr0_components(cpu) & (1ULL << count)) {
                *eax = esa->size;
                *ebx = esa->offset;
                *ecx = esa->ecx &
                       (ESA_FEATURE_ALIGN64_MASK | ESA_FEATURE_XFD_MASK);
            } else if (x86_cpu_xsave_xss_components(cpu) & (1ULL << count)) {
                *eax = esa->size;
                *ebx = 0;
                *ecx = 1;
            }
        }
        break;
    }
    case 0x12:
#ifndef CONFIG_USER_ONLY
        if (!kvm_enabled() ||
            !(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX)) {
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }

        /*
         * SGX sub-leafs CPUID.0x12.{0x2..N} enumerate EPC sections.  Retrieve
         * the EPC properties, e.g. confidentiality and integrity, from the
         * host's first EPC section, i.e. assume there is one EPC section or
         * that all EPC sections have the same security properties.
         */
        if (count > 1) {
            uint64_t epc_addr, epc_size;

            if (sgx_epc_get_section(count - 2, &epc_addr, &epc_size)) {
                *eax = *ebx = *ecx = *edx = 0;
                break;
            }
            host_cpuid(index, 2, eax, ebx, ecx, edx);
            *eax = (uint32_t)(epc_addr & 0xfffff000) | 0x1;
            *ebx = (uint32_t)(epc_addr >> 32);
            *ecx = (uint32_t)(epc_size & 0xfffff000) | (*ecx & 0xf);
            *edx = (uint32_t)(epc_size >> 32);
            break;
        }

        /*
         * SGX sub-leafs CPUID.0x12.{0x0,0x1} are heavily dependent on hardware
         * and KVM, i.e. QEMU cannot emulate features to override what KVM
         * supports.  Features can be further restricted by userspace, but not
         * made more permissive.
         */
        x86_cpu_get_supported_cpuid(0x12, count, eax, ebx, ecx, edx);

        if (count == 0) {
            *eax &= env->features[FEAT_SGX_12_0_EAX];
            *ebx &= env->features[FEAT_SGX_12_0_EBX];
        } else {
            *eax &= env->features[FEAT_SGX_12_1_EAX];
            *ebx &= 0; /* ebx reserve */
            *ecx &= env->features[FEAT_XSAVE_XSS_LO];
            *edx &= env->features[FEAT_XSAVE_XSS_HI];

            /* FP and SSE are always allowed regardless of XSAVE/XCR0. */
            *ecx |= XSTATE_FP_MASK | XSTATE_SSE_MASK;

            /* Access to PROVISIONKEY requires additional credentials. */
            if ((*eax & (1U << 4)) &&
                !kvm_enable_sgx_provisioning(cs->kvm_state)) {
                *eax &= ~(1U << 4);
            }
        }
#endif
        break;
    case 0x14: {
        /* Intel Processor Trace Enumeration */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT) ||
            !kvm_enabled()) {
            break;
        }

        if (count == 0) {
            *eax = INTEL_PT_MAX_SUBLEAF;
            *ebx = INTEL_PT_MINIMAL_EBX;
            *ecx = INTEL_PT_MINIMAL_ECX;
            if (env->features[FEAT_14_0_ECX] & CPUID_14_0_ECX_LIP) {
                *ecx |= CPUID_14_0_ECX_LIP;
            }
        } else if (count == 1) {
            *eax = INTEL_PT_MTC_BITMAP | INTEL_PT_ADDR_RANGES_NUM;
            *ebx = INTEL_PT_PSB_BITMAP | INTEL_PT_CYCLE_BITMAP;
        }
        break;
    }
    case 0x1D: {
        /* AMX TILE */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE)) {
            break;
        }

        if (count == 0) {
            /* Highest numbered palette subleaf */
            *eax = INTEL_AMX_TILE_MAX_SUBLEAF;
        } else if (count == 1) {
            *eax = INTEL_AMX_TOTAL_TILE_BYTES |
                   (INTEL_AMX_BYTES_PER_TILE << 16);
            *ebx = INTEL_AMX_BYTES_PER_ROW | (INTEL_AMX_TILE_MAX_NAMES << 16);
            *ecx = INTEL_AMX_TILE_MAX_ROWS;
        }
        break;
    }
    case 0x1E: {
        /* AMX TMUL */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        if (!(env->features[FEAT_7_0_EDX] & CPUID_7_0_EDX_AMX_TILE)) {
            break;
        }

        if (count == 0) {
            /* Highest numbered palette subleaf */
            *ebx = INTEL_AMX_TMUL_MAX_K | (INTEL_AMX_TMUL_MAX_N << 8);
        }
        break;
    }
    case 0x40000000:
        /*
         * CPUID code in kvm_arch_init_vcpu() ignores stuff
         * set here, but we restrict to TCG none the less.
         */
        if (tcg_enabled() && cpu->expose_tcg) {
            memcpy(signature, "TCGTCGTCGTCG", 12);
            *eax = 0x40000001;
            *ebx = signature[0];
            *ecx = signature[1];
            *edx = signature[2];
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x40000001:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0x80000000:
        *eax = env->cpuid_xlevel;
        *ebx = env->cpuid_vendor1;
        *edx = env->cpuid_vendor2;
        *ecx = env->cpuid_vendor3;
        break;
    case 0x80000001:
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = env->features[FEAT_8000_0001_ECX];
        *edx = env->features[FEAT_8000_0001_EDX];

        /* The Linux kernel checks for the CMPLegacy bit and
         * discards multiple thread information if it is set.
         * So don't set it here for Intel to make Linux guests happy.
         */
        if (cs->nr_cores * cs->nr_threads > 1) {
            if (env->cpuid_vendor1 != CPUID_VENDOR_INTEL_1 ||
                env->cpuid_vendor2 != CPUID_VENDOR_INTEL_2 ||
                env->cpuid_vendor3 != CPUID_VENDOR_INTEL_3) {
                *ecx |= 1 << 1;    /* CmpLegacy bit */
            }
        }
        break;
    case 0x80000002:
    case 0x80000003:
    case 0x80000004:
        *eax = env->cpuid_model[(index - 0x80000002) * 4 + 0];
        *ebx = env->cpuid_model[(index - 0x80000002) * 4 + 1];
        *ecx = env->cpuid_model[(index - 0x80000002) * 4 + 2];
        *edx = env->cpuid_model[(index - 0x80000002) * 4 + 3];
        break;
    case 0x80000005:
        /* cache info (L1 cache) */
        if (cpu->cache_info_passthrough) {
            x86_cpu_get_cache_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (L1_DTLB_2M_ASSOC << 24) | (L1_DTLB_2M_ENTRIES << 16) |
               (L1_ITLB_2M_ASSOC <<  8) | (L1_ITLB_2M_ENTRIES);
        *ebx = (L1_DTLB_4K_ASSOC << 24) | (L1_DTLB_4K_ENTRIES << 16) |
               (L1_ITLB_4K_ASSOC <<  8) | (L1_ITLB_4K_ENTRIES);
        *ecx = encode_cache_cpuid80000005(env->cache_info_amd.l1d_cache);
        *edx = encode_cache_cpuid80000005(env->cache_info_amd.l1i_cache);
        break;
    case 0x80000006:
        /* cache info (L2 cache) */
        if (cpu->cache_info_passthrough) {
            x86_cpu_get_cache_cpuid(index, 0, eax, ebx, ecx, edx);
            break;
        }
        *eax = (AMD_ENC_ASSOC(L2_DTLB_2M_ASSOC) << 28) |
               (L2_DTLB_2M_ENTRIES << 16) |
               (AMD_ENC_ASSOC(L2_ITLB_2M_ASSOC) << 12) |
               (L2_ITLB_2M_ENTRIES);
        *ebx = (AMD_ENC_ASSOC(L2_DTLB_4K_ASSOC) << 28) |
               (L2_DTLB_4K_ENTRIES << 16) |
               (AMD_ENC_ASSOC(L2_ITLB_4K_ASSOC) << 12) |
               (L2_ITLB_4K_ENTRIES);
        encode_cache_cpuid80000006(env->cache_info_amd.l2_cache,
                                   cpu->enable_l3_cache ?
                                   env->cache_info_amd.l3_cache : NULL,
                                   ecx, edx);
        break;
    case 0x80000007:
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_8000_0007_EDX];
        break;
    case 0x80000008:
        /* virtual & phys address size in low 2 bytes. */
        *eax = cpu->phys_bits;
        if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
            /* 64 bit processor */
             *eax |= (cpu_x86_virtual_addr_width(env) << 8);
        }
        *ebx = env->features[FEAT_8000_0008_EBX];
        if (cs->nr_cores * cs->nr_threads > 1) {
            /*
             * Bits 15:12 is "The number of bits in the initial
             * Core::X86::Apic::ApicId[ApicId] value that indicate
             * thread ID within a package".
             * Bits 7:0 is "The number of threads in the package is NC+1"
             */
            *ecx = (apicid_pkg_offset(&topo_info) << 12) |
                   ((cs->nr_cores * cs->nr_threads) - 1);
        } else {
            *ecx = 0;
        }
        *edx = 0;
        break;
    case 0x8000000A:
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            *eax = 0x00000001; /* SVM Revision */
            *ebx = 0x00000010; /* nr of ASIDs */
            *ecx = 0;
            *edx = env->features[FEAT_SVM]; /* optional features */
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0x8000001D:
        *eax = 0;
        if (cpu->cache_info_passthrough) {
            x86_cpu_get_cache_cpuid(index, count, eax, ebx, ecx, edx);
            break;
        }
        switch (count) {
        case 0: /* L1 dcache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l1d_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 1: /* L1 icache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l1i_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 2: /* L2 cache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l2_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        case 3: /* L3 cache info */
            encode_cache_cpuid8000001d(env->cache_info_amd.l3_cache,
                                       &topo_info, eax, ebx, ecx, edx);
            break;
        default: /* end of info */
            *eax = *ebx = *ecx = *edx = 0;
            break;
        }
        break;
    case 0x8000001E:
        if (cpu->core_id <= 255) {
            encode_topo_cpuid8000001e(cpu, &topo_info, eax, ebx, ecx, edx);
        } else {
            *eax = 0;
            *ebx = 0;
            *ecx = 0;
            *edx = 0;
        }
        break;
    case 0xC0000000:
        *eax = env->cpuid_xlevel2;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0xC0000001:
        /* Support for VIA CPU's CPUID instruction */
        *eax = env->cpuid_version;
        *ebx = 0;
        *ecx = 0;
        *edx = env->features[FEAT_C000_0001_EDX];
        break;
    case 0xC0000002:
    case 0xC0000003:
    case 0xC0000004:
        /* Reserved for the future, and now filled with zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    case 0x8000001F:
        *eax = *ebx = *ecx = *edx = 0;
        if (sev_enabled()) {
            *eax = 0x2;
            *eax |= sev_es_enabled() ? 0x8 : 0;
            *ebx = sev_get_cbit_position();
            *ebx |= sev_get_reduced_phys_bits() << 6;
        }
        break;
    default:
        /* reserved values: zero */
        *eax = 0;
        *ebx = 0;
        *ecx = 0;
        *edx = 0;
        break;
    }
}

static void x86_cpu_set_sgxlepubkeyhash(CPUX86State *env)
{
#ifndef CONFIG_USER_ONLY
    /* Those default values are defined in Skylake HW */
    env->msr_ia32_sgxlepubkeyhash[0] = 0xa6053e051270b7acULL;
    env->msr_ia32_sgxlepubkeyhash[1] = 0x6cfbe8ba8b3b413dULL;
    env->msr_ia32_sgxlepubkeyhash[2] = 0xc4916d99f2b3735dULL;
    env->msr_ia32_sgxlepubkeyhash[3] = 0xd4f8c05909f9bb3bULL;
#endif
}

static void x86_cpu_reset_hold(Object *obj)
{
    CPUState *s = CPU(obj);
    X86CPU *cpu = X86_CPU(s);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);
    CPUX86State *env = &cpu->env;
    target_ulong cr4;
    uint64_t xcr0;
    int i;

    if (xcc->parent_phases.hold) {
        xcc->parent_phases.hold(obj);
    }

    memset(env, 0, offsetof(CPUX86State, end_reset_fields));

    env->old_exception = -1;

    /* init to reset state */
    env->int_ctl = 0;
    env->hflags2 |= HF2_GIF_MASK;
    env->hflags2 |= HF2_VGIF_MASK;
    env->hflags &= ~HF_GUEST_MASK;

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = ~0x0;
    env->smbase = 0x30000;
    env->msr_smi_count = 0;

    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK | (2 << DESC_TYPE_SHIFT);
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, 0xffff0000, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);

    env->eip = 0xfff0;
    env->regs[R_EDX] = env->cpuid_version;

    env->eflags = 0x2;

    /* FPU init */
    for (i = 0; i < 8; i++) {
        env->fptags[i] = 1;
    }
    cpu_set_fpuc(env, 0x37f);

    env->mxcsr = 0x1f80;
    /* All units are in INIT state.  */
    env->xstate_bv = 0;

    env->pat = 0x0007040600070406ULL;

    if (kvm_enabled()) {
        /*
         * KVM handles TSC = 0 specially and thinks we are hot-plugging
         * a new CPU, use 1 instead to force a reset.
         */
        if (env->tsc != 0) {
            env->tsc = 1;
        }
    } else {
        env->tsc = 0;
    }

    env->msr_ia32_misc_enable = MSR_IA32_MISC_ENABLE_DEFAULT;
    if (env->features[FEAT_1_ECX] & CPUID_EXT_MONITOR) {
        env->msr_ia32_misc_enable |= MSR_IA32_MISC_ENABLE_MWAIT;
    }

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(s, BP_CPU);
    cpu_watchpoint_remove_all(s, BP_CPU);

    cr4 = 0;
    xcr0 = XSTATE_FP_MASK;

#ifdef CONFIG_USER_ONLY
    /* Enable all the features for user-mode.  */
    if (env->features[FEAT_1_EDX] & CPUID_SSE) {
        xcr0 |= XSTATE_SSE_MASK;
    }
    for (i = 2; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if (!((1 << i) & CPUID_XSTATE_XCR0_MASK)) {
            continue;
        }
        if (env->features[esa->feature] & esa->bits) {
            xcr0 |= 1ull << i;
        }
    }

    if (env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE) {
        cr4 |= CR4_OSFXSR_MASK | CR4_OSXSAVE_MASK;
    }
    if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_FSGSBASE) {
        cr4 |= CR4_FSGSBASE_MASK;
    }
#endif

    env->xcr0 = xcr0;
    cpu_x86_update_cr4(env, cr4);

    /*
     * SDM 11.11.5 requires:
     *  - IA32_MTRR_DEF_TYPE MSR.E = 0
     *  - IA32_MTRR_PHYSMASKn.V = 0
     * All other bits are undefined.  For simplification, zero it all.
     */
    env->mtrr_deftype = 0;
    memset(env->mtrr_var, 0, sizeof(env->mtrr_var));
    memset(env->mtrr_fixed, 0, sizeof(env->mtrr_fixed));

    env->interrupt_injected = -1;
    env->exception_nr = -1;
    env->exception_pending = 0;
    env->exception_injected = 0;
    env->exception_has_payload = false;
    env->exception_payload = 0;
    env->nmi_injected = false;
    env->triple_fault_pending = false;
#if !defined(CONFIG_USER_ONLY)
    /* We hard-wire the BSP to the first CPU. */
    apic_designate_bsp(cpu->apic_state, s->cpu_index == 0);

    s->halted = !cpu_is_bsp(cpu);

    if (kvm_enabled()) {
        kvm_arch_reset_vcpu(cpu);
    }

    x86_cpu_set_sgxlepubkeyhash(env);

    env->amd_tsc_scale_msr =  MSR_AMD64_TSC_RATIO_DEFAULT;

#endif
}

void x86_cpu_after_reset(X86CPU *cpu)
{
#ifndef CONFIG_USER_ONLY
    if (kvm_enabled()) {
        kvm_arch_after_reset_vcpu(cpu);
    }

    if (cpu->apic_state) {
        device_cold_reset(cpu->apic_state);
    }
#endif
}

static void mce_init(X86CPU *cpu)
{
    CPUX86State *cenv = &cpu->env;
    unsigned int bank;

    if (((cenv->cpuid_version >> 8) & 0xf) >= 6
        && (cenv->features[FEAT_1_EDX] & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF |
                        (cpu->enable_lmce ? MCG_LMCE_P : 0);
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

static void x86_cpu_adjust_level(X86CPU *cpu, uint32_t *min, uint32_t value)
{
    if (*min < value) {
        *min = value;
    }
}

/* Increase cpuid_min_{level,xlevel,xlevel2} automatically, if appropriate */
static void x86_cpu_adjust_feat_level(X86CPU *cpu, FeatureWord w)
{
    CPUX86State *env = &cpu->env;
    FeatureWordInfo *fi = &feature_word_info[w];
    uint32_t eax = fi->cpuid.eax;
    uint32_t region = eax & 0xF0000000;

    assert(feature_word_info[w].type == CPUID_FEATURE_WORD);
    if (!env->features[w]) {
        return;
    }

    switch (region) {
    case 0x00000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_level, eax);
    break;
    case 0x80000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, eax);
    break;
    case 0xC0000000:
        x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel2, eax);
    break;
    }

    if (eax == 7) {
        x86_cpu_adjust_level(cpu, &env->cpuid_min_level_func7,
                             fi->cpuid.ecx);
    }
}

/* Calculate XSAVE components based on the configured CPU feature flags */
static void x86_cpu_enable_xsave_components(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;
    int i;
    uint64_t mask;
    static bool request_perm;

    if (!(env->features[FEAT_1_ECX] & CPUID_EXT_XSAVE)) {
        env->features[FEAT_XSAVE_XCR0_LO] = 0;
        env->features[FEAT_XSAVE_XCR0_HI] = 0;
        return;
    }

    mask = 0;
    for (i = 0; i < ARRAY_SIZE(x86_ext_save_areas); i++) {
        const ExtSaveArea *esa = &x86_ext_save_areas[i];
        if (env->features[esa->feature] & esa->bits) {
            mask |= (1ULL << i);
        }
    }

    /* Only request permission for first vcpu */
    if (kvm_enabled() && !request_perm) {
        kvm_request_xsave_components(cpu, mask);
        request_perm = true;
    }

    env->features[FEAT_XSAVE_XCR0_LO] = mask & CPUID_XSTATE_XCR0_MASK;
    env->features[FEAT_XSAVE_XCR0_HI] = mask >> 32;
    env->features[FEAT_XSAVE_XSS_LO] = mask & CPUID_XSTATE_XSS_MASK;
    env->features[FEAT_XSAVE_XSS_HI] = mask >> 32;
}

/***** Steps involved on loading and filtering CPUID data
 *
 * When initializing and realizing a CPU object, the steps
 * involved in setting up CPUID data are:
 *
 * 1) Loading CPU model definition (X86CPUDefinition). This is
 *    implemented by x86_cpu_load_model() and should be completely
 *    transparent, as it is done automatically by instance_init.
 *    No code should need to look at X86CPUDefinition structs
 *    outside instance_init.
 *
 * 2) CPU expansion. This is done by realize before CPUID
 *    filtering, and will make sure host/accelerator data is
 *    loaded for CPU models that depend on host capabilities
 *    (e.g. "host"). Done by x86_cpu_expand_features().
 *
 * 3) CPUID filtering. This initializes extra data related to
 *    CPUID, and checks if the host supports all capabilities
 *    required by the CPU. Runnability of a CPU model is
 *    determined at this step. Done by x86_cpu_filter_features().
 *
 * Some operations don't require all steps to be performed.
 * More precisely:
 *
 * - CPU instance creation (instance_init) will run only CPU
 *   model loading. CPU expansion can't run at instance_init-time
 *   because host/accelerator data may be not available yet.
 * - CPU realization will perform both CPU model expansion and CPUID
 *   filtering, and return an error in case one of them fails.
 * - query-cpu-definitions needs to run all 3 steps. It needs
 *   to run CPUID filtering, as the 'unavailable-features'
 *   field is set based on the filtering results.
 * - The query-cpu-model-expansion QMP command only needs to run
 *   CPU model loading and CPU expansion. It should not filter
 *   any CPUID data based on host capabilities.
 */

/* Expand CPU configuration data, based on configured features
 * and host/accelerator capabilities when appropriate.
 */
void x86_cpu_expand_features(X86CPU *cpu, Error **errp)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;
    int i;
    GList *l;

    for (l = plus_features; l; l = l->next) {
        const char *prop = l->data;
        if (!object_property_set_bool(OBJECT(cpu), prop, true, errp)) {
            return;
        }
    }

    for (l = minus_features; l; l = l->next) {
        const char *prop = l->data;
        if (!object_property_set_bool(OBJECT(cpu), prop, false, errp)) {
            return;
        }
    }

    /*TODO: Now cpu->max_features doesn't overwrite features
     * set using QOM properties, and we can convert
     * plus_features & minus_features to global properties
     * inside x86_cpu_parse_featurestr() too.
     */
    if (cpu->max_features) {
        for (w = 0; w < FEATURE_WORDS; w++) {
            /* Override only features that weren't set explicitly
             * by the user.
             */
            env->features[w] |=
                x86_cpu_get_supported_feature_word(w, cpu->migratable) &
                ~env->user_features[w] &
                ~feature_word_info[w].no_autoenable_flags;
        }
    }

    for (i = 0; i < ARRAY_SIZE(feature_dependencies); i++) {
        FeatureDep *d = &feature_dependencies[i];
        if (!(env->features[d->from.index] & d->from.mask)) {
            uint64_t unavailable_features = env->features[d->to.index] & d->to.mask;

            /* Not an error unless the dependent feature was added explicitly.  */
            mark_unavailable_features(cpu, d->to.index,
                                      unavailable_features & env->user_features[d->to.index],
                                      "This feature depends on other features that were not requested");

            env->features[d->to.index] &= ~unavailable_features;
        }
    }

    if (!kvm_enabled() || !cpu->expose_kvm) {
        env->features[FEAT_KVM] = 0;
    }

    x86_cpu_enable_xsave_components(cpu);

    /* CPUID[EAX=7,ECX=0].EBX always increased level automatically: */
    x86_cpu_adjust_feat_level(cpu, FEAT_7_0_EBX);
    if (cpu->full_cpuid_auto_level) {
        x86_cpu_adjust_feat_level(cpu, FEAT_1_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_1_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_6_EAX);
        x86_cpu_adjust_feat_level(cpu, FEAT_7_0_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_7_1_EAX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0001_ECX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0007_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_8000_0008_EBX);
        x86_cpu_adjust_feat_level(cpu, FEAT_C000_0001_EDX);
        x86_cpu_adjust_feat_level(cpu, FEAT_SVM);
        x86_cpu_adjust_feat_level(cpu, FEAT_XSAVE);

        /* Intel Processor Trace requires CPUID[0x14] */
        if ((env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT)) {
            if (cpu->intel_pt_auto_level) {
                x86_cpu_adjust_level(cpu, &cpu->env.cpuid_min_level, 0x14);
            } else if (cpu->env.cpuid_min_level < 0x14) {
                mark_unavailable_features(cpu, FEAT_7_0_EBX,
                    CPUID_7_0_EBX_INTEL_PT,
                    "Intel PT need CPUID leaf 0x14, please set by \"-cpu ...,intel-pt=on,min-level=0x14\"");
            }
        }

        /*
         * Intel CPU topology with multi-dies support requires CPUID[0x1F].
         * For AMD Rome/Milan, cpuid level is 0x10, and guest OS should detect
         * extended toplogy by leaf 0xB. Only adjust it for Intel CPU, unless
         * cpu->vendor_cpuid_only has been unset for compatibility with older
         * machine types.
         */
        if ((env->nr_dies > 1) &&
            (IS_INTEL_CPU(env) || !cpu->vendor_cpuid_only)) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_level, 0x1F);
        }

        /* SVM requires CPUID[0x8000000A] */
        if (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_SVM) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, 0x8000000A);
        }

        /* SEV requires CPUID[0x8000001F] */
        if (sev_enabled()) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_xlevel, 0x8000001F);
        }

        /* SGX requires CPUID[0x12] for EPC enumeration */
        if (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SGX) {
            x86_cpu_adjust_level(cpu, &env->cpuid_min_level, 0x12);
        }
    }

    /* Set cpuid_*level* based on cpuid_min_*level, if not explicitly set */
    if (env->cpuid_level_func7 == UINT32_MAX) {
        env->cpuid_level_func7 = env->cpuid_min_level_func7;
    }
    if (env->cpuid_level == UINT32_MAX) {
        env->cpuid_level = env->cpuid_min_level;
    }
    if (env->cpuid_xlevel == UINT32_MAX) {
        env->cpuid_xlevel = env->cpuid_min_xlevel;
    }
    if (env->cpuid_xlevel2 == UINT32_MAX) {
        env->cpuid_xlevel2 = env->cpuid_min_xlevel2;
    }

    if (kvm_enabled()) {
        kvm_hyperv_expand_features(cpu, errp);
    }
}

/*
 * Finishes initialization of CPUID data, filters CPU feature
 * words based on host availability of each feature.
 *
 * Returns: 0 if all flags are supported by the host, non-zero otherwise.
 */
static void x86_cpu_filter_features(X86CPU *cpu, bool verbose)
{
    CPUX86State *env = &cpu->env;
    FeatureWord w;
    const char *prefix = NULL;

    if (verbose) {
        prefix = accel_uses_host_cpuid()
                 ? "host doesn't support requested feature"
                 : "TCG doesn't support requested feature";
    }

    for (w = 0; w < FEATURE_WORDS; w++) {
        uint64_t host_feat =
            x86_cpu_get_supported_feature_word(w, false);
        uint64_t requested_features = env->features[w];
        uint64_t unavailable_features = requested_features & ~host_feat;
        mark_unavailable_features(cpu, w, unavailable_features, prefix);
    }

    if ((env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_INTEL_PT) &&
        kvm_enabled()) {
        KVMState *s = CPU(cpu)->kvm_state;
        uint32_t eax_0 = kvm_arch_get_supported_cpuid(s, 0x14, 0, R_EAX);
        uint32_t ebx_0 = kvm_arch_get_supported_cpuid(s, 0x14, 0, R_EBX);
        uint32_t ecx_0 = kvm_arch_get_supported_cpuid(s, 0x14, 0, R_ECX);
        uint32_t eax_1 = kvm_arch_get_supported_cpuid(s, 0x14, 1, R_EAX);
        uint32_t ebx_1 = kvm_arch_get_supported_cpuid(s, 0x14, 1, R_EBX);

        if (!eax_0 ||
           ((ebx_0 & INTEL_PT_MINIMAL_EBX) != INTEL_PT_MINIMAL_EBX) ||
           ((ecx_0 & INTEL_PT_MINIMAL_ECX) != INTEL_PT_MINIMAL_ECX) ||
           ((eax_1 & INTEL_PT_MTC_BITMAP) != INTEL_PT_MTC_BITMAP) ||
           ((eax_1 & INTEL_PT_ADDR_RANGES_NUM_MASK) <
                                           INTEL_PT_ADDR_RANGES_NUM) ||
           ((ebx_1 & (INTEL_PT_PSB_BITMAP | INTEL_PT_CYCLE_BITMAP)) !=
                (INTEL_PT_PSB_BITMAP | INTEL_PT_CYCLE_BITMAP)) ||
           ((ecx_0 & CPUID_14_0_ECX_LIP) !=
                (env->features[FEAT_14_0_ECX] & CPUID_14_0_ECX_LIP))) {
            /*
             * Processor Trace capabilities aren't configurable, so if the
             * host can't emulate the capabilities we report on
             * cpu_x86_cpuid(), intel-pt can't be enabled on the current host.
             */
            mark_unavailable_features(cpu, FEAT_7_0_EBX, CPUID_7_0_EBX_INTEL_PT, prefix);
        }
    }
}

static void x86_cpu_hyperv_realize(X86CPU *cpu)
{
    size_t len;

    /* Hyper-V vendor id */
    if (!cpu->hyperv_vendor) {
        object_property_set_str(OBJECT(cpu), "hv-vendor-id", "Microsoft Hv",
                                &error_abort);
    }
    len = strlen(cpu->hyperv_vendor);
    if (len > 12) {
        warn_report("hv-vendor-id truncated to 12 characters");
        len = 12;
    }
    memset(cpu->hyperv_vendor_id, 0, 12);
    memcpy(cpu->hyperv_vendor_id, cpu->hyperv_vendor, len);

    /* 'Hv#1' interface identification*/
    cpu->hyperv_interface_id[0] = 0x31237648;
    cpu->hyperv_interface_id[1] = 0;
    cpu->hyperv_interface_id[2] = 0;
    cpu->hyperv_interface_id[3] = 0;

    /* Hypervisor implementation limits */
    cpu->hyperv_limits[0] = 64;
    cpu->hyperv_limits[1] = 0;
    cpu->hyperv_limits[2] = 0;
}

static void x86_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);
    CPUX86State *env = &cpu->env;
    Error *local_err = NULL;
    static bool ht_warned;
    unsigned requested_lbr_fmt;

    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        error_setg(errp, "apic-id property was not initialized properly");
        return;
    }

    /*
     * Process Hyper-V enlightenments.
     * Note: this currently has to happen before the expansion of CPU features.
     */
    x86_cpu_hyperv_realize(cpu);

    x86_cpu_expand_features(cpu, &local_err);
    if (local_err) {
        goto out;
    }

    /*
     * Override env->features[FEAT_PERF_CAPABILITIES].LBR_FMT
     * with user-provided setting.
     */
    if (cpu->lbr_fmt != ~PERF_CAP_LBR_FMT) {
        if ((cpu->lbr_fmt & PERF_CAP_LBR_FMT) != cpu->lbr_fmt) {
            error_setg(errp, "invalid lbr-fmt");
            return;
        }
        env->features[FEAT_PERF_CAPABILITIES] &= ~PERF_CAP_LBR_FMT;
        env->features[FEAT_PERF_CAPABILITIES] |= cpu->lbr_fmt;
    }

    /*
     * vPMU LBR is supported when 1) KVM is enabled 2) Option pmu=on and
     * 3)vPMU LBR format matches that of host setting.
     */
    requested_lbr_fmt =
        env->features[FEAT_PERF_CAPABILITIES] & PERF_CAP_LBR_FMT;
    if (requested_lbr_fmt && kvm_enabled()) {
        uint64_t host_perf_cap =
            x86_cpu_get_supported_feature_word(FEAT_PERF_CAPABILITIES, false);
        unsigned host_lbr_fmt = host_perf_cap & PERF_CAP_LBR_FMT;

        if (!cpu->enable_pmu) {
            error_setg(errp, "vPMU: LBR is unsupported without pmu=on");
            return;
        }
        if (requested_lbr_fmt != host_lbr_fmt) {
            error_setg(errp, "vPMU: the lbr-fmt value (0x%x) does not match "
                        "the host value (0x%x).",
                        requested_lbr_fmt, host_lbr_fmt);
            return;
        }
    }

    x86_cpu_filter_features(cpu, cpu->check_cpuid || cpu->enforce_cpuid);

    if (cpu->enforce_cpuid && x86_cpu_have_filtered_features(cpu)) {
        error_setg(&local_err,
                   accel_uses_host_cpuid() ?
                       "Host doesn't support requested features" :
                       "TCG doesn't support requested features");
        goto out;
    }

    /* On AMD CPUs, some CPUID[8000_0001].EDX bits must match the bits on
     * CPUID[1].EDX.
     */
    if (IS_AMD_CPU(env)) {
        env->features[FEAT_8000_0001_EDX] &= ~CPUID_EXT2_AMD_ALIASES;
        env->features[FEAT_8000_0001_EDX] |= (env->features[FEAT_1_EDX]
           & CPUID_EXT2_AMD_ALIASES);
    }

    x86_cpu_set_sgxlepubkeyhash(env);

    /*
     * note: the call to the framework needs to happen after feature expansion,
     * but before the checks/modifications to ucode_rev, mwait, phys_bits.
     * These may be set by the accel-specific code,
     * and the results are subsequently checked / assumed in this function.
     */
    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (xcc->host_cpuid_required && !accel_uses_host_cpuid()) {
        g_autofree char *name = x86_cpu_class_get_model_name(xcc);
        error_setg(&local_err, "CPU model '%s' requires KVM or HVF", name);
        goto out;
    }

    if (cpu->ucode_rev == 0) {
        /*
         * The default is the same as KVM's. Note that this check
         * needs to happen after the evenual setting of ucode_rev in
         * accel-specific code in cpu_exec_realizefn.
         */
        if (IS_AMD_CPU(env)) {
            cpu->ucode_rev = 0x01000065;
        } else {
            cpu->ucode_rev = 0x100000000ULL;
        }
    }

    /*
     * mwait extended info: needed for Core compatibility
     * We always wake on interrupt even if host does not have the capability.
     *
     * requires the accel-specific code in cpu_exec_realizefn to
     * have already acquired the CPUID data into cpu->mwait.
     */
    cpu->mwait.ecx |= CPUID_MWAIT_EMX | CPUID_MWAIT_IBE;

    /* For 64bit systems think about the number of physical bits to present.
     * ideally this should be the same as the host; anything other than matching
     * the host can cause incorrect guest behaviour.
     * QEMU used to pick the magic value of 40 bits that corresponds to
     * consumer AMD devices but nothing else.
     *
     * Note that this code assumes features expansion has already been done
     * (as it checks for CPUID_EXT2_LM), and also assumes that potential
     * phys_bits adjustments to match the host have been already done in
     * accel-specific code in cpu_exec_realizefn.
     */
    if (env->features[FEAT_8000_0001_EDX] & CPUID_EXT2_LM) {
        if (cpu->phys_bits &&
            (cpu->phys_bits > TARGET_PHYS_ADDR_SPACE_BITS ||
            cpu->phys_bits < 32)) {
            error_setg(errp, "phys-bits should be between 32 and %u "
                             " (but is %u)",
                             TARGET_PHYS_ADDR_SPACE_BITS, cpu->phys_bits);
            return;
        }
        /*
         * 0 means it was not explicitly set by the user (or by machine
         * compat_props or by the host code in host-cpu.c).
         * In this case, the default is the value used by TCG (40).
         */
        if (cpu->phys_bits == 0) {
            cpu->phys_bits = TCG_PHYS_ADDR_BITS;
        }
    } else {
        /* For 32 bit systems don't use the user set value, but keep
         * phys_bits consistent with what we tell the guest.
         */
        if (cpu->phys_bits != 0) {
            error_setg(errp, "phys-bits is not user-configurable in 32 bit");
            return;
        }

        if (env->features[FEAT_1_EDX] & CPUID_PSE36) {
            cpu->phys_bits = 36;
        } else {
            cpu->phys_bits = 32;
        }
    }

    /* Cache information initialization */
    if (!cpu->legacy_cache) {
        if (!xcc->model || !xcc->model->cpudef->cache_info) {
            g_autofree char *name = x86_cpu_class_get_model_name(xcc);
            error_setg(errp,
                       "CPU model '%s' doesn't support legacy-cache=off", name);
            return;
        }
        env->cache_info_cpuid2 = env->cache_info_cpuid4 = env->cache_info_amd =
            *xcc->model->cpudef->cache_info;
    } else {
        /* Build legacy cache information */
        env->cache_info_cpuid2.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid2.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid2.l2_cache = &legacy_l2_cache_cpuid2;
        env->cache_info_cpuid2.l3_cache = &legacy_l3_cache;

        env->cache_info_cpuid4.l1d_cache = &legacy_l1d_cache;
        env->cache_info_cpuid4.l1i_cache = &legacy_l1i_cache;
        env->cache_info_cpuid4.l2_cache = &legacy_l2_cache;
        env->cache_info_cpuid4.l3_cache = &legacy_l3_cache;

        env->cache_info_amd.l1d_cache = &legacy_l1d_cache_amd;
        env->cache_info_amd.l1i_cache = &legacy_l1i_cache_amd;
        env->cache_info_amd.l2_cache = &legacy_l2_cache_amd;
        env->cache_info_amd.l3_cache = &legacy_l3_cache;
    }

#ifndef CONFIG_USER_ONLY
    MachineState *ms = MACHINE(qdev_get_machine());
    qemu_register_reset(x86_cpu_machine_reset_cb, cpu);

    if (cpu->env.features[FEAT_1_EDX] & CPUID_APIC || ms->smp.cpus > 1) {
        x86_cpu_apic_create(cpu, &local_err);
        if (local_err != NULL) {
            goto out;
        }
    }
#endif

    mce_init(cpu);

    qemu_init_vcpu(cs);

    /*
     * Most Intel and certain AMD CPUs support hyperthreading. Even though QEMU
     * fixes this issue by adjusting CPUID_0000_0001_EBX and CPUID_8000_0008_ECX
     * based on inputs (sockets,cores,threads), it is still better to give
     * users a warning.
     *
     * NOTE: the following code has to follow qemu_init_vcpu(). Otherwise
     * cs->nr_threads hasn't be populated yet and the checking is incorrect.
     */
    if (IS_AMD_CPU(env) &&
        !(env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_TOPOEXT) &&
        cs->nr_threads > 1 && !ht_warned) {
            warn_report("This family of AMD CPU doesn't support "
                        "hyperthreading(%d)",
                        cs->nr_threads);
            error_printf("Please configure -smp options properly"
                         " or try enabling topoext feature.\n");
            ht_warned = true;
    }

#ifndef CONFIG_USER_ONLY
    x86_cpu_apic_realize(cpu, &local_err);
    if (local_err != NULL) {
        goto out;
    }
#endif /* !CONFIG_USER_ONLY */
    cpu_reset(cs);

    xcc->parent_realize(dev, &local_err);

out:
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }
}

static void x86_cpu_unrealizefn(DeviceState *dev)
{
    X86CPU *cpu = X86_CPU(dev);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(dev);

#ifndef CONFIG_USER_ONLY
    cpu_remove_sync(CPU(dev));
    qemu_unregister_reset(x86_cpu_machine_reset_cb, dev);
#endif

    if (cpu->apic_state) {
        object_unparent(OBJECT(cpu->apic_state));
        cpu->apic_state = NULL;
    }

    xcc->parent_unrealize(dev);
}

typedef struct BitProperty {
    FeatureWord w;
    uint64_t mask;
} BitProperty;

static void x86_cpu_get_bit_prop(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    X86CPU *cpu = X86_CPU(obj);
    BitProperty *fp = opaque;
    uint64_t f = cpu->env.features[fp->w];
    bool value = (f & fp->mask) == fp->mask;
    visit_type_bool(v, name, &value, errp);
}

static void x86_cpu_set_bit_prop(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    X86CPU *cpu = X86_CPU(obj);
    BitProperty *fp = opaque;
    bool value;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }

    if (value) {
        cpu->env.features[fp->w] |= fp->mask;
    } else {
        cpu->env.features[fp->w] &= ~fp->mask;
    }
    cpu->env.user_features[fp->w] |= fp->mask;
}

/* Register a boolean property to get/set a single bit in a uint32_t field.
 *
 * The same property name can be registered multiple times to make it affect
 * multiple bits in the same FeatureWord. In that case, the getter will return
 * true only if all bits are set.
 */
static void x86_cpu_register_bit_prop(X86CPUClass *xcc,
                                      const char *prop_name,
                                      FeatureWord w,
                                      int bitnr)
{
    ObjectClass *oc = OBJECT_CLASS(xcc);
    BitProperty *fp;
    ObjectProperty *op;
    uint64_t mask = (1ULL << bitnr);

    op = object_class_property_find(oc, prop_name);
    if (op) {
        fp = op->opaque;
        assert(fp->w == w);
        fp->mask |= mask;
    } else {
        fp = g_new0(BitProperty, 1);
        fp->w = w;
        fp->mask = mask;
        object_class_property_add(oc, prop_name, "bool",
                                  x86_cpu_get_bit_prop,
                                  x86_cpu_set_bit_prop,
                                  NULL, fp);
    }
}

static void x86_cpu_register_feature_bit_props(X86CPUClass *xcc,
                                               FeatureWord w,
                                               int bitnr)
{
    FeatureWordInfo *fi = &feature_word_info[w];
    const char *name = fi->feat_names[bitnr];

    if (!name) {
        return;
    }

    /* Property names should use "-" instead of "_".
     * Old names containing underscores are registered as aliases
     * using object_property_add_alias()
     */
    assert(!strchr(name, '_'));
    /* aliases don't use "|" delimiters anymore, they are registered
     * manually using object_property_add_alias() */
    assert(!strchr(name, '|'));
    x86_cpu_register_bit_prop(xcc, name, w, bitnr);
}

static void x86_cpu_post_initfn(Object *obj)
{
    accel_cpu_instance_init(CPU(obj));
}

static void x86_cpu_initfn(Object *obj)
{
    X86CPU *cpu = X86_CPU(obj);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(obj);
    CPUX86State *env = &cpu->env;

    env->nr_dies = 1;
    cpu_set_cpustate_pointers(cpu);

    object_property_add(obj, "feature-words", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)env->features);
    object_property_add(obj, "filtered-features", "X86CPUFeatureWordInfo",
                        x86_cpu_get_feature_words,
                        NULL, NULL, (void *)cpu->filtered_features);

    object_property_add_alias(obj, "sse3", obj, "pni");
    object_property_add_alias(obj, "pclmuldq", obj, "pclmulqdq");
    object_property_add_alias(obj, "sse4-1", obj, "sse4.1");
    object_property_add_alias(obj, "sse4-2", obj, "sse4.2");
    object_property_add_alias(obj, "xd", obj, "nx");
    object_property_add_alias(obj, "ffxsr", obj, "fxsr-opt");
    object_property_add_alias(obj, "i64", obj, "lm");

    object_property_add_alias(obj, "ds_cpl", obj, "ds-cpl");
    object_property_add_alias(obj, "tsc_adjust", obj, "tsc-adjust");
    object_property_add_alias(obj, "fxsr_opt", obj, "fxsr-opt");
    object_property_add_alias(obj, "lahf_lm", obj, "lahf-lm");
    object_property_add_alias(obj, "cmp_legacy", obj, "cmp-legacy");
    object_property_add_alias(obj, "nodeid_msr", obj, "nodeid-msr");
    object_property_add_alias(obj, "perfctr_core", obj, "perfctr-core");
    object_property_add_alias(obj, "perfctr_nb", obj, "perfctr-nb");
    object_property_add_alias(obj, "kvm_nopiodelay", obj, "kvm-nopiodelay");
    object_property_add_alias(obj, "kvm_mmu", obj, "kvm-mmu");
    object_property_add_alias(obj, "kvm_asyncpf", obj, "kvm-asyncpf");
    object_property_add_alias(obj, "kvm_asyncpf_int", obj, "kvm-asyncpf-int");
    object_property_add_alias(obj, "kvm_steal_time", obj, "kvm-steal-time");
    object_property_add_alias(obj, "kvm_pv_eoi", obj, "kvm-pv-eoi");
    object_property_add_alias(obj, "kvm_pv_unhalt", obj, "kvm-pv-unhalt");
    object_property_add_alias(obj, "kvm_poll_control", obj, "kvm-poll-control");
    object_property_add_alias(obj, "svm_lock", obj, "svm-lock");
    object_property_add_alias(obj, "nrip_save", obj, "nrip-save");
    object_property_add_alias(obj, "tsc_scale", obj, "tsc-scale");
    object_property_add_alias(obj, "vmcb_clean", obj, "vmcb-clean");
    object_property_add_alias(obj, "pause_filter", obj, "pause-filter");
    object_property_add_alias(obj, "sse4_1", obj, "sse4.1");
    object_property_add_alias(obj, "sse4_2", obj, "sse4.2");

    object_property_add_alias(obj, "hv-apicv", obj, "hv-avic");
    cpu->lbr_fmt = ~PERF_CAP_LBR_FMT;
    object_property_add_alias(obj, "lbr_fmt", obj, "lbr-fmt");

    if (xcc->model) {
        x86_cpu_load_model(cpu, xcc->model);
    }
}

static int64_t x86_cpu_get_arch_id(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->apic_id;
}

#if !defined(CONFIG_USER_ONLY)
static bool x86_cpu_get_paging_enabled(const CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    return cpu->env.cr[0] & CR0_PG_MASK;
}
#endif /* !CONFIG_USER_ONLY */

static void x86_cpu_set_pc(CPUState *cs, vaddr value)
{
    X86CPU *cpu = X86_CPU(cs);

    cpu->env.eip = value;
}

static vaddr x86_cpu_get_pc(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    /* Match cpu_get_tb_cpu_state. */
    return cpu->env.eip + cpu->env.segs[R_CS].base;
}

int x86_cpu_pending_interrupt(CPUState *cs, int interrupt_request)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

#if !defined(CONFIG_USER_ONLY)
    if (interrupt_request & CPU_INTERRUPT_POLL) {
        return CPU_INTERRUPT_POLL;
    }
#endif
    if (interrupt_request & CPU_INTERRUPT_SIPI) {
        return CPU_INTERRUPT_SIPI;
    }

    if (env->hflags2 & HF2_GIF_MASK) {
        if ((interrupt_request & CPU_INTERRUPT_SMI) &&
            !(env->hflags & HF_SMM_MASK)) {
            return CPU_INTERRUPT_SMI;
        } else if ((interrupt_request & CPU_INTERRUPT_NMI) &&
                   !(env->hflags2 & HF2_NMI_MASK)) {
            return CPU_INTERRUPT_NMI;
        } else if (interrupt_request & CPU_INTERRUPT_MCE) {
            return CPU_INTERRUPT_MCE;
        } else if ((interrupt_request & CPU_INTERRUPT_HARD) &&
                   (((env->hflags2 & HF2_VINTR_MASK) &&
                     (env->hflags2 & HF2_HIF_MASK)) ||
                    (!(env->hflags2 & HF2_VINTR_MASK) &&
                     (env->eflags & IF_MASK &&
                      !(env->hflags & HF_INHIBIT_IRQ_MASK))))) {
            return CPU_INTERRUPT_HARD;
#if !defined(CONFIG_USER_ONLY)
        } else if (env->hflags2 & HF2_VGIF_MASK) {
            if((interrupt_request & CPU_INTERRUPT_VIRQ) &&
                   (env->eflags & IF_MASK) &&
                   !(env->hflags & HF_INHIBIT_IRQ_MASK)) {
                        return CPU_INTERRUPT_VIRQ;
            }
#endif
        }
    }

    return 0;
}

static bool x86_cpu_has_work(CPUState *cs)
{
    return x86_cpu_pending_interrupt(cs, cs->interrupt_request) != 0;
}

static void x86_disas_set_info(CPUState *cs, disassemble_info *info)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    info->mach = (env->hflags & HF_CS64_MASK ? bfd_mach_x86_64
                  : env->hflags & HF_CS32_MASK ? bfd_mach_i386_i386
                  : bfd_mach_i386_i8086);

    info->cap_arch = CS_ARCH_X86;
    info->cap_mode = (env->hflags & HF_CS64_MASK ? CS_MODE_64
                      : env->hflags & HF_CS32_MASK ? CS_MODE_32
                      : CS_MODE_16);
    info->cap_insn_unit = 1;
    info->cap_insn_split = 8;
}

void x86_update_hflags(CPUX86State *env)
{
   uint32_t hflags;
#define HFLAG_COPY_MASK \
    ~( HF_CPL_MASK | HF_PE_MASK | HF_MP_MASK | HF_EM_MASK | \
       HF_TS_MASK | HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK | \
       HF_OSFXSR_MASK | HF_LMA_MASK | HF_CS32_MASK | \
       HF_SS32_MASK | HF_CS64_MASK | HF_ADDSEG_MASK)

    hflags = env->hflags & HFLAG_COPY_MASK;
    hflags |= (env->segs[R_SS].flags >> DESC_DPL_SHIFT) & HF_CPL_MASK;
    hflags |= (env->cr[0] & CR0_PE_MASK) << (HF_PE_SHIFT - CR0_PE_SHIFT);
    hflags |= (env->cr[0] << (HF_MP_SHIFT - CR0_MP_SHIFT)) &
                (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK);
    hflags |= (env->eflags & (HF_TF_MASK | HF_VM_MASK | HF_IOPL_MASK));

    if (env->cr[4] & CR4_OSFXSR_MASK) {
        hflags |= HF_OSFXSR_MASK;
    }

    if (env->efer & MSR_EFER_LMA) {
        hflags |= HF_LMA_MASK;
    }

    if ((hflags & HF_LMA_MASK) && (env->segs[R_CS].flags & DESC_L_MASK)) {
        hflags |= HF_CS32_MASK | HF_SS32_MASK | HF_CS64_MASK;
    } else {
        hflags |= (env->segs[R_CS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_CS32_SHIFT);
        hflags |= (env->segs[R_SS].flags & DESC_B_MASK) >>
                    (DESC_B_SHIFT - HF_SS32_SHIFT);
        if (!(env->cr[0] & CR0_PE_MASK) || (env->eflags & VM_MASK) ||
            !(hflags & HF_CS32_MASK)) {
            hflags |= HF_ADDSEG_MASK;
        } else {
            hflags |= ((env->segs[R_DS].base | env->segs[R_ES].base |
                        env->segs[R_SS].base) != 0) << HF_ADDSEG_SHIFT;
        }
    }
    env->hflags = hflags;
}

static Property x86_cpu_properties[] = {
#ifdef CONFIG_USER_ONLY
    /* apic_id = 0 by default for *-user, see commit 9886e834 */
    DEFINE_PROP_UINT32("apic-id", X86CPU, apic_id, 0),
    DEFINE_PROP_INT32("thread-id", X86CPU, thread_id, 0),
    DEFINE_PROP_INT32("core-id", X86CPU, core_id, 0),
    DEFINE_PROP_INT32("die-id", X86CPU, die_id, 0),
    DEFINE_PROP_INT32("socket-id", X86CPU, socket_id, 0),
#else
    DEFINE_PROP_UINT32("apic-id", X86CPU, apic_id, UNASSIGNED_APIC_ID),
    DEFINE_PROP_INT32("thread-id", X86CPU, thread_id, -1),
    DEFINE_PROP_INT32("core-id", X86CPU, core_id, -1),
    DEFINE_PROP_INT32("die-id", X86CPU, die_id, -1),
    DEFINE_PROP_INT32("socket-id", X86CPU, socket_id, -1),
#endif
    DEFINE_PROP_INT32("node-id", X86CPU, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_BOOL("pmu", X86CPU, enable_pmu, false),
    DEFINE_PROP_UINT64_CHECKMASK("lbr-fmt", X86CPU, lbr_fmt, PERF_CAP_LBR_FMT),

    DEFINE_PROP_UINT32("hv-spinlocks", X86CPU, hyperv_spinlock_attempts,
                       HYPERV_SPINLOCK_NEVER_NOTIFY),
    DEFINE_PROP_BIT64("hv-relaxed", X86CPU, hyperv_features,
                      HYPERV_FEAT_RELAXED, 0),
    DEFINE_PROP_BIT64("hv-vapic", X86CPU, hyperv_features,
                      HYPERV_FEAT_VAPIC, 0),
    DEFINE_PROP_BIT64("hv-time", X86CPU, hyperv_features,
                      HYPERV_FEAT_TIME, 0),
    DEFINE_PROP_BIT64("hv-crash", X86CPU, hyperv_features,
                      HYPERV_FEAT_CRASH, 0),
    DEFINE_PROP_BIT64("hv-reset", X86CPU, hyperv_features,
                      HYPERV_FEAT_RESET, 0),
    DEFINE_PROP_BIT64("hv-vpindex", X86CPU, hyperv_features,
                      HYPERV_FEAT_VPINDEX, 0),
    DEFINE_PROP_BIT64("hv-runtime", X86CPU, hyperv_features,
                      HYPERV_FEAT_RUNTIME, 0),
    DEFINE_PROP_BIT64("hv-synic", X86CPU, hyperv_features,
                      HYPERV_FEAT_SYNIC, 0),
    DEFINE_PROP_BIT64("hv-stimer", X86CPU, hyperv_features,
                      HYPERV_FEAT_STIMER, 0),
    DEFINE_PROP_BIT64("hv-frequencies", X86CPU, hyperv_features,
                      HYPERV_FEAT_FREQUENCIES, 0),
    DEFINE_PROP_BIT64("hv-reenlightenment", X86CPU, hyperv_features,
                      HYPERV_FEAT_REENLIGHTENMENT, 0),
    DEFINE_PROP_BIT64("hv-tlbflush", X86CPU, hyperv_features,
                      HYPERV_FEAT_TLBFLUSH, 0),
    DEFINE_PROP_BIT64("hv-evmcs", X86CPU, hyperv_features,
                      HYPERV_FEAT_EVMCS, 0),
    DEFINE_PROP_BIT64("hv-ipi", X86CPU, hyperv_features,
                      HYPERV_FEAT_IPI, 0),
    DEFINE_PROP_BIT64("hv-stimer-direct", X86CPU, hyperv_features,
                      HYPERV_FEAT_STIMER_DIRECT, 0),
    DEFINE_PROP_BIT64("hv-avic", X86CPU, hyperv_features,
                      HYPERV_FEAT_AVIC, 0),
    DEFINE_PROP_BIT64("hv-emsr-bitmap", X86CPU, hyperv_features,
                      HYPERV_FEAT_MSR_BITMAP, 0),
    DEFINE_PROP_BIT64("hv-xmm-input", X86CPU, hyperv_features,
                      HYPERV_FEAT_XMM_INPUT, 0),
    DEFINE_PROP_BIT64("hv-tlbflush-ext", X86CPU, hyperv_features,
                      HYPERV_FEAT_TLBFLUSH_EXT, 0),
    DEFINE_PROP_BIT64("hv-tlbflush-direct", X86CPU, hyperv_features,
                      HYPERV_FEAT_TLBFLUSH_DIRECT, 0),
    DEFINE_PROP_ON_OFF_AUTO("hv-no-nonarch-coresharing", X86CPU,
                            hyperv_no_nonarch_cs, ON_OFF_AUTO_OFF),
    DEFINE_PROP_BIT64("hv-syndbg", X86CPU, hyperv_features,
                      HYPERV_FEAT_SYNDBG, 0),
    DEFINE_PROP_BOOL("hv-passthrough", X86CPU, hyperv_passthrough, false),
    DEFINE_PROP_BOOL("hv-enforce-cpuid", X86CPU, hyperv_enforce_cpuid, false),

    /* WS2008R2 identify by default */
    DEFINE_PROP_UINT32("hv-version-id-build", X86CPU, hyperv_ver_id_build,
                       0x3839),
    DEFINE_PROP_UINT16("hv-version-id-major", X86CPU, hyperv_ver_id_major,
                       0x000A),
    DEFINE_PROP_UINT16("hv-version-id-minor", X86CPU, hyperv_ver_id_minor,
                       0x0000),
    DEFINE_PROP_UINT32("hv-version-id-spack", X86CPU, hyperv_ver_id_sp, 0),
    DEFINE_PROP_UINT8("hv-version-id-sbranch", X86CPU, hyperv_ver_id_sb, 0),
    DEFINE_PROP_UINT32("hv-version-id-snumber", X86CPU, hyperv_ver_id_sn, 0),

    DEFINE_PROP_BOOL("check", X86CPU, check_cpuid, true),
    DEFINE_PROP_BOOL("enforce", X86CPU, enforce_cpuid, false),
    DEFINE_PROP_BOOL("x-force-features", X86CPU, force_features, false),
    DEFINE_PROP_BOOL("kvm", X86CPU, expose_kvm, true),
    DEFINE_PROP_UINT32("phys-bits", X86CPU, phys_bits, 0),
    DEFINE_PROP_BOOL("host-phys-bits", X86CPU, host_phys_bits, false),
    DEFINE_PROP_UINT8("host-phys-bits-limit", X86CPU, host_phys_bits_limit, 0),
    DEFINE_PROP_BOOL("fill-mtrr-mask", X86CPU, fill_mtrr_mask, true),
    DEFINE_PROP_UINT32("level-func7", X86CPU, env.cpuid_level_func7,
                       UINT32_MAX),
    DEFINE_PROP_UINT32("level", X86CPU, env.cpuid_level, UINT32_MAX),
    DEFINE_PROP_UINT32("xlevel", X86CPU, env.cpuid_xlevel, UINT32_MAX),
    DEFINE_PROP_UINT32("xlevel2", X86CPU, env.cpuid_xlevel2, UINT32_MAX),
    DEFINE_PROP_UINT32("min-level", X86CPU, env.cpuid_min_level, 0),
    DEFINE_PROP_UINT32("min-xlevel", X86CPU, env.cpuid_min_xlevel, 0),
    DEFINE_PROP_UINT32("min-xlevel2", X86CPU, env.cpuid_min_xlevel2, 0),
    DEFINE_PROP_UINT64("ucode-rev", X86CPU, ucode_rev, 0),
    DEFINE_PROP_BOOL("full-cpuid-auto-level", X86CPU, full_cpuid_auto_level, true),
    DEFINE_PROP_STRING("hv-vendor-id", X86CPU, hyperv_vendor),
    DEFINE_PROP_BOOL("cpuid-0xb", X86CPU, enable_cpuid_0xb, true),
    DEFINE_PROP_BOOL("x-vendor-cpuid-only", X86CPU, vendor_cpuid_only, true),
    DEFINE_PROP_BOOL("lmce", X86CPU, enable_lmce, false),
    DEFINE_PROP_BOOL("l3-cache", X86CPU, enable_l3_cache, true),
    DEFINE_PROP_BOOL("kvm-no-smi-migration", X86CPU, kvm_no_smi_migration,
                     false),
    DEFINE_PROP_BOOL("kvm-pv-enforce-cpuid", X86CPU, kvm_pv_enforce_cpuid,
                     false),
    DEFINE_PROP_BOOL("vmware-cpuid-freq", X86CPU, vmware_cpuid_freq, true),
    DEFINE_PROP_BOOL("tcg-cpuid", X86CPU, expose_tcg, true),
    DEFINE_PROP_BOOL("x-migrate-smi-count", X86CPU, migrate_smi_count,
                     true),
    /*
     * lecacy_cache defaults to true unless the CPU model provides its
     * own cache information (see x86_cpu_load_def()).
     */
    DEFINE_PROP_BOOL("legacy-cache", X86CPU, legacy_cache, true),

    /*
     * From "Requirements for Implementing the Microsoft
     * Hypervisor Interface":
     * https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/reference/tlfs
     *
     * "Starting with Windows Server 2012 and Windows 8, if
     * CPUID.40000005.EAX contains a value of -1, Windows assumes that
     * the hypervisor imposes no specific limit to the number of VPs.
     * In this case, Windows Server 2012 guest VMs may use more than
     * 64 VPs, up to the maximum supported number of processors applicable
     * to the specific Windows version being used."
     */
    DEFINE_PROP_INT32("x-hv-max-vps", X86CPU, hv_max_vps, -1),
    DEFINE_PROP_BOOL("x-hv-synic-kvm-only", X86CPU, hyperv_synic_kvm_only,
                     false),
    DEFINE_PROP_BOOL("x-intel-pt-auto-level", X86CPU, intel_pt_auto_level,
                     true),
    DEFINE_PROP_END_OF_LIST()
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps i386_sysemu_ops = {
    .get_memory_mapping = x86_cpu_get_memory_mapping,
    .get_paging_enabled = x86_cpu_get_paging_enabled,
    .get_phys_page_attrs_debug = x86_cpu_get_phys_page_attrs_debug,
    .asidx_from_attrs = x86_asidx_from_attrs,
    .get_crash_info = x86_cpu_get_crash_info,
    .write_elf32_note = x86_cpu_write_elf32_note,
    .write_elf64_note = x86_cpu_write_elf64_note,
    .write_elf32_qemunote = x86_cpu_write_elf32_qemunote,
    .write_elf64_qemunote = x86_cpu_write_elf64_qemunote,
    .legacy_vmsd = &vmstate_x86_cpu,
};
#endif

static void x86_cpu_common_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);
    FeatureWord w;

    device_class_set_parent_realize(dc, x86_cpu_realizefn,
                                    &xcc->parent_realize);
    device_class_set_parent_unrealize(dc, x86_cpu_unrealizefn,
                                      &xcc->parent_unrealize);
    device_class_set_props(dc, x86_cpu_properties);

    resettable_class_set_parent_phases(rc, NULL, x86_cpu_reset_hold, NULL,
                                       &xcc->parent_phases);
    cc->reset_dump_flags = CPU_DUMP_FPU | CPU_DUMP_CCOP;

    cc->class_by_name = x86_cpu_class_by_name;
    cc->parse_features = x86_cpu_parse_featurestr;
    cc->has_work = x86_cpu_has_work;
    cc->dump_state = x86_cpu_dump_state;
    cc->set_pc = x86_cpu_set_pc;
    cc->get_pc = x86_cpu_get_pc;
    cc->gdb_read_register = x86_cpu_gdb_read_register;
    cc->gdb_write_register = x86_cpu_gdb_write_register;
    cc->get_arch_id = x86_cpu_get_arch_id;

#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &i386_sysemu_ops;
#endif /* !CONFIG_USER_ONLY */

    cc->gdb_arch_name = x86_gdb_arch_name;
#ifdef TARGET_X86_64
    cc->gdb_core_xml_file = "i386-64bit.xml";
    cc->gdb_num_core_regs = 66;
#else
    cc->gdb_core_xml_file = "i386-32bit.xml";
    cc->gdb_num_core_regs = 50;
#endif
    cc->disas_set_info = x86_disas_set_info;

    dc->user_creatable = true;

    object_class_property_add(oc, "family", "int",
                              x86_cpuid_version_get_family,
                              x86_cpuid_version_set_family, NULL, NULL);
    object_class_property_add(oc, "model", "int",
                              x86_cpuid_version_get_model,
                              x86_cpuid_version_set_model, NULL, NULL);
    object_class_property_add(oc, "stepping", "int",
                              x86_cpuid_version_get_stepping,
                              x86_cpuid_version_set_stepping, NULL, NULL);
    object_class_property_add_str(oc, "vendor",
                                  x86_cpuid_get_vendor,
                                  x86_cpuid_set_vendor);
    object_class_property_add_str(oc, "model-id",
                                  x86_cpuid_get_model_id,
                                  x86_cpuid_set_model_id);
    object_class_property_add(oc, "tsc-frequency", "int",
                              x86_cpuid_get_tsc_freq,
                              x86_cpuid_set_tsc_freq, NULL, NULL);
    /*
     * The "unavailable-features" property has the same semantics as
     * CpuDefinitionInfo.unavailable-features on the "query-cpu-definitions"
     * QMP command: they list the features that would have prevented the
     * CPU from running if the "enforce" flag was set.
     */
    object_class_property_add(oc, "unavailable-features", "strList",
                              x86_cpu_get_unavailable_features,
                              NULL, NULL, NULL);

#if !defined(CONFIG_USER_ONLY)
    object_class_property_add(oc, "crash-information", "GuestPanicInformation",
                              x86_cpu_get_crash_info_qom, NULL, NULL, NULL);
#endif

    for (w = 0; w < FEATURE_WORDS; w++) {
        int bitnr;
        for (bitnr = 0; bitnr < 64; bitnr++) {
            x86_cpu_register_feature_bit_props(xcc, w, bitnr);
        }
    }
}

static const TypeInfo x86_cpu_type_info = {
    .name = TYPE_X86_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(X86CPU),
    .instance_init = x86_cpu_initfn,
    .instance_post_init = x86_cpu_post_initfn,

    .abstract = true,
    .class_size = sizeof(X86CPUClass),
    .class_init = x86_cpu_common_class_init,
};

/* "base" CPU model, used by query-cpu-model-expansion */
static void x86_cpu_base_class_init(ObjectClass *oc, void *data)
{
    X86CPUClass *xcc = X86_CPU_CLASS(oc);

    xcc->static_model = true;
    xcc->migration_safe = true;
    xcc->model_description = "base CPU model type with no features enabled";
    xcc->ordering = 8;
}

static const TypeInfo x86_base_cpu_type_info = {
        .name = X86_CPU_TYPE_NAME("base"),
        .parent = TYPE_X86_CPU,
        .class_init = x86_cpu_base_class_init,
};

static void x86_cpu_register_types(void)
{
    int i;

    type_register_static(&x86_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(builtin_x86_defs); i++) {
        x86_register_cpudef_types(&builtin_x86_defs[i]);
    }
    type_register_static(&max_x86_cpu_type_info);
    type_register_static(&x86_base_cpu_type_info);
}

type_init(x86_cpu_register_types)
