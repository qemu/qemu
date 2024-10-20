/*
 * S390 IPL (boot) from a real DASD device via vfio framework.
 *
 * Copyright (c) 2019 Jason J. Herne <jjherne@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#ifndef DASD_IPL_H
#define DASD_IPL_H

int dasd_ipl(SubChannelId schid, uint16_t cutype);

#endif /* DASD_IPL_H */
