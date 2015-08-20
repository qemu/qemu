/*
 * QEMU Geforce NV2A shader common definitions
 *
 * Copyright (c) 2015 espes
 * Copyright (c) 2015 Jannik Vogel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_NV2A_SHADERS_COMMON_H
#define HW_NV2A_SHADERS_COMMON_H

#define STRUCT_VERTEX_DATA "struct VertexData {\n" \
                           "  float inv_w;\n" \
                           "  vec4 D0;\n" \
                           "  vec4 D1;\n" \
                           "  vec4 B0;\n" \
                           "  vec4 B1;\n" \
                           "  float Fog;\n" \
                           "  vec4 T0;\n" \
                           "  vec4 T1;\n" \
                           "  vec4 T2;\n" \
                           "  vec4 T3;\n" \
                           "};\n"

#endif
