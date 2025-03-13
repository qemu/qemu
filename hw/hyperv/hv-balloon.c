/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hv-balloon-internal.h"

#include "system/address-spaces.h"
#include "exec/cpu-common.h"
#include "system/ramblock.h"
#include "hw/boards.h"
#include "hw/hyperv/dynmem-proto.h"
#include "hw/hyperv/hv-balloon.h"
#include "hw/hyperv/vmbus.h"
#include "hw/mem/memory-device.h"
#include "hw/mem/pc-dimm.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "monitor/qdev.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-machine.h"
#include "qapi/qapi-events-machine.h"
#include "qapi/qapi-types-machine.h"
#include "qobject/qdict.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/timer.h"
#include "system/balloon.h"
#include "system/hostmem.h"
#include "system/reset.h"
#include "hv-balloon-our_range_memslots.h"
#include "hv-balloon-page_range_tree.h"
#include "trace.h"

#define HV_BALLOON_ADDR_PROP "addr"
#define HV_BALLOON_MEMDEV_PROP "memdev"
#define HV_BALLOON_GUID "525074DC-8985-46e2-8057-A307DC18A502"

/*
 * Some Windows versions (at least Server 2019) will crash with various
 * error codes when receiving DM protocol requests (at least
 * DM_MEM_HOT_ADD_REQUEST) immediately after boot.
 *
 * It looks like Hyper-V from Server 2016 uses a 50-second after-boot
 * delay, probably to workaround this issue, so we'll use this value, too.
 */
#define HV_BALLOON_POST_INIT_WAIT (50 * 1000)

#define HV_BALLOON_HA_CHUNK_SIZE (2 * GiB)
#define HV_BALLOON_HA_CHUNK_PAGES (HV_BALLOON_HA_CHUNK_SIZE / HV_BALLOON_PAGE_SIZE)

#define HV_BALLOON_HA_MEMSLOT_SIZE_ALIGN (128 * MiB)

#define HV_BALLOON_HR_CHUNK_PAGES 585728
/*
 *                                ^ that's the maximum number of pages
 * that Windows returns in one hot remove response
 *
 * If the number requested is too high Windows will no longer honor
 * these requests
 */

struct HvBalloonClass {
    VMBusDeviceClass parent_class;
} HvBalloonClass;

typedef enum State {
    /* not a real state */
    S_NO_CHANGE = 0,

    S_WAIT_RESET,
    S_POST_RESET_CLOSED,

    /* init flow */
    S_VERSION,
    S_CAPS,
    S_POST_INIT_WAIT,

    S_IDLE,

    /* balloon op flow */
    S_BALLOON_POSTING,
    S_BALLOON_RB_WAIT,
    S_BALLOON_REPLY_WAIT,

    /* unballoon + hot add ops flow */
    S_UNBALLOON_POSTING,
    S_UNBALLOON_RB_WAIT,
    S_UNBALLOON_REPLY_WAIT,
    S_HOT_ADD_SETUP,
    S_HOT_ADD_RB_WAIT,
    S_HOT_ADD_POSTING,
    S_HOT_ADD_REPLY_WAIT,
} State;

typedef struct StateDesc {
    State state;
    const char *desc;
} StateDesc;

typedef struct HvBalloon {
    VMBusDevice parent;
    State state;

    union dm_version version;
    union dm_caps caps;

    QEMUTimer post_init_timer;

    unsigned int trans_id;

    struct {
        bool enabled;
        bool received;
        uint64_t committed;
        uint64_t available;
    } status_report;

    /* Guest target size */
    uint64_t target;
    bool target_changed;

    /* Current (un)balloon / hot-add operation parameters */
    union {
        uint64_t balloon_diff;

        struct {
            uint64_t unballoon_diff;
            uint64_t hot_add_diff;
        };

        struct {
            PageRange hot_add_range;
            uint64_t ha_current_count;
        };
    };

    OurRangeMemslots *our_range;

    /* Count of memslots covering our memory */
    unsigned int memslot_count;

    /* Nominal size of each memslot (the last one might be smaller) */
    uint64_t memslot_size;

    /* Non-ours removed memory */
    PageRangeTree removed_guest, removed_both;

    /* Grand totals of removed memory (both ours and non-ours) */
    uint64_t removed_guest_ctr, removed_both_ctr;

    /* MEMORY_DEVICE props */
    uint64_t addr;
    HostMemoryBackend *hostmem;
    MemoryRegion *mr;
} HvBalloon;

OBJECT_DEFINE_TYPE_WITH_INTERFACES(HvBalloon, hv_balloon, HV_BALLOON, VMBUS_DEVICE, \
                                   { TYPE_MEMORY_DEVICE }, { })

#define HV_BALLOON_SET_STATE(hvb, news)             \
    do {                                            \
        assert(news != S_NO_CHANGE);                \
        hv_balloon_state_set(hvb, news, # news);    \
    } while (0)

#define HV_BALLOON_STATE_DESC_SET(stdesc, news)         \
    _hv_balloon_state_desc_set(stdesc, news, # news)

#define HV_BALLOON_STATE_DESC_INIT \
    {                              \
        .state = S_NO_CHANGE,      \
    }

typedef struct HvBalloonReq {
    VMBusChanReq vmreq;
} HvBalloonReq;

/* total our memory includes parts currently removed from the guest */
static uint64_t hv_balloon_total_our_ram(HvBalloon *balloon)
{
    if (!balloon->our_range) {
        return 0;
    }

    return balloon->our_range->range.added;
}

/* TODO: unify the code below with virtio-balloon and cache the value */
static int build_dimm_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_PC_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized DIMMs matter */
            *list = g_slist_prepend(*list, dev);
        }
    }

    object_child_foreach(obj, build_dimm_list, opaque);
    return 0;
}

static ram_addr_t get_current_ram_size(void)
{
    GSList *list = NULL, *item;
    ram_addr_t size = current_machine->ram_size;

    build_dimm_list(qdev_get_machine(), &list);
    for (item = list; item; item = g_slist_next(item)) {
        Object *obj = OBJECT(item->data);
        if (!strcmp(object_get_typename(obj), TYPE_PC_DIMM))
            size += object_property_get_int(obj, PC_DIMM_SIZE_PROP,
                                            &error_abort);
    }
    g_slist_free(list);

    return size;
}

/* total RAM includes memory currently removed from the guest */
static uint64_t hv_balloon_total_ram(HvBalloon *balloon)
{
    ram_addr_t ram_size = get_current_ram_size();
    uint64_t ram_size_pages = ram_size >> HV_BALLOON_PFN_SHIFT;
    uint64_t our_ram_size_pages = hv_balloon_total_our_ram(balloon);

    assert(ram_size_pages > 0);

    return SUM_SATURATE_U64(ram_size_pages, our_ram_size_pages);
}

/*
 * calculating the total RAM size is a slow operation,
 * avoid it as much as possible
 */
