/*
 * QEMU OpenGL extensions
 *
 * Copyright (c) 2015 espes
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
#ifndef GLEXTEENSIONS_H_
#define GLEXTEENSIONS_H_

#ifdef __APPLE__
#include "gl/gloffscreen.h"
extern void (*glFrameTerminatorGREMEDY)(void);

#define GL_DEBUG_SOURCE_APPLICATION       0x824A
#define GL_DEBUG_TYPE_MARKER              0x8268
#define GL_DEBUG_SEVERITY_NOTIFICATION    0x826B
#define GL_DEBUG_OUTPUT                   0x92E0

extern void (*glDebugMessageInsert) (GLenum source, GLenum type, GLuint id,
                                     GLenum severity, GLsizei length,
                                     const GLchar *buf);
extern void (*glPushDebugGroup)(GLenum source, GLuint id, GLsizei length,
                                const GLchar *message);
extern void (*glPopDebugGroup)(void);
extern void (*glObjectLabel)(GLenum identifier, GLuint name, GLsizei length,
                             const GLchar *label);

#endif

void glextensions_init(void);

#endif
