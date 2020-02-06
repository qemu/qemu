/*
 * Block driver for the QCOW version 2 format
 *
 * Copyright (c) 2004-2006 Fabrice Bellard
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qcow2.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"

static void qcow2_free_single_snapshot(BlockDriverState *bs, int i)
{
    BDRVQcow2State *s = bs->opaque;

    assert(i >= 0 && i < s->nb_snapshots);
    g_free(s->snapshots[i].name);
    g_free(s->snapshots[i].id_str);
    g_free(s->snapshots[i].unknown_extra_data);
    memset(&s->snapshots[i], 0, sizeof(s->snapshots[i]));
}

void qcow2_free_snapshots(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    int i;

    for(i = 0; i < s->nb_snapshots; i++) {
        qcow2_free_single_snapshot(bs, i);
    }
    g_free(s->snapshots);
    s->snapshots = NULL;
    s->nb_snapshots = 0;
}

/*
 * If @repair is true, try to repair a broken snapshot table instead
 * of just returning an error:
 *
 * - If the snapshot table was too long, set *nb_clusters_reduced to
 *   the number of snapshots removed off the end.
 *   The caller will update the on-disk nb_snapshots accordingly;
 *   this leaks clusters, but is safe.
 *   (The on-disk information must be updated before
 *   qcow2_check_refcounts(), because that function relies on
 *   s->nb_snapshots to reflect the on-disk value.)
 *
 * - If there were snapshots with too much extra metadata, increment
 *   *extra_data_dropped for each.
 *   This requires the caller to eventually rewrite the whole snapshot
 *   table, which requires cluster allocation.  Therefore, this should
 *   be done only after qcow2_check_refcounts() made sure the refcount
 *   structures are valid.
 *   (In the meantime, the image is still valid because
 *   qcow2_check_refcounts() does not do anything with snapshots'
 *   extra data.)
 */