static uint64_t hv_balloon_total_removed_rs(HvBalloon *balloon,
                                            uint64_t ram_size_pages)
{
    uint64_t total_removed;

    total_removed = SUM_SATURATE_U64(balloon->removed_guest_ctr,
                                     balloon->removed_both_ctr);

    /* possible if guest returns pages outside actual RAM */
    if (total_removed > ram_size_pages) {
        total_removed = ram_size_pages;
    }

    return total_removed;
}

/* Returns whether the state has actually changed */
static bool hv_balloon_state_set(HvBalloon *balloon,
                                 State newst, const char *newststr)
{
    if (newst == S_NO_CHANGE || balloon->state == newst) {
        return false;
    }

    balloon->state = newst;
    trace_hv_balloon_state_change(newststr);
    return true;
}

static void _hv_balloon_state_desc_set(StateDesc *stdesc,
                                       State newst, const char *newststr)
{
    /* state setting is only permitted on a freshly init desc */
    assert(stdesc->state == S_NO_CHANGE);

    assert(newst != S_NO_CHANGE);

    stdesc->state = newst;
    stdesc->desc = newststr;
}

static VMBusChannel *hv_balloon_get_channel_maybe(HvBalloon *balloon)
{
    return vmbus_device_channel(&balloon->parent, 0);
}

static VMBusChannel *hv_balloon_get_channel(HvBalloon *balloon)
{
    VMBusChannel *chan;

    chan = hv_balloon_get_channel_maybe(balloon);
    assert(chan != NULL);
    return chan;
}

static ssize_t hv_balloon_send_packet(VMBusChannel *chan,
                                      struct dm_message *msg)
{
    int ret;

    ret = vmbus_channel_reserve(chan, 0, msg->hdr.size);
    if (ret < 0) {
        return ret;
    }

    return vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                              NULL, 0, msg, msg->hdr.size, false,
                              msg->hdr.trans_id);
}

static bool hv_balloon_unballoon_get_source(HvBalloon *balloon,
                                            PageRangeTree *dtree,
                                            uint64_t **dctr,
                                            bool *is_our_range)
{
    OurRange *our_range = OUR_RANGE(balloon->our_range);

    /* Try the boot memory first */
    if (g_tree_nnodes(balloon->removed_guest.t) > 0) {
        *dtree = balloon->removed_guest;
        *dctr = &balloon->removed_guest_ctr;
        *is_our_range = false;
    } else if (g_tree_nnodes(balloon->removed_both.t) > 0) {
        *dtree = balloon->removed_both;
        *dctr = &balloon->removed_both_ctr;
        *is_our_range = false;
    } else if (!our_range) {
        return false;
    } else if (!our_range_is_removed_tree_empty(our_range, false)) {
        *dtree = our_range_get_removed_tree(our_range, false);
        *dctr = &balloon->removed_guest_ctr;
        *is_our_range = true;
    } else if (!our_range_is_removed_tree_empty(our_range, true)) {
        *dtree = our_range_get_removed_tree(our_range, true);
        *dctr = &balloon->removed_both_ctr;
        *is_our_range = true;
    } else {
        return false;
    }

    return true;
}

static void hv_balloon_unballoon_rb_wait(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_unballoon_request *ur;
    size_t ur_size = sizeof(*ur) + sizeof(ur->range_array[0]);

    assert(balloon->state == S_UNBALLOON_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, ur_size) < 0) {
        return;
    }

    HV_BALLOON_STATE_DESC_SET(stdesc, S_UNBALLOON_POSTING);
}

static void hv_balloon_unballoon_posting(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    PageRangeTree dtree;
    uint64_t *dctr;
    bool our_range;
    g_autofree struct dm_unballoon_request *ur = NULL;
    size_t ur_size = sizeof(*ur) + sizeof(ur->range_array[0]);
    PageRange range;
    bool bret;
    ssize_t ret;

    assert(balloon->state == S_UNBALLOON_POSTING);
    assert(balloon->unballoon_diff > 0);

    if (!hv_balloon_unballoon_get_source(balloon, &dtree, &dctr, &our_range)) {
        error_report("trying to unballoon but nothing seems to be ballooned");
        /*
         * there is little we can do as we might have already
         * sent the guest a partial request we can't cancel
         */
        return;
    }

    assert(balloon->our_range || !our_range);
    assert(dtree.t);
    assert(dctr);

    ur = g_malloc0(ur_size);
    ur->hdr.type = DM_UNBALLOON_REQUEST;
    ur->hdr.size = ur_size;
    ur->hdr.trans_id = balloon->trans_id;

    bret = hvb_page_range_tree_pop(dtree, &range, MIN(balloon->unballoon_diff,
                                                      HV_BALLOON_HA_CHUNK_PAGES));
    assert(bret);
    /* TODO: madvise? */

    *dctr -= range.count;
    balloon->unballoon_diff -= range.count;

    ur->range_count = 1;
    ur->range_array[0].finfo.start_page = range.start;
    ur->range_array[0].finfo.page_cnt = range.count;
    ur->more_pages = balloon->unballoon_diff > 0;

    trace_hv_balloon_outgoing_unballoon(ur->hdr.trans_id,
                                        range.count, range.start,
                                        balloon->unballoon_diff);

    if (ur->more_pages) {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_UNBALLOON_RB_WAIT);
    } else {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_UNBALLOON_REPLY_WAIT);
    }

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, ur, ur_size, false,
                             ur->hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting unballoon msg, expect problems",
                     ret);
    }
}

static bool hv_balloon_our_range_ensure(HvBalloon *balloon)
{
    uint64_t align;
    MemoryRegion *hostmem_mr;
    g_autoptr(OurRangeMemslots) our_range_memslots = NULL;
    OurRange *our_range;

    if (balloon->our_range) {
        return true;
    }

    if (!balloon->hostmem) {
        return false;
    }

    align = (1 << balloon->caps.cap_bits.hot_add_alignment) * MiB;
    assert(QEMU_IS_ALIGNED(balloon->addr, align));

    hostmem_mr = host_memory_backend_get_memory(balloon->hostmem);

    our_range_memslots = hvb_our_range_memslots_new(balloon->addr,
                                                    balloon->mr, hostmem_mr,
                                                    OBJECT(balloon),
                                                    balloon->memslot_count,
                                                    balloon->memslot_size);
    our_range = OUR_RANGE(our_range_memslots);

    if (hvb_page_range_tree_intree_any(balloon->removed_guest,
                                       our_range->range.start,
                                       our_range->range.count) ||
        hvb_page_range_tree_intree_any(balloon->removed_both,
                                       our_range->range.start,
                                       our_range->range.count)) {
        error_report("some parts of the memory backend were already returned by the guest. this should not happen, please reboot the guest and try again");
        return false;
    }

    trace_hv_balloon_our_range_add(our_range->range.count,
                                   our_range->range.start);

    balloon->our_range = g_steal_pointer(&our_range_memslots);
    return true;
}

