/*
 *  xen paravirt block device backend
 *
 *  (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributions after 2012-01-13 are licensed under the terms of the
 *  GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include <sys/ioctl.h>
#include <sys/uio.h>

#include "hw/hw.h"
#include "hw/xen/xen_backend.h"
#include "xen_blkif.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"

/* ------------------------------------------------------------- */

static int batch_maps   = 0;

/* ------------------------------------------------------------- */

#define BLOCK_SIZE  512
#define IOCB_COUNT  (BLKIF_MAX_SEGMENTS_PER_REQUEST + 2)

struct PersistentGrant {
    void *page;
    struct XenBlkDev *blkdev;
};

typedef struct PersistentGrant PersistentGrant;

struct PersistentRegion {
    void *addr;
    int num;
};

typedef struct PersistentRegion PersistentRegion;

struct ioreq {
    blkif_request_t     req;
    int16_t             status;

    /* parsed request */
    off_t               start;
    QEMUIOVector        v;
    int                 presync;
    uint8_t             mapped;

    /* grant mapping */
    uint32_t            domids[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    uint32_t            refs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int                 prot;
    void                *page[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    void                *pages;
    int                 num_unmap;

    /* aio status */
    int                 aio_inflight;
    int                 aio_errors;

    struct XenBlkDev    *blkdev;
    QLIST_ENTRY(ioreq)   list;
    BlockAcctCookie     acct;
};

#define MAX_RING_PAGE_ORDER 4

struct XenBlkDev {
    struct XenDevice    xendev;  /* must be first */
    char                *params;
    char                *mode;
    char                *type;
    char                *dev;
    char                *devtype;
    bool                directiosafe;
    const char          *fileproto;
    const char          *filename;
    unsigned int        ring_ref[1 << MAX_RING_PAGE_ORDER];
    unsigned int        nr_ring_ref;
    void                *sring;
    int64_t             file_blk;
    int64_t             file_size;
    int                 protocol;
    blkif_back_rings_t  rings;
    int                 more_work;
    int                 cnt_map;

    /* request lists */
    QLIST_HEAD(inflight_head, ioreq) inflight;
    QLIST_HEAD(finished_head, ioreq) finished;
    QLIST_HEAD(freelist_head, ioreq) freelist;
    int                 requests_total;
    int                 requests_inflight;
    int                 requests_finished;
    unsigned int        max_requests;

    /* Persistent grants extension */
    gboolean            feature_discard;
    gboolean            feature_persistent;
    GTree               *persistent_gnts;
    GSList              *persistent_regions;
    unsigned int        persistent_gnt_count;
    unsigned int        max_grants;

    /* Grant copy */
    gboolean            feature_grant_copy;

    /* qemu block driver */
    DriveInfo           *dinfo;
    BlockBackend        *blk;
    QEMUBH              *bh;
};

/* ------------------------------------------------------------- */

static void ioreq_reset(struct ioreq *ioreq)
{
    memset(&ioreq->req, 0, sizeof(ioreq->req));
    ioreq->status = 0;
    ioreq->start = 0;
    ioreq->presync = 0;
    ioreq->mapped = 0;

    memset(ioreq->domids, 0, sizeof(ioreq->domids));
    memset(ioreq->refs, 0, sizeof(ioreq->refs));
    ioreq->prot = 0;
    memset(ioreq->page, 0, sizeof(ioreq->page));
    ioreq->pages = NULL;

    ioreq->aio_inflight = 0;
    ioreq->aio_errors = 0;

    ioreq->blkdev = NULL;
    memset(&ioreq->list, 0, sizeof(ioreq->list));
    memset(&ioreq->acct, 0, sizeof(ioreq->acct));

    qemu_iovec_reset(&ioreq->v);
}

static gint int_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
    uint ua = GPOINTER_TO_UINT(a);
    uint ub = GPOINTER_TO_UINT(b);
    return (ua > ub) - (ua < ub);
}

static void destroy_grant(gpointer pgnt)
{
    PersistentGrant *grant = pgnt;
    xengnttab_handle *gnt = grant->blkdev->xendev.gnttabdev;

    if (xengnttab_unmap(gnt, grant->page, 1) != 0) {
        xen_pv_printf(&grant->blkdev->xendev, 0,
                      "xengnttab_unmap failed: %s\n",
                      strerror(errno));
    }
    grant->blkdev->persistent_gnt_count--;
    xen_pv_printf(&grant->blkdev->xendev, 3,
                  "unmapped grant %p\n", grant->page);
    g_free(grant);
}

static void remove_persistent_region(gpointer data, gpointer dev)
{
    PersistentRegion *region = data;
    struct XenBlkDev *blkdev = dev;
    xengnttab_handle *gnt = blkdev->xendev.gnttabdev;

    if (xengnttab_unmap(gnt, region->addr, region->num) != 0) {
        xen_pv_printf(&blkdev->xendev, 0,
                      "xengnttab_unmap region %p failed: %s\n",
                      region->addr, strerror(errno));
    }
    xen_pv_printf(&blkdev->xendev, 3,
                  "unmapped grant region %p with %d pages\n",
                  region->addr, region->num);
    g_free(region);
}

static struct ioreq *ioreq_start(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq = NULL;

    if (QLIST_EMPTY(&blkdev->freelist)) {
        if (blkdev->requests_total >= blkdev->max_requests) {
            goto out;
        }
        /* allocate new struct */
        ioreq = g_malloc0(sizeof(*ioreq));
        ioreq->blkdev = blkdev;
        blkdev->requests_total++;
        qemu_iovec_init(&ioreq->v, BLKIF_MAX_SEGMENTS_PER_REQUEST);
    } else {
        /* get one from freelist */
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
    }
    QLIST_INSERT_HEAD(&blkdev->inflight, ioreq, list);
    blkdev->requests_inflight++;

out:
    return ioreq;
}

static void ioreq_finish(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    QLIST_REMOVE(ioreq, list);
    QLIST_INSERT_HEAD(&blkdev->finished, ioreq, list);
    blkdev->requests_inflight--;
    blkdev->requests_finished++;
}

static void ioreq_release(struct ioreq *ioreq, bool finish)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    QLIST_REMOVE(ioreq, list);
    ioreq_reset(ioreq);
    ioreq->blkdev = blkdev;
    QLIST_INSERT_HEAD(&blkdev->freelist, ioreq, list);
    if (finish) {
        blkdev->requests_finished--;
    } else {
        blkdev->requests_inflight--;
    }
}

/*
 * translate request into iovec + start offset
 * do sanity checks along the way
 */
static int ioreq_parse(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    uintptr_t mem;
    size_t len;
    int i;

    xen_pv_printf(&blkdev->xendev, 3,
                  "op %d, nr %d, handle %d, id %" PRId64 ", sector %" PRId64 "\n",
                  ioreq->req.operation, ioreq->req.nr_segments,
                  ioreq->req.handle, ioreq->req.id, ioreq->req.sector_number);
    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        ioreq->prot = PROT_WRITE; /* to memory */
        break;
    case BLKIF_OP_FLUSH_DISKCACHE:
        ioreq->presync = 1;
        if (!ioreq->req.nr_segments) {
            return 0;
        }
        /* fall through */
    case BLKIF_OP_WRITE:
        ioreq->prot = PROT_READ; /* from memory */
        break;
    case BLKIF_OP_DISCARD:
        return 0;
    default:
        xen_pv_printf(&blkdev->xendev, 0, "error: unknown operation (%d)\n",
                      ioreq->req.operation);
        goto err;
    };

    if (ioreq->req.operation != BLKIF_OP_READ && blkdev->mode[0] != 'w') {
        xen_pv_printf(&blkdev->xendev, 0, "error: write req for ro device\n");
        goto err;
    }

    ioreq->start = ioreq->req.sector_number * blkdev->file_blk;
    for (i = 0; i < ioreq->req.nr_segments; i++) {
        if (i == BLKIF_MAX_SEGMENTS_PER_REQUEST) {
            xen_pv_printf(&blkdev->xendev, 0, "error: nr_segments too big\n");
            goto err;
        }
        if (ioreq->req.seg[i].first_sect > ioreq->req.seg[i].last_sect) {
            xen_pv_printf(&blkdev->xendev, 0, "error: first > last sector\n");
            goto err;
        }
        if (ioreq->req.seg[i].last_sect * BLOCK_SIZE >= XC_PAGE_SIZE) {
            xen_pv_printf(&blkdev->xendev, 0, "error: page crossing\n");
            goto err;
        }

        ioreq->domids[i] = blkdev->xendev.dom;
        ioreq->refs[i]   = ioreq->req.seg[i].gref;

        mem = ioreq->req.seg[i].first_sect * blkdev->file_blk;
        len = (ioreq->req.seg[i].last_sect - ioreq->req.seg[i].first_sect + 1) * blkdev->file_blk;
        qemu_iovec_add(&ioreq->v, (void*)mem, len);
    }
    if (ioreq->start + ioreq->v.size > blkdev->file_size) {
        xen_pv_printf(&blkdev->xendev, 0, "error: access beyond end of file\n");
        goto err;
    }
    return 0;

err:
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static void ioreq_unmap(struct ioreq *ioreq)
{
    xengnttab_handle *gnt = ioreq->blkdev->xendev.gnttabdev;
    int i;

    if (ioreq->num_unmap == 0 || ioreq->mapped == 0) {
        return;
    }
    if (batch_maps) {
        if (!ioreq->pages) {
            return;
        }
        if (xengnttab_unmap(gnt, ioreq->pages, ioreq->num_unmap) != 0) {
            xen_pv_printf(&ioreq->blkdev->xendev, 0,
                          "xengnttab_unmap failed: %s\n",
                          strerror(errno));
        }
        ioreq->blkdev->cnt_map -= ioreq->num_unmap;
        ioreq->pages = NULL;
    } else {
        for (i = 0; i < ioreq->num_unmap; i++) {
            if (!ioreq->page[i]) {
                continue;
            }
            if (xengnttab_unmap(gnt, ioreq->page[i], 1) != 0) {
                xen_pv_printf(&ioreq->blkdev->xendev, 0,
                              "xengnttab_unmap failed: %s\n",
                              strerror(errno));
            }
            ioreq->blkdev->cnt_map--;
            ioreq->page[i] = NULL;
        }
    }
    ioreq->mapped = 0;
}

static int ioreq_map(struct ioreq *ioreq)
{
    xengnttab_handle *gnt = ioreq->blkdev->xendev.gnttabdev;
    uint32_t domids[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    uint32_t refs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    void *page[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int i, j, new_maps = 0;
    PersistentGrant *grant;
    PersistentRegion *region;
    /* domids and refs variables will contain the information necessary
     * to map the grants that are needed to fulfill this request.
     *
     * After mapping the needed grants, the page array will contain the
     * memory address of each granted page in the order specified in ioreq
     * (disregarding if it's a persistent grant or not).
     */

    if (ioreq->v.niov == 0 || ioreq->mapped == 1) {
        return 0;
    }
    if (ioreq->blkdev->feature_persistent) {
        for (i = 0; i < ioreq->v.niov; i++) {
            grant = g_tree_lookup(ioreq->blkdev->persistent_gnts,
                                    GUINT_TO_POINTER(ioreq->refs[i]));

            if (grant != NULL) {
                page[i] = grant->page;
                xen_pv_printf(&ioreq->blkdev->xendev, 3,
                              "using persistent-grant %" PRIu32 "\n",
                              ioreq->refs[i]);
            } else {
                    /* Add the grant to the list of grants that
                     * should be mapped
                     */
                    domids[new_maps] = ioreq->domids[i];
                    refs[new_maps] = ioreq->refs[i];
                    page[i] = NULL;
                    new_maps++;
            }
        }
        /* Set the protection to RW, since grants may be reused later
         * with a different protection than the one needed for this request
         */
        ioreq->prot = PROT_WRITE | PROT_READ;
    } else {
        /* All grants in the request should be mapped */
        memcpy(refs, ioreq->refs, sizeof(refs));
        memcpy(domids, ioreq->domids, sizeof(domids));
        memset(page, 0, sizeof(page));
        new_maps = ioreq->v.niov;
    }

    if (batch_maps && new_maps) {
        ioreq->pages = xengnttab_map_grant_refs
            (gnt, new_maps, domids, refs, ioreq->prot);
        if (ioreq->pages == NULL) {
            xen_pv_printf(&ioreq->blkdev->xendev, 0,
                          "can't map %d grant refs (%s, %d maps)\n",
                          new_maps, strerror(errno), ioreq->blkdev->cnt_map);
            return -1;
        }
        for (i = 0, j = 0; i < ioreq->v.niov; i++) {
            if (page[i] == NULL) {
                page[i] = ioreq->pages + (j++) * XC_PAGE_SIZE;
            }
        }
        ioreq->blkdev->cnt_map += new_maps;
    } else if (new_maps)  {
        for (i = 0; i < new_maps; i++) {
            ioreq->page[i] = xengnttab_map_grant_ref
                (gnt, domids[i], refs[i], ioreq->prot);
            if (ioreq->page[i] == NULL) {
                xen_pv_printf(&ioreq->blkdev->xendev, 0,
                              "can't map grant ref %d (%s, %d maps)\n",
                              refs[i], strerror(errno), ioreq->blkdev->cnt_map);
                ioreq->mapped = 1;
                ioreq_unmap(ioreq);
                return -1;
            }
            ioreq->blkdev->cnt_map++;
        }
        for (i = 0, j = 0; i < ioreq->v.niov; i++) {
            if (page[i] == NULL) {
                page[i] = ioreq->page[j++];
            }
        }
    }
    if (ioreq->blkdev->feature_persistent && new_maps != 0 &&
        (!batch_maps || (ioreq->blkdev->persistent_gnt_count + new_maps <=
        ioreq->blkdev->max_grants))) {
        /*
         * If we are using persistent grants and batch mappings only
         * add the new maps to the list of persistent grants if the whole
         * area can be persistently mapped.
         */
        if (batch_maps) {
            region = g_malloc0(sizeof(*region));
            region->addr = ioreq->pages;
            region->num = new_maps;
            ioreq->blkdev->persistent_regions = g_slist_append(
                                            ioreq->blkdev->persistent_regions,
                                            region);
        }
        while ((ioreq->blkdev->persistent_gnt_count < ioreq->blkdev->max_grants)
              && new_maps) {
            /* Go through the list of newly mapped grants and add as many
             * as possible to the list of persistently mapped grants.
             *
             * Since we start at the end of ioreq->page(s), we only need
             * to decrease new_maps to prevent this granted pages from
             * being unmapped in ioreq_unmap.
             */
            grant = g_malloc0(sizeof(*grant));
            new_maps--;
            if (batch_maps) {
                grant->page = ioreq->pages + (new_maps) * XC_PAGE_SIZE;
            } else {
                grant->page = ioreq->page[new_maps];
            }
            grant->blkdev = ioreq->blkdev;
            xen_pv_printf(&ioreq->blkdev->xendev, 3,
                          "adding grant %" PRIu32 " page: %p\n",
                          refs[new_maps], grant->page);
            g_tree_insert(ioreq->blkdev->persistent_gnts,
                          GUINT_TO_POINTER(refs[new_maps]),
                          grant);
            ioreq->blkdev->persistent_gnt_count++;
        }
        assert(!batch_maps || new_maps == 0);
    }
    for (i = 0; i < ioreq->v.niov; i++) {
        ioreq->v.iov[i].iov_base += (uintptr_t)page[i];
    }
    ioreq->mapped = 1;
    ioreq->num_unmap = new_maps;
    return 0;
}

#if CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40800

static void ioreq_free_copy_buffers(struct ioreq *ioreq)
{
    int i;

    for (i = 0; i < ioreq->v.niov; i++) {
        ioreq->page[i] = NULL;
    }

    qemu_vfree(ioreq->pages);
}

static int ioreq_init_copy_buffers(struct ioreq *ioreq)
{
    int i;

    if (ioreq->v.niov == 0) {
        return 0;
    }

    ioreq->pages = qemu_memalign(XC_PAGE_SIZE, ioreq->v.niov * XC_PAGE_SIZE);

    for (i = 0; i < ioreq->v.niov; i++) {
        ioreq->page[i] = ioreq->pages + i * XC_PAGE_SIZE;
        ioreq->v.iov[i].iov_base = ioreq->page[i];
    }

    return 0;
}

static int ioreq_grant_copy(struct ioreq *ioreq)
{
    xengnttab_handle *gnt = ioreq->blkdev->xendev.gnttabdev;
    xengnttab_grant_copy_segment_t segs[BLKIF_MAX_SEGMENTS_PER_REQUEST];
    int i, count, rc;
    int64_t file_blk = ioreq->blkdev->file_blk;

    if (ioreq->v.niov == 0) {
        return 0;
    }

    count = ioreq->v.niov;

    for (i = 0; i < count; i++) {
        if (ioreq->req.operation == BLKIF_OP_READ) {
            segs[i].flags = GNTCOPY_dest_gref;
            segs[i].dest.foreign.ref = ioreq->refs[i];
            segs[i].dest.foreign.domid = ioreq->domids[i];
            segs[i].dest.foreign.offset = ioreq->req.seg[i].first_sect * file_blk;
            segs[i].source.virt = ioreq->v.iov[i].iov_base;
        } else {
            segs[i].flags = GNTCOPY_source_gref;
            segs[i].source.foreign.ref = ioreq->refs[i];
            segs[i].source.foreign.domid = ioreq->domids[i];
            segs[i].source.foreign.offset = ioreq->req.seg[i].first_sect * file_blk;
            segs[i].dest.virt = ioreq->v.iov[i].iov_base;
        }
        segs[i].len = (ioreq->req.seg[i].last_sect
                       - ioreq->req.seg[i].first_sect + 1) * file_blk;
    }

    rc = xengnttab_grant_copy(gnt, count, segs);

    if (rc) {
        xen_pv_printf(&ioreq->blkdev->xendev, 0,
                      "failed to copy data %d\n", rc);
        ioreq->aio_errors++;
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (segs[i].status != GNTST_okay) {
            xen_pv_printf(&ioreq->blkdev->xendev, 3,
                          "failed to copy data %d for gref %d, domid %d\n",
                          segs[i].status, ioreq->refs[i], ioreq->domids[i]);
            ioreq->aio_errors++;
            rc = -1;
        }
    }

    return rc;
}
#else
static void ioreq_free_copy_buffers(struct ioreq *ioreq)
{
    abort();
}

static int ioreq_init_copy_buffers(struct ioreq *ioreq)
{
    abort();
}

static int ioreq_grant_copy(struct ioreq *ioreq)
{
    abort();
}
#endif

static int ioreq_runio_qemu_aio(struct ioreq *ioreq);

static void qemu_aio_complete(void *opaque, int ret)
{
    struct ioreq *ioreq = opaque;

    if (ret != 0) {
        xen_pv_printf(&ioreq->blkdev->xendev, 0, "%s I/O error\n",
                      ioreq->req.operation == BLKIF_OP_READ ? "read" : "write");
        ioreq->aio_errors++;
    }

    ioreq->aio_inflight--;
    if (ioreq->presync) {
        ioreq->presync = 0;
        ioreq_runio_qemu_aio(ioreq);
        return;
    }
    if (ioreq->aio_inflight > 0) {
        return;
    }

    if (ioreq->blkdev->feature_grant_copy) {
        switch (ioreq->req.operation) {
        case BLKIF_OP_READ:
            /* in case of failure ioreq->aio_errors is increased */
            if (ret == 0) {
                ioreq_grant_copy(ioreq);
            }
            ioreq_free_copy_buffers(ioreq);
            break;
        case BLKIF_OP_WRITE:
        case BLKIF_OP_FLUSH_DISKCACHE:
            if (!ioreq->req.nr_segments) {
                break;
            }
            ioreq_free_copy_buffers(ioreq);
            break;
        default:
            break;
        }
    }

    ioreq->status = ioreq->aio_errors ? BLKIF_RSP_ERROR : BLKIF_RSP_OKAY;
    if (!ioreq->blkdev->feature_grant_copy) {
        ioreq_unmap(ioreq);
    }
    ioreq_finish(ioreq);
    switch (ioreq->req.operation) {
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }
    case BLKIF_OP_READ:
        if (ioreq->status == BLKIF_RSP_OKAY) {
            block_acct_done(blk_get_stats(ioreq->blkdev->blk), &ioreq->acct);
        } else {
            block_acct_failed(blk_get_stats(ioreq->blkdev->blk), &ioreq->acct);
        }
        break;
    case BLKIF_OP_DISCARD:
    default:
        break;
    }
    qemu_bh_schedule(ioreq->blkdev->bh);
}

static bool blk_split_discard(struct ioreq *ioreq, blkif_sector_t sector_number,
                              uint64_t nr_sectors)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;
    int64_t byte_offset;
    int byte_chunk;
    uint64_t byte_remaining, limit;
    uint64_t sec_start = sector_number;
    uint64_t sec_count = nr_sectors;

    /* Wrap around, or overflowing byte limit? */
    if (sec_start + sec_count < sec_count ||
        sec_start + sec_count > INT64_MAX >> BDRV_SECTOR_BITS) {
        return false;
    }

    limit = BDRV_REQUEST_MAX_SECTORS << BDRV_SECTOR_BITS;
    byte_offset = sec_start << BDRV_SECTOR_BITS;
    byte_remaining = sec_count << BDRV_SECTOR_BITS;

    do {
        byte_chunk = byte_remaining > limit ? limit : byte_remaining;
        ioreq->aio_inflight++;
        blk_aio_pdiscard(blkdev->blk, byte_offset, byte_chunk,
                         qemu_aio_complete, ioreq);
        byte_remaining -= byte_chunk;
        byte_offset += byte_chunk;
    } while (byte_remaining > 0);

    return true;
}

