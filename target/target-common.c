/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/target_page.h"

int qemu_target_page_mask(void)
{
    return TARGET_PAGE_MASK;
}
