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
        uint16_t newfid =
            (i == 1) ? V9P_RACE_D1_FID
                     : (i == 2) ? V9P_RACE_D2_FID : V9P_RACE_D3_FID;

        snprintf(dir, sizeof(dir), "d%d", i);
        if (v9p->proto_version == V9FS_PROTO_2000L) {
            char *wnames[] = { dir };

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
        } else {
            /*
             * clone dfid for Tcreate
             *
             * The legacy 9p2000(.u) Tcreate re-points the supplied FID to the
             * created (in this case) directory and requires it to be unopened.
             */
            v9fs_twalk((TWalkOpt) {
                .client = v9p,
                .fid = dfid,
                .newfid = newfid,
                .nwname = 0, /* 0 -> clone FID */
                .tag = v9p_race_new_tag(),
            });
            v9fs_tcreate((TCreateOpt) {
                .client = v9p,
                .fid = newfid,
                .name = dir,
                .perm = P9_CREATE_PERM_DMDIR | 0755,
                .tag = v9p_race_new_tag(),
            });
        }
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

/*
 * Race 9p server's internal directory stream for Treaddir.
 *
 * Race Treaddir request against another Treaddir or Tstat request on the same
 * FID.
 */
static void v9p_race_readdir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    QVirtio9P *v9p = obj;
    uint32_t rounds;
    uint32_t i;

    if (v9p_race_skip()) {
        return;
    }
    rounds = v9p_race_rounds();

    v9fs_set_allocator(t_alloc);
    v9p_race_prepare(v9p);

    for (i = 0; i < rounds; i++) {
        P9Req *req_a, *req_b;
        uint16_t tag_a = v9p_race_new_tag();
        uint16_t tag_b = v9p_race_new_tag();

        if (i % 2000 == 0) {
            g_print("race/readdir: %u/%u rounds\n", i, rounds);
            fflush(stdout);
        }

        /*
         * Reset the descriptor pool at the start of every round, where
         * no request is in-flight anymore.
         */
        v9p_race_reset_pool(v9p);

        /* initial Treaddir request */
        req_a = v9fs_treaddir((TReadDirOpt) {
            .client = v9p,
            .fid = V9P_RACE_D2_FID,
            .offset = 0,
            .count = 64,
            .tag = tag_a,
            .requestOnly = true,
        }).req;

        /*
         * On every 3rd round send a concurrent Tstat, otherwise a
         * concurrent Treaddir request.
         */
        if (i % 3 == 0) {
            /* a Tstat on the same directory FID */
            req_b = v9fs_tstat((TStatOpt) {
                .client = v9p,
                .fid = V9P_RACE_D2_FID,
                .tag = tag_b,
                .requestOnly = true,
            }).req;
        } else {
            /* a concurrent Treaddir on the same FID */
            req_b = v9fs_treaddir((TReadDirOpt) {
                .client = v9p,
                .fid = V9P_RACE_D2_FID,
                .offset = 0,
                .count = 64,
                .tag = tag_b,
                .requestOnly = true,
            }).req;
        }

        uint8_t resp_a, resp_b;

        resp_a = v9p_race_resp_collect(v9p, req_a);
        g_assert_cmpint(resp_a, ==, P9_RREADDIR);
        resp_b = v9p_race_resp_collect(v9p, req_b);
        g_assert_cmpint(resp_b, ==,
                        (i % 3 == 0) ? P9_RSTAT : P9_RREADDIR);
    }

    g_print("race/readdir: %u rounds done\n", rounds);
    fflush(stdout);
}

/* number of files created for "race/tread-dir" test */
#define V9P_RACE_TREAD_DIR_NFILES 40

/* returns true if @name is one of the dir entries known to exist in d3 */
static bool v9p_race_tread_dir_entry_name_valid(const char *name)
{
    int n;

    if (!strcmp(name, ".") || !strcmp(name, "..")) {
        return true;
    }
    if (name[0] != 'f' || strlen(name) != 3) {
        return false;
    }
    /*
     * atoi() returns 0 on error, therefore the files created for this test
     * start with 1, not zero.
     */
    n = atoi(&name[1]);
    return n >= 1 && n <= V9P_RACE_TREAD_DIR_NFILES;
}