static int qcow2_do_read_snapshots(BlockDriverState *bs, bool repair,
                                   int *nb_clusters_reduced,
                                   int *extra_data_dropped,
                                   Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshotHeader h;
    QCowSnapshotExtraData extra;
    QCowSnapshot *sn;
    int i, id_str_size, name_size;
    int64_t offset, pre_sn_offset;
    uint64_t table_length = 0;
    int ret;

    if (!s->nb_snapshots) {
        s->snapshots = NULL;
        s->snapshots_size = 0;
        return 0;
    }

    offset = s->snapshots_offset;
    s->snapshots = g_new0(QCowSnapshot, s->nb_snapshots);

    for(i = 0; i < s->nb_snapshots; i++) {
        bool truncate_unknown_extra_data = false;

        pre_sn_offset = offset;
        table_length = ROUND_UP(table_length, 8);

        /* Read statically sized part of the snapshot header */
        offset = ROUND_UP(offset, 8);
        ret = bdrv_pread(bs->file, offset, &h, sizeof(h));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to read snapshot table");
            goto fail;
        }

        offset += sizeof(h);
        sn = s->snapshots + i;
        sn->l1_table_offset = be64_to_cpu(h.l1_table_offset);
        sn->l1_size = be32_to_cpu(h.l1_size);
        sn->vm_state_size = be32_to_cpu(h.vm_state_size);
        sn->date_sec = be32_to_cpu(h.date_sec);
        sn->date_nsec = be32_to_cpu(h.date_nsec);
        sn->vm_clock_nsec = be64_to_cpu(h.vm_clock_nsec);
        sn->extra_data_size = be32_to_cpu(h.extra_data_size);

        id_str_size = be16_to_cpu(h.id_str_size);
        name_size = be16_to_cpu(h.name_size);

        if (sn->extra_data_size > QCOW_MAX_SNAPSHOT_EXTRA_DATA) {
            if (!repair) {
                ret = -EFBIG;
                error_setg(errp, "Too much extra metadata in snapshot table "
                           "entry %i", i);
                error_append_hint(errp, "You can force-remove this extra "
                                  "metadata with qemu-img check -r all\n");
                goto fail;
            }

            fprintf(stderr, "Discarding too much extra metadata in snapshot "
                    "table entry %i (%" PRIu32 " > %u)\n",
                    i, sn->extra_data_size, QCOW_MAX_SNAPSHOT_EXTRA_DATA);

            (*extra_data_dropped)++;
            truncate_unknown_extra_data = true;
        }

        /* Read known extra data */
        ret = bdrv_pread(bs->file, offset, &extra,
                         MIN(sizeof(extra), sn->extra_data_size));
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to read snapshot table");
            goto fail;
        }
        offset += MIN(sizeof(extra), sn->extra_data_size);

        if (sn->extra_data_size >= endof(QCowSnapshotExtraData,
                                         vm_state_size_large)) {
            sn->vm_state_size = be64_to_cpu(extra.vm_state_size_large);
        }

        if (sn->extra_data_size >= endof(QCowSnapshotExtraData, disk_size)) {
            sn->disk_size = be64_to_cpu(extra.disk_size);
        } else {
            sn->disk_size = bs->total_sectors * BDRV_SECTOR_SIZE;
        }

        if (sn->extra_data_size > sizeof(extra)) {
            uint64_t extra_data_end;
            size_t unknown_extra_data_size;

            extra_data_end = offset + sn->extra_data_size - sizeof(extra);

            if (truncate_unknown_extra_data) {
                sn->extra_data_size = QCOW_MAX_SNAPSHOT_EXTRA_DATA;
            }

            /* Store unknown extra data */
            unknown_extra_data_size = sn->extra_data_size - sizeof(extra);
            sn->unknown_extra_data = g_malloc(unknown_extra_data_size);
            ret = bdrv_pread(bs->file, offset, sn->unknown_extra_data,
                             unknown_extra_data_size);
            if (ret < 0) {
                error_setg_errno(errp, -ret,
                                 "Failed to read snapshot table");
                goto fail;
            }
            offset = extra_data_end;
        }

        /* Read snapshot ID */
        sn->id_str = g_malloc(id_str_size + 1);
        ret = bdrv_pread(bs->file, offset, sn->id_str, id_str_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to read snapshot table");
            goto fail;
        }
        offset += id_str_size;
        sn->id_str[id_str_size] = '\0';

        /* Read snapshot name */
        sn->name = g_malloc(name_size + 1);
        ret = bdrv_pread(bs->file, offset, sn->name, name_size);
        if (ret < 0) {
            error_setg_errno(errp, -ret, "Failed to read snapshot table");
            goto fail;
        }
        offset += name_size;
        sn->name[name_size] = '\0';

        /* Note that the extra data may have been truncated */
        table_length += sizeof(h) + sn->extra_data_size + id_str_size +
                        name_size;
        if (!repair) {
            assert(table_length == offset - s->snapshots_offset);
        }

        if (table_length > QCOW_MAX_SNAPSHOTS_SIZE ||
            offset - s->snapshots_offset > INT_MAX)
        {
            if (!repair) {
                ret = -EFBIG;
                error_setg(errp, "Snapshot table is too big");
                error_append_hint(errp, "You can force-remove all %u "
                                  "overhanging snapshots with qemu-img check "
                                  "-r all\n", s->nb_snapshots - i);
                goto fail;
            }

            fprintf(stderr, "Discarding %u overhanging snapshots (snapshot "
                    "table is too big)\n", s->nb_snapshots - i);

            *nb_clusters_reduced += (s->nb_snapshots - i);

            /* Discard current snapshot also */
            qcow2_free_single_snapshot(bs, i);

            /*
             * This leaks all the rest of the snapshot table and the
             * snapshots' clusters, but we run in check -r all mode,
             * so qcow2_check_refcounts() will take care of it.
             */
            s->nb_snapshots = i;
            offset = pre_sn_offset;
            break;
        }
    }

    assert(offset - s->snapshots_offset <= INT_MAX);
    s->snapshots_size = offset - s->snapshots_offset;
    return 0;

fail:
    qcow2_free_snapshots(bs);
    return ret;
}

int qcow2_read_snapshots(BlockDriverState *bs, Error **errp)
{
    return qcow2_do_read_snapshots(bs, false, NULL, NULL, errp);
}

