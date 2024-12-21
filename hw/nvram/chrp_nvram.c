/*
 * Common Hardware Reference Platform NVRAM helper functions.
 *
 * The CHRP NVRAM layout is used by OpenBIOS and SLOF. See CHRP
 * specification, chapter 8, or the LoPAPR specification for details
 * about the NVRAM layout.
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "hw/nvram/chrp_nvram.h"
#include "system/system.h"

static int chrp_nvram_set_var(uint8_t *nvram, int addr, const char *str,
                              int max_len)
{
    int len;

    len = strlen(str) + 1;

    if (max_len < len) {
        return -1;
    }

    memcpy(&nvram[addr], str, len);

    return addr + len;
}

/**
 * Create a "system partition", used for the Open Firmware
 * environment variables.
 */
int chrp_nvram_create_system_partition(uint8_t *data, int min_len, int max_len)
{
    ChrpNvramPartHdr *part_header;
    unsigned int i;
    int end;

    if (max_len < sizeof(*part_header)) {
        goto fail;
    }

    part_header = (ChrpNvramPartHdr *)data;
    part_header->signature = CHRP_NVPART_SYSTEM;
    pstrcpy(part_header->name, sizeof(part_header->name), "system");

    end = sizeof(ChrpNvramPartHdr);
    for (i = 0; i < nb_prom_envs; i++) {
        end = chrp_nvram_set_var(data, end, prom_envs[i], max_len - end);
        if (end == -1) {
            goto fail;
        }
    }

    /* End marker */
    data[end++] = '\0';

    end = (end + 15) & ~15;
    /* XXX: OpenBIOS is not able to grow up a partition. Leave some space for
       new variables. */
    if (end < min_len) {
        end = min_len;
    }
    chrp_nvram_finish_partition(part_header, end);

    return end;

fail:
    error_report("NVRAM is too small. Try to pass less data to -prom-env");
    exit(EXIT_FAILURE);
}

/**
 * Create a "free space" partition
 */
int chrp_nvram_create_free_partition(uint8_t *data, int len)
{
    ChrpNvramPartHdr *part_header;

    part_header = (ChrpNvramPartHdr *)data;
    part_header->signature = CHRP_NVPART_FREE;
    pstrcpy(part_header->name, sizeof(part_header->name), "free");

    chrp_nvram_finish_partition(part_header, len);

    return len;
}
