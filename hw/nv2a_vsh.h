/*
 * QEMU Geforce NV2A vertex shader translation
 *
 * Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_NV2A_VSH_H
#define HW_NV2A_VSH_H

#include "qapi/qmp/qstring.h"

// vs.1.1, not an official value
#define VSH_VERSION_VS                     0xF078

// Xbox vertex shader
#define VSH_VERSION_XVS                    0x2078

// Xbox vertex state shader
#define VSH_VERSION_XVSS                   0x7378

// Xbox vertex read/write shader
#define VSH_VERSION_XVSW                   0x7778


#define VSH_D3DSCM_CORRECTION 96

QString* vsh_translate(uint16_t version,
                       uint32_t *tokens, unsigned int tokens_length);


#endif