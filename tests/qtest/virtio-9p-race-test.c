/*
 * virtio 9p fs concurrency race test
 *
 * Copyright (c) 2026 Christian Schoenebeck <qemu_oss@crudebyte.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Implements a probabilistic stress test suite (not deterministic reproducers)
 * by simulating concurrent requests on the same 9p FID. This test suite is
 * expected to be built and run with sanitizers (e.g. ASan) that are supposed
 * to actually uncover potential races e.g. by detecting memory violations.
 */

/*
 * Not so fast! You might want to read the 9p developer docs first:
 * https://wiki.qemu.org/Documentation/9p
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "libqos/virtio.h"
#include "libqos/virtio-9p-client.h"
#include "libqos/virtio-9p.h"

/*
 * RACE TEST SUITE INFRASTRUCTURE
 *
 * The following functions implement the shared code for all 9p race
 * tests.
 */

/* depth of the d1/d2/d3 directory chain created below the export root */
#define V9P_RACE_DIR_DEPTH 3

/* export root FID */
#define V9P_RACE_ROOT_FID 0

/* d1 FID of the d1/d2/d3 path chain */
#define V9P_RACE_D1_FID   40
/* d2 FID of the d1/d2/d3 path chain */
#define V9P_RACE_D2_FID   41
/* d3 FID of the d1/d2/d3 path chain */
#define V9P_RACE_D3_FID   42

/*
 * FID range of automatically generated and recycled FIDs per race round.
 * Kept small and away from the constant FIDs above.
 */
#define V9P_RACE_FID_ROUND_BASE 100
#define V9P_RACE_FID_ROUND_SLOTS 32

static uint16_t v9p_race_tag;

/* returns a new tag for the next 9p request */
static uint16_t v9p_race_new_tag(void)
{
    v9p_race_tag++;
    /* tag 0 is the reserved no-tag (P9_NOTAG) value, not a real tag */
    if (v9p_race_tag == 0) {
        v9p_race_tag = 1;
    }
    return v9p_race_tag;
}

/*
 * How long the test suite waits for one pending request to arrive before
 * concluding the 9p server is stuck; ASan builds may be slow, so keep it
 * generous.
 */
#define V9P_RACE_TIMEOUT_US (30 * G_USEC_PER_SEC)

/*
 * Maximum amount of pending 9p server responses ever expected.
 *
 * This is directly tied to server's MAX_REQ (which currently yields to 128).
 */
#define V9P_RACE_RCVD_REPL_MAX (MAX_REQ)

/*
 * A local stash of already received 9p responses.
 *
 * With several 9p requests in-flight, used ring entries can arrive in a
 * different order than the requests were originally sent. A consumed virtio
 * ring entry belonging to a not-yet-collected response is queued here as a
 * received-but-unprocessed reply record. The response payload itself stays in
 * the request's guest buffer either way, so the descriptor index alone is
 * enough to acquire it later on.
 */
static struct {
    uint32_t desc_idx;
    uint32_t len;
    bool used;
} v9p_race_rcvd_resp[V9P_RACE_RCVD_REPL_MAX];

/*
 * Reset virtio test driver's descriptor pool.
 *
 * The qtest virtio driver never recycles used descriptors, so the
 * descriptor table (one queue-size worth of entries) must be reset
 * whenever it is about to run out. Safe as long as no request is in
 * flight (call between completed round-trips or at the start of a
 * round where all previous requests have been collected).
 */
static void v9p_race_reset_pool(QVirtio9P *v9p)
{
    qvirtqueue_reset_pool(v9p->vq);
}

/*
 * Feed the QEMU main loop with one no-op qtest command round-trip.
 *
 * In qtest mode the 9p device state only advances while the QEMU main
 * loop services a qtest command: servicing the command runs inside one
 * main loop iteration, which also dispatches previously queued
 * virtqueue kicks, steps the 9p handler coroutines on the main thread
 * (their worker-side parts run on independent thread pool threads) and
 * writes completed responses back into the virtio used ring.
 */
