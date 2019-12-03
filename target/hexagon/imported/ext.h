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

#ifndef EXT_H
#define EXT_H 1

enum {
#define DEF_EXT(NAME,START,NUM) EXT_IDX_##NAME = START, EXT_IDX_##NAME##_AFTER = (START+NUM),
#include "ext.def"
#undef DEF_EXT
	XX_LAST_EXT_IDX
};

enum {
#define DEF_EXT(NAME,START,NUM) EXTOPSTAB_IDX_##NAME,
#include "ext.def"
#undef DEF_EXT
	XX_LAST_EXTOPSTAB_IDX
};

#endif
