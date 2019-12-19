/*
 * Block layer snapshot related functions
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

#ifndef SNAPSHOT_H
#define SNAPSHOT_H



#define SNAPSHOT_OPT_BASE       "snapshot."
#define SNAPSHOT_OPT_ID         "snapshot.id"
#define SNAPSHOT_OPT_NAME       "snapshot.name"

extern QemuOptsList internal_snapshot_opts;

typedef struct QEMUSnapshotInfo {
    char id_str[128]; /* unique snapshot id */
    /* the following fields are informative. They are not needed for
       the consistency of the snapshot */
    char name[256]; /* user chosen name */
    uint64_t vm_state_size; /* VM state info size */
    uint32_t date_sec; /* UTC date of the snapshot */
    uint32_t date_nsec;
    uint64_t vm_clock_nsec; /* VM clock relative to boot */
} QEMUSnapshotInfo;

int bdrv_snapshot_find(BlockDriverState *bs, QEMUSnapshotInfo *sn_info,
                       const char *name);
bool bdrv_snapshot_find_by_id_and_name(BlockDriverState *bs,
                                       const char *id,
                                       const char *name,
                                       QEMUSnapshotInfo *sn_info,
                                       Error **errp);
int bdrv_can_snapshot(BlockDriverState *bs);
int bdrv_snapshot_create(BlockDriverState *bs,
                         QEMUSnapshotInfo *sn_info);
int bdrv_snapshot_goto(BlockDriverState *bs,
                       const char *snapshot_id,
                       Error **errp);
int bdrv_snapshot_delete(BlockDriverState *bs,
                         const char *snapshot_id,
                         const char *name,
                         Error **errp);
int bdrv_snapshot_list(BlockDriverState *bs,
                       QEMUSnapshotInfo **psn_info);
int bdrv_snapshot_load_tmp(BlockDriverState *bs,
                           const char *snapshot_id,
                           const char *name,
                           Error **errp);
int bdrv_snapshot_load_tmp_by_id_or_name(BlockDriverState *bs,
                                         const char *id_or_name,
                                         Error **errp);


/* Group operations. All block drivers are involved.
 * These functions will properly handle dataplane (take aio_context_acquire
 * when appropriate for appropriate block drivers */

bool bdrv_all_can_snapshot(BlockDriverState **first_bad_bs);
int bdrv_all_delete_snapshot(const char *name, BlockDriverState **first_bsd_bs,
                             Error **errp);
int bdrv_all_goto_snapshot(const char *name, BlockDriverState **first_bad_bs,
                           Error **errp);
int bdrv_all_find_snapshot(const char *name, BlockDriverState **first_bad_bs);
int bdrv_all_create_snapshot(QEMUSnapshotInfo *sn,
                             BlockDriverState *vm_state_bs,
                             uint64_t vm_state_size,
                             BlockDriverState **first_bad_bs);

BlockDriverState *bdrv_all_find_vmstate_bs(void);

#endif
