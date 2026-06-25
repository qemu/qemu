/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bitmap.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "qom/qom-qobject.h"
#include "glib.h"
#include "system/memory.h"

#define TYPE_TEST_RAM_DISCARD_SOURCE "test-ram-discard-source"

OBJECT_DECLARE_SIMPLE_TYPE(TestRamDiscardSource, TEST_RAM_DISCARD_SOURCE)

struct TestRamDiscardSource {
    Object parent;

    MemoryRegion *mr;
    uint64_t granularity;
    unsigned long *bitmap;
    uint64_t bitmap_size;
};

static uint64_t test_rds_get_min_granularity(const RamDiscardSource *rds,
                                             const MemoryRegion *mr)
{
    TestRamDiscardSource *src = TEST_RAM_DISCARD_SOURCE(rds);

    g_assert(mr == src->mr);
    return src->granularity;
}

static bool test_rds_is_populated(const RamDiscardSource *rds,
                                  const MemoryRegionSection *section)
{
    TestRamDiscardSource *src = TEST_RAM_DISCARD_SOURCE(rds);
    uint64_t offset = section->offset_within_region;
    uint64_t size = int128_get64(section->size);
    uint64_t first_bit = offset / src->granularity;
    uint64_t last_bit = (offset + size - 1) / src->granularity;
    unsigned long found;

    g_assert(section->mr == src->mr);

    /* Check if any bit in range is zero (discarded) */
    found = find_next_zero_bit(src->bitmap, last_bit + 1, first_bit);
    return found > last_bit;
}

static void test_rds_class_init(ObjectClass *klass, const void *data)
{
    RamDiscardSourceClass *rdsc = RAM_DISCARD_SOURCE_CLASS(klass);

    rdsc->get_min_granularity = test_rds_get_min_granularity;
    rdsc->is_populated = test_rds_is_populated;
}

static const TypeInfo test_rds_info = {
    .name = TYPE_TEST_RAM_DISCARD_SOURCE,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(TestRamDiscardSource),
    .class_init = test_rds_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_RAM_DISCARD_SOURCE },
        { }
    },
};

static TestRamDiscardSource *test_source_new(MemoryRegion *mr,
                                             uint64_t granularity)
{
    TestRamDiscardSource *src;
    uint64_t region_size = memory_region_size(mr);

    src = TEST_RAM_DISCARD_SOURCE(object_new(TYPE_TEST_RAM_DISCARD_SOURCE));
    src->mr = mr;
    src->granularity = granularity;
    src->bitmap_size = DIV_ROUND_UP(region_size, granularity);
    src->bitmap = bitmap_new(src->bitmap_size);

    return src;
}

static void test_source_free(TestRamDiscardSource *src)
{
    g_free(src->bitmap);
    object_unref(OBJECT(src));
}

static void test_source_populate(TestRamDiscardSource *src,
                                 uint64_t offset, uint64_t size)
{
    uint64_t first_bit = offset / src->granularity;
    uint64_t nbits = size / src->granularity;

    bitmap_set(src->bitmap, first_bit, nbits);
}

static void test_source_discard(TestRamDiscardSource *src,
                                uint64_t offset, uint64_t size)
{
    uint64_t first_bit = offset / src->granularity;
    uint64_t nbits = size / src->granularity;

    bitmap_clear(src->bitmap, first_bit, nbits);
}

typedef struct TestListener {
    RamDiscardListener rdl;
    int populate_count;
    int discard_count;
    uint64_t last_populate_offset;
    uint64_t last_populate_size;
    uint64_t last_discard_offset;
    uint64_t last_discard_size;
    int fail_on_populate;  /* Return error on Nth populate */
    int populate_call_num;
} TestListener;

static int test_listener_populate(RamDiscardListener *rdl,
                                  const MemoryRegionSection *section)
{
    TestListener *tl = container_of(rdl, TestListener, rdl);

    tl->populate_call_num++;
    if (tl->fail_on_populate > 0 &&
        tl->populate_call_num >= tl->fail_on_populate) {
        return -ENOMEM;
    }

    tl->populate_count++;
    tl->last_populate_offset = section->offset_within_region;
    tl->last_populate_size = int128_get64(section->size);
    return 0;
}

