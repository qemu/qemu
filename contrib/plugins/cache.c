/*
 * Copyright (C) 2021, Mahmoud Mandour <ma.mandourr@gmail.com>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */

#include <inttypes.h>
#include <stdio.h>
#include <glib.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static enum qemu_plugin_mem_rw rw = QEMU_PLUGIN_MEM_RW;

static GHashTable *miss_ht;

static GMutex hashtable_lock;
static GRand *rng;

static int limit;
static bool sys;

enum EvictionPolicy {
    LRU,
    FIFO,
    RAND,
};

enum EvictionPolicy policy;

/*
 * A CacheSet is a set of cache blocks. A memory block that maps to a set can be
 * put in any of the blocks inside the set. The number of block per set is
 * called the associativity (assoc).
 *
 * Each block contains the the stored tag and a valid bit. Since this is not
 * a functional simulator, the data itself is not stored. We only identify
 * whether a block is in the cache or not by searching for its tag.
 *
 * In order to search for memory data in the cache, the set identifier and tag
 * are extracted from the address and the set is probed to see whether a tag
 * match occur.
 *
 * An address is logically divided into three portions: The block offset,
 * the set number, and the tag.
 *
 * The set number is used to identify the set in which the block may exist.
 * The tag is compared against all the tags of a set to search for a match. If a
 * match is found, then the access is a hit.
 *
 * The CacheSet also contains bookkeaping information about eviction details.
 */

typedef struct {
    uint64_t tag;
    bool valid;
} CacheBlock;

typedef struct {
    CacheBlock *blocks;
    uint64_t *lru_priorities;
    uint64_t lru_gen_counter;
    GQueue *fifo_queue;
} CacheSet;

typedef struct {
    CacheSet *sets;
    int num_sets;
    int cachesize;
    int assoc;
    int blksize_shift;
    uint64_t set_mask;
    uint64_t tag_mask;
    uint64_t accesses;
    uint64_t misses;
} Cache;

typedef struct {
    char *disas_str;
    const char *symbol;
    uint64_t addr;
    uint64_t dmisses;
    uint64_t imisses;
} InsnData;

void (*update_hit)(Cache *cache, int set, int blk);
void (*update_miss)(Cache *cache, int set, int blk);

void (*metadata_init)(Cache *cache);
void (*metadata_destroy)(Cache *cache);

static int cores;
static Cache **dcaches, **icaches;

static GMutex *dcache_locks;
static GMutex *icache_locks;

static uint64_t all_dmem_accesses;
static uint64_t all_imem_accesses;
static uint64_t all_imisses;
static uint64_t all_dmisses;

static int pow_of_two(int num)
{
    g_assert((num & (num - 1)) == 0);
    int ret = 0;
    while (num /= 2) {
        ret++;
    }
    return ret;
}

/*
 * LRU evection policy: For each set, a generation counter is maintained
 * alongside a priority array.
 *
 * On each set access, the generation counter is incremented.
 *
 * On a cache hit: The hit-block is assigned the current generation counter,
 * indicating that it is the most recently used block.
 *
 * On a cache miss: The block with the least priority is searched and replaced
 * with the newly-cached block, of which the priority is set to the current
 * generation number.
 */

static void lru_priorities_init(Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].lru_priorities = g_new0(uint64_t, cache->assoc);
        cache->sets[i].lru_gen_counter = 0;
    }
}

static void lru_update_blk(Cache *cache, int set_idx, int blk_idx)
{
    CacheSet *set = &cache->sets[set_idx];
    set->lru_priorities[blk_idx] = cache->sets[set_idx].lru_gen_counter;
    set->lru_gen_counter++;
}

static int lru_get_lru_block(Cache *cache, int set_idx)
{
    int i, min_idx, min_priority;

    min_priority = cache->sets[set_idx].lru_priorities[0];
    min_idx = 0;

    for (i = 1; i < cache->assoc; i++) {
        if (cache->sets[set_idx].lru_priorities[i] < min_priority) {
            min_priority = cache->sets[set_idx].lru_priorities[i];
            min_idx = i;
        }
    }
    return min_idx;
}

static void lru_priorities_destroy(Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        g_free(cache->sets[i].lru_priorities);
    }
}

