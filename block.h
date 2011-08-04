#ifndef BLOCK_H
#define BLOCK_H

#include "qemu-aio.h"
#include "qemu-common.h"
#include "qemu-option.h"
#include "qemu-coroutine.h"
#include "qobject.h"

/* block.c */
typedef struct BlockDriver BlockDriver;

typedef struct BlockDriverInfo {
    /* in bytes, 0 if irrelevant */
    int cluster_size;
    /* offset at which the VM state can be saved (0 if not possible) */
    int64_t vm_state_offset;
} BlockDriverInfo;

typedef struct QEMUSnapshotInfo {
    char id_str[128]; /* unique snapshot id */
    /* the following fields are informative. They are not needed for
       the consistency of the snapshot */
    char name[256]; /* user choosen name */
    uint32_t vm_state_size; /* VM state info size */
    uint32_t date_sec; /* UTC date of the snapshot */
    uint32_t date_nsec;
    uint64_t vm_clock_nsec; /* VM clock relative to boot */
} QEMUSnapshotInfo;

#define BDRV_O_RDWR        0x0002
#define BDRV_O_SNAPSHOT    0x0008 /* open the file read only and save writes in a snapshot */
#define BDRV_O_NOCACHE     0x0020 /* do not use the host page cache */
#define BDRV_O_CACHE_WB    0x0040 /* use write-back caching */
#define BDRV_O_NATIVE_AIO  0x0080 /* use native AIO instead of the thread pool */
#define BDRV_O_NO_BACKING  0x0100 /* don't open the backing file */
#define BDRV_O_NO_FLUSH    0x0200 /* disable flushing on this disk */

#define BDRV_O_CACHE_MASK  (BDRV_O_NOCACHE | BDRV_O_CACHE_WB | BDRV_O_NO_FLUSH)

#define BDRV_SECTOR_BITS   9
#define BDRV_SECTOR_SIZE   (1ULL << BDRV_SECTOR_BITS)
#define BDRV_SECTOR_MASK   ~(BDRV_SECTOR_SIZE - 1)

typedef enum {
    BLOCK_ERR_REPORT, BLOCK_ERR_IGNORE, BLOCK_ERR_STOP_ENOSPC,
    BLOCK_ERR_STOP_ANY
} BlockErrorAction;

typedef enum {
    BDRV_ACTION_REPORT, BDRV_ACTION_IGNORE, BDRV_ACTION_STOP
} BlockMonEventAction;

void bdrv_mon_event(const BlockDriverState *bdrv,
                    BlockMonEventAction action, int is_read);
void bdrv_info_print(Monitor *mon, const QObject *data);
void bdrv_info(Monitor *mon, QObject **ret_data);
void bdrv_stats_print(Monitor *mon, const QObject *data);
void bdrv_info_stats(Monitor *mon, QObject **ret_data);

void bdrv_init(void);
void bdrv_init_with_whitelist(void);
BlockDriver *bdrv_find_protocol(const char *filename);
BlockDriver *bdrv_find_format(const char *format_name);
BlockDriver *bdrv_find_whitelisted_format(const char *format_name);
int bdrv_create(BlockDriver *drv, const char* filename,
    QEMUOptionParameter *options);
int bdrv_create_file(const char* filename, QEMUOptionParameter *options);
BlockDriverState *bdrv_new(const char *device_name);
void bdrv_make_anon(BlockDriverState *bs);
void bdrv_delete(BlockDriverState *bs);
int bdrv_parse_cache_flags(const char *mode, int *flags);
int bdrv_file_open(BlockDriverState **pbs, const char *filename, int flags);
int bdrv_open(BlockDriverState *bs, const char *filename, int flags,
              BlockDriver *drv);
void bdrv_close(BlockDriverState *bs);
int bdrv_attach(BlockDriverState *bs, DeviceState *qdev);
void bdrv_detach(BlockDriverState *bs, DeviceState *qdev);
DeviceState *bdrv_get_attached(BlockDriverState *bs);
int bdrv_read(BlockDriverState *bs, int64_t sector_num,
              uint8_t *buf, int nb_sectors);
int bdrv_write(BlockDriverState *bs, int64_t sector_num,
               const uint8_t *buf, int nb_sectors);
int bdrv_pread(BlockDriverState *bs, int64_t offset,
               void *buf, int count);
int bdrv_pwrite(BlockDriverState *bs, int64_t offset,
                const void *buf, int count);
int bdrv_pwrite_sync(BlockDriverState *bs, int64_t offset,
    const void *buf, int count);
int coroutine_fn bdrv_co_readv(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov);
int coroutine_fn bdrv_co_writev(BlockDriverState *bs, int64_t sector_num,
    int nb_sectors, QEMUIOVector *qiov);
int bdrv_truncate(BlockDriverState *bs, int64_t offset);
int64_t bdrv_getlength(BlockDriverState *bs);
int64_t bdrv_get_allocated_file_size(BlockDriverState *bs);
void bdrv_get_geometry(BlockDriverState *bs, uint64_t *nb_sectors_ptr);
void bdrv_guess_geometry(BlockDriverState *bs, int *pcyls, int *pheads, int *psecs);
int bdrv_commit(BlockDriverState *bs);
void bdrv_commit_all(void);
int bdrv_change_backing_file(BlockDriverState *bs,
    const char *backing_file, const char *backing_fmt);