static void test_listener_discard(RamDiscardListener *rdl,
                                  const MemoryRegionSection *section)
{
    TestListener *tl = container_of(rdl, TestListener, rdl);

    tl->discard_count++;
    tl->last_discard_offset = section->offset_within_region;
    tl->last_discard_size = int128_get64(section->size);
}

static void test_listener_init(TestListener *tl)
{
    ram_discard_listener_init(&tl->rdl,
        test_listener_populate,
        test_listener_discard);
}

#define TEST_REGION_SIZE (16 * 1024 * 1024)  /* 16 MB */
#define GRANULARITY_4K   (4 * 1024)
#define GRANULARITY_2M   (2 * 1024 * 1024)

static MemoryRegion *test_mr;

static void test_setup(void)
{
    test_mr = g_new0(MemoryRegion, 1);
    test_mr->size = int128_make64(TEST_REGION_SIZE);
    test_mr->ram = true;
}

static void test_teardown(void)
{
    g_clear_pointer(&test_mr->rdm, object_unref);
    object_unparent(OBJECT(test_mr));
    g_free(test_mr);
    test_mr = NULL;
}

static void test_single_source_basic(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);
    rdm = memory_region_get_ram_discard_manager(test_mr);
    g_assert_null(rdm);

    /* Add source */
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);
    g_assert_nonnull(rdm);

    g_assert_cmpuint(ram_discard_manager_get_min_granularity(rdm, test_mr),
                     ==, GRANULARITY_4K);

    /* Initially all discarded */
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(GRANULARITY_4K);
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate a range in source */
    test_source_populate(src, 0, GRANULARITY_4K * 4);

    /* Now should be populated */
    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    /* Check larger section */
    section.size = int128_make64(GRANULARITY_4K * 4);
    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    /* Check section that spans populated and discarded */
    section.size = int128_make64(GRANULARITY_4K * 8);
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));

    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    test_source_free(src);
    test_teardown();
}

static void test_single_source_listener(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    /* Populate some ranges before adding listener */
    test_source_populate(src, 0, GRANULARITY_4K * 4);
    test_source_populate(src, GRANULARITY_4K * 8, GRANULARITY_4K * 4);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);
    g_assert_nonnull(rdm);

    /* Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);

    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Should have been notified about populated regions */
    g_assert_cmpint(tl.populate_count, ==, 2);

    /* Notify populate for new range */
    tl.populate_count = 0;
    test_source_populate(src, GRANULARITY_4K * 16, GRANULARITY_4K * 2);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              GRANULARITY_4K * 16,
                                              GRANULARITY_4K * 2);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_offset, ==, GRANULARITY_4K * 16);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 2);

    /* Notify discard */
    tl.discard_count = 0;
    test_source_discard(src, 0, GRANULARITY_4K * 4);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src),
                                       0, GRANULARITY_4K * 4);
    g_assert_cmpint(tl.discard_count, ==, 1);
    g_assert_cmpuint(tl.last_discard_offset, ==, 0);
    g_assert_cmpuint(tl.last_discard_size, ==, GRANULARITY_4K * 4);

    /* Unregister listener */
    ram_discard_manager_unregister_listener(rdm, &tl.rdl);

    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

static void test_two_sources_same_granularity(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /* Add first source */
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);

    /* Add second source */
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);
    g_assert_nonnull(rdm);

    /* Check granularity */
    g_assert_cmpuint(ram_discard_manager_get_min_granularity(rdm, test_mr),
                     ==, GRANULARITY_4K);

    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(GRANULARITY_4K);

    /* Both discarded -> aggregated discarded */
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate in src1 only */
    test_source_populate(src1, 0, GRANULARITY_4K);
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate in src2 only */
    test_source_discard(src1, 0, GRANULARITY_4K);
    test_source_populate(src2, 0, GRANULARITY_4K);
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate in both -> aggregated populated */
    test_source_populate(src1, 0, GRANULARITY_4K);
    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    /* Remove sources */
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Two sources with different granularities (4K and 2M).
 * The aggregated granularity should be GCD(4K, 2M) = 4K.
 */
