/*
 * QEMU Geforce NV2A debug helpers
 *
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2012 espes
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

#ifndef HW_NV2A_DEBUG_H
#define HW_NV2A_DEBUG_H

// #define DEBUG_NV2A
#ifdef DEBUG_NV2A
# define NV2A_DPRINTF(format, ...)       printf("nv2a: " format, ## __VA_ARGS__)
#else
# define NV2A_DPRINTF(format, ...)       do { } while (0)
#endif

// #define DEBUG_NV2A_GL
#ifdef DEBUG_NV2A_GL

#include <stdbool.h>
#include "gl/gloffscreen.h"

void gl_debug_message(bool cc, const char *fmt, ...);
void gl_debug_group_begin(const char *fmt, ...);
void gl_debug_group_end(void);
void gl_debug_label(GLenum target, GLuint name, const char *fmt, ...);

# define NV2A_GL_DPRINTF(cc, format, ...) \
    gl_debug_message(cc, "nv2a: " format, ## __VA_ARGS__)
# define NV2A_GL_DGROUP_BEGIN(format, ...) \
    gl_debug_group_begin("nv2a: " format, ## __VA_ARGS__)
# define NV2A_GL_DGROUP_END() \
    gl_debug_group_end()
# define NV2A_GL_DLABEL(target, name, format, ...)  \
    gl_debug_label(target, name, "nv2a: { " format " }", ## __VA_ARGS__)

#else
# define NV2A_GL_DPRINTF(cc, format, ...)          do { \
        if (cc) NV2A_DPRINTF(format "\n", ##__VA_ARGS__ ); \
    } while (0)
# define NV2A_GL_DGROUP_BEGIN(format, ...)         do { } while (0)
# define NV2A_GL_DGROUP_END()                      do { } while (0)
# define NV2A_GL_DLABEL(target, name, format, ...) do { } while (0)
#endif

#endif