static int ioreq_runio_qemu_aio(struct ioreq *ioreq)
{
    struct XenBlkDev *blkdev = ioreq->blkdev;

    if (ioreq->blkdev->feature_grant_copy) {
        ioreq_init_copy_buffers(ioreq);
        if (ioreq->req.nr_segments && (ioreq->req.operation == BLKIF_OP_WRITE ||
            ioreq->req.operation == BLKIF_OP_FLUSH_DISKCACHE) &&
            ioreq_grant_copy(ioreq)) {
                ioreq_free_copy_buffers(ioreq);
                goto err;
        }
    } else {
        if (ioreq->req.nr_segments && ioreq_map(ioreq)) {
            goto err;
        }
    }

    ioreq->aio_inflight++;
    if (ioreq->presync) {
        blk_aio_flush(ioreq->blkdev->blk, qemu_aio_complete, ioreq);
        return 0;
    }

    switch (ioreq->req.operation) {
    case BLKIF_OP_READ:
        block_acct_start(blk_get_stats(blkdev->blk), &ioreq->acct,
                         ioreq->v.size, BLOCK_ACCT_READ);
        ioreq->aio_inflight++;
        blk_aio_preadv(blkdev->blk, ioreq->start, &ioreq->v, 0,
                       qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_WRITE:
    case BLKIF_OP_FLUSH_DISKCACHE:
        if (!ioreq->req.nr_segments) {
            break;
        }

        block_acct_start(blk_get_stats(blkdev->blk), &ioreq->acct,
                         ioreq->v.size,
                         ioreq->req.operation == BLKIF_OP_WRITE ?
                         BLOCK_ACCT_WRITE : BLOCK_ACCT_FLUSH);
        ioreq->aio_inflight++;
        blk_aio_pwritev(blkdev->blk, ioreq->start, &ioreq->v, 0,
                        qemu_aio_complete, ioreq);
        break;
    case BLKIF_OP_DISCARD:
    {
        struct blkif_request_discard *req = (void *)&ioreq->req;
        if (!blk_split_discard(ioreq, req->sector_number, req->nr_sectors)) {
            goto err;
        }
        break;
    }
    default:
        /* unknown operation (shouldn't happen -- parse catches this) */
        if (!ioreq->blkdev->feature_grant_copy) {
            ioreq_unmap(ioreq);
        }
        goto err;
    }

    qemu_aio_complete(ioreq, 0);

    return 0;

err:
    ioreq_finish(ioreq);
    ioreq->status = BLKIF_RSP_ERROR;
    return -1;
}

static int blk_send_response_one(struct ioreq *ioreq)
{
    struct XenBlkDev  *blkdev = ioreq->blkdev;
    int               send_notify   = 0;
    int               have_requests = 0;
    blkif_response_t  *resp;

    /* Place on the response ring for the relevant domain. */
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        resp = (blkif_response_t *) RING_GET_RESPONSE(&blkdev->rings.native,
                                 blkdev->rings.native.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_32:
        resp = (blkif_response_t *) RING_GET_RESPONSE(&blkdev->rings.x86_32_part,
                                 blkdev->rings.x86_32_part.rsp_prod_pvt);
        break;
    case BLKIF_PROTOCOL_X86_64:
        resp = (blkif_response_t *) RING_GET_RESPONSE(&blkdev->rings.x86_64_part,
                                 blkdev->rings.x86_64_part.rsp_prod_pvt);
        break;
    default:
        return 0;
    }

    resp->id        = ioreq->req.id;
    resp->operation = ioreq->req.operation;
    resp->status    = ioreq->status;

    blkdev->rings.common.rsp_prod_pvt++;

    RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&blkdev->rings.common, send_notify);
    if (blkdev->rings.common.rsp_prod_pvt == blkdev->rings.common.req_cons) {
        /*
         * Tail check for pending requests. Allows frontend to avoid
         * notifications if requests are already in flight (lower
         * overheads and promotes batching).
         */
        RING_FINAL_CHECK_FOR_REQUESTS(&blkdev->rings.common, have_requests);
    } else if (RING_HAS_UNCONSUMED_REQUESTS(&blkdev->rings.common)) {
        have_requests = 1;
    }

    if (have_requests) {
        blkdev->more_work++;
    }
    return send_notify;
}