static void test_two_sources_different_granularity(void)
{
    TestRamDiscardSource *src_4k, *src_2m;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    int ret;

    test_setup();

    src_4k = test_source_new(test_mr, GRANULARITY_4K);
    src_2m = test_source_new(test_mr, GRANULARITY_2M);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src_4k));
    g_assert_cmpint(ret, ==, 0);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src_2m));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    g_assert_cmpuint(ram_discard_manager_get_min_granularity(rdm, test_mr),
                     ==, GRANULARITY_4K);

    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(GRANULARITY_4K);

    /* Both discarded */
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate 4K in src_4k, but src_2m still discarded the whole 2M block */
    test_source_populate(src_4k, 0, GRANULARITY_4K);
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate 2M in src_2m (which includes the 4K block) */
    test_source_populate(src_2m, 0, GRANULARITY_2M);
    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    /* Check a 4K block at offset 4K (populated in src_2m but not in src_4k) */
    section.offset_within_region = GRANULARITY_4K;
    g_assert_false(ram_discard_manager_is_populated(rdm, &section));

    /* Populate it in src_4k */
    test_source_populate(src_4k, GRANULARITY_4K, GRANULARITY_4K);
    g_assert_true(ram_discard_manager_is_populated(rdm, &section));

    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src_2m));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src_4k));

    test_source_free(src_2m);
    test_source_free(src_4k);
    test_teardown();
}

/*
 * Test: Notification with two sources.
 * Populate notification should only fire when all sources are populated.
 */
static void test_two_sources_notification(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* No populate notifications yet (all discarded) */
    g_assert_cmpint(tl.populate_count, ==, 0);

    /* Populate in src1 only - no notification (src2 still discarded) */
    test_source_populate(src1, 0, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src1),
                                              0, GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl.populate_count, ==, 0);

    /* Populate same range in src2 - now should notify */
    test_source_populate(src2, 0, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src2),
                                              0, GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl.populate_count, ==, 1);

    /* Discard from src1 - should notify discard immediately */
    tl.discard_count = 0;
    test_source_discard(src1, 0, GRANULARITY_4K * 2);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src1),
                                       0, GRANULARITY_4K * 2);
    g_assert_cmpint(tl.discard_count, ==, 1);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Adding source with existing listener.
 * When a new source is added, listeners should be notified about
 * regions that become discarded.
 */
static void test_add_source_with_listener(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /* Populate some range in src1 */
    test_source_populate(src1, 0, GRANULARITY_4K * 8);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Should have been notified about populated region */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpint(tl.last_populate_offset, ==, 0);
    g_assert_cmpint(tl.last_populate_size, ==, GRANULARITY_4K * 8);

    /* src2 has part of the region populated, part discarded */
    /* src2 has 0-4 populated, 4-8 discarded */
    test_source_populate(src2, 0, GRANULARITY_4K * 4);

    /* Add src2 - listener should be notified about newly discarded regions */
    tl.discard_count = 0;
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    /*
     * The range 4K*4 to 4K*8 was populated in src1 but discarded in src2,
     * so it becomes aggregated-discarded. Listener should be notified.
     * Only this range should trigger a discard notification - regions beyond
     * 4K*8 were already discarded in src1, so adding src2 doesn't change them.
     */
    g_assert_cmpint(tl.discard_count, ==, 1);
    g_assert_cmpint(tl.last_discard_offset, ==, GRANULARITY_4K * 4);
    g_assert_cmpint(tl.last_discard_size, ==, GRANULARITY_4K * 4);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Removing source with existing listener.
 * When a source is removed, listeners should be notified about
 * regions that become populated.
 */
static void test_remove_source_with_listener(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /* src1: all of first 8 blocks populated */
    test_source_populate(src1, 0, GRANULARITY_4K * 8);
    /* src2: only first 4 blocks populated */
    test_source_populate(src2, 0, GRANULARITY_4K * 4);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Only first 4 blocks are aggregated-populated */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 4);

    /* Remove src2 - blocks 4-8 should become populated */
    tl.populate_count = 0;
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));

    /* Listener should be notified about newly populated region (4K*4 to 4K*8) */
    g_assert_cmpint(tl.populate_count, >=, 1);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Add a source, register a listener, remove the source, then add it back.
 * This checks the transition from 0 sources (all populated) to 1 source
 * (partially discarded) with an active listener.
 */
