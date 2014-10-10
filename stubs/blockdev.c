#include <assert.h>
#include "sysemu/blockdev.h"

DriveInfo *drive_get_by_blockdev(BlockDriverState *bs)
{
    return NULL;
}

void drive_info_del(DriveInfo *dinfo)
{
    assert(!dinfo);
}