static void hv_balloon_hot_add_setup(HvBalloon *balloon, StateDesc *stdesc)
{
    /* need to make copy since it is in union with hot_add_range */
    uint64_t hot_add_diff = balloon->hot_add_diff;
    PageRange *hot_add_range = &balloon->hot_add_range;
    uint64_t align, our_range_remaining;
    OurRange *our_range;

    assert(balloon->state == S_HOT_ADD_SETUP);
    assert(hot_add_diff > 0);

    if (!hv_balloon_our_range_ensure(balloon)) {
        goto ret_idle;
    }

    our_range = OUR_RANGE(balloon->our_range);

    align = (1 << balloon->caps.cap_bits.hot_add_alignment) *
        (MiB / HV_BALLOON_PAGE_SIZE);

    /* Absolute GPA in pages */
    hot_add_range->start = our_range_get_remaining_start(our_range);
    assert(QEMU_IS_ALIGNED(hot_add_range->start, align));

    our_range_remaining = our_range_get_remaining_size(our_range);
    hot_add_range->count = MIN(our_range_remaining, hot_add_diff);
    hot_add_range->count = QEMU_ALIGN_DOWN(hot_add_range->count, align);
    if (hot_add_range->count == 0) {
        goto ret_idle;
    }

    hvb_our_range_memslots_ensure_mapped_additional(balloon->our_range,
                                                    hot_add_range->count);

    HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_RB_WAIT);
    return;

ret_idle:
    HV_BALLOON_STATE_DESC_SET(stdesc, S_IDLE);
}

static void hv_balloon_hot_add_rb_wait(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_hot_add_with_region *ha;
    size_t ha_size = sizeof(*ha);

    assert(balloon->state == S_HOT_ADD_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, ha_size) < 0) {
        return;
    }

    HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_POSTING);
}

static void hv_balloon_hot_add_posting(HvBalloon *balloon, StateDesc *stdesc)
{
    PageRange *hot_add_range = &balloon->hot_add_range;
    uint64_t *current_count = &balloon->ha_current_count;
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    g_autofree struct dm_hot_add_with_region *ha = NULL;
    size_t ha_size = sizeof(*ha);
    union dm_mem_page_range *ha_region;
    uint64_t align, chunk_max_size;
    ssize_t ret;

    assert(balloon->state == S_HOT_ADD_POSTING);
    assert(hot_add_range->count > 0);

    align = (1 << balloon->caps.cap_bits.hot_add_alignment) *
        (MiB / HV_BALLOON_PAGE_SIZE);
    if (align >= HV_BALLOON_HA_CHUNK_PAGES) {
        /*
         * If the required alignment is higher than the chunk size we let it
         * override that size.
         */
        chunk_max_size = align;
    } else {
        chunk_max_size = QEMU_ALIGN_DOWN(HV_BALLOON_HA_CHUNK_PAGES, align);
    }

    /*
     * hot_add_range->count starts aligned in hv_balloon_hot_add_setup(),
     * then it is either reduced by subtracting aligned current_count or
     * further hot-adds are prevented by marking the whole remaining our range
     * as unusable in hv_balloon_handle_hot_add_response().
     */
    *current_count = MIN(hot_add_range->count, chunk_max_size);

    ha = g_malloc0(ha_size);
    ha_region = &ha->region;
    ha->hdr.type = DM_MEM_HOT_ADD_REQUEST;
    ha->hdr.size = ha_size;
    ha->hdr.trans_id = balloon->trans_id;

    ha->range.finfo.start_page = hot_add_range->start;
    ha->range.finfo.page_cnt = *current_count;
    ha_region->finfo.start_page = hot_add_range->start;
    ha_region->finfo.page_cnt = ha->range.finfo.page_cnt;

    trace_hv_balloon_outgoing_hot_add(ha->hdr.trans_id,
                                      *current_count, hot_add_range->start);

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, ha, ha_size, false,
                             ha->hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting hot add msg, expect problems",
                     ret);
    }

    HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_REPLY_WAIT);
}

static void hv_balloon_balloon_rb_wait(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    size_t bl_size = sizeof(struct dm_balloon);

    assert(balloon->state == S_BALLOON_RB_WAIT);

    if (vmbus_channel_reserve(chan, 0, bl_size) < 0) {
        return;
    }

    HV_BALLOON_STATE_DESC_SET(stdesc, S_BALLOON_POSTING);
}

static void hv_balloon_balloon_posting(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan = hv_balloon_get_channel(balloon);
    struct dm_balloon bl;
    size_t bl_size = sizeof(bl);
    ssize_t ret;

    assert(balloon->state == S_BALLOON_POSTING);
    assert(balloon->balloon_diff > 0);

    memset(&bl, 0, sizeof(bl));
    bl.hdr.type = DM_BALLOON_REQUEST;
    bl.hdr.size = bl_size;
    bl.hdr.trans_id = balloon->trans_id;
    bl.num_pages = MIN(balloon->balloon_diff, HV_BALLOON_HR_CHUNK_PAGES);

    trace_hv_balloon_outgoing_balloon(bl.hdr.trans_id, bl.num_pages,
                                      balloon->balloon_diff);

    ret = vmbus_channel_send(chan, VMBUS_PACKET_DATA_INBAND,
                             NULL, 0, &bl, bl_size, false,
                             bl.hdr.trans_id);
    if (ret <= 0) {
        error_report("error %zd when posting balloon msg, expect problems",
                     ret);
    }

    HV_BALLOON_STATE_DESC_SET(stdesc, S_BALLOON_REPLY_WAIT);
}

static void hv_balloon_idle_state_process_target(HvBalloon *balloon,
                                                 StateDesc *stdesc)
{
    bool can_balloon = balloon->caps.cap_bits.balloon;
    uint64_t ram_size_pages, total_removed;

    ram_size_pages = hv_balloon_total_ram(balloon);
    total_removed = hv_balloon_total_removed_rs(balloon, ram_size_pages);

    /*
     * we need to cache the values computed from the balloon target value when
     * starting the adjustment procedure in case someone changes the target when
     * the procedure is in progress
     */
    if (balloon->target > ram_size_pages - total_removed) {
        bool can_hot_add = balloon->caps.cap_bits.hot_add;
        uint64_t target_diff = balloon->target -
            (ram_size_pages - total_removed);

        balloon->unballoon_diff = MIN(target_diff, total_removed);

        if (can_hot_add) {
            balloon->hot_add_diff = target_diff - balloon->unballoon_diff;
        } else {
            balloon->hot_add_diff = 0;
        }

        if (balloon->unballoon_diff > 0) {
            assert(can_balloon);
            HV_BALLOON_STATE_DESC_SET(stdesc, S_UNBALLOON_RB_WAIT);
        } else if (balloon->hot_add_diff > 0) {
            HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_SETUP);
        }
    } else if (can_balloon &&
               balloon->target < ram_size_pages - total_removed) {
        balloon->balloon_diff = ram_size_pages - total_removed -
            balloon->target;
        HV_BALLOON_STATE_DESC_SET(stdesc, S_BALLOON_RB_WAIT);
    }
}

static void hv_balloon_idle_state(HvBalloon *balloon,
                                  StateDesc *stdesc)
{
    assert(balloon->state == S_IDLE);

    if (balloon->target_changed) {
        balloon->target_changed = false;
        hv_balloon_idle_state_process_target(balloon, stdesc);
        return;
    }
}