static void test_readd_source_with_listener(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    /* Populate some range in src */
    test_source_populate(src, 0, GRANULARITY_4K * 8);

    /* 1. Add source */
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* 2. Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Listener notified about populated region (0 - 32K) */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 8);

    /* 3. Remove source */
    tl.populate_count = 0;
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));

    /*
     * With 0 sources, everything is populated.
     * The range that was discarded in src (from 32K to end) becomes populated.
     */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_offset, ==, GRANULARITY_4K * 8);
    g_assert_cmpuint(tl.last_populate_size, ==, TEST_REGION_SIZE - GRANULARITY_4K * 8);

    /* 4. Add source back */
    tl.discard_count = 0;
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);

    /*
     * Now we have 1 source again. The range (32K to end) is discarded again.
     * Listener should be notified about this discard.
     */
    g_assert_cmpint(tl.discard_count, ==, 1);
    g_assert_cmpuint(tl.last_discard_offset, ==, GRANULARITY_4K * 8);
    g_assert_cmpuint(tl.last_discard_size, ==, TEST_REGION_SIZE - GRANULARITY_4K * 8);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Duplicate source registration should fail.
 */
static void test_duplicate_source(void)
{
    TestRamDiscardSource *src;
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);

    /* Adding same source again should fail */
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, -EBUSY);

    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Populate notification rollback on listener error.
 */
static void test_populate_rollback(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl1 = { 0, }, tl2 = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register two listeners */
    test_listener_init(&tl1);
    test_listener_init(&tl2);
    tl2.fail_on_populate = 1;  /* Second listener fails on first populate */

    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);

    /*
     * Register tl2 first so it's visited second (QLIST_INSERT_HEAD reverses
     * registration order). This ensures tl1 receives populate before tl2
     * fails.
     */
    ram_discard_manager_register_listener(rdm, &tl2.rdl, &section);
    ram_discard_manager_register_listener(rdm, &tl1.rdl, &section);

    /* Try to populate - should fail and roll back */
    test_source_populate(src, 0, GRANULARITY_4K);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              0, GRANULARITY_4K);
    g_assert_cmpint(ret, ==, -ENOMEM);

    /* First listener should have received populate then discard (rollback) */
    g_assert_cmpint(tl1.populate_count, ==, 1);
    g_assert_cmpint(tl1.discard_count, ==, 1);

    ram_discard_manager_unregister_listener(rdm, &tl1.rdl);
    ram_discard_manager_unregister_listener(rdm, &tl2.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Replay populated with two sources (intersection).
 */
static void test_replay_populated_intersection(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /*
     * src1: blocks 0-7 populated
     * src2: blocks 4-11 populated
     * Intersection: blocks 4-7
     */
    test_source_populate(src1, 0, GRANULARITY_4K * 8);
    test_source_populate(src2, GRANULARITY_4K * 4, GRANULARITY_4K * 8);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener - should only get notified about intersection */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Should have been notified about blocks 4-7 (intersection) */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_offset, ==, GRANULARITY_4K * 4);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 4);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Empty region (no sources).
 */
static void test_no_sources(void)
{
    test_setup();

    /* No sources - should have no manager */
    g_assert_null(memory_region_get_ram_discard_manager(test_mr));
    g_assert_false(memory_region_has_ram_discard_manager(test_mr));

    test_teardown();
}

