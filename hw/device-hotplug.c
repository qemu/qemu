/*
 * QEMU device hotplug helpers
 *
 * Copyright (c) 2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw.h"
#include "boards.h"
#include "net.h"
#include "block_int.h"
#include "sysemu.h"

DriveInfo *add_init_drive(const char *opts)
{
    int drive_opt_idx;
    int fatal_error;
    DriveInfo *dinfo;

    drive_opt_idx = drive_add(NULL, "%s", opts);
    if (!drive_opt_idx)
        return NULL;

    dinfo = drive_init(&drives_opt[drive_opt_idx], 0, current_machine, &fatal_error);
    if (!dinfo) {
        drive_remove(drive_opt_idx);
        return NULL;
    }

    return dinfo;
}

void destroy_nic(dev_match_fn *match_fn, void *arg)
{
    int i;
    NICInfo *nic;

    for (i = 0; i < MAX_NICS; i++) {
        nic = &nd_table[i];
        if (nic->used) {
            if (nic->private && match_fn(nic->private, arg)) {
                qemu_del_vlan_client(nic->vc);
                net_client_uninit(nic);
            }
        }
    }
}

void destroy_bdrvs(dev_match_fn *match_fn, void *arg)
{
    DriveInfo *dinfo;
    struct BlockDriverState *bs;

    TAILQ_FOREACH(dinfo, &drives, next) {
        bs = dinfo->bdrv;
        if (bs) {
            if (bs->private && match_fn(bs->private, arg)) {
                drive_uninit(bs);
                bdrv_delete(bs);
            }
        }
    }
}