static void v9p_race_feed_main_loop(QVirtio9P *v9p)
{
    /*
     * The read target is the virtqueue descriptor table, which only this
     * test writes, so the command has no side effects. The read value is
     * discarded.
     */
    qtest_readl(global_qtest, v9p->vq->desc);
}

/* stash the received reply identified by @desc_idx for later processing */
static void v9p_race_rcvd_resp_add(uint32_t desc_idx, uint32_t len)
{
    int i;

    for (i = 0; i < V9P_RACE_RCVD_REPL_MAX; i++) {
        if (!v9p_race_rcvd_resp[i].used) {
            v9p_race_rcvd_resp[i].used = true;
            v9p_race_rcvd_resp[i].desc_idx = desc_idx;
            v9p_race_rcvd_resp[i].len = len;
            return;
        }
    }
    g_assert_not_reached();
}

/*
 * Pickup the previously stashed reply for @desc_idx, returning its length,
 * or UINT32_MAX if no reply for that request has been received yet.
 */
static uint32_t v9p_race_rcvd_resp_remove(uint32_t desc_idx)
{
    int i;

    for (i = 0; i < V9P_RACE_RCVD_REPL_MAX; i++) {
        if (v9p_race_rcvd_resp[i].used &&
            v9p_race_rcvd_resp[i].desc_idx == desc_idx) {
            uint32_t len = v9p_race_rcvd_resp[i].len;

            v9p_race_rcvd_resp[i].used = false;
            return len;
        }
    }
    return UINT32_MAX;
}

/*
 * Wait for the used ring entry of @req, returns immediately if the response
 * was already received.
 *
 * As responses may arrive in a different order than their requests were
 * originally sent, received responses that belong to a different, in-flight
 * request are locally stashed instead (waiting for later pickup).
 *
 * NOTE: this implementation polls the used ring directly instead of waiting
 * on the legacy virtio interrupt status register (ISR) that
 * qvirtio_wait_used_elem() uses: that register is a single boolean read-clear
 * flag, not a counter. So it could hide several used-ring entries. And since
 * that flag is not re-raised for entries that already arrived in the ring,
 * waiting on the ISR could continue for good, even though all response
 * entries were already received. Furthermore, qvirtio_wait_used_elem() asserts
 * that it receives the response of its own request next from the virtio ring.
 * So we can't use qvirtio_wait_used_elem(), as it is designed to receive
 * exactly one (in-order) response, whereas this race test is designed to
 * receive several out-of-order responses.
 */
static void v9p_race_req_wait(QVirtio9P *v9p, P9Req *req, uint32_t *len)
{
    gint64 start = g_get_monotonic_time();

    /* did the response already arrive and was just stashed locally? */
    *len = v9p_race_rcvd_resp_remove(req->free_head);
    if (*len != UINT32_MAX) {
        return;
    }

    /* poll virtio ring until requested response arrived */
    while (true) {
        uint32_t idx;

        if (qvirtqueue_get_buf(global_qtest, v9p->vq, &idx, len)) {
            if (idx == req->free_head) {
                return;
            }
            /* not the response we asked for, stash it then continue */
            v9p_race_rcvd_resp_add(idx, *len);
        }
        g_usleep(1000);
        if (g_get_monotonic_time() - start > V9P_RACE_TIMEOUT_US) {
            g_printerr("TIMEOUT waiting for tag=%u (free_head=%u)\n",
                       req->tag, req->free_head);
            fflush(stderr);
            g_assert_not_reached();
        }
    }
}

/*
 * Wait for the response of @req, verify the tag and return the response
 * ID. Frees @req.
 */