static void test_redundant_discard(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /* Add sources */
    ret = memory_region_add_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    ret = memory_region_add_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(TEST_REGION_SIZE);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Populate intersection (0-4K) in both sources */
    test_source_populate(src1, 0, GRANULARITY_4K);
    test_source_populate(src2, 0, GRANULARITY_4K);

    /* Notify populate src1 - should trigger listener populate */
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src1),
                                              0, GRANULARITY_4K);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl.populate_count, ==, 1);

    /* Now Discard src1 -> Aggregate Discarded */
    tl.discard_count = 0;
    test_source_discard(src1, 0, GRANULARITY_4K);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src1), 0, GRANULARITY_4K);
    g_assert_cmpint(tl.discard_count, ==, 1);

    /* Now Discard src2 -> Aggregate Discarded (Already Discarded!) */
    /* Listener should NOT receive another discard notification for the same range. */
    test_source_discard(src2, 0, GRANULARITY_4K);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src2), 0, GRANULARITY_4K);

    g_assert_cmpint(tl.discard_count, ==, 1);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

/*
 * Test: Listener with partial section coverage.
 * Listener should only receive notifications for its registered range.
 */
static void test_partial_listener_section(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    /* Populate blocks 0-7 */
    test_source_populate(src, 0, GRANULARITY_4K * 8);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener for only blocks 2-5 (not the full region) */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = GRANULARITY_4K * 2;
    section.size = int128_make64(GRANULARITY_4K * 4);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Should be notified only about blocks 2-5 (intersection) */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_offset, ==, GRANULARITY_4K * 2);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 4);

    /* Discard block 0 - outside listener's section, no notification */
    tl.discard_count = 0;
    test_source_discard(src, 0, GRANULARITY_4K);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src),
                                       0, GRANULARITY_4K);
    g_assert_cmpint(tl.discard_count, ==, 0);

    /* Discard block 3 - inside listener's section */
    test_source_discard(src, GRANULARITY_4K * 3, GRANULARITY_4K);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src),
                                       GRANULARITY_4K * 3, GRANULARITY_4K);
    g_assert_cmpint(tl.discard_count, ==, 1);
    g_assert_cmpuint(tl.last_discard_offset, ==, GRANULARITY_4K * 3);

    /* Discard spanning boundary (blocks 5-6) - only block 5 in section */
    tl.discard_count = 0;
    test_source_discard(src, GRANULARITY_4K * 5, GRANULARITY_4K * 2);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src),
                                       GRANULARITY_4K * 5, GRANULARITY_4K * 2);
    g_assert_cmpint(tl.discard_count, ==, 1);
    g_assert_cmpuint(tl.last_discard_offset, ==, GRANULARITY_4K * 5);
    g_assert_cmpuint(tl.last_discard_size, ==, GRANULARITY_4K);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Multiple listeners with different (non-overlapping) sections.
 */