void bdrv_register(BlockDriver *bdrv);


typedef struct BdrvCheckResult {
    int corruptions;
    int leaks;
    int check_errors;
} BdrvCheckResult;

int bdrv_check(BlockDriverState *bs, BdrvCheckResult *res);

/* async block I/O */
typedef struct BlockDriverAIOCB BlockDriverAIOCB;
typedef void BlockDriverCompletionFunc(void *opaque, int ret);
typedef void BlockDriverDirtyHandler(BlockDriverState *bs, int64_t sector,
                                     int sector_num);
BlockDriverAIOCB *bdrv_aio_readv(BlockDriverState *bs, int64_t sector_num,
                                 QEMUIOVector *iov, int nb_sectors,
                                 BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_writev(BlockDriverState *bs, int64_t sector_num,
                                  QEMUIOVector *iov, int nb_sectors,
                                  BlockDriverCompletionFunc *cb, void *opaque);
BlockDriverAIOCB *bdrv_aio_flush(BlockDriverState *bs,
                                 BlockDriverCompletionFunc *cb, void *opaque);
void bdrv_aio_cancel(BlockDriverAIOCB *acb);

typedef struct BlockRequest {
    /* Fields to be filled by multiwrite caller */
    int64_t sector;
    int nb_sectors;
    QEMUIOVector *qiov;
    BlockDriverCompletionFunc *cb;
    void *opaque;

    /* Filled by multiwrite implementation */
    int error;
} BlockRequest;

int bdrv_aio_multiwrite(BlockDriverState *bs, BlockRequest *reqs,
    int num_reqs);

/* sg packet commands */
int bdrv_ioctl(BlockDriverState *bs, unsigned long int req, void *buf);
BlockDriverAIOCB *bdrv_aio_ioctl(BlockDriverState *bs,
        unsigned long int req, void *buf,
        BlockDriverCompletionFunc *cb, void *opaque);

/* Ensure contents are flushed to disk.  */
int bdrv_flush(BlockDriverState *bs);
void bdrv_flush_all(void);
void bdrv_close_all(void);

int bdrv_discard(BlockDriverState *bs, int64_t sector_num, int nb_sectors);
int bdrv_has_zero_init(BlockDriverState *bs);
int bdrv_is_allocated(BlockDriverState *bs, int64_t sector_num, int nb_sectors,
                      int *pnum);

#define BIOS_ATA_TRANSLATION_AUTO   0
#define BIOS_ATA_TRANSLATION_NONE   1
#define BIOS_ATA_TRANSLATION_LBA    2
#define BIOS_ATA_TRANSLATION_LARGE  3
#define BIOS_ATA_TRANSLATION_RECHS  4

void bdrv_set_geometry_hint(BlockDriverState *bs,
                            int cyls, int heads, int secs);
void bdrv_set_translation_hint(BlockDriverState *bs, int translation);
void bdrv_get_geometry_hint(BlockDriverState *bs,
                            int *pcyls, int *pheads, int *psecs);
typedef enum FDriveType {
    FDRIVE_DRV_144  = 0x00,   /* 1.44 MB 3"5 drive      */
    FDRIVE_DRV_288  = 0x01,   /* 2.88 MB 3"5 drive      */
    FDRIVE_DRV_120  = 0x02,   /* 1.2  MB 5"25 drive     */
    FDRIVE_DRV_NONE = 0x03,   /* No drive connected     */
} FDriveType;

void bdrv_get_floppy_geometry_hint(BlockDriverState *bs, int *nb_heads,
                                   int *max_track, int *last_sect,
                                   FDriveType drive_in, FDriveType *drive);
int bdrv_get_translation_hint(BlockDriverState *bs);
void bdrv_set_on_error(BlockDriverState *bs, BlockErrorAction on_read_error,
                       BlockErrorAction on_write_error);
BlockErrorAction bdrv_get_on_error(BlockDriverState *bs, int is_read);
void bdrv_set_removable(BlockDriverState *bs, int removable);
int bdrv_is_removable(BlockDriverState *bs);
int bdrv_is_read_only(BlockDriverState *bs);
int bdrv_is_sg(BlockDriverState *bs);
int bdrv_enable_write_cache(BlockDriverState *bs);
int bdrv_is_inserted(BlockDriverState *bs);
int bdrv_media_changed(BlockDriverState *bs);
int bdrv_is_locked(BlockDriverState *bs);
void bdrv_set_locked(BlockDriverState *bs, int locked);
int bdrv_eject(BlockDriverState *bs, int eject_flag);
void bdrv_set_change_cb(BlockDriverState *bs,
                        void (*change_cb)(void *opaque, int reason),
                        void *opaque);
void bdrv_get_format(BlockDriverState *bs, char *buf, int buf_size);
BlockDriverState *bdrv_find(const char *name);
BlockDriverState *bdrv_next(BlockDriverState *bs);
void bdrv_iterate(void (*it)(void *opaque, BlockDriverState *bs),
                  void *opaque);
int bdrv_is_encrypted(BlockDriverState *bs);
int bdrv_key_required(BlockDriverState *bs);
int bdrv_set_key(BlockDriverState *bs, const char *key);
int bdrv_query_missing_keys(void);
void bdrv_iterate_format(void (*it)(void *opaque, const char *name),
                         void *opaque);
const char *bdrv_get_device_name(BlockDriverState *bs);
int bdrv_write_compressed(BlockDriverState *bs, int64_t sector_num,
                          const uint8_t *buf, int nb_sectors);
int bdrv_get_info(BlockDriverState *bs, BlockDriverInfo *bdi);

const char *bdrv_get_encrypted_filename(BlockDriverState *bs);
void bdrv_get_backing_filename(BlockDriverState *bs,
                               char *filename, int filename_size);
int bdrv_can_snapshot(BlockDriverState *bs);
int bdrv_is_snapshot(BlockDriverState *bs);
BlockDriverState *bdrv_snapshots(void);
int bdrv_snapshot_create(BlockDriverState *bs,
                         QEMUSnapshotInfo *sn_info);
int bdrv_snapshot_goto(BlockDriverState *bs,
                       const char *snapshot_id);
int bdrv_snapshot_delete(BlockDriverState *bs, const char *snapshot_id);
int bdrv_snapshot_list(BlockDriverState *bs,
                       QEMUSnapshotInfo **psn_info);
int bdrv_snapshot_load_tmp(BlockDriverState *bs,
                           const char *snapshot_name);
char *bdrv_snapshot_dump(char *buf, int buf_size, QEMUSnapshotInfo *sn);

char *get_human_readable_size(char *buf, int buf_size, int64_t size);
int path_is_absolute(const char *path);
void path_combine(char *dest, int dest_size,
                  const char *base_path,
                  const char *filename);

int bdrv_save_vmstate(BlockDriverState *bs, const uint8_t *buf,
                      int64_t pos, int size);

int bdrv_load_vmstate(BlockDriverState *bs, uint8_t *buf,
                      int64_t pos, int size);

int bdrv_img_create(const char *filename, const char *fmt,
                    const char *base_filename, const char *base_fmt,
                    char *options, uint64_t img_size, int flags);

#define BDRV_SECTORS_PER_DIRTY_CHUNK 2048

void bdrv_set_dirty_tracking(BlockDriverState *bs, int enable);
int bdrv_get_dirty(BlockDriverState *bs, int64_t sector);
void bdrv_reset_dirty(BlockDriverState *bs, int64_t cur_sector,
                      int nr_sectors);
int64_t bdrv_get_dirty_count(BlockDriverState *bs);

void bdrv_set_in_use(BlockDriverState *bs, int in_use);
int bdrv_in_use(BlockDriverState *bs);

typedef enum {
    BLKDBG_L1_UPDATE,

    BLKDBG_L1_GROW_ALLOC_TABLE,
    BLKDBG_L1_GROW_WRITE_TABLE,
    BLKDBG_L1_GROW_ACTIVATE_TABLE,

    BLKDBG_L2_LOAD,
    BLKDBG_L2_UPDATE,
    BLKDBG_L2_UPDATE_COMPRESSED,
    BLKDBG_L2_ALLOC_COW_READ,
    BLKDBG_L2_ALLOC_WRITE,

    BLKDBG_READ,
    BLKDBG_READ_AIO,
    BLKDBG_READ_BACKING,
    BLKDBG_READ_BACKING_AIO,
    BLKDBG_READ_COMPRESSED,

    BLKDBG_WRITE_AIO,
    BLKDBG_WRITE_COMPRESSED,

    BLKDBG_VMSTATE_LOAD,
    BLKDBG_VMSTATE_SAVE,

    BLKDBG_COW_READ,
    BLKDBG_COW_WRITE,

    BLKDBG_REFTABLE_LOAD,
    BLKDBG_REFTABLE_GROW,

    BLKDBG_REFBLOCK_LOAD,
    BLKDBG_REFBLOCK_UPDATE,
    BLKDBG_REFBLOCK_UPDATE_PART,
    BLKDBG_REFBLOCK_ALLOC,
    BLKDBG_REFBLOCK_ALLOC_HOOKUP,
    BLKDBG_REFBLOCK_ALLOC_WRITE,
    BLKDBG_REFBLOCK_ALLOC_WRITE_BLOCKS,
    BLKDBG_REFBLOCK_ALLOC_WRITE_TABLE,
    BLKDBG_REFBLOCK_ALLOC_SWITCH_TABLE,

    BLKDBG_CLUSTER_ALLOC,
    BLKDBG_CLUSTER_ALLOC_BYTES,
    BLKDBG_CLUSTER_FREE,

    BLKDBG_EVENT_MAX,
} BlkDebugEvent;

#define BLKDBG_EVENT(bs, evt) bdrv_debug_event(bs, evt)
void bdrv_debug_event(BlockDriverState *bs, BlkDebugEvent event);

#endif