/*
 * Read one directory entry (as v9stat struct) from Rread response.
 *
 * Only the directory entry's name is returned
 * (in @name, at most @name_size - 1 chars).
 *
 * Returns the number of response body bytes consumed for this entry.
 */
static size_t v9p_race_tread_dir_next_entry(P9Req *req, char *name,
                                            size_t name_size)
{
    /*
     * Each directory entry within Tread response is (with 9p2000.u):
     *
     * size[2] type[2] dev[4] qid[13] mode[4] atime[4] mtime[4] length[8]
     * name[s] uid[s] gid[s] muid[s] ext[s] n_uid[4] n_gid[4] n_muid[4]
     */
    uint16_t len = 0;
    size_t consumed = 2 + 2 + 4 + 13 + 4 + 4 + 4 + 8;
    int s;

    /* fast forward to name[s] */
    v9fs_memskip(req, consumed);

    /* read name[s] */
    v9fs_uint16_read(req, &len);
    g_assert_cmpint(len, <, name_size);
    v9fs_memread(req, name, len);
    name[len] = '\0';
    consumed += 2 + len;

    /* consume the rest of this entry */
    for (s = 0; s < 4; s++) { /* uid, gid, muid, ext */
        v9fs_string_read(req, &len, NULL);
        consumed += 2 + len;
    }
    v9fs_memskip(req, 4 + 4 + 4); /* n_uid, n_gid, n_muid */
    consumed += 4 + 4 + 4;

    return consumed;
}

/*
 * Parse and verify an Rread response.
 *
 * We have created V9P_RACE_TREAD_DIR_NFILES number of files in directory d3,
 * so we already know the file name scheme to verify for individual entries.
 *
 * Additionally, compare the returned directory entries with the passed
 * reference list of entries. They must always match exactly in name and order.
 */
static void v9p_race_tread_dir_verify_rread(P9Req *req,
                                            const char *const *ref_names,
                                            int n_ref_names)
{
    uint32_t count = 0, consumed = 0;
    char name[64];
    int ientry;

    v9fs_uint32_read(req, &count);

    for (ientry = 0; consumed < count; ientry++) {
        consumed += v9p_race_tread_dir_next_entry(req, name, sizeof(name));
        if (!v9p_race_tread_dir_entry_name_valid(name)) {
            g_printerr("invalid dirent name: %.16s\n", name);
            fflush(stderr);
            g_assert_not_reached();
        }
        if (ref_names && ientry < n_ref_names) {
            if (strcmp(name, ref_names[ientry]) != 0) {
                g_printerr("entry-order mismatch at #%d: got '%s' expected "
                           "'%s'\n", ientry, name, ref_names[ientry]);
                fflush(stderr);
                g_assert_not_reached();
            }
        }
    }
    /*
     * an empty Rread body means the dirent stream was corrupted
     * (e.g. by a garbage seek)
     */
    g_assert_cmpint(ientry, >, 0);
}

/*
 * Wait for response for @req, then verify if response is an Rread response
 * with expected tag, then verify all directory entries.
 */
static void v9p_race_tread_dir_collect(QVirtio9P *v9p, P9Req *req,
                                       const char *const *ref_names,
                                       int n_ref_names)
{
    uint32_t len;
    P9MsgHeader hdr;

    v9p_race_req_wait(v9p, req, &len);
    v9fs_memread(req, &hdr, sizeof(hdr));
    g_assert_cmpint(hdr.tag_le, ==, req->tag);
    if (hdr.id != P9_RREAD) {
        uint32_t ecode = 0;

        if (hdr.id == P9_RERROR) {
            v9fs_string_read(req, NULL, NULL); /* skip error string */
        }
        if (hdr.id == P9_RLERROR || hdr.id == P9_RERROR) {
            v9fs_uint32_read(req, &ecode);
        }
        g_error("Tread on dir FID returned ID %d (ecode %u, %s)", hdr.id,
                ecode, strerror(ecode));
    }
    g_assert_cmpint(hdr.size_le, >=, 11);
    v9p_race_tread_dir_verify_rread(req, ref_names, n_ref_names);
    v9fs_req_free(req);
}

