/*
 *  SCSI helpers
 *
 *  Copyright 2017 Red Hat, Inc.
 *
 *  Authors:
 *   Fam Zheng <famz@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#ifndef QEMU_SCSI_H
#define QEMU_SCSI_H

int scsi_sense_to_errno(int key, int asc, int ascq);
int scsi_sense_buf_to_errno(const uint8_t *sense, size_t sense_size);

#endif