static void test_multiple_listeners_different_sections(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section1, section2;
    TestListener tl1 = { 0, }, tl2 = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Listener 1: blocks 0-3 */
    test_listener_init(&tl1);
    section1.mr = test_mr;
    section1.offset_within_region = 0;
    section1.size = int128_make64(GRANULARITY_4K * 4);
    ram_discard_manager_register_listener(rdm, &tl1.rdl, &section1);

    /* Listener 2: blocks 8-11 */
    test_listener_init(&tl2);
    section2.mr = test_mr;
    section2.offset_within_region = GRANULARITY_4K * 8;
    section2.size = int128_make64(GRANULARITY_4K * 4);
    ram_discard_manager_register_listener(rdm, &tl2.rdl, &section2);

    /* Initially all discarded - no populate notifications */
    g_assert_cmpint(tl1.populate_count, ==, 0);
    g_assert_cmpint(tl2.populate_count, ==, 0);

    /* Populate blocks 0-3 - only tl1 should be notified */
    test_source_populate(src, 0, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              0, GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 1);
    g_assert_cmpint(tl2.populate_count, ==, 0);

    /* Populate blocks 8-11 - only tl2 should be notified */
    test_source_populate(src, GRANULARITY_4K * 8, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              GRANULARITY_4K * 8,
                                              GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 1);
    g_assert_cmpint(tl2.populate_count, ==, 1);

    /* Populate blocks 4-7 (gap) - neither listener should be notified */
    test_source_populate(src, GRANULARITY_4K * 4, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              GRANULARITY_4K * 4,
                                              GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 1);
    g_assert_cmpint(tl2.populate_count, ==, 1);

    ram_discard_manager_unregister_listener(rdm, &tl2.rdl);
    ram_discard_manager_unregister_listener(rdm, &tl1.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Multiple listeners with overlapping sections.
 */
static void test_overlapping_listener_sections(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section1, section2;
    TestListener tl1 = { 0, }, tl2 = { 0, };
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Listener 1: blocks 0-7 */
    test_listener_init(&tl1);
    section1.mr = test_mr;
    section1.offset_within_region = 0;
    section1.size = int128_make64(GRANULARITY_4K * 8);
    ram_discard_manager_register_listener(rdm, &tl1.rdl, &section1);

    /* Listener 2: blocks 4-11 (overlaps with tl1 on blocks 4-7) */
    test_listener_init(&tl2);
    section2.mr = test_mr;
    section2.offset_within_region = GRANULARITY_4K * 4;
    section2.size = int128_make64(GRANULARITY_4K * 8);
    ram_discard_manager_register_listener(rdm, &tl2.rdl, &section2);

    /* Populate blocks 4-7 (overlap region) - both should be notified */
    test_source_populate(src, GRANULARITY_4K * 4, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              GRANULARITY_4K * 4,
                                              GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 1);
    g_assert_cmpint(tl2.populate_count, ==, 1);

    /* Populate blocks 0-3 - only tl1 */
    test_source_populate(src, 0, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              0, GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 2);
    g_assert_cmpint(tl2.populate_count, ==, 1);

    /* Populate blocks 8-11 - only tl2 */
    test_source_populate(src, GRANULARITY_4K * 8, GRANULARITY_4K * 4);
    ret = ram_discard_manager_notify_populate(rdm, RAM_DISCARD_SOURCE(src),
                                              GRANULARITY_4K * 8,
                                              GRANULARITY_4K * 4);
    g_assert_cmpint(ret, ==, 0);
    g_assert_cmpint(tl1.populate_count, ==, 2);
    g_assert_cmpint(tl2.populate_count, ==, 2);

    ram_discard_manager_unregister_listener(rdm, &tl2.rdl);
    ram_discard_manager_unregister_listener(rdm, &tl1.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

/*
 * Test: Listener at exact memory region boundaries.
 */
static void test_boundary_section(void)
{
    TestRamDiscardSource *src;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    TestListener tl = { 0, };
    uint64_t last_offset;
    int ret;

    test_setup();

    src = test_source_new(test_mr, GRANULARITY_4K);

    /* Populate last 4 blocks of the region */
    last_offset = TEST_REGION_SIZE - GRANULARITY_4K * 4;
    test_source_populate(src, last_offset, GRANULARITY_4K * 4);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src));
    g_assert_cmpint(ret, ==, 0);
    rdm = memory_region_get_ram_discard_manager(test_mr);

    /* Register listener for exactly the last 4 blocks */
    test_listener_init(&tl);
    section.mr = test_mr;
    section.offset_within_region = last_offset;
    section.size = int128_make64(GRANULARITY_4K * 4);
    ram_discard_manager_register_listener(rdm, &tl.rdl, &section);

    /* Should receive notification for the populated range */
    g_assert_cmpint(tl.populate_count, ==, 1);
    g_assert_cmpuint(tl.last_populate_offset, ==, last_offset);
    g_assert_cmpuint(tl.last_populate_size, ==, GRANULARITY_4K * 4);

    /* Discard exactly at boundary */
    tl.discard_count = 0;
    test_source_discard(src, last_offset, GRANULARITY_4K * 4);
    ram_discard_manager_notify_discard(rdm, RAM_DISCARD_SOURCE(src),
                                       last_offset, GRANULARITY_4K * 4);
    g_assert_cmpint(tl.discard_count, ==, 1);

    ram_discard_manager_unregister_listener(rdm, &tl.rdl);
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src));
    test_source_free(src);
    test_teardown();
}

static int count_discarded_blocks(const MemoryRegionSection *section,
                                  void *opaque)
{
    int *count = opaque;
    *count += int128_get64(section->size) / GRANULARITY_4K;
    return 0;
}