/* add at the end of the file a new list of snapshots */
int qcow2_write_snapshots(BlockDriverState *bs)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot *sn;
    QCowSnapshotHeader h;
    QCowSnapshotExtraData extra;
    int i, name_size, id_str_size, snapshots_size;
    struct {
        uint32_t nb_snapshots;
        uint64_t snapshots_offset;
    } QEMU_PACKED header_data;
    int64_t offset, snapshots_offset = 0;
    int ret;

    /* compute the size of the snapshots */
    offset = 0;
    for(i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        offset = ROUND_UP(offset, 8);
        offset += sizeof(h);
        offset += MAX(sizeof(extra), sn->extra_data_size);
        offset += strlen(sn->id_str);
        offset += strlen(sn->name);

        if (offset > QCOW_MAX_SNAPSHOTS_SIZE) {
            ret = -EFBIG;
            goto fail;
        }
    }

    assert(offset <= INT_MAX);
    snapshots_size = offset;

    /* Allocate space for the new snapshot list */
    snapshots_offset = qcow2_alloc_clusters(bs, snapshots_size);
    offset = snapshots_offset;
    if (offset < 0) {
        ret = offset;
        goto fail;
    }
    ret = bdrv_flush(bs);
    if (ret < 0) {
        goto fail;
    }

    /* The snapshot list position has not yet been updated, so these clusters
     * must indeed be completely free */
    ret = qcow2_pre_write_overlap_check(bs, 0, offset, snapshots_size, false);
    if (ret < 0) {
        goto fail;
    }


    /* Write all snapshots to the new list */
    for(i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        memset(&h, 0, sizeof(h));
        h.l1_table_offset = cpu_to_be64(sn->l1_table_offset);
        h.l1_size = cpu_to_be32(sn->l1_size);
        /* If it doesn't fit in 32 bit, older implementations should treat it
         * as a disk-only snapshot rather than truncate the VM state */
        if (sn->vm_state_size <= 0xffffffff) {
            h.vm_state_size = cpu_to_be32(sn->vm_state_size);
        }
        h.date_sec = cpu_to_be32(sn->date_sec);
        h.date_nsec = cpu_to_be32(sn->date_nsec);
        h.vm_clock_nsec = cpu_to_be64(sn->vm_clock_nsec);
        h.extra_data_size = cpu_to_be32(MAX(sizeof(extra),
                                            sn->extra_data_size));

        memset(&extra, 0, sizeof(extra));
        extra.vm_state_size_large = cpu_to_be64(sn->vm_state_size);
        extra.disk_size = cpu_to_be64(sn->disk_size);

        id_str_size = strlen(sn->id_str);
        name_size = strlen(sn->name);
        assert(id_str_size <= UINT16_MAX && name_size <= UINT16_MAX);
        h.id_str_size = cpu_to_be16(id_str_size);
        h.name_size = cpu_to_be16(name_size);
        offset = ROUND_UP(offset, 8);

        ret = bdrv_pwrite(bs->file, offset, &h, sizeof(h));
        if (ret < 0) {
            goto fail;
        }
        offset += sizeof(h);

        ret = bdrv_pwrite(bs->file, offset, &extra, sizeof(extra));
        if (ret < 0) {
            goto fail;
        }
        offset += sizeof(extra);

        if (sn->extra_data_size > sizeof(extra)) {
            size_t unknown_extra_data_size =
                sn->extra_data_size - sizeof(extra);

            /* qcow2_read_snapshots() ensures no unbounded allocation */
            assert(unknown_extra_data_size <= BDRV_REQUEST_MAX_BYTES);
            assert(sn->unknown_extra_data);

            ret = bdrv_pwrite(bs->file, offset, sn->unknown_extra_data,
                              unknown_extra_data_size);
            if (ret < 0) {
                goto fail;
            }
            offset += unknown_extra_data_size;
        }

        ret = bdrv_pwrite(bs->file, offset, sn->id_str, id_str_size);
        if (ret < 0) {
            goto fail;
        }
        offset += id_str_size;

        ret = bdrv_pwrite(bs->file, offset, sn->name, name_size);
        if (ret < 0) {
            goto fail;
        }
        offset += name_size;
    }

    /*
     * Update the header to point to the new snapshot table. This requires the
     * new table and its refcounts to be stable on disk.
     */
    ret = bdrv_flush(bs);
    if (ret < 0) {
        goto fail;
    }

    QEMU_BUILD_BUG_ON(offsetof(QCowHeader, snapshots_offset) !=
                      endof(QCowHeader, nb_snapshots));

    header_data.nb_snapshots        = cpu_to_be32(s->nb_snapshots);
    header_data.snapshots_offset    = cpu_to_be64(snapshots_offset);

    ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, nb_snapshots),
                           &header_data, sizeof(header_data));
    if (ret < 0) {
        goto fail;
    }

    /* free the old snapshot table */
    qcow2_free_clusters(bs, s->snapshots_offset, s->snapshots_size,
                        QCOW2_DISCARD_SNAPSHOT);
    s->snapshots_offset = snapshots_offset;
    s->snapshots_size = snapshots_size;
    return 0;