static const struct {
    void (*handler)(HvBalloon *balloon, StateDesc *stdesc);
} state_handlers[] = {
    [S_IDLE].handler = hv_balloon_idle_state,
    [S_BALLOON_POSTING].handler = hv_balloon_balloon_posting,
    [S_BALLOON_RB_WAIT].handler = hv_balloon_balloon_rb_wait,
    [S_UNBALLOON_POSTING].handler = hv_balloon_unballoon_posting,
    [S_UNBALLOON_RB_WAIT].handler = hv_balloon_unballoon_rb_wait,
    [S_HOT_ADD_SETUP].handler = hv_balloon_hot_add_setup,
    [S_HOT_ADD_RB_WAIT].handler = hv_balloon_hot_add_rb_wait,
    [S_HOT_ADD_POSTING].handler = hv_balloon_hot_add_posting,
};

static void hv_balloon_handle_state(HvBalloon *balloon, StateDesc *stdesc)
{
    if (balloon->state >= ARRAY_SIZE(state_handlers) ||
        !state_handlers[balloon->state].handler) {
        return;
    }

    state_handlers[balloon->state].handler(balloon, stdesc);
}

static void hv_balloon_remove_response_insert_range(PageRangeTree tree,
                                                    const PageRange *range,
                                                    uint64_t *ctr1,
                                                    uint64_t *ctr2,
                                                    uint64_t *ctr3)
{
    uint64_t dupcount, effcount;

    if (range->count == 0) {
        return;
    }

    dupcount = 0;
    hvb_page_range_tree_insert(tree, range->start, range->count, &dupcount);

    assert(dupcount <= range->count);
    effcount = range->count - dupcount;

    *ctr1 += effcount;
    *ctr2 += effcount;
    if (ctr3) {
        *ctr3 += effcount;
    }
}

static void hv_balloon_remove_response_handle_range(HvBalloon *balloon,
                                                    PageRange *range,
                                                    bool both,
                                                    uint64_t *removedctr)
{
    OurRange *our_range = OUR_RANGE(balloon->our_range);
    PageRangeTree globaltree =
        both ? balloon->removed_both : balloon->removed_guest;
    uint64_t *globalctr =
        both ? &balloon->removed_both_ctr : &balloon->removed_guest_ctr;
    PageRange rangeeff;

    if (range->count == 0) {
        return;
    }

    trace_hv_balloon_remove_response(range->count, range->start, both);

    if (our_range) {
        /* Includes the not-yet-hot-added and unusable parts. */
        rangeeff = our_range->range;
    } else {
        rangeeff.start = rangeeff.count = 0;
    }

    if (page_range_intersection_size(range, rangeeff.start, rangeeff.count) > 0) {
        PageRangeTree ourtree = our_range_get_removed_tree(our_range, both);
        PageRange rangehole, rangecommon;
        uint64_t ourremoved = 0;

        /* process the hole before our range, if it exists */
        page_range_part_before(range, rangeeff.start, &rangehole);
        hv_balloon_remove_response_insert_range(globaltree, &rangehole,
                                                globalctr, removedctr, NULL);
        if (rangehole.count > 0) {
            trace_hv_balloon_remove_response_hole(rangehole.count,
                                                  rangehole.start,
                                                  range->count, range->start,
                                                  rangeeff.start, both);
        }

        /* process our part */
        page_range_intersect(range, rangeeff.start, rangeeff.count,
                             &rangecommon);
        hv_balloon_remove_response_insert_range(ourtree, &rangecommon,
                                                globalctr, removedctr,
                                                &ourremoved);
        if (rangecommon.count > 0) {
            trace_hv_balloon_remove_response_common(rangecommon.count,
                                                    rangecommon.start,
                                                    range->count, range->start,
                                                    rangeeff.count,
                                                    rangeeff.start, ourremoved,
                                                    both);
        }

        /* calculate what's left after our range */
        rangecommon = *range;
        page_range_part_after(&rangecommon, rangeeff.start, rangeeff.count,
                              range);
    }

    /* process the remainder of the range that lies after our range */
    if (range->count > 0) {
        hv_balloon_remove_response_insert_range(globaltree, range,
                                                globalctr, removedctr, NULL);
        trace_hv_balloon_remove_response_remainder(range->count, range->start,
                                                   both);
        range->count = 0;
    }
}

static void hv_balloon_remove_response_handle_pages(HvBalloon *balloon,
                                                    PageRange *range,
                                                    uint64_t start,
                                                    uint64_t count,
                                                    bool both,
                                                    uint64_t *removedctr)
{
    assert(count > 0);

    /*
     * if there is an existing range that the new range can't be joined to
     * dump it into tree(s)
     */
    if (range->count > 0 && !page_range_joinable(range, start, count)) {
        hv_balloon_remove_response_handle_range(balloon, range, both,
                                                removedctr);
    }

    if (range->count == 0) {
        range->start = start;
        range->count = count;
    } else if (page_range_joinable_left(range, start, count)) {
        range->start = start;
        range->count += count;
    } else { /* page_range_joinable_right() */
        range->count += count;
    }
}

static gboolean hv_balloon_handle_remove_host_addr_node(gpointer key,
                                                        gpointer value,
                                                        gpointer data)
{
    PageRange *range = value;
    uint64_t pageoff;

    for (pageoff = 0; pageoff < range->count; ) {
        uint64_t addr_64 = (range->start + pageoff) * HV_BALLOON_PAGE_SIZE;
        void *addr;
        RAMBlock *rb;
        ram_addr_t rb_offset;
        size_t rb_page_size;
        size_t discard_size;

        assert(addr_64 <= UINTPTR_MAX);
        addr = (void *)((uintptr_t)addr_64);
        rb = qemu_ram_block_from_host(addr, false, &rb_offset);
        rb_page_size = qemu_ram_pagesize(rb);

        if (rb_page_size != HV_BALLOON_PAGE_SIZE) {
            /* TODO: these should end in "removed_guest" */
            warn_report("guest reported removed page backed by unsupported page size %zu",
                        rb_page_size);
            pageoff++;
            continue;
        }

        discard_size = MIN(range->count - pageoff,
                           (rb->max_length - rb_offset) /
                           HV_BALLOON_PAGE_SIZE);
        discard_size = MAX(discard_size, 1);

        if (ram_block_discard_range(rb, rb_offset, discard_size *
                                    HV_BALLOON_PAGE_SIZE) != 0) {
            warn_report("guest reported removed page failed discard");
        }

        pageoff += discard_size;
    }

    return false;
}

static void hv_balloon_handle_remove_host_addr_tree(PageRangeTree tree)
{
    g_tree_foreach(tree.t, hv_balloon_handle_remove_host_addr_node, NULL);
}

static int hv_balloon_handle_remove_section(PageRangeTree tree,
                                            const MemoryRegionSection *section,
                                            uint64_t count)
{
    void *addr = memory_region_get_ram_ptr(section->mr) +
        section->offset_within_region;
    uint64_t addr_page;

    assert(count > 0);

    if ((uintptr_t)addr % HV_BALLOON_PAGE_SIZE) {
        warn_report("guest reported removed pages at an unaligned host addr %p",
                    addr);
        return -EINVAL;
    }

    addr_page = (uintptr_t)addr / HV_BALLOON_PAGE_SIZE;
    hvb_page_range_tree_insert(tree, addr_page, count, NULL);

    return 0;
}