/*
 * FIFO eviction policy: a FIFO queue is maintained for each CacheSet that
 * stores accesses to the cache.
 *
 * On a compulsory miss: The block index is enqueued to the fifo_queue to
 * indicate that it's the latest cached block.
 *
 * On a conflict miss: The first-in block is removed from the cache and the new
 * block is put in its place and enqueued to the FIFO queue.
 */

static void fifo_init(Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].fifo_queue = g_queue_new();
    }
}

static int fifo_get_first_block(Cache *cache, int set)
{
    GQueue *q = cache->sets[set].fifo_queue;
    return GPOINTER_TO_INT(g_queue_pop_tail(q));
}

static void fifo_update_on_miss(Cache *cache, int set, int blk_idx)
{
    GQueue *q = cache->sets[set].fifo_queue;
    g_queue_push_head(q, GINT_TO_POINTER(blk_idx));
}

static void fifo_destroy(Cache *cache)
{
    int i;

    for (i = 0; i < cache->num_sets; i++) {
        g_queue_free(cache->sets[i].fifo_queue);
    }
}

static inline uint64_t extract_tag(Cache *cache, uint64_t addr)
{
    return addr & cache->tag_mask;
}

static inline uint64_t extract_set(Cache *cache, uint64_t addr)
{
    return (addr & cache->set_mask) >> cache->blksize_shift;
}

static const char *cache_config_error(int blksize, int assoc, int cachesize)
{
    if (cachesize % blksize != 0) {
        return "cache size must be divisible by block size";
    } else if (cachesize % (blksize * assoc) != 0) {
        return "cache size must be divisible by set size (assoc * block size)";
    } else {
        return NULL;
    }
}

static bool bad_cache_params(int blksize, int assoc, int cachesize)
{
    return (cachesize % blksize) != 0 || (cachesize % (blksize * assoc) != 0);
}

static Cache *cache_init(int blksize, int assoc, int cachesize)
{
    Cache *cache;
    int i;
    uint64_t blk_mask;

    /*
     * This function shall not be called directly, and hence expects suitable
     * parameters.
     */
    g_assert(!bad_cache_params(blksize, assoc, cachesize));

    cache = g_new(Cache, 1);
    cache->assoc = assoc;
    cache->cachesize = cachesize;
    cache->num_sets = cachesize / (blksize * assoc);
    cache->sets = g_new(CacheSet, cache->num_sets);
    cache->blksize_shift = pow_of_two(blksize);
    cache->accesses = 0;
    cache->misses = 0;

    for (i = 0; i < cache->num_sets; i++) {
        cache->sets[i].blocks = g_new0(CacheBlock, assoc);
    }

    blk_mask = blksize - 1;
    cache->set_mask = ((cache->num_sets - 1) << cache->blksize_shift);
    cache->tag_mask = ~(cache->set_mask | blk_mask);

    if (metadata_init) {
        metadata_init(cache);
    }

    return cache;
}

static Cache **caches_init(int blksize, int assoc, int cachesize)
{
    Cache **caches;
    int i;

    if (bad_cache_params(blksize, assoc, cachesize)) {
        return NULL;
    }

    caches = g_new(Cache *, cores);

    for (i = 0; i < cores; i++) {
        caches[i] = cache_init(blksize, assoc, cachesize);
    }

    return caches;
}