/* walk finished list, send outstanding responses, free requests */
static void blk_send_response_all(struct XenBlkDev *blkdev)
{
    struct ioreq *ioreq;
    int send_notify = 0;

    while (!QLIST_EMPTY(&blkdev->finished)) {
        ioreq = QLIST_FIRST(&blkdev->finished);
        send_notify += blk_send_response_one(ioreq);
        ioreq_release(ioreq, true);
    }
    if (send_notify) {
        xen_pv_send_notify(&blkdev->xendev);
    }
}

static int blk_get_request(struct XenBlkDev *blkdev, struct ioreq *ioreq, RING_IDX rc)
{
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
        memcpy(&ioreq->req, RING_GET_REQUEST(&blkdev->rings.native, rc),
               sizeof(ioreq->req));
        break;
    case BLKIF_PROTOCOL_X86_32:
        blkif_get_x86_32_req(&ioreq->req,
                             RING_GET_REQUEST(&blkdev->rings.x86_32_part, rc));
        break;
    case BLKIF_PROTOCOL_X86_64:
        blkif_get_x86_64_req(&ioreq->req,
                             RING_GET_REQUEST(&blkdev->rings.x86_64_part, rc));
        break;
    }
    /* Prevent the compiler from accessing the on-ring fields instead. */
    barrier();
    return 0;
}

