/*
 * QEMU interface to CFI1 und CFI2 flash emulation.
 *
 * Copyright (c) 2006 Stefan Weil
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "pflash.h"

pflash_t *pflash_register (target_ulong base, ram_addr_t off,
                           BlockDriverState *bs, int width,
                           uint16_t flash_manufacturer, uint16_t flash_type)
{
    /* The values for blocksize and nblocks are defaults which must be
       replaced by the correct values based on flash manufacturer and type.
       This is done by the cfi1 and cfi2 emulation code. */
    const target_ulong blocksize = 0x10000;
    const unsigned nblocks = 0;
    const uint16_t id2 = 0x33;
    const uint16_t id3 = 0x44;
    pflash_t *pf;
    switch (flash_manufacturer) {
        case MANUFACTURER_AMD:
        case 0x4a:  /* Which manufacturer is this? */
            pf = pflash_amd_register(base, off, bs, blocksize, nblocks, width,
                    flash_manufacturer, flash_type, id2, id3);
            break;
        case MANUFACTURER_INTEL:
        case MANUFACTURER_MACRONIX:
            pf = pflash_cfi01_register(base, off, bs, blocksize, nblocks, width,
                    flash_manufacturer, flash_type, id2, id3);
            break;
        default:
            pf = pflash_cfi02_register(base, off, bs, blocksize, nblocks, width,
                    flash_manufacturer, flash_type, id2, id3);
    }
    return pf;
}
