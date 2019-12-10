/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef REGS_H
#define REGS_H

#define NUM_GEN_REGS 32

typedef struct {
    const char *name;
    int offset;
    int width;
    const char *description;
} reg_field_t;

extern reg_field_t reg_field_info[];

enum reg_fields_enum {
#define DEF_REG_FIELD(TAG, NAME, START, WIDTH, DESCRIPTION) \
    TAG,
#include "reg_fields_def.h"
    NUM_REG_FIELDS
#undef DEF_REG_FIELD
};

#endif