static void hv_balloon_handle_remove_ranges(HvBalloon *balloon,
                                            union dm_mem_page_range ranges[],
                                            uint32_t count)
{
    uint64_t removedcnt;
    PageRangeTree removed_host_addr;
    PageRange range_guest, range_both;

    hvb_page_range_tree_init(&removed_host_addr);
    range_guest.count = range_both.count = removedcnt = 0;
    for (unsigned int ctr = 0; ctr < count; ctr++) {
        union dm_mem_page_range *mr = &ranges[ctr];
        hwaddr pa;
        MemoryRegionSection section;

        for (unsigned int offset = 0; offset < mr->finfo.page_cnt; ) {
            int ret;
            uint64_t pageno = mr->finfo.start_page + offset;
            uint64_t pagecnt = 1;

            pa = (hwaddr)pageno << HV_BALLOON_PFN_SHIFT;
            section = memory_region_find(get_system_memory(), pa,
                                         (mr->finfo.page_cnt - offset) *
                                         HV_BALLOON_PAGE_SIZE);
            if (!section.mr) {
                warn_report("guest reported removed page %"PRIu64" not found in RAM",
                            pageno);
                ret = -EINVAL;
                goto finish_page;
            }

            pagecnt = int128_get64(section.size) / HV_BALLOON_PAGE_SIZE;
            if (pagecnt <= 0) {
                warn_report("guest reported removed page %"PRIu64" in a section smaller than page size",
                            pageno);
                pagecnt = 1; /* skip the whole page */
                ret = -EINVAL;
                goto finish_page;
            }

            if (!memory_region_is_ram(section.mr) ||
                memory_region_is_rom(section.mr) ||
                memory_region_is_romd(section.mr)) {
                warn_report("guest reported removed page %"PRIu64" in a section that is not an ordinary RAM",
                            pageno);
                ret = -EINVAL;
                goto finish_page;
            }

            ret = hv_balloon_handle_remove_section(removed_host_addr, &section,
                                                   pagecnt);

        finish_page:
            if (ret == 0) {
                hv_balloon_remove_response_handle_pages(balloon,
                                                        &range_both,
                                                        pageno, pagecnt,
                                                        true, &removedcnt);
            } else {
                hv_balloon_remove_response_handle_pages(balloon,
                                                        &range_guest,
                                                        pageno, pagecnt,
                                                        false, &removedcnt);
            }

            if (section.mr) {
                memory_region_unref(section.mr);
            }

            offset += pagecnt;
        }
    }

    hv_balloon_remove_response_handle_range(balloon, &range_both, true,
                                            &removedcnt);
    hv_balloon_remove_response_handle_range(balloon, &range_guest, false,
                                            &removedcnt);

    hv_balloon_handle_remove_host_addr_tree(removed_host_addr);
    hvb_page_range_tree_destroy(&removed_host_addr);

    if (removedcnt > balloon->balloon_diff) {
        warn_report("guest reported more pages removed than currently pending (%"PRIu64" vs %"PRIu64")",
                    removedcnt, balloon->balloon_diff);
        balloon->balloon_diff = 0;
    } else {
        balloon->balloon_diff -= removedcnt;
    }
}

static bool hv_balloon_handle_msg_size(HvBalloonReq *req, size_t minsize,
                                       const char *msgname)
{
    VMBusChanReq *vmreq = &req->vmreq;
    uint32_t msglen = vmreq->msglen;

    if (msglen >= minsize) {
        return true;
    }

    warn_report("%s message too short (%u vs %zu), ignoring", msgname,
                (unsigned int)msglen, minsize);
    return false;
}

static void hv_balloon_handle_version_request(HvBalloon *balloon,
                                              HvBalloonReq *req,
                                              StateDesc *stdesc)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_version_request *msgVr = vmreq->msg;
    struct dm_version_response respVr;

    if (balloon->state != S_VERSION) {
        warn_report("unexpected DM_VERSION_REQUEST in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgVr),
                                    "DM_VERSION_REQUEST")) {
        return;
    }

    trace_hv_balloon_incoming_version(msgVr->version.major_version,
                                      msgVr->version.minor_version);

    memset(&respVr, 0, sizeof(respVr));
    respVr.hdr.type = DM_VERSION_RESPONSE;
    respVr.hdr.size = sizeof(respVr);
    respVr.hdr.trans_id = msgVr->hdr.trans_id;
    respVr.is_accepted = msgVr->version.version >= DYNMEM_PROTOCOL_VERSION_1 &&
        msgVr->version.version <= DYNMEM_PROTOCOL_VERSION_3;

    hv_balloon_send_packet(vmreq->chan, (struct dm_message *)&respVr);

    if (respVr.is_accepted) {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_CAPS);
    }
}

static void hv_balloon_handle_caps_report(HvBalloon *balloon,
                                          HvBalloonReq *req,
                                          StateDesc *stdesc)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_capabilities *msgCap = vmreq->msg;
    struct dm_capabilities_resp_msg respCap;

    if (balloon->state != S_CAPS) {
        warn_report("unexpected DM_CAPABILITIES_REPORT in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgCap),
                                    "DM_CAPABILITIES_REPORT")) {
        return;
    }

    trace_hv_balloon_incoming_caps(msgCap->caps.caps);
    balloon->caps = msgCap->caps;

    memset(&respCap, 0, sizeof(respCap));
    respCap.hdr.type = DM_CAPABILITIES_RESPONSE;
    respCap.hdr.size = sizeof(respCap);
    respCap.hdr.trans_id = msgCap->hdr.trans_id;
    respCap.is_accepted = 1;
    respCap.hot_remove = 1;
    respCap.suppress_pressure_reports = !balloon->status_report.enabled;
    hv_balloon_send_packet(vmreq->chan, (struct dm_message *)&respCap);

    timer_mod(&balloon->post_init_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
              HV_BALLOON_POST_INIT_WAIT);

    HV_BALLOON_STATE_DESC_SET(stdesc, S_POST_INIT_WAIT);
}

static void hv_balloon_handle_status_report(HvBalloon *balloon,
                                            HvBalloonReq *req)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_status *msgStatus = vmreq->msg;

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgStatus),
                                    "DM_STATUS_REPORT")) {
        return;
    }

    if (!balloon->status_report.enabled) {
        return;
    }

    balloon->status_report.committed = msgStatus->num_committed;
    balloon->status_report.committed *= HV_BALLOON_PAGE_SIZE;
    balloon->status_report.available = msgStatus->num_avail;
    balloon->status_report.available *= HV_BALLOON_PAGE_SIZE;
    balloon->status_report.received = true;

    qapi_event_send_hv_balloon_status_report(balloon->status_report.committed,
                                             balloon->status_report.available);
}

