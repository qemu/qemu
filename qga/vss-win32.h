/*
 * QEMU Guest Agent VSS utility declarations
 *
 * Copyright Hitachi Data Systems Corp. 2013
 *
 * Authors:
 *  Tomoki Sekiyama   <tomoki.sekiyama@hds.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef VSS_WIN32_H
#define VSS_WIN32_H

#include "qga/vss-win32/vss-handles.h"

bool vss_init(bool init_requester);
void vss_deinit(bool deinit_requester);
bool vss_initialized(void);

int ga_install_vss_provider(void);
void ga_uninstall_vss_provider(void);

void qga_vss_fsfreeze(int *nr_volume, bool freeze,
                      strList *mountpints, Error **errp);

#endif
