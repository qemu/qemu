#ifndef HW_FLASH_H
#define HW_FLASH_H

/* NOR flash devices */

#include "exec/hwaddr.h"
#include "qom/object.h"

/* pflash_cfi01.c */

#define TYPE_PFLASH_CFI01 "cfi.pflash01"
OBJECT_DECLARE_SIMPLE_TYPE(PFlashCFI01, PFLASH_CFI01)


PFlashCFI01 *pflash_cfi01_register(hwaddr base,
                                   const char *name,
                                   hwaddr size,
                                   BlockBackend *blk,
                                   uint32_t sector_len,
                                   int width,
                                   uint16_t id0, uint16_t id1,
                                   uint16_t id2, uint16_t id3,
                                   int be);
BlockBackend *pflash_cfi01_get_blk(PFlashCFI01 *fl);
MemoryRegion *pflash_cfi01_get_memory(PFlashCFI01 *fl);
void pflash_cfi01_legacy_drive(PFlashCFI01 *dev, DriveInfo *dinfo);

/* pflash_cfi02.c */

#define TYPE_PFLASH_CFI02 "cfi.pflash02"
OBJECT_DECLARE_SIMPLE_TYPE(PFlashCFI02, PFLASH_CFI02)


PFlashCFI02 *pflash_cfi02_register(hwaddr base,
                                   const char *name,
                                   hwaddr size,
                                   BlockBackend *blk,
                                   uint32_t sector_len,
                                   int nb_mappings,
                                   int width,
                                   uint16_t id0, uint16_t id1,
                                   uint16_t id2, uint16_t id3,
                                   uint16_t unlock_addr0,
                                   uint16_t unlock_addr1,
                                   int be);

/* m25p80.c */

#define TYPE_M25P80 "m25p80-generic"

BlockBackend *m25p80_get_blk(DeviceState *dev);

#endif