fail:
    if (snapshots_offset > 0) {
        qcow2_free_clusters(bs, snapshots_offset, snapshots_size,
                            QCOW2_DISCARD_ALWAYS);
    }
    return ret;
}

int coroutine_fn qcow2_check_read_snapshot_table(BlockDriverState *bs,
                                                 BdrvCheckResult *result,
                                                 BdrvCheckMode fix)
{
    BDRVQcow2State *s = bs->opaque;
    Error *local_err = NULL;
    int nb_clusters_reduced = 0;
    int extra_data_dropped = 0;
    int ret;
    struct {
        uint32_t nb_snapshots;
        uint64_t snapshots_offset;
    } QEMU_PACKED snapshot_table_pointer;

    /* qcow2_do_open() discards this information in check mode */
    ret = bdrv_pread(bs->file, offsetof(QCowHeader, nb_snapshots),
                     &snapshot_table_pointer, sizeof(snapshot_table_pointer));
    if (ret < 0) {
        result->check_errors++;
        fprintf(stderr, "ERROR failed to read the snapshot table pointer from "
                "the image header: %s\n", strerror(-ret));
        return ret;
    }

    s->snapshots_offset = be64_to_cpu(snapshot_table_pointer.snapshots_offset);
    s->nb_snapshots = be32_to_cpu(snapshot_table_pointer.nb_snapshots);

    if (s->nb_snapshots > QCOW_MAX_SNAPSHOTS && (fix & BDRV_FIX_ERRORS)) {
        fprintf(stderr, "Discarding %u overhanging snapshots\n",
                s->nb_snapshots - QCOW_MAX_SNAPSHOTS);

        nb_clusters_reduced += s->nb_snapshots - QCOW_MAX_SNAPSHOTS;
        s->nb_snapshots = QCOW_MAX_SNAPSHOTS;
    }

    ret = qcow2_validate_table(bs, s->snapshots_offset, s->nb_snapshots,
                               sizeof(QCowSnapshotHeader),
                               sizeof(QCowSnapshotHeader) * QCOW_MAX_SNAPSHOTS,
                               "snapshot table", &local_err);
    if (ret < 0) {
        result->check_errors++;
        error_reportf_err(local_err, "ERROR ");

        if (s->nb_snapshots > QCOW_MAX_SNAPSHOTS) {
            fprintf(stderr, "You can force-remove all %u overhanging snapshots "
                    "with qemu-img check -r all\n",
                    s->nb_snapshots - QCOW_MAX_SNAPSHOTS);
        }

        /* We did not read the snapshot table, so invalidate this information */
        s->snapshots_offset = 0;
        s->nb_snapshots = 0;

        return ret;
    }

    qemu_co_mutex_unlock(&s->lock);
    ret = qcow2_do_read_snapshots(bs, fix & BDRV_FIX_ERRORS,
                                  &nb_clusters_reduced, &extra_data_dropped,
                                  &local_err);
    qemu_co_mutex_lock(&s->lock);
    if (ret < 0) {
        result->check_errors++;
        error_reportf_err(local_err,
                          "ERROR failed to read the snapshot table: ");

        /* We did not read the snapshot table, so invalidate this information */
        s->snapshots_offset = 0;
        s->nb_snapshots = 0;

        return ret;
    }
    result->corruptions += nb_clusters_reduced + extra_data_dropped;

    if (nb_clusters_reduced) {
        /*
         * Update image header now, because:
         * (1) qcow2_check_refcounts() relies on s->nb_snapshots to be
         *     the same as what the image header says,
         * (2) this leaks clusters, but qcow2_check_refcounts() will
         *     fix that.
         */
        assert(fix & BDRV_FIX_ERRORS);

        snapshot_table_pointer.nb_snapshots = cpu_to_be32(s->nb_snapshots);
        ret = bdrv_pwrite_sync(bs->file, offsetof(QCowHeader, nb_snapshots),
                               &snapshot_table_pointer.nb_snapshots,
                               sizeof(snapshot_table_pointer.nb_snapshots));
        if (ret < 0) {
            result->check_errors++;
            fprintf(stderr, "ERROR failed to update the snapshot count in the "
                    "image header: %s\n", strerror(-ret));
            return ret;
        }

        result->corruptions_fixed += nb_clusters_reduced;
        result->corruptions -= nb_clusters_reduced;
    }

    /*
     * All of v3 images' snapshot table entries need to have at least
     * 16 bytes of extra data.
     */
    if (s->qcow_version >= 3) {
        int i;
        for (i = 0; i < s->nb_snapshots; i++) {
            if (s->snapshots[i].extra_data_size <
                sizeof_field(QCowSnapshotExtraData, vm_state_size_large) +
                sizeof_field(QCowSnapshotExtraData, disk_size))
            {
                result->corruptions++;
                fprintf(stderr, "%s snapshot table entry %i is incomplete\n",
                        fix & BDRV_FIX_ERRORS ? "Repairing" : "ERROR", i);
            }
        }
    }

    return 0;
}

