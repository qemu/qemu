/*
 * QEMU SEV system stub
 *
 * Copyright Advanced Micro Devices 2018
 *
 * Authors:
 *      Brijesh Singh <brijesh.singh@amd.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "monitor/monitor.h"
#include "monitor/hmp-target.h"
#include "qapi/error.h"
#include "sev.h"

int sev_encrypt_flash(hwaddr gpa, uint8_t *ptr, uint64_t len, Error **errp)
{
    g_assert_not_reached();
}

void sev_es_set_reset_vector(CPUState *cpu)
{
}

int sev_es_save_reset_vector(void *flash_ptr, uint64_t flash_size)
{
    g_assert_not_reached();
}

void hmp_info_sev(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "SEV is not available in this QEMU\n");
}

void pc_system_parse_sev_metadata(uint8_t *flash_ptr, size_t flash_size)
{
}
