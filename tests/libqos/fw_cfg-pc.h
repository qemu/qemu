/*
 * libqos fw_cfg support for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_FW_CFG_PC_H
#define LIBQOS_FW_CFG_PC_H

#include "libqos/fw_cfg.h"

QFWCFG *pc_fw_cfg_init(void);

#endif
