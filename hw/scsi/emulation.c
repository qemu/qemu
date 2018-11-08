#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/bswap.h"
#include "hw/scsi/emulation.h"

int scsi_emulate_block_limits(uint8_t *outbuf, const SCSIBlockLimits *bl)
{
    /* required VPD size with unmap support */
    memset(outbuf, 0, 0x3c);

    outbuf[0] = bl->wsnz; /* wsnz */

    if (bl->max_io_sectors) {
        /* optimal transfer length granularity.  This field and the optimal
         * transfer length can't be greater than maximum transfer length.
         */
        stw_be_p(outbuf + 2, MIN(bl->min_io_size, bl->max_io_sectors));

        /* maximum transfer length */
        stl_be_p(outbuf + 4, bl->max_io_sectors);

        /* optimal transfer length */
        stl_be_p(outbuf + 8, MIN(bl->opt_io_size, bl->max_io_sectors));
    } else {
        stw_be_p(outbuf + 2, bl->min_io_size);
        stl_be_p(outbuf + 8, bl->opt_io_size);
    }

    /* max unmap LBA count */
    stl_be_p(outbuf + 16, bl->max_unmap_sectors);

    /* max unmap descriptors */
    stl_be_p(outbuf + 20, bl->max_unmap_descr);

    /* optimal unmap granularity; alignment is zero */
    stl_be_p(outbuf + 24, bl->unmap_sectors);

    /* max write same size, make it the same as maximum transfer length */
    stl_be_p(outbuf + 36, bl->max_io_sectors);

    return 0x3c;
}