int coroutine_fn qcow2_check_fix_snapshot_table(BlockDriverState *bs,
                                                BdrvCheckResult *result,
                                                BdrvCheckMode fix)
{
    BDRVQcow2State *s = bs->opaque;
    int ret;

    if (result->corruptions && (fix & BDRV_FIX_ERRORS)) {
        qemu_co_mutex_unlock(&s->lock);
        ret = qcow2_write_snapshots(bs);
        qemu_co_mutex_lock(&s->lock);
        if (ret < 0) {
            result->check_errors++;
            fprintf(stderr, "ERROR failed to update snapshot table: %s\n",
                    strerror(-ret));
            return ret;
        }

        result->corruptions_fixed += result->corruptions;
        result->corruptions = 0;
    }

    return 0;
}

static void find_new_snapshot_id(BlockDriverState *bs,
                                 char *id_str, int id_str_size)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot *sn;
    int i;
    unsigned long id, id_max = 0;

    for(i = 0; i < s->nb_snapshots; i++) {
        sn = s->snapshots + i;
        id = strtoul(sn->id_str, NULL, 10);
        if (id > id_max)
            id_max = id;
    }
    snprintf(id_str, id_str_size, "%lu", id_max + 1);
}

static int find_snapshot_by_id_and_name(BlockDriverState *bs,
                                        const char *id,
                                        const char *name)
{
    BDRVQcow2State *s = bs->opaque;
    int i;

    if (id && name) {
        for (i = 0; i < s->nb_snapshots; i++) {
            if (!strcmp(s->snapshots[i].id_str, id) &&
                !strcmp(s->snapshots[i].name, name)) {
                return i;
            }
        }
    } else if (id) {
        for (i = 0; i < s->nb_snapshots; i++) {
            if (!strcmp(s->snapshots[i].id_str, id)) {
                return i;
            }
        }
    } else if (name) {
        for (i = 0; i < s->nb_snapshots; i++) {
            if (!strcmp(s->snapshots[i].name, name)) {
                return i;
            }
        }
    }

    return -1;
}

static int find_snapshot_by_id_or_name(BlockDriverState *bs,
                                       const char *id_or_name)
{
    int ret;

    ret = find_snapshot_by_id_and_name(bs, id_or_name, NULL);
    if (ret >= 0) {
        return ret;
    }
    return find_snapshot_by_id_and_name(bs, NULL, id_or_name);
}