HvBalloonInfo *qmp_query_hv_balloon_status_report(Error **errp)
{
    HvBalloon *balloon;
    HvBalloonInfo *info;

    balloon = HV_BALLOON(object_resolve_path_type("", TYPE_HV_BALLOON, NULL));
    if (!balloon) {
        error_setg(errp, "no %s device present", TYPE_HV_BALLOON);
        return NULL;
    }

    if (!balloon->status_report.enabled) {
        error_setg(errp, "guest memory status reporting not enabled");
        return NULL;
    }

    if (!balloon->status_report.received) {
        error_setg(errp, "no guest memory status report received yet");
        return NULL;
    }

    info = g_malloc0(sizeof(*info));
    info->committed = balloon->status_report.committed;
    info->available = balloon->status_report.available;
    return info;
}

static void hv_balloon_handle_unballoon_response(HvBalloon *balloon,
                                                 HvBalloonReq *req,
                                                 StateDesc *stdesc)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_unballoon_response *msgUrR = vmreq->msg;

    if (balloon->state != S_UNBALLOON_REPLY_WAIT) {
        warn_report("unexpected DM_UNBALLOON_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgUrR),
                                    "DM_UNBALLOON_RESPONSE"))
        return;

    trace_hv_balloon_incoming_unballoon(msgUrR->hdr.trans_id);

    balloon->trans_id++;

    if (balloon->hot_add_diff > 0) {
        bool can_hot_add = balloon->caps.cap_bits.hot_add;

        assert(can_hot_add);
        HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_SETUP);
    } else {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_IDLE);
    }
}

static void hv_balloon_handle_hot_add_response(HvBalloon *balloon,
                                               HvBalloonReq *req,
                                               StateDesc *stdesc)
{
    PageRange *hot_add_range = &balloon->hot_add_range;
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_hot_add_response *msgHaR = vmreq->msg;
    OurRange *our_range;

    if (balloon->state != S_HOT_ADD_REPLY_WAIT) {
        warn_report("unexpected DM_HOT_ADD_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    assert(balloon->our_range);
    our_range = OUR_RANGE(balloon->our_range);

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgHaR),
                                    "DM_HOT_ADD_RESPONSE"))
        return;

    trace_hv_balloon_incoming_hot_add(msgHaR->hdr.trans_id, msgHaR->result,
                                      msgHaR->page_count);

    balloon->trans_id++;

    if (msgHaR->result) {
        if (msgHaR->page_count > balloon->ha_current_count) {
            warn_report("DM_HOT_ADD_RESPONSE page count higher than requested (%"PRIu32" vs %"PRIu64")",
                        msgHaR->page_count, balloon->ha_current_count);
            msgHaR->page_count = balloon->ha_current_count;
        }

        hvb_our_range_mark_added(our_range, msgHaR->page_count);
        hot_add_range->start += msgHaR->page_count;
        hot_add_range->count -= msgHaR->page_count;
    }

    if (!msgHaR->result || msgHaR->page_count < balloon->ha_current_count) {
        /*
         * the current planned range was only partially hot-added, take note
         * how much of it remains and don't attempt any further hot adds
         */
        our_range_mark_remaining_unusable(our_range);

        goto ret_idle;
    }

    /* any pages remaining to hot-add in our range? */
    if (hot_add_range->count > 0) {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_HOT_ADD_RB_WAIT);
        return;
    }

ret_idle:
    HV_BALLOON_STATE_DESC_SET(stdesc, S_IDLE);
}

static void hv_balloon_handle_balloon_response(HvBalloon *balloon,
                                               HvBalloonReq *req,
                                               StateDesc *stdesc)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_balloon_response *msgBR = vmreq->msg;

    if (balloon->state != S_BALLOON_REPLY_WAIT) {
        warn_report("unexpected DM_BALLOON_RESPONSE in %d state",
                    balloon->state);
        return;
    }

    if (!hv_balloon_handle_msg_size(req, sizeof(*msgBR),
                                    "DM_BALLOON_RESPONSE"))
        return;

    trace_hv_balloon_incoming_balloon(msgBR->hdr.trans_id, msgBR->range_count,
                                      msgBR->more_pages);

    if (vmreq->msglen < sizeof(*msgBR) +
        (uint64_t)sizeof(msgBR->range_array[0]) * msgBR->range_count) {
        warn_report("DM_BALLOON_RESPONSE too short for the range count");
        return;
    }

    if (msgBR->range_count == 0) {
        /* The guest is already at its minimum size */
        balloon->balloon_diff = 0;
        goto ret_end_trans;
    } else {
        hv_balloon_handle_remove_ranges(balloon,
                                        msgBR->range_array,
                                        msgBR->range_count);
    }

    /* More responses expected? */
    if (msgBR->more_pages) {
        return;
    }

ret_end_trans:
    balloon->trans_id++;

    if (balloon->balloon_diff > 0) {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_BALLOON_RB_WAIT);
    } else {
        HV_BALLOON_STATE_DESC_SET(stdesc, S_IDLE);
    }
}

static void hv_balloon_handle_packet(HvBalloon *balloon, HvBalloonReq *req,
                                     StateDesc *stdesc)
{
    VMBusChanReq *vmreq = &req->vmreq;
    struct dm_message *msg = vmreq->msg;

    if (vmreq->msglen < sizeof(msg->hdr)) {
        return;
    }

    switch (msg->hdr.type) {
    case DM_VERSION_REQUEST:
        hv_balloon_handle_version_request(balloon, req, stdesc);
        break;

    case DM_CAPABILITIES_REPORT:
        hv_balloon_handle_caps_report(balloon, req, stdesc);
        break;

    case DM_STATUS_REPORT:
        hv_balloon_handle_status_report(balloon, req);
        break;

    case DM_MEM_HOT_ADD_RESPONSE:
        hv_balloon_handle_hot_add_response(balloon, req, stdesc);
        break;

    case DM_UNBALLOON_RESPONSE:
        hv_balloon_handle_unballoon_response(balloon, req, stdesc);
        break;

    case DM_BALLOON_RESPONSE:
        hv_balloon_handle_balloon_response(balloon, req, stdesc);
        break;

    default:
        warn_report("unknown DM message %u", msg->hdr.type);
        break;
    }
}

static bool hv_balloon_recv_channel(HvBalloon *balloon, StateDesc *stdesc)
{
    VMBusChannel *chan;
    HvBalloonReq *req;

    if (balloon->state == S_WAIT_RESET ||
        balloon->state == S_POST_RESET_CLOSED) {
        return false;
    }

    chan = hv_balloon_get_channel(balloon);
    if (vmbus_channel_recv_start(chan)) {
        return false;
    }

    while ((req = vmbus_channel_recv_peek(chan, sizeof(*req)))) {
        hv_balloon_handle_packet(balloon, req, stdesc);
        vmbus_free_req(req);
        vmbus_channel_recv_pop(chan);

        if (stdesc->state != S_NO_CHANGE) {
            break;
        }
    }

    return vmbus_channel_recv_done(chan) > 0;
}

/* old state handler -> new state transition (potential) */
static bool hv_balloon_event_loop_state(HvBalloon *balloon)
{
    StateDesc state_new = HV_BALLOON_STATE_DESC_INIT;

    hv_balloon_handle_state(balloon, &state_new);
    return hv_balloon_state_set(balloon, state_new.state, state_new.desc);
}