static void blk_handle_requests(struct XenBlkDev *blkdev)
{
    RING_IDX rc, rp;
    struct ioreq *ioreq;

    blkdev->more_work = 0;

    rc = blkdev->rings.common.req_cons;
    rp = blkdev->rings.common.sring->req_prod;
    xen_rmb(); /* Ensure we see queued requests up to 'rp'. */

    blk_send_response_all(blkdev);
    while (rc != rp) {
        /* pull request from ring */
        if (RING_REQUEST_CONS_OVERFLOW(&blkdev->rings.common, rc)) {
            break;
        }
        ioreq = ioreq_start(blkdev);
        if (ioreq == NULL) {
            blkdev->more_work++;
            break;
        }
        blk_get_request(blkdev, ioreq, rc);
        blkdev->rings.common.req_cons = ++rc;

        /* parse them */
        if (ioreq_parse(ioreq) != 0) {

            switch (ioreq->req.operation) {
            case BLKIF_OP_READ:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_READ);
                break;
            case BLKIF_OP_WRITE:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_WRITE);
                break;
            case BLKIF_OP_FLUSH_DISKCACHE:
                block_acct_invalid(blk_get_stats(blkdev->blk),
                                   BLOCK_ACCT_FLUSH);
            default:
                break;
            };

            if (blk_send_response_one(ioreq)) {
                xen_pv_send_notify(&blkdev->xendev);
            }
            ioreq_release(ioreq, false);
            continue;
        }

        ioreq_runio_qemu_aio(ioreq);
    }

    if (blkdev->more_work && blkdev->requests_inflight < blkdev->max_requests) {
        qemu_bh_schedule(blkdev->bh);
    }
}

/* ------------------------------------------------------------- */

static void blk_bh(void *opaque)
{
    struct XenBlkDev *blkdev = opaque;
    blk_handle_requests(blkdev);
}

static void blk_alloc(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    QLIST_INIT(&blkdev->inflight);
    QLIST_INIT(&blkdev->finished);
    QLIST_INIT(&blkdev->freelist);
    blkdev->bh = qemu_bh_new(blk_bh, blkdev);
    if (xen_mode != XEN_EMULATE) {
        batch_maps = 1;
    }
}

static void blk_parse_discard(struct XenBlkDev *blkdev)
{
    int enable;

    blkdev->feature_discard = true;

    if (xenstore_read_be_int(&blkdev->xendev, "discard-enable", &enable) == 0) {
        blkdev->feature_discard = !!enable;
    }

    if (blkdev->feature_discard) {
        xenstore_write_be_int(&blkdev->xendev, "feature-discard", 1);
    }
}

static int blk_init(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);
    int info = 0;
    char *directiosafe = NULL;

    /* read xenstore entries */
    if (blkdev->params == NULL) {
        char *h = NULL;
        blkdev->params = xenstore_read_be_str(&blkdev->xendev, "params");
        if (blkdev->params != NULL) {
            h = strchr(blkdev->params, ':');
        }
        if (h != NULL) {
            blkdev->fileproto = blkdev->params;
            blkdev->filename  = h+1;
            *h = 0;
        } else {
            blkdev->fileproto = "<unset>";
            blkdev->filename  = blkdev->params;
        }
    }
    if (!strcmp("aio", blkdev->fileproto)) {
        blkdev->fileproto = "raw";
    }
    if (!strcmp("vhd", blkdev->fileproto)) {
        blkdev->fileproto = "vpc";
    }
    if (blkdev->mode == NULL) {
        blkdev->mode = xenstore_read_be_str(&blkdev->xendev, "mode");
    }
    if (blkdev->type == NULL) {
        blkdev->type = xenstore_read_be_str(&blkdev->xendev, "type");
    }
    if (blkdev->dev == NULL) {
        blkdev->dev = xenstore_read_be_str(&blkdev->xendev, "dev");
    }
    if (blkdev->devtype == NULL) {
        blkdev->devtype = xenstore_read_be_str(&blkdev->xendev, "device-type");
    }
    directiosafe = xenstore_read_be_str(&blkdev->xendev, "direct-io-safe");
    blkdev->directiosafe = (directiosafe && atoi(directiosafe));

    /* do we have all we need? */
    if (blkdev->params == NULL ||
        blkdev->mode == NULL   ||
        blkdev->type == NULL   ||
        blkdev->dev == NULL) {
        goto out_error;
    }

    /* read-only ? */
    if (strcmp(blkdev->mode, "w")) {
        info  |= VDISK_READONLY;
    }

    /* cdrom ? */
    if (blkdev->devtype && !strcmp(blkdev->devtype, "cdrom")) {
        info  |= VDISK_CDROM;
    }

    blkdev->file_blk  = BLOCK_SIZE;

    blkdev->feature_grant_copy =
                (xengnttab_grant_copy(blkdev->xendev.gnttabdev, 0, NULL) == 0);

    xen_pv_printf(&blkdev->xendev, 3, "grant copy operation %s\n",
                  blkdev->feature_grant_copy ? "enabled" : "disabled");

    /* fill info
     * blk_connect supplies sector-size and sectors
     */
    xenstore_write_be_int(&blkdev->xendev, "feature-flush-cache", 1);
    xenstore_write_be_int(&blkdev->xendev, "feature-persistent",
                          !blkdev->feature_grant_copy);
    xenstore_write_be_int(&blkdev->xendev, "info", info);

    xenstore_write_be_int(&blkdev->xendev, "max-ring-page-order",
                          MAX_RING_PAGE_ORDER);

    blk_parse_discard(blkdev);

    g_free(directiosafe);
    return 0;

out_error:
    g_free(blkdev->params);
    blkdev->params = NULL;
    g_free(blkdev->mode);
    blkdev->mode = NULL;
    g_free(blkdev->type);
    blkdev->type = NULL;
    g_free(blkdev->dev);
    blkdev->dev = NULL;
    g_free(blkdev->devtype);
    blkdev->devtype = NULL;
    g_free(directiosafe);
    blkdev->directiosafe = false;
    return -1;
}

/*
 * We need to account for the grant allocations requiring contiguous
 * chunks; the worst case number would be
 *     max_req * max_seg + (max_req - 1) * (max_seg - 1) + 1,
 * but in order to keep things simple just use
 *     2 * max_req * max_seg.
 */