/* if no id is provided, a new one is constructed */
int qcow2_snapshot_create(BlockDriverState *bs, QEMUSnapshotInfo *sn_info)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot *new_snapshot_list = NULL;
    QCowSnapshot *old_snapshot_list = NULL;
    QCowSnapshot sn1, *sn = &sn1;
    int i, ret;
    uint64_t *l1_table = NULL;
    int64_t l1_table_offset;

    if (s->nb_snapshots >= QCOW_MAX_SNAPSHOTS) {
        return -EFBIG;
    }

    if (has_data_file(bs)) {
        return -ENOTSUP;
    }

    memset(sn, 0, sizeof(*sn));

    /* Generate an ID */
    find_new_snapshot_id(bs, sn_info->id_str, sizeof(sn_info->id_str));

    /* Populate sn with passed data */
    sn->id_str = g_strdup(sn_info->id_str);
    sn->name = g_strdup(sn_info->name);

    sn->disk_size = bs->total_sectors * BDRV_SECTOR_SIZE;
    sn->vm_state_size = sn_info->vm_state_size;
    sn->date_sec = sn_info->date_sec;
    sn->date_nsec = sn_info->date_nsec;
    sn->vm_clock_nsec = sn_info->vm_clock_nsec;
    sn->extra_data_size = sizeof(QCowSnapshotExtraData);

    /* Allocate the L1 table of the snapshot and copy the current one there. */
    l1_table_offset = qcow2_alloc_clusters(bs, s->l1_size * sizeof(uint64_t));
    if (l1_table_offset < 0) {
        ret = l1_table_offset;
        goto fail;
    }

    sn->l1_table_offset = l1_table_offset;
    sn->l1_size = s->l1_size;

    l1_table = g_try_new(uint64_t, s->l1_size);
    if (s->l1_size && l1_table == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    for(i = 0; i < s->l1_size; i++) {
        l1_table[i] = cpu_to_be64(s->l1_table[i]);
    }

    ret = qcow2_pre_write_overlap_check(bs, 0, sn->l1_table_offset,
                                        s->l1_size * sizeof(uint64_t), false);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_pwrite(bs->file, sn->l1_table_offset, l1_table,
                      s->l1_size * sizeof(uint64_t));
    if (ret < 0) {
        goto fail;
    }

    g_free(l1_table);
    l1_table = NULL;

    /*
     * Increase the refcounts of all clusters and make sure everything is
     * stable on disk before updating the snapshot table to contain a pointer
     * to the new L1 table.
     */
    ret = qcow2_update_snapshot_refcount(bs, s->l1_table_offset, s->l1_size, 1);
    if (ret < 0) {
        goto fail;
    }

    /* Append the new snapshot to the snapshot list */
    new_snapshot_list = g_new(QCowSnapshot, s->nb_snapshots + 1);
    if (s->snapshots) {
        memcpy(new_snapshot_list, s->snapshots,
               s->nb_snapshots * sizeof(QCowSnapshot));
        old_snapshot_list = s->snapshots;
    }
    s->snapshots = new_snapshot_list;
    s->snapshots[s->nb_snapshots++] = *sn;

    ret = qcow2_write_snapshots(bs);
    if (ret < 0) {
        g_free(s->snapshots);
        s->snapshots = old_snapshot_list;
        s->nb_snapshots--;
        goto fail;
    }

    g_free(old_snapshot_list);

    /* The VM state isn't needed any more in the active L1 table; in fact, it
     * hurts by causing expensive COW for the next snapshot. */
    qcow2_cluster_discard(bs, qcow2_vm_state_offset(s),
                          ROUND_UP(sn->vm_state_size, s->cluster_size),
                          QCOW2_DISCARD_NEVER, false);

#ifdef DEBUG_ALLOC
    {
      BdrvCheckResult result = {0};
      qcow2_check_refcounts(bs, &result, 0);
    }
#endif
    return 0;

fail:
    g_free(sn->id_str);
    g_free(sn->name);
    g_free(l1_table);

    return ret;
}

