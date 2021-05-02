/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_REG_FIELDS_H
#define HEXAGON_REG_FIELDS_H

typedef struct {
    int offset;
    int width;
} RegField;

enum {
#define DEF_REG_FIELD(TAG, START, WIDTH) \
    TAG,
#include "reg_fields_def.h.inc"
    NUM_REG_FIELDS
#undef DEF_REG_FIELD
};

extern const RegField reg_field_info[NUM_REG_FIELDS];

#endif
