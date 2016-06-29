/*
 * QEMU VNC display driver: Websockets support
 *
 * Copyright (C) 2010 Joel Martin
 * Copyright (C) 2012 Tim Hardeck
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_UI_VNC_WS_H
#define QEMU_UI_VNC_WS_H

gboolean vncws_tls_handshake_io(QIOChannel *ioc,
                                GIOCondition condition,
                                void *opaque);
gboolean vncws_handshake_io(QIOChannel *ioc,
                            GIOCondition condition,
                            void *opaque);

#endif /* QEMU_UI_VNC_WS_H */