#define MAX_GRANTS(max_req, max_seg) (2 * (max_req) * (max_seg))

static int blk_connect(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);
    int pers, index, qflags;
    bool readonly = true;
    bool writethrough = true;
    int order, ring_ref;
    unsigned int ring_size, max_grants;
    unsigned int i;
    uint32_t *domids;

    /* read-only ? */
    if (blkdev->directiosafe) {
        qflags = BDRV_O_NOCACHE | BDRV_O_NATIVE_AIO;
    } else {
        qflags = 0;
        writethrough = false;
    }
    if (strcmp(blkdev->mode, "w") == 0) {
        qflags |= BDRV_O_RDWR;
        readonly = false;
    }
    if (blkdev->feature_discard) {
        qflags |= BDRV_O_UNMAP;
    }

    /* init qemu block driver */
    index = (blkdev->xendev.dev - 202 * 256) / 16;
    blkdev->dinfo = drive_get(IF_XEN, 0, index);
    if (!blkdev->dinfo) {
        Error *local_err = NULL;
        QDict *options = NULL;

        if (strcmp(blkdev->fileproto, "<unset>")) {
            options = qdict_new();
            qdict_put_str(options, "driver", blkdev->fileproto);
        }

        /* setup via xenbus -> create new block driver instance */
        xen_pv_printf(&blkdev->xendev, 2, "create new bdrv (xenbus setup)\n");
        blkdev->blk = blk_new_open(blkdev->filename, NULL, options,
                                   qflags, &local_err);
        if (!blkdev->blk) {
            xen_pv_printf(&blkdev->xendev, 0, "error: %s\n",
                          error_get_pretty(local_err));
            error_free(local_err);
            return -1;
        }
        blk_set_enable_write_cache(blkdev->blk, !writethrough);
    } else {
        /* setup via qemu cmdline -> already setup for us */
        xen_pv_printf(&blkdev->xendev, 2,
                      "get configured bdrv (cmdline setup)\n");
        blkdev->blk = blk_by_legacy_dinfo(blkdev->dinfo);
        if (blk_is_read_only(blkdev->blk) && !readonly) {
            xen_pv_printf(&blkdev->xendev, 0, "Unexpected read-only drive");
            blkdev->blk = NULL;
            return -1;
        }
        /* blkdev->blk is not create by us, we get a reference
         * so we can blk_unref() unconditionally */
        blk_ref(blkdev->blk);
    }
    blk_attach_dev_legacy(blkdev->blk, blkdev);
    blkdev->file_size = blk_getlength(blkdev->blk);
    if (blkdev->file_size < 0) {
        BlockDriverState *bs = blk_bs(blkdev->blk);
        const char *drv_name = bs ? bdrv_get_format_name(bs) : NULL;
        xen_pv_printf(&blkdev->xendev, 1, "blk_getlength: %d (%s) | drv %s\n",
                      (int)blkdev->file_size, strerror(-blkdev->file_size),
                      drv_name ?: "-");
        blkdev->file_size = 0;
    }

    xen_pv_printf(xendev, 1, "type \"%s\", fileproto \"%s\", filename \"%s\","
                  " size %" PRId64 " (%" PRId64 " MB)\n",
                  blkdev->type, blkdev->fileproto, blkdev->filename,
                  blkdev->file_size, blkdev->file_size >> 20);

    /* Fill in number of sector size and number of sectors */
    xenstore_write_be_int(&blkdev->xendev, "sector-size", blkdev->file_blk);
    xenstore_write_be_int64(&blkdev->xendev, "sectors",
                            blkdev->file_size / blkdev->file_blk);

    if (xenstore_read_fe_int(&blkdev->xendev, "ring-page-order",
                             &order) == -1) {
        blkdev->nr_ring_ref = 1;

        if (xenstore_read_fe_int(&blkdev->xendev, "ring-ref",
                                 &ring_ref) == -1) {
            return -1;
        }
        blkdev->ring_ref[0] = ring_ref;

    } else if (order >= 0 && order <= MAX_RING_PAGE_ORDER) {
        blkdev->nr_ring_ref = 1 << order;

        for (i = 0; i < blkdev->nr_ring_ref; i++) {
            char *key;

            key = g_strdup_printf("ring-ref%u", i);
            if (!key) {
                return -1;
            }

            if (xenstore_read_fe_int(&blkdev->xendev, key,
                                     &ring_ref) == -1) {
                g_free(key);
                return -1;
            }
            blkdev->ring_ref[i] = ring_ref;

            g_free(key);
        }
    } else {
        xen_pv_printf(xendev, 0, "invalid ring-page-order: %d\n",
                      order);
        return -1;
    }

    if (xenstore_read_fe_int(&blkdev->xendev, "event-channel",
                             &blkdev->xendev.remote_port) == -1) {
        return -1;
    }
    if (xenstore_read_fe_int(&blkdev->xendev, "feature-persistent", &pers)) {
        blkdev->feature_persistent = FALSE;
    } else {
        blkdev->feature_persistent = !!pers;
    }

    if (!blkdev->xendev.protocol) {
        blkdev->protocol = BLKIF_PROTOCOL_NATIVE;
    } else if (strcmp(blkdev->xendev.protocol, XEN_IO_PROTO_ABI_NATIVE) == 0) {
        blkdev->protocol = BLKIF_PROTOCOL_NATIVE;
    } else if (strcmp(blkdev->xendev.protocol, XEN_IO_PROTO_ABI_X86_32) == 0) {
        blkdev->protocol = BLKIF_PROTOCOL_X86_32;
    } else if (strcmp(blkdev->xendev.protocol, XEN_IO_PROTO_ABI_X86_64) == 0) {
        blkdev->protocol = BLKIF_PROTOCOL_X86_64;
    } else {
        blkdev->protocol = BLKIF_PROTOCOL_NATIVE;
    }

    ring_size = XC_PAGE_SIZE * blkdev->nr_ring_ref;
    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif_x86_32, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkdev->max_requests = __CONST_RING_SIZE(blkif_x86_64, ring_size);
        break;
    }
    default:
        return -1;
    }

    /* Calculate the maximum number of grants needed by ioreqs */
    max_grants = MAX_GRANTS(blkdev->max_requests,
                            BLKIF_MAX_SEGMENTS_PER_REQUEST);
    /* Add on the number needed for the ring pages */
    max_grants += blkdev->nr_ring_ref;

    if (xengnttab_set_max_grants(blkdev->xendev.gnttabdev, max_grants)) {
        xen_pv_printf(xendev, 0, "xengnttab_set_max_grants failed: %s\n",
                      strerror(errno));
        return -1;
    }

    domids = g_malloc0_n(blkdev->nr_ring_ref, sizeof(uint32_t));
    for (i = 0; i < blkdev->nr_ring_ref; i++) {
        domids[i] = blkdev->xendev.dom;
    }

    blkdev->sring = xengnttab_map_grant_refs(blkdev->xendev.gnttabdev,
                                             blkdev->nr_ring_ref,
                                             domids,
                                             blkdev->ring_ref,
                                             PROT_READ | PROT_WRITE);

    g_free(domids);

    if (!blkdev->sring) {
        return -1;
    }

    blkdev->cnt_map++;

    switch (blkdev->protocol) {
    case BLKIF_PROTOCOL_NATIVE:
    {
        blkif_sring_t *sring_native = blkdev->sring;
        BACK_RING_INIT(&blkdev->rings.native, sring_native, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_32:
    {
        blkif_x86_32_sring_t *sring_x86_32 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_32_part, sring_x86_32, ring_size);
        break;
    }
    case BLKIF_PROTOCOL_X86_64:
    {
        blkif_x86_64_sring_t *sring_x86_64 = blkdev->sring;

        BACK_RING_INIT(&blkdev->rings.x86_64_part, sring_x86_64, ring_size);
        break;
    }
    }

    if (blkdev->feature_persistent) {
        /* Init persistent grants */
        blkdev->max_grants = blkdev->max_requests *
            BLKIF_MAX_SEGMENTS_PER_REQUEST;
        blkdev->persistent_gnts = g_tree_new_full((GCompareDataFunc)int_cmp,
                                             NULL, NULL,
                                             batch_maps ?
                                             (GDestroyNotify)g_free :
                                             (GDestroyNotify)destroy_grant);
        blkdev->persistent_regions = NULL;
        blkdev->persistent_gnt_count = 0;
    }

    xen_be_bind_evtchn(&blkdev->xendev);

    xen_pv_printf(&blkdev->xendev, 1, "ok: proto %s, nr-ring-ref %u, "
                  "remote port %d, local port %d\n",
                  blkdev->xendev.protocol, blkdev->nr_ring_ref,
                  blkdev->xendev.remote_port, blkdev->xendev.local_port);
    return 0;
}