/* copy the snapshot 'snapshot_name' into the current disk image */
int qcow2_snapshot_goto(BlockDriverState *bs, const char *snapshot_id)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot *sn;
    Error *local_err = NULL;
    int i, snapshot_index;
    int cur_l1_bytes, sn_l1_bytes;
    int ret;
    uint64_t *sn_l1_table = NULL;

    if (has_data_file(bs)) {
        return -ENOTSUP;
    }

    /* Search the snapshot */
    snapshot_index = find_snapshot_by_id_or_name(bs, snapshot_id);
    if (snapshot_index < 0) {
        return -ENOENT;
    }
    sn = &s->snapshots[snapshot_index];

    ret = qcow2_validate_table(bs, sn->l1_table_offset, sn->l1_size,
                               sizeof(uint64_t), QCOW_MAX_L1_SIZE,
                               "Snapshot L1 table", &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        goto fail;
    }

    if (sn->disk_size != bs->total_sectors * BDRV_SECTOR_SIZE) {
        error_report("qcow2: Loading snapshots with different disk "
            "size is not implemented");
        ret = -ENOTSUP;
        goto fail;
    }

    /*
     * Make sure that the current L1 table is big enough to contain the whole
     * L1 table of the snapshot. If the snapshot L1 table is smaller, the
     * current one must be padded with zeros.
     */
    ret = qcow2_grow_l1_table(bs, sn->l1_size, true);
    if (ret < 0) {
        goto fail;
    }

    cur_l1_bytes = s->l1_size * sizeof(uint64_t);
    sn_l1_bytes = sn->l1_size * sizeof(uint64_t);

    /*
     * Copy the snapshot L1 table to the current L1 table.
     *
     * Before overwriting the old current L1 table on disk, make sure to
     * increase all refcounts for the clusters referenced by the new one.
     * Decrease the refcount referenced by the old one only when the L1
     * table is overwritten.
     */
    sn_l1_table = g_try_malloc0(cur_l1_bytes);
    if (cur_l1_bytes && sn_l1_table == NULL) {
        ret = -ENOMEM;
        goto fail;
    }

    ret = bdrv_pread(bs->file, sn->l1_table_offset,
                     sn_l1_table, sn_l1_bytes);
    if (ret < 0) {
        goto fail;
    }

    ret = qcow2_update_snapshot_refcount(bs, sn->l1_table_offset,
                                         sn->l1_size, 1);
    if (ret < 0) {
        goto fail;
    }

    ret = qcow2_pre_write_overlap_check(bs, QCOW2_OL_ACTIVE_L1,
                                        s->l1_table_offset, cur_l1_bytes,
                                        false);
    if (ret < 0) {
        goto fail;
    }

    ret = bdrv_pwrite_sync(bs->file, s->l1_table_offset, sn_l1_table,
                           cur_l1_bytes);
    if (ret < 0) {
        goto fail;
    }

    /*
     * Decrease refcount of clusters of current L1 table.
     *
     * At this point, the in-memory s->l1_table points to the old L1 table,
     * whereas on disk we already have the new one.
     *
     * qcow2_update_snapshot_refcount special cases the current L1 table to use
     * the in-memory data instead of really using the offset to load a new one,
     * which is why this works.
     */
    ret = qcow2_update_snapshot_refcount(bs, s->l1_table_offset,
                                         s->l1_size, -1);

    /*
     * Now update the in-memory L1 table to be in sync with the on-disk one. We
     * need to do this even if updating refcounts failed.
     */
    for(i = 0;i < s->l1_size; i++) {
        s->l1_table[i] = be64_to_cpu(sn_l1_table[i]);
    }

    if (ret < 0) {
        goto fail;
    }

    g_free(sn_l1_table);
    sn_l1_table = NULL;

    /*
     * Update QCOW_OFLAG_COPIED in the active L1 table (it may have changed
     * when we decreased the refcount of the old snapshot.
     */
    ret = qcow2_update_snapshot_refcount(bs, s->l1_table_offset, s->l1_size, 0);
    if (ret < 0) {
        goto fail;
    }

#ifdef DEBUG_ALLOC
    {
        BdrvCheckResult result = {0};
        qcow2_check_refcounts(bs, &result, 0);
    }
#endif
    return 0;

fail:
    g_free(sn_l1_table);
    return ret;
}

int qcow2_snapshot_delete(BlockDriverState *bs,
                          const char *snapshot_id,
                          const char *name,
                          Error **errp)
{
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot sn;
    int snapshot_index, ret;

    if (has_data_file(bs)) {
        return -ENOTSUP;
    }

    /* Search the snapshot */
    snapshot_index = find_snapshot_by_id_and_name(bs, snapshot_id, name);
    if (snapshot_index < 0) {
        error_setg(errp, "Can't find the snapshot");
        return -ENOENT;
    }
    sn = s->snapshots[snapshot_index];

    ret = qcow2_validate_table(bs, sn.l1_table_offset, sn.l1_size,
                               sizeof(uint64_t), QCOW_MAX_L1_SIZE,
                               "Snapshot L1 table", errp);
    if (ret < 0) {
        return ret;
    }

    /* Remove it from the snapshot list */
    memmove(s->snapshots + snapshot_index,
            s->snapshots + snapshot_index + 1,
            (s->nb_snapshots - snapshot_index - 1) * sizeof(sn));
    s->nb_snapshots--;
    ret = qcow2_write_snapshots(bs);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Failed to remove snapshot from snapshot list");
        return ret;
    }

    /*
     * The snapshot is now unused, clean up. If we fail after this point, we
     * won't recover but just leak clusters.
     */
    g_free(sn.unknown_extra_data);
    g_free(sn.id_str);
    g_free(sn.name);

    /*
     * Now decrease the refcounts of clusters referenced by the snapshot and
     * free the L1 table.
     */
    ret = qcow2_update_snapshot_refcount(bs, sn.l1_table_offset,
                                         sn.l1_size, -1);
    if (ret < 0) {
        error_setg_errno(errp, -ret, "Failed to free the cluster and L1 table");
        return ret;
    }
    qcow2_free_clusters(bs, sn.l1_table_offset, sn.l1_size * sizeof(uint64_t),
                        QCOW2_DISCARD_SNAPSHOT);

    /* must update the copied flag on the current cluster offsets */
    ret = qcow2_update_snapshot_refcount(bs, s->l1_table_offset, s->l1_size, 0);
    if (ret < 0) {
        error_setg_errno(errp, -ret,
                         "Failed to update snapshot status in disk");
        return ret;
    }