/*
 * Test: replay_discarded with two sources (union semantics).
 */
static void test_replay_discarded(void)
{
    TestRamDiscardSource *src1, *src2;
    RamDiscardManager *rdm;
    MemoryRegionSection section;
    int count = 0;
    int ret;

    test_setup();

    src1 = test_source_new(test_mr, GRANULARITY_4K);
    src2 = test_source_new(test_mr, GRANULARITY_4K);

    /*
     * src1: blocks 0-3 populated, rest discarded
     * src2: blocks 2-5 populated, rest discarded
     * Aggregated populated: blocks 2-3 (intersection)
     * Aggregated discarded: blocks 0-1, 4-5, 6+ (union of discarded)
     */
    test_source_populate(src1, 0, GRANULARITY_4K * 4);
    test_source_populate(src2, GRANULARITY_4K * 2, GRANULARITY_4K * 4);

    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src1));
    g_assert_cmpint(ret, ==, 0);
    ret = memory_region_add_ram_discard_source(test_mr,
                                               RAM_DISCARD_SOURCE(src2));
    g_assert_cmpint(ret, ==, 0);

    rdm = memory_region_get_ram_discard_manager(test_mr);

    section.mr = test_mr;
    section.offset_within_region = 0;
    section.size = int128_make64(GRANULARITY_4K * 8);

    /* Count discarded blocks */
    ret = ram_discard_manager_replay_discarded(rdm, &section,
                                               count_discarded_blocks, &count);

    g_assert_cmpint(ret, ==, 0);
    /* Discarded: blocks 0-1 (2), blocks 4-5 (2), blocks 6-7 (2) = 6 blocks */
    g_assert_cmpint(count, ==, 6);

    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src2));
    memory_region_del_ram_discard_source(test_mr, RAM_DISCARD_SOURCE(src1));

    test_source_free(src2);
    test_source_free(src1);
    test_teardown();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    module_call_init(MODULE_INIT_QOM);
    type_register_static(&test_rds_info);

    g_test_add_func("/ram-discard-manager/single-source/basic",
                    test_single_source_basic);
    g_test_add_func("/ram-discard-manager/single-source/listener",
                    test_single_source_listener);
    g_test_add_func("/ram-discard-manager/two-sources/same-granularity",
                    test_two_sources_same_granularity);
    g_test_add_func("/ram-discard-manager/two-sources/different-granularity",
                    test_two_sources_different_granularity);
    g_test_add_func("/ram-discard-manager/two-sources/notification",
                    test_two_sources_notification);
    g_test_add_func("/ram-discard-manager/dynamic/add-source-with-listener",
                    test_add_source_with_listener);
    g_test_add_func("/ram-discard-manager/dynamic/remove-source-with-listener",
                    test_remove_source_with_listener);
    g_test_add_func("/ram-discard-manager/dynamic/readd-source-with-listener",
                    test_readd_source_with_listener);
    g_test_add_func("/ram-discard-manager/edge/duplicate-source",
                    test_duplicate_source);
    g_test_add_func("/ram-discard-manager/edge/populate-rollback",
                    test_populate_rollback);
    g_test_add_func("/ram-discard-manager/edge/replay-intersection",
                    test_replay_populated_intersection);
    g_test_add_func("/ram-discard-manager/edge/no-sources",
                    test_no_sources);
    g_test_add_func("/ram-discard-manager/multi-source/redundant-discard",
                    test_redundant_discard);
    g_test_add_func("/ram-discard-manager/listener/partial-section",
                    test_partial_listener_section);
    g_test_add_func("/ram-discard-manager/listener/multiple-different",
                    test_multiple_listeners_different_sections);
    g_test_add_func("/ram-discard-manager/listener/overlapping",
                    test_overlapping_listener_sections);
    g_test_add_func("/ram-discard-manager/edge/boundary-section",
                    test_boundary_section);
    g_test_add_func("/ram-discard-manager/multi-source/replay-discarded",
                    test_replay_discarded);

    return g_test_run();
}