static void blk_disconnect(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    if (blkdev->blk) {
        blk_detach_dev(blkdev->blk, blkdev);
        blk_unref(blkdev->blk);
        blkdev->blk = NULL;
    }
    xen_pv_unbind_evtchn(&blkdev->xendev);

    if (blkdev->sring) {
        xengnttab_unmap(blkdev->xendev.gnttabdev, blkdev->sring,
                        blkdev->nr_ring_ref);
        blkdev->cnt_map--;
        blkdev->sring = NULL;
    }

    /*
     * Unmap persistent grants before switching to the closed state
     * so the frontend can free them.
     *
     * In the !batch_maps case g_tree_destroy will take care of unmapping
     * the grant, but in the batch_maps case we need to iterate over every
     * region in persistent_regions and unmap it.
     */
    if (blkdev->feature_persistent) {
        g_tree_destroy(blkdev->persistent_gnts);
        assert(batch_maps || blkdev->persistent_gnt_count == 0);
        if (batch_maps) {
            blkdev->persistent_gnt_count = 0;
            g_slist_foreach(blkdev->persistent_regions,
                            (GFunc)remove_persistent_region, blkdev);
            g_slist_free(blkdev->persistent_regions);
        }
        blkdev->feature_persistent = false;
    }
}

static int blk_free(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);
    struct ioreq *ioreq;

    if (blkdev->blk || blkdev->sring) {
        blk_disconnect(xendev);
    }

    while (!QLIST_EMPTY(&blkdev->freelist)) {
        ioreq = QLIST_FIRST(&blkdev->freelist);
        QLIST_REMOVE(ioreq, list);
        qemu_iovec_destroy(&ioreq->v);
        g_free(ioreq);
    }

    g_free(blkdev->params);
    g_free(blkdev->mode);
    g_free(blkdev->type);
    g_free(blkdev->dev);
    g_free(blkdev->devtype);
    qemu_bh_delete(blkdev->bh);
    return 0;
}

static void blk_event(struct XenDevice *xendev)
{
    struct XenBlkDev *blkdev = container_of(xendev, struct XenBlkDev, xendev);

    qemu_bh_schedule(blkdev->bh);
}

struct XenDevOps xen_blkdev_ops = {
    .size       = sizeof(struct XenBlkDev),
    .flags      = DEVOPS_FLAG_NEED_GNTDEV,
    .alloc      = blk_alloc,
    .init       = blk_init,
    .initialise    = blk_connect,
    .disconnect = blk_disconnect,
    .event      = blk_event,
    .free       = blk_free,
};
