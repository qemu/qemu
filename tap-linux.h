/*
 *  Universal TUN/TAP device driver.
 *  Copyright (C) 1999-2000 Maxim Krasnyansky <max_mk@yahoo.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 */

#ifndef QEMU_TAP_H
#define QEMU_TAP_H

#include <linux/ioctl.h>

/* Ioctl defines */
#define TUNSETIFF     _IOW('T', 202, int)
#define TUNSETSNDBUF   _IOW('T', 212, int)

/* TUNSETIFF ifr flags */
#define IFF_TAP		0x0002
#define IFF_NO_PI	0x1000

#endif /* QEMU_TAP_H */
