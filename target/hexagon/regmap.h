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

/*
 *  Certain operand types represent a non-contiguous set of values.
 *  For example, the compound compare-and-jump instruction can only access
 *  registers R0-R7 and R16-23.
 *  This table represents the mapping from the encoding to the actual values.
 */

#ifndef REGMAP_H
#define REGMAP_H

        /* Name   Num Table */
DEF_REGMAP(R_16,  16, 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23)
DEF_REGMAP(R__8,  8,  0, 2, 4, 6, 16, 18, 20, 22)
DEF_REGMAP(R__4,  4,  0, 2, 4, 6)
DEF_REGMAP(R_4,   4,  0, 1, 2, 3)
DEF_REGMAP(R_8S,  8,  0, 1, 2, 3, 16, 17, 18, 19)
DEF_REGMAP(R_8,   8,  0, 1, 2, 3, 4, 5, 6, 7)
DEF_REGMAP(V__8,  8,  0, 4, 8, 12, 16, 20, 24, 28)
DEF_REGMAP(V__16, 16, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30)

#endif
