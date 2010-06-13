/*
 * Virtio 9p
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_9P_H
#define QEMU_9P_H

typedef struct V9fsConf
{
    /* tag name for the device */
    char *tag;
    char *fsdev_id;
} V9fsConf;

#endif