static uint8_t v9p_race_resp_collect(QVirtio9P *v9p, P9Req *req)
{
    uint32_t len;
    P9MsgHeader hdr;

    v9p_race_req_wait(v9p, req, &len);
    v9fs_memread(req, &hdr, sizeof(hdr));
    g_assert_cmpint(hdr.tag_le, ==, req->tag);
    g_assert_cmpint(hdr.size_le, >=, sizeof(hdr));
    v9fs_req_free(req);
    return hdr.id;
}

/* Tolerant Tclunk: the FID may or may not exist on the server. */
static void v9p_race_clunk_relaxed(QVirtio9P *v9p, uint32_t fid)
{
    TClunkRes res = v9fs_tclunk((TClunkOpt) {
        .client = v9p,
        .fid = fid,
        .tag = v9p_race_new_tag(),
        .requestOnly = true,
    });

    v9p_race_resp_collect(v9p, res.req);
}

/* Tolerant Tunlinkat: the entry may or may not exist on the server. */
static void v9p_race_unlink_relaxed(QVirtio9P *v9p, uint32_t dfid,
                                    const char *name, uint32_t flags)
{
    TunlinkatRes res = v9fs_tunlinkat((TunlinkatOpt) {
        .client = v9p,
        .dirfd = dfid,
        .name = name,
        .flags = flags,
        .tag = v9p_race_new_tag(),
        .requestOnly = true,
    });

    v9p_race_resp_collect(v9p, res.req);
}

/*
 * Create the d1/d2/d3 directory chain inside the attached export root,
 * walking each level into its own FID (V9P_RACE_D1_FID .. V9P_RACE_D3_FID).
 */
static void v9p_race_create_subdir_chain(QVirtio9P *v9p)
{
    uint16_t dfid = V9P_RACE_ROOT_FID;
    int i;

    for (i = 1; i <= V9P_RACE_DIR_DEPTH; ++i) {
        char dir[8];
        char *wnames[] = { dir };
        uint16_t newfid =
            (i == 1) ? V9P_RACE_D1_FID
                     : (i == 2) ? V9P_RACE_D2_FID : V9P_RACE_D3_FID;

        snprintf(dir, sizeof(dir), "d%d", i);
        v9fs_tmkdir((TMkdirOpt) {
            .client = v9p,
            .dfid = dfid,
            .name = dir,
            .mode = 0755,
            .gid = 0,
            .tag = v9p_race_new_tag(),
        });
        v9fs_twalk((TWalkOpt) {
            .client = v9p,
            .fid = dfid,
            .newfid = newfid,
            .nwname = 1,
            .wnames = wnames,
            .tag = v9p_race_new_tag(),
        });
        dfid = newfid;
    }
}

/* Prepare this test suite for running a race test. */
static void v9p_race_prepare(QVirtio9P *v9p)
{
    int i;

    /* init local response stash */
    for (i = 0; i < V9P_RACE_RCVD_REPL_MAX; i++) {
        v9p_race_rcvd_resp[i].used = false;
    }

    /* attach to export root */
    v9fs_tattach((TAttachOpt) {
        .client = v9p,
        .fid = V9P_RACE_ROOT_FID,
        .tag = v9p_race_new_tag(),
    });

    /* create the d1/d2/d3 subdir chain */
    v9p_race_create_subdir_chain(v9p);

    /* open d2 and d3 as directory FIDs (V9P_RACE_D2_FID and V9P_RACE_D3_FID) */
    v9fs_tlopen((TLOpenOpt) {
        .client = v9p,
        .fid = V9P_RACE_D2_FID,
        .flags = O_DIRECTORY,
        .tag = v9p_race_new_tag(),
    });
    v9fs_tlopen((TLOpenOpt) {
        .client = v9p,
        .fid = V9P_RACE_D3_FID,
        .flags = O_DIRECTORY,
        .tag = v9p_race_new_tag(),
    });
}