/* VMBus message -> new state transition (potential) */
static bool hv_balloon_event_loop_recv(HvBalloon *balloon)
{
    StateDesc state_new = HV_BALLOON_STATE_DESC_INIT;
    bool any_recv, state_changed;

    any_recv = hv_balloon_recv_channel(balloon, &state_new);
    state_changed = hv_balloon_state_set(balloon,
                                         state_new.state, state_new.desc);

    return state_changed || any_recv;
}

static void hv_balloon_event_loop(HvBalloon *balloon)
{
    bool state_repeat, recv_repeat;

    do {
        state_repeat = hv_balloon_event_loop_state(balloon);
        recv_repeat = hv_balloon_event_loop_recv(balloon);
    } while (state_repeat || recv_repeat);
}

static void hv_balloon_vmdev_chan_notify(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    hv_balloon_event_loop(balloon);
}

static void hv_balloon_stat(void *opaque, BalloonInfo *info)
{
    HvBalloon *balloon = opaque;
    info->actual = (hv_balloon_total_ram(balloon) - balloon->removed_both_ctr)
        << HV_BALLOON_PFN_SHIFT;
}

static void hv_balloon_to_target(void *opaque, ram_addr_t target)
{
    HvBalloon *balloon = opaque;
    uint64_t target_pages = target >> HV_BALLOON_PFN_SHIFT;

    if (!target_pages) {
        return;
    }

    /*
     * always set target_changed, even with unchanged target, as the user
     * might be asking us to try again reaching it
     */
    balloon->target = target_pages;
    balloon->target_changed = true;

    hv_balloon_event_loop(balloon);
}

static int hv_balloon_vmdev_open_channel(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    if (balloon->state != S_POST_RESET_CLOSED) {
        warn_report("guest trying to open a DM channel in invalid %d state",
                    balloon->state);
        return -EINVAL;
    }

    HV_BALLOON_SET_STATE(balloon, S_VERSION);
    hv_balloon_event_loop(balloon);

    return 0;
}

static void hv_balloon_vmdev_close_channel(VMBusChannel *chan)
{
    HvBalloon *balloon = HV_BALLOON(vmbus_channel_device(chan));

    timer_del(&balloon->post_init_timer);

    /* Don't report stale data */
    balloon->status_report.received = false;

    HV_BALLOON_SET_STATE(balloon, S_WAIT_RESET);
    hv_balloon_event_loop(balloon);
}

static void hv_balloon_post_init_timer(void *opaque)
{
    HvBalloon *balloon = opaque;

    if (balloon->state != S_POST_INIT_WAIT) {
        return;
    }

    HV_BALLOON_SET_STATE(balloon, S_IDLE);
    hv_balloon_event_loop(balloon);
}

static void hv_balloon_system_reset_unrealize_common(HvBalloon *balloon)
{
    g_clear_pointer(&balloon->our_range, hvb_our_range_memslots_free);
}

static void hv_balloon_system_reset(void *opaque)
{
    HvBalloon *balloon = HV_BALLOON(opaque);

    hv_balloon_system_reset_unrealize_common(balloon);
}

static void hv_balloon_ensure_mr(HvBalloon *balloon)
{
    MemoryRegion *hostmem_mr;

    assert(balloon->hostmem);

    if (balloon->mr) {
        return;
    }

    hostmem_mr = host_memory_backend_get_memory(balloon->hostmem);

    balloon->mr = g_new0(MemoryRegion, 1);
    memory_region_init(balloon->mr, OBJECT(balloon), TYPE_HV_BALLOON,
                       memory_region_size(hostmem_mr));
    balloon->mr->align = memory_region_get_alignment(hostmem_mr);
}

static void hv_balloon_free_mr(HvBalloon *balloon)
{
    if (!balloon->mr) {
        return;
    }

    object_unparent(OBJECT(balloon->mr));
    g_clear_pointer(&balloon->mr, g_free);
}

static void hv_balloon_vmdev_realize(VMBusDevice *vdev, Error **errp)
{
    ERRP_GUARD();
    HvBalloon *balloon = HV_BALLOON(vdev);
    int ret;

    balloon->state = S_WAIT_RESET;

    ret = qemu_add_balloon_handler(hv_balloon_to_target, hv_balloon_stat,
                                   balloon);
    if (ret < 0) {
        /* This also protects against having multiple hv-balloon instances */
        error_setg(errp, "Only one balloon device is supported");
        return;
    }

    if (balloon->hostmem) {
        if (host_memory_backend_is_mapped(balloon->hostmem)) {
            Object *obj = OBJECT(balloon->hostmem);

            error_setg(errp, "'%s' property specifies a busy memdev: %s",
                       HV_BALLOON_MEMDEV_PROP,
                       object_get_canonical_path_component(obj));
            goto out_balloon_handler;
        }

        hv_balloon_ensure_mr(balloon);

        /* This is rather unlikely to happen, but let's still check for it. */
        if (!QEMU_IS_ALIGNED(memory_region_size(balloon->mr),
                             HV_BALLOON_PAGE_SIZE)) {
            error_setg(errp, "'%s' property memdev size has to be a multiple of 0x%" PRIx64,
                       HV_BALLOON_MEMDEV_PROP, (uint64_t)HV_BALLOON_PAGE_SIZE);
            goto out_balloon_handler;
        }

        host_memory_backend_set_mapped(balloon->hostmem, true);
        vmstate_register_ram(host_memory_backend_get_memory(balloon->hostmem),
                             DEVICE(balloon));
    } else if (balloon->addr) {
        error_setg(errp, "'%s' property must not be set without a memdev",
                   HV_BALLOON_MEMDEV_PROP);
        goto out_balloon_handler;
    }

    timer_init_ms(&balloon->post_init_timer, QEMU_CLOCK_VIRTUAL,
                  hv_balloon_post_init_timer, balloon);

    qemu_register_reset(hv_balloon_system_reset, balloon);

    return;

out_balloon_handler:
    qemu_remove_balloon_handler(balloon);
}

/*
 * VMBus device reset has to be implemented in case the guest decides to
 * disconnect and reconnect to the VMBus without rebooting the whole system.
 *
 * However, the hot-added memory can't be removed here as Windows keeps on using
 * it until the system is restarted, even after disconnecting from the VMBus.
 */
static void hv_balloon_vmdev_reset(VMBusDevice *vdev)
{
    HvBalloon *balloon = HV_BALLOON(vdev);

    if (balloon->state == S_POST_RESET_CLOSED) {
        return;
    }

    if (balloon->our_range) {
        hvb_our_range_clear_removed_trees(OUR_RANGE(balloon->our_range));
    }

    hvb_page_range_tree_destroy(&balloon->removed_guest);
    hvb_page_range_tree_destroy(&balloon->removed_both);
    hvb_page_range_tree_init(&balloon->removed_guest);
    hvb_page_range_tree_init(&balloon->removed_both);

    balloon->trans_id = 0;
    balloon->removed_guest_ctr = 0;
    balloon->removed_both_ctr = 0;

    HV_BALLOON_SET_STATE(balloon, S_POST_RESET_CLOSED);
    hv_balloon_event_loop(balloon);
}

