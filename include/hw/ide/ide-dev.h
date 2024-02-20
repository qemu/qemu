/*
 * ide device definitions
 *
 * Copyright (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This code is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IDE_DEV_H
#define IDE_DEV_H

#include "hw/qdev-properties.h"
#include "hw/block/block.h"
#include "hw/ide/internal.h"

typedef struct IDEDrive {
    IDEDevice dev;
} IDEDrive;

#define DEFINE_IDE_DEV_PROPERTIES()                     \
    DEFINE_BLOCK_PROPERTIES(IDEDrive, dev.conf),        \
    DEFINE_BLOCK_ERROR_PROPERTIES(IDEDrive, dev.conf),  \
    DEFINE_PROP_STRING("ver",  IDEDrive, dev.version),  \
    DEFINE_PROP_UINT64("wwn",  IDEDrive, dev.wwn, 0),   \
    DEFINE_PROP_STRING("serial",  IDEDrive, dev.serial),\
    DEFINE_PROP_STRING("model", IDEDrive, dev.model)

void ide_dev_initfn(IDEDevice *dev, IDEDriveKind kind, Error **errp);

#endif