/*
 * Race legacy (9p2000.u) Tread requests against each other on the same
 * directory FID.
 *
 * This test creates V9P_RACE_TREAD_DIR_NFILES number of files under directory
 * d3 and verifies that (a portion of) these files are always returned by
 * server in a stable manner (i.e. always exact same entry names, and in exact
 * same order) for each Tread request sent on the same directory FID.
 *
 * We actually read less directory entries than we created files, just to keep
 * things simple. A small amount of dir entries retrieval is still enough to
 * detect corrupted Rread responses.
 */
static void v9p_race_tread_dir(void *obj, void *data, QGuestAllocator *t_alloc)
{
    if (v9p_race_skip()) {
        return;
    }

    QVirtio9P *v9p = obj;
    uint32_t rounds = v9p_race_rounds();
    uint32_t i = 0, n;

    /* max. Rread bytes requested per Tread request */
    const uint32_t tread_size = 512;

    /*
     * Max. amount of reference directory entries taken for verification
     * (kept small enough to fit into one tread_size Rread response)
     */
    const uint32_t ref_max = 8;

    v9fs_set_allocator(t_alloc);

    /*
     * Need to set this session to legacy 9P2000.u dialect, because Tread on
     * directories is only valid there (9p2000.L provides Treaddir instead).
     */
    v9fs_tversion((TVersionOpt) {
        .client = v9p,
        .version = "9P2000.u",
        .tag = v9p_race_new_tag(),
    });
    v9fs_tattach((TAttachOpt) {
        .client = v9p,
        .fid = V9P_RACE_ROOT_FID,
        .tag = v9p_race_new_tag(),
    });

    /* create d1/d2/d3 directory chain */
    v9p_race_create_subdir_chain(v9p);

    /* populate d3 with known directory entries */
    for (n = 0; n < V9P_RACE_TREAD_DIR_NFILES; n++) {
        char fname[8];

        /*
         * use a new FID for each file created, just to be sure
         * (probably not really needed as we clunk it at the end of the loop)
         */
        uint16_t fid = 60 + (n % 32);

        /* no in-flight requests, good to reset virtio descriptor pool */
        v9p_race_reset_pool(v9p);

        /*
         * starting to create files by 1, not zero
         * (see v9p_race_tread_dir_entry_name_valid() why)
         */
        snprintf(fname, sizeof(fname), "f%02d", n + 1);

        /*
         * clone d3 FID for Tcreate
         *
         * The legacy 9p2000(.u) Tcreate re-points the supplied FID to the
         * created (in this case) file and requires it to be unopened.
         */
        v9fs_twalk((TWalkOpt) {
            .client = v9p,
            .fid = V9P_RACE_D3_FID,
            .newfid = fid,
            .nwname = 0, /* 0 -> clone FID */
            .tag = v9p_race_new_tag(),
        });
        /* create the file */
        v9fs_tcreate((TCreateOpt) {
            .client = v9p,
            .fid = fid,
            .name = fname,
            .tag = v9p_race_new_tag(),
        });
        v9p_race_clunk_relaxed(v9p, fid);
    }

    /* start the actual Tread-dir race test ... */
    {
        g_autofree char **ref_storage = g_new0(char *, ref_max);
        g_autofree const char **ref_names = g_new0(const char *, ref_max);
        uint32_t len = 0, count = 0, consumed = 0;
        int idx = 0;
        int ref_count = 0;
        g_autofree P9Req **reqs = NULL;
        int nreq, r;

        /* no in-flight requests, good to reset virtio descriptor pool */
        v9p_race_reset_pool(v9p);

        /*
         * Send and collect the initial "reference" Tread.
         *
         * One fundamental verification mechanism of this race test is to
         * verify the order of the individual directory entries returned by
         * server to be clean and stable. However the order is fs
         * implementation specific, so we cannot simply assume the same order
         * in which we created the files.
         *
         * Instead we first send out one safe (no race) Tread and use its
         * entries returned as THE reference list of directory entries.
         *
         * Then in the concurrent Tread loop below, we compare the subsequently
         * collected follow-up Tread responses to always exactly match those
         * reference directory entries.
         *
         * One caveat: POSIX does not guarantee order of entries to be stable,
         * however in practice for non-modified directories it is actually
         * stable. Let's not overcomplicate things (exotic systems) as this
         * test is not run by default anyway.
         */
        P9Req *req = v9fs_tread((TReadOpt) {
            .client = v9p,
            .fid = V9P_RACE_D3_FID,
            .count = tread_size,
            .tag = v9p_race_new_tag(),
            .requestOnly = true,
        }).req;
        v9p_race_req_wait(v9p, req, &len);
        {
            P9MsgHeader hdr;

            v9fs_memread(req, &hdr, sizeof(hdr));
            g_assert_cmpint(hdr.id, ==, P9_RREAD);
        }
        v9fs_uint32_read(req, &count);

        /* collect reference directory entries */
        for (consumed = 0; consumed < count && idx < ref_max; idx++) {
            char nbuf[64];

            consumed += v9p_race_tread_dir_next_entry(req, nbuf, sizeof(nbuf));
            g_assert(v9p_race_tread_dir_entry_name_valid(nbuf));
            ref_storage[idx] = g_strdup(nbuf);
            ref_names[idx] = ref_storage[idx];
        }
        v9fs_req_free(req);
        ref_count = idx;
        g_assert_cmpint(ref_count, >=, 2);

        /* dump the reference list of entries */
        g_print("race/tread-dir: reference listing: %d entries (as follows):\n",
                ref_count);
        for (i = 0; i < ref_count; i++) {
            g_print("race/tread-dir: reference entry #%u: '%s'\n",
                    i, ref_storage[i]);
        }
        fflush(stdout);

        nreq = v9p_race_nconcurrent();
        /*
         * Limit the number of concurrent Tread requests to the descriptor pool
         * size.
         */
        if (nreq > v9p->vq->size / 2) {
            g_error("V9FS_RACE_NCONCURRENT: %d concurrent requests "
                    "exceeds the queue's max. request amount of %d",
                    nreq, v9p->vq->size / 2);
        }
        reqs = g_new0(P9Req *, nreq);

        /* run the requested number of test rounds */
        for (i = 0; i < rounds; i++) {
            if (i % 2000 == 0) {
                g_print("race/tread-dir: %u/%u rounds\n", i, rounds);
                fflush(stdout);
            }

            /* no in-flight requests, good to reset virtio descriptor pool */
            v9p_race_reset_pool(v9p);

            /* send the actual concurrent Tread requests */
            for (r = 0; r < nreq; r++) {
                reqs[r] = v9fs_tread((TReadOpt) {
                    .client = v9p,
                    .fid = V9P_RACE_D3_FID,
                    .count = tread_size,
                    .tag = v9p_race_new_tag(),
                    .requestOnly = true,
                }).req;
            }

            /* collect and verify all responses */
            for (r = 0; r < nreq; r++) {
                v9p_race_tread_dir_collect(v9p, reqs[r], ref_names, ref_count);
            }
        }

        for (i = 0; i < ref_max; i++) {
            g_free(ref_storage[i]);
        }
    }

    g_print("race/tread-dir: %u rounds done\n", rounds);
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
    qos_add_test("race/readdir", "virtio-9p", v9p_race_readdir, &opts);
    qos_add_test("race/tread-dir", "virtio-9p", v9p_race_tread_dir, &opts);
}

libqos_init(v9p_race_register_tests);