/* Return true if race test shall be skipped.  */
static bool v9p_race_skip(void) {
    const char *val = getenv("V9FS_RACE");

    if (val) {
        if (!strcmp(val, "0")) {
            g_test_skip("V9FS_RACE=0 was set");
            return true;
        }
        if (!strcmp(val, "1")) {
            g_print("V9FS_RACE=1 was set\n");
            return false;
        }
    }
    if (!g_test_slow()) {
        g_test_skip("This is a slow test, run with -m slow or set V9FS_RACE=1");
        return true;
    }
    g_print("Skip by setting V9FS_RACE=0\n");
    return false;
}

/* Cleanup after running a race test. */
static void v9p_race_cleanup(void *data)
{
    /* remove the temp directory used as export root */
    virtio_9p_remove_local_test_dir();
}

/* number of rounds a race test runs, shared by all race tests */
static uint32_t v9p_race_rounds(void)
{
    const char *val = getenv("V9FS_RACE_ROUNDS");

    if (val && *val) {
        return atoi(val);
    }
    return 20000;
}

/* number of unsynchronized in-flight race requests per round */
static int v9p_race_nconcurrent(void)
{
    const char *val = getenv("V9FS_RACE_NCONCURRENT");
    int nreqs = 4;

    if (val && *val) {
        nreqs = atoi(val);
    }
    /* an empty or invalid value falls back to the built-in default */
    if (nreqs < 1) {
        nreqs = 4;
    }
    return nreqs;
}

/*
 * RACE TEST CASES
 *
 * The individual 9p race test scenarios.
 */

/*
 * Race 9p server's internal FID-path string (CVE-2026-93834).
 *
 * Per round, race one Tlcreate (mutating the FID path) request against
 * FID path reader requests (Twalk and Txattrwalk) on the same FID.
 */
static void v9p_race_fid_path(void *obj, void *data, QGuestAllocator *t_alloc)
{
    if (v9p_race_skip()) {
        return;
    }
    QVirtio9P *v9p = obj;
    uint32_t rounds = v9p_race_rounds();
    unsigned long created_files = 0;
    unsigned long xattr_fids = 0, walk_fids = 0;
    uint32_t i, d, k;
    int nconcurrent = v9p_race_nconcurrent();
    g_autofree P9Req **readers = NULL;

    v9fs_set_allocator(t_alloc);
    v9p_race_prepare(v9p);

    /*
     * A round consumes one twalk, the Tlcreate itself, nconcurrent
     * readers and up to nconcurrent + 2 clunks/unlinks from the
     * per-round reset descriptor pool, so limit nconcurrent by the
     * queue size.
     */
    if (4 + 2 * nconcurrent > v9p->vq->size / 2) {
        g_error("V9FS_RACE_NCONCURRENT: %d readers need %d requests "
                "per round, but the queue holds at most %d",
                nconcurrent, 4 + 2 * nconcurrent, v9p->vq->size / 2);
    }
    readers = g_new0(P9Req *, nconcurrent);

    for (i = 0; i < rounds; i++) {
        uint16_t fid_a = V9P_RACE_FID_ROUND_BASE +
                         (i % V9P_RACE_FID_ROUND_SLOTS);
        uint16_t fid_b = fid_a + 100;
        uint16_t fid_c = fid_a + 200;
        char name[16];
        uint16_t tag_a = v9p_race_new_tag();
        P9Req *req_a;
        uint8_t resp_a;

        /* progress notification */
        if (i % 2000 == 0) {
            g_print("race/fid-path: %u/%u rounds\n", i, rounds);
            fflush(stdout);
        }

        /*
         * Reset the descriptor pool at the start of every round, where
         * no request is in-flight anymore.
         */
        v9p_race_reset_pool(v9p);

        /* clone a FID of the target directory */
        v9fs_twalk((TWalkOpt) {
            .client = v9p,
            .fid = V9P_RACE_D3_FID,
            .newfid = fid_a,
            .nwname = 0,
            .tag = v9p_race_new_tag(),
        });

        snprintf(name, sizeof(name), "r%05x", i);

        /*
         * writer: Tlcreate on fid_a mutates its (9p server internal) FID-path
         * string
         */
        req_a = v9fs_tlcreate((TlcreateOpt) {
            .client = v9p,
            .fid = fid_a,
            .name = name,
            .flags = O_RDWR | O_CREAT,
            .mode = 0644,
            .gid = 0,
            .tag = tag_a,
            .requestOnly = true,
        }).req;

        /*
         * readers: Twalk and Txattrwalk requests
         *
         * Each reader request is sent after a few v9p_race_feed_main_loop()
         * calls. That allows the Tlcreate PDU advance processing.
         */
        for (k = 0; k < nconcurrent; k++) {
            /*
             * Feed the main loop between 0..4 times.
             *
             * The amount of main loop feeds is changed per round and per
             * reader. That way we create different time gaps between the
             * writer (Tlcreate) and readers, which increases probability
             * to hit the quite small window where a FID path is mutated.
             */
            for (d = 0; d < ((i + k) % 5); d++) {
                v9p_race_feed_main_loop(v9p);
            }
            if (!(k & 1)) {
                uint16_t tag_c = v9p_race_new_tag();

                /* Txattrwalk (list xattrs) */
                readers[k] = v9fs_txattrwalk((TXattrWalkOpt) {
                    .client = v9p,
                    .fid = fid_a,
                    .newfid = fid_c + k,
                    .name = "", /* empty xattr name -> list xattrs */
                    .tag = tag_c,
                    .requestOnly = true,
                }).req;
            } else {
                uint16_t tag_b = v9p_race_new_tag();

                /* Twalk (clone FID) */
                readers[k] = v9fs_twalk((TWalkOpt) {
                    .client = v9p,
                    .fid = fid_a,
                    .newfid = fid_b + k,
                    .nwname = 0, /* no names to be walked -> clone FID */
                    .tag = tag_b,
                    .requestOnly = true,
                }).req;
            }
        }

        resp_a = v9p_race_resp_collect(v9p, req_a);
        for (k = 0; k < nconcurrent; k++) {
            uint8_t resp_r = v9p_race_resp_collect(v9p, readers[k]);

            if (!(k & 1) && resp_r == P9_RXATTRWALK) {
                xattr_fids++;
                v9p_race_clunk_relaxed(v9p, fid_c + k);
            } else if ((k & 1) && resp_r == P9_RWALK) {
                walk_fids++;
                v9p_race_clunk_relaxed(v9p, fid_b + k);
            }
        }
        v9p_race_clunk_relaxed(v9p, fid_a);
        if (resp_a == P9_RLCREATE) {
            created_files++;
            v9p_race_unlink_relaxed(v9p, V9P_RACE_D3_FID, name, 0);
        }
    }

    g_print("race/fid-path: %u rounds done, %lu files created, %lu walk "
            "fids, %lu xattr fids\n",
            rounds, created_files, walk_fids, xattr_fids);
    fflush(stdout);
}

/* setup and cleanup for race tests using the 9p 'local' fs driver */
static void *v9p_race_local_driver(GString *cmd_line, void *arg)
{
    /* create a temp directory for being used as 9p export root */
    virtio_9p_create_local_test_dir();
    /*
     * Use the "mapped-file" security model (not "mapped-xattr"): xattr only
     * works on file systems and systems that support and permit writing
     * xattrs. mapped-file (hidden .virtfs_metadata files) OTOH is universally
     * more tolerant.
     */
    virtio_9p_assign_local_driver(cmd_line, "security_model=mapped-file");
    g_test_queue_destroy(v9p_race_cleanup, NULL);
    return arg;
}

/* register this overall test-suite */
static void v9p_race_register_tests(void)
{
    QOSGraphTestOptions opts = {
        .before = v9p_race_local_driver,
    };

    qos_add_test("race/fid-path", "virtio-9p", v9p_race_fid_path, &opts);
}

libqos_init(v9p_race_register_tests);