/*
 * Clean up things that were (possibly) allocated pre-realization, for example
 * from memory_device_pre_plug(), so we don't leak them if the device don't
 * actually get realized in the end.
 */
static void hv_balloon_unrealize_finalize_common(HvBalloon *balloon)
{
    hv_balloon_free_mr(balloon);
    balloon->addr = 0;

    balloon->memslot_count = 0;
}

static void hv_balloon_vmdev_unrealize(VMBusDevice *vdev)
{
    HvBalloon *balloon = HV_BALLOON(vdev);

    qemu_unregister_reset(hv_balloon_system_reset, balloon);

    hv_balloon_system_reset_unrealize_common(balloon);

    qemu_remove_balloon_handler(balloon);

    if (balloon->hostmem) {
        vmstate_unregister_ram(host_memory_backend_get_memory(balloon->hostmem),
                               DEVICE(balloon));
        host_memory_backend_set_mapped(balloon->hostmem, false);
    }

    hvb_page_range_tree_destroy(&balloon->removed_guest);
    hvb_page_range_tree_destroy(&balloon->removed_both);

    hv_balloon_unrealize_finalize_common(balloon);
}

static uint64_t hv_balloon_md_get_addr(const MemoryDeviceState *md)
{
    return object_property_get_uint(OBJECT(md), HV_BALLOON_ADDR_PROP,
                                    &error_abort);
}

static void hv_balloon_md_set_addr(MemoryDeviceState *md, uint64_t addr,
                                   Error **errp)
{
    object_property_set_uint(OBJECT(md), HV_BALLOON_ADDR_PROP, addr, errp);
}

static MemoryRegion *hv_balloon_md_get_memory_region(MemoryDeviceState *md,
                                                     Error **errp)
{
    HvBalloon *balloon = HV_BALLOON(md);

    if (!balloon->hostmem) {
        return NULL;
    }

    hv_balloon_ensure_mr(balloon);

    return balloon->mr;
}

static uint64_t hv_balloon_md_get_min_alignment(const MemoryDeviceState *md)
{
    /*
     * The VM can indicate an alignment up to 32 GiB. Memory device core can
     * usually only handle/guarantee 1 GiB alignment. The user will have to
     * specify a larger maxmem eventually.
     *
     * The memory device core will warn the user in case maxmem might have to be
     * increased and will fail plugging the device if there is not sufficient
     * space after alignment.
     *
     * TODO: we could do the alignment ourselves in a slightly bigger region.
     * But this feels better, although the warning might be annoying. Maybe
     * we can optimize that in the future (e.g., with such a device on the
     * cmdline place/size the device memory region differently.
     */
    return 32 * GiB;
}

static void hv_balloon_md_fill_device_info(const MemoryDeviceState *md,
                                           MemoryDeviceInfo *info)
{
    HvBalloonDeviceInfo *hi = g_new0(HvBalloonDeviceInfo, 1);
    const HvBalloon *balloon = HV_BALLOON(md);
    DeviceState *dev = DEVICE(md);

    if (dev->id) {
        hi->id = g_strdup(dev->id);
    }

    if (balloon->hostmem) {
        hi->memdev = object_get_canonical_path(OBJECT(balloon->hostmem));
        hi->memaddr = balloon->addr;
        hi->has_memaddr = true;
        hi->max_size = memory_region_size(balloon->mr);
        /* TODO: expose current provided size or something else? */
    } else {
        hi->max_size = 0;
    }

    info->u.hv_balloon.data = hi;
    info->type = MEMORY_DEVICE_INFO_KIND_HV_BALLOON;
}

static void hv_balloon_decide_memslots(MemoryDeviceState *md,
                                       unsigned int limit)
{
    HvBalloon *balloon = HV_BALLOON(md);
    MemoryRegion *hostmem_mr;
    uint64_t region_size, memslot_size, memslots;

    /* We're called exactly once, before realizing the device. */
    assert(!balloon->memslot_count);

    /* We should not be called if we don't have a memory backend */
    assert(balloon->hostmem);

    hostmem_mr = host_memory_backend_get_memory(balloon->hostmem);
    region_size = memory_region_size(hostmem_mr);

    assert(region_size > 0);
    memslot_size = QEMU_ALIGN_UP(region_size / limit,
                                 HV_BALLOON_HA_MEMSLOT_SIZE_ALIGN);
    memslots = QEMU_ALIGN_UP(region_size, memslot_size) / memslot_size;

    if (memslots > 1) {
        balloon->memslot_size = memslot_size;
    } else {
        balloon->memslot_size = region_size;
    }

    assert(memslots <= UINT_MAX);
    balloon->memslot_count = memslots;
}

static unsigned int hv_balloon_get_memslots(MemoryDeviceState *md)
{
    const HvBalloon *balloon = HV_BALLOON(md);

    /* We're called after setting the suggested limit. */
    assert(balloon->memslot_count > 0);

    return balloon->memslot_count;
}

static void hv_balloon_init(Object *obj)
{
}

static void hv_balloon_finalize(Object *obj)
{
    HvBalloon *balloon = HV_BALLOON(obj);

    hv_balloon_unrealize_finalize_common(balloon);
}

static const Property hv_balloon_properties[] = {
    DEFINE_PROP_BOOL("status-report", HvBalloon,
                     status_report.enabled, false),

    /* MEMORY_DEVICE props */
    DEFINE_PROP_LINK(HV_BALLOON_MEMDEV_PROP, HvBalloon, hostmem,
                     TYPE_MEMORY_BACKEND, HostMemoryBackend *),
    DEFINE_PROP_UINT64(HV_BALLOON_ADDR_PROP, HvBalloon, addr, 0),
};

static void hv_balloon_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VMBusDeviceClass *vdc = VMBUS_DEVICE_CLASS(klass);
    MemoryDeviceClass *mdc = MEMORY_DEVICE_CLASS(klass);

    device_class_set_props(dc, hv_balloon_properties);
    qemu_uuid_parse(HV_BALLOON_GUID, &vdc->classid);
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    vdc->vmdev_realize = hv_balloon_vmdev_realize;
    vdc->vmdev_unrealize = hv_balloon_vmdev_unrealize;
    vdc->vmdev_reset = hv_balloon_vmdev_reset;
    vdc->open_channel = hv_balloon_vmdev_open_channel;
    vdc->close_channel = hv_balloon_vmdev_close_channel;
    vdc->chan_notify_cb = hv_balloon_vmdev_chan_notify;

    mdc->get_addr = hv_balloon_md_get_addr;
    mdc->set_addr = hv_balloon_md_set_addr;
    mdc->get_plugged_size = memory_device_get_region_size;
    mdc->get_memory_region = hv_balloon_md_get_memory_region;
    mdc->decide_memslots = hv_balloon_decide_memslots;
    mdc->get_memslots = hv_balloon_get_memslots;
    mdc->get_min_alignment = hv_balloon_md_get_min_alignment;
    mdc->fill_device_info = hv_balloon_md_fill_device_info;
}