static int get_invalid_block(Cache *cache, uint64_t set)
{
    int i;

    for (i = 0; i < cache->assoc; i++) {
        if (!cache->sets[set].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

static int get_replaced_block(Cache *cache, int set)
{
    switch (policy) {
    case RAND:
        return g_rand_int_range(rng, 0, cache->assoc);
    case LRU:
        return lru_get_lru_block(cache, set);
    case FIFO:
        return fifo_get_first_block(cache, set);
    default:
        g_assert_not_reached();
    }
}

static int in_cache(Cache *cache, uint64_t addr)
{
    int i;
    uint64_t tag, set;

    tag = extract_tag(cache, addr);
    set = extract_set(cache, addr);

    for (i = 0; i < cache->assoc; i++) {
        if (cache->sets[set].blocks[i].tag == tag &&
                cache->sets[set].blocks[i].valid) {
            return i;
        }
    }

    return -1;
}

/**
 * access_cache(): Simulate a cache access
 * @cache: The cache under simulation
 * @addr: The address of the requested memory location
 *
 * Returns true if the requsted data is hit in the cache and false when missed.
 * The cache is updated on miss for the next access.
 */
static bool access_cache(Cache *cache, uint64_t addr)
{
    int hit_blk, replaced_blk;
    uint64_t tag, set;

    tag = extract_tag(cache, addr);
    set = extract_set(cache, addr);

    hit_blk = in_cache(cache, addr);
    if (hit_blk != -1) {
        if (update_hit) {
            update_hit(cache, set, hit_blk);
        }
        return true;
    }

    replaced_blk = get_invalid_block(cache, set);

    if (replaced_blk == -1) {
        replaced_blk = get_replaced_block(cache, set);
    }

    if (update_miss) {
        update_miss(cache, set, replaced_blk);
    }

    cache->sets[set].blocks[replaced_blk].tag = tag;
    cache->sets[set].blocks[replaced_blk].valid = true;

    return false;
}

static void vcpu_mem_access(unsigned int vcpu_index, qemu_plugin_meminfo_t info,
                            uint64_t vaddr, void *userdata)
{
    uint64_t effective_addr;
    struct qemu_plugin_hwaddr *hwaddr;
    int cache_idx;
    InsnData *insn;

    hwaddr = qemu_plugin_get_hwaddr(info, vaddr);
    if (hwaddr && qemu_plugin_hwaddr_is_io(hwaddr)) {
        return;
    }

    effective_addr = hwaddr ? qemu_plugin_hwaddr_phys_addr(hwaddr) : vaddr;
    cache_idx = vcpu_index % cores;

    g_mutex_lock(&dcache_locks[cache_idx]);
    if (!access_cache(dcaches[cache_idx], effective_addr)) {
        insn = (InsnData *) userdata;
        __atomic_fetch_add(&insn->dmisses, 1, __ATOMIC_SEQ_CST);
        dcaches[cache_idx]->misses++;
    }
    dcaches[cache_idx]->accesses++;
    g_mutex_unlock(&dcache_locks[cache_idx]);
}

static void vcpu_insn_exec(unsigned int vcpu_index, void *userdata)
{
    uint64_t insn_addr;
    InsnData *insn;
    int cache_idx;

    insn_addr = ((InsnData *) userdata)->addr;

    cache_idx = vcpu_index % cores;
    g_mutex_lock(&icache_locks[cache_idx]);
    if (!access_cache(icaches[cache_idx], insn_addr)) {
        insn = (InsnData *) userdata;
        __atomic_fetch_add(&insn->imisses, 1, __ATOMIC_SEQ_CST);
        icaches[cache_idx]->misses++;
    }
    icaches[cache_idx]->accesses++;
    g_mutex_unlock(&icache_locks[cache_idx]);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n_insns;
    size_t i;
    InsnData *data;

    n_insns = qemu_plugin_tb_n_insns(tb);
    for (i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t effective_addr;

        if (sys) {
            effective_addr = (uint64_t) qemu_plugin_insn_haddr(insn);
        } else {
            effective_addr = (uint64_t) qemu_plugin_insn_vaddr(insn);
        }

        /*
         * Instructions might get translated multiple times, we do not create
         * new entries for those instructions. Instead, we fetch the same
         * entry from the hash table and register it for the callback again.
         */
        g_mutex_lock(&hashtable_lock);
        data = g_hash_table_lookup(miss_ht, GUINT_TO_POINTER(effective_addr));
        if (data == NULL) {
            data = g_new0(InsnData, 1);
            data->disas_str = qemu_plugin_insn_disas(insn);
            data->symbol = qemu_plugin_insn_symbol(insn);
            data->addr = effective_addr;
            g_hash_table_insert(miss_ht, GUINT_TO_POINTER(effective_addr),
                               (gpointer) data);
        }
        g_mutex_unlock(&hashtable_lock);

        qemu_plugin_register_vcpu_mem_cb(insn, vcpu_mem_access,
                                         QEMU_PLUGIN_CB_NO_REGS,
                                         rw, data);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec,
                                               QEMU_PLUGIN_CB_NO_REGS, data);
    }
}

static void insn_free(gpointer data)
{
    InsnData *insn = (InsnData *) data;
    g_free(insn->disas_str);
    g_free(insn);
}

static void cache_free(Cache *cache)
{
    for (int i = 0; i < cache->num_sets; i++) {
        g_free(cache->sets[i].blocks);
    }

    if (metadata_destroy) {
        metadata_destroy(cache);
    }

    g_free(cache->sets);
    g_free(cache);
}

static void caches_free(Cache **caches)
{
    int i;

    for (i = 0; i < cores; i++) {
        cache_free(caches[i]);
    }
}

static int dcmp(gconstpointer a, gconstpointer b)
{
    InsnData *insn_a = (InsnData *) a;
    InsnData *insn_b = (InsnData *) b;

    return insn_a->dmisses < insn_b->dmisses ? 1 : -1;
}

static void append_stats_line(GString *line, uint64_t daccess, uint64_t dmisses,
                              uint64_t iaccess, uint64_t imisses)
{
    double dmiss_rate, imiss_rate;

    dmiss_rate = ((double) dmisses) / (daccess) * 100.0;
    imiss_rate = ((double) imisses) / (iaccess) * 100.0;

    g_string_append_printf(line, "%-14lu %-12lu %9.4lf%%  %-14lu %-12lu"
                           " %9.4lf%%\n",
                           daccess,
                           dmisses,
                           daccess ? dmiss_rate : 0.0,
                           iaccess,
                           imisses,
                           iaccess ? imiss_rate : 0.0);
}

static void sum_stats(void)
{
    int i;

    g_assert(cores > 1);
    for (i = 0; i < cores; i++) {
        all_imisses += icaches[i]->misses;
        all_dmisses += dcaches[i]->misses;
        all_imem_accesses += icaches[i]->accesses;
        all_dmem_accesses += dcaches[i]->accesses;
    }
}

static int icmp(gconstpointer a, gconstpointer b)
{
    InsnData *insn_a = (InsnData *) a;
    InsnData *insn_b = (InsnData *) b;

    return insn_a->imisses < insn_b->imisses ? 1 : -1;
}

static void log_stats(void)
{
    int i;
    Cache *icache, *dcache;

    g_autoptr(GString) rep = g_string_new("core #, data accesses, data misses,"
                                          " dmiss rate, insn accesses,"
                                          " insn misses, imiss rate\n");

    for (i = 0; i < cores; i++) {
        g_string_append_printf(rep, "%-8d", i);
        dcache = dcaches[i];
        icache = icaches[i];
        append_stats_line(rep, dcache->accesses, dcache->misses,
                icache->accesses, icache->misses);
    }

    if (cores > 1) {
        sum_stats();
        g_string_append_printf(rep, "%-8s", "sum");
        append_stats_line(rep, all_dmem_accesses, all_dmisses,
                all_imem_accesses, all_imisses);
    }

    g_string_append(rep, "\n");
    qemu_plugin_outs(rep->str);
}

static void log_top_insns(void)
{
    int i;
    GList *curr, *miss_insns;
    InsnData *insn;

    miss_insns = g_hash_table_get_values(miss_ht);
    miss_insns = g_list_sort(miss_insns, dcmp);
    g_autoptr(GString) rep = g_string_new("");
    g_string_append_printf(rep, "%s", "address, data misses, instruction\n");

    for (curr = miss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        insn = (InsnData *) curr->data;
        g_string_append_printf(rep, "0x%" PRIx64, insn->addr);
        if (insn->symbol) {
            g_string_append_printf(rep, " (%s)", insn->symbol);
        }
        g_string_append_printf(rep, ", %ld, %s\n", insn->dmisses,
                               insn->disas_str);
    }

    miss_insns = g_list_sort(miss_insns, icmp);
    g_string_append_printf(rep, "%s", "\naddress, fetch misses, instruction\n");

    for (curr = miss_insns, i = 0; curr && i < limit; i++, curr = curr->next) {
        insn = (InsnData *) curr->data;
        g_string_append_printf(rep, "0x%" PRIx64, insn->addr);
        if (insn->symbol) {
            g_string_append_printf(rep, " (%s)", insn->symbol);
        }
        g_string_append_printf(rep, ", %ld, %s\n", insn->imisses,
                               insn->disas_str);
    }

    qemu_plugin_outs(rep->str);
    g_list_free(miss_insns);
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    log_stats();
    log_top_insns();

    caches_free(dcaches);
    caches_free(icaches);

    g_hash_table_destroy(miss_ht);
}

static void policy_init(void)
{
    switch (policy) {
    case LRU:
        update_hit = lru_update_blk;
        update_miss = lru_update_blk;
        metadata_init = lru_priorities_init;
        metadata_destroy = lru_priorities_destroy;
        break;
    case FIFO:
        update_miss = fifo_update_on_miss;
        metadata_init = fifo_init;
        metadata_destroy = fifo_destroy;
        break;
    case RAND:
        rng = g_rand_new();
        break;
    default:
        g_assert_not_reached();
    }
}

QEMU_PLUGIN_EXPORT
int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                        int argc, char **argv)
{
    int i;
    int iassoc, iblksize, icachesize;
    int dassoc, dblksize, dcachesize;

    limit = 32;
    sys = info->system_emulation;

    dassoc = 8;
    dblksize = 64;
    dcachesize = dblksize * dassoc * 32;

    iassoc = 8;
    iblksize = 64;
    icachesize = iblksize * iassoc * 32;

    policy = LRU;

    cores = sys ? qemu_plugin_n_vcpus() : 1;

    for (i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_str_has_prefix(opt, "iblksize=")) {
            iblksize = g_ascii_strtoll(opt + 9, NULL, 10);
        } else if (g_str_has_prefix(opt, "iassoc=")) {
            iassoc = g_ascii_strtoll(opt + 7, NULL, 10);
        } else if (g_str_has_prefix(opt, "icachesize=")) {
            icachesize = g_ascii_strtoll(opt + 11, NULL, 10);
        } else if (g_str_has_prefix(opt, "dblksize=")) {
            dblksize = g_ascii_strtoll(opt + 9, NULL, 10);
        } else if (g_str_has_prefix(opt, "dassoc=")) {
            dassoc = g_ascii_strtoll(opt + 7, NULL, 10);
        } else if (g_str_has_prefix(opt, "dcachesize=")) {
            dcachesize = g_ascii_strtoll(opt + 11, NULL, 10);
        } else if (g_str_has_prefix(opt, "limit=")) {
            limit = g_ascii_strtoll(opt + 6, NULL, 10);
        } else if (g_str_has_prefix(opt, "cores=")) {
            cores = g_ascii_strtoll(opt + 6, NULL, 10);
        } else if (g_str_has_prefix(opt, "evict=")) {
            gchar *p = opt + 6;
            if (g_strcmp0(p, "rand") == 0) {
                policy = RAND;
            } else if (g_strcmp0(p, "lru") == 0) {
                policy = LRU;
            } else if (g_strcmp0(p, "fifo") == 0) {
                policy = FIFO;
            } else {
                fprintf(stderr, "invalid eviction policy: %s\n", opt);
                return -1;
            }
        } else {
            fprintf(stderr, "option parsing failed: %s\n", opt);
            return -1;
        }
    }

    policy_init();

    dcaches = caches_init(dblksize, dassoc, dcachesize);
    if (!dcaches) {
        const char *err = cache_config_error(dblksize, dassoc, dcachesize);
        fprintf(stderr, "dcache cannot be constructed from given parameters\n");
        fprintf(stderr, "%s\n", err);
        return -1;
    }

    icaches = caches_init(iblksize, iassoc, icachesize);
    if (!icaches) {
        const char *err = cache_config_error(iblksize, iassoc, icachesize);
        fprintf(stderr, "icache cannot be constructed from given parameters\n");
        fprintf(stderr, "%s\n", err);
        return -1;
    }

    dcache_locks = g_new0(GMutex, cores);
    icache_locks = g_new0(GMutex, cores);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);

    miss_ht = g_hash_table_new_full(NULL, g_direct_equal, NULL, insn_free);

    return 0;
}