#ifdef DEBUG_ALLOC
    {
        BdrvCheckResult result = {0};
        qcow2_check_refcounts(bs, &result, 0);
    }
#endif
    return 0;
}

int qcow2_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_tab)
{
    BDRVQcow2State *s = bs->opaque;
    QEMUSnapshotInfo *sn_tab, *sn_info;
    QCowSnapshot *sn;
    int i;

    if (has_data_file(bs)) {
        return -ENOTSUP;
    }
    if (!s->nb_snapshots) {
        *psn_tab = NULL;
        return s->nb_snapshots;
    }

    sn_tab = g_new0(QEMUSnapshotInfo, s->nb_snapshots);
    for(i = 0; i < s->nb_snapshots; i++) {
        sn_info = sn_tab + i;
        sn = s->snapshots + i;
        pstrcpy(sn_info->id_str, sizeof(sn_info->id_str),
                sn->id_str);
        pstrcpy(sn_info->name, sizeof(sn_info->name),
                sn->name);
        sn_info->vm_state_size = sn->vm_state_size;
        sn_info->date_sec = sn->date_sec;
        sn_info->date_nsec = sn->date_nsec;
        sn_info->vm_clock_nsec = sn->vm_clock_nsec;
    }
    *psn_tab = sn_tab;
    return s->nb_snapshots;
}

int qcow2_snapshot_load_tmp(BlockDriverState *bs,
                            const char *snapshot_id,
                            const char *name,
                            Error **errp)
{
    int i, snapshot_index;
    BDRVQcow2State *s = bs->opaque;
    QCowSnapshot *sn;
    uint64_t *new_l1_table;
    int new_l1_bytes;
    int ret;

    assert(bs->read_only);

    /* Search the snapshot */
    snapshot_index = find_snapshot_by_id_and_name(bs, snapshot_id, name);
    if (snapshot_index < 0) {
        error_setg(errp,
                   "Can't find snapshot");
        return -ENOENT;
    }
    sn = &s->snapshots[snapshot_index];

    /* Allocate and read in the snapshot's L1 table */
    ret = qcow2_validate_table(bs, sn->l1_table_offset, sn->l1_size,
                               sizeof(uint64_t), QCOW_MAX_L1_SIZE,
                               "Snapshot L1 table", errp);
    if (ret < 0) {
        return ret;
    }
    new_l1_bytes = sn->l1_size * sizeof(uint64_t);
    new_l1_table = qemu_try_blockalign(bs->file->bs, new_l1_bytes);
    if (new_l1_table == NULL) {
        return -ENOMEM;
    }

    ret = bdrv_pread(bs->file, sn->l1_table_offset,
                     new_l1_table, new_l1_bytes);
    if (ret < 0) {
        error_setg(errp, "Failed to read l1 table for snapshot");
        qemu_vfree(new_l1_table);
        return ret;
    }

    /* Switch the L1 table */
    qemu_vfree(s->l1_table);

    s->l1_size = sn->l1_size;
    s->l1_table_offset = sn->l1_table_offset;
    s->l1_table = new_l1_table;

    for(i = 0;i < s->l1_size; i++) {
        be64_to_cpus(&s->l1_table[i]);
    }

    return 0;
}
