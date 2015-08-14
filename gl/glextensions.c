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
#include "gloffscreen.h"
#include "glextensions.h"

#ifdef __APPLE__
void (*glFrameTerminatorGREMEDY)(void);

void (*glDebugMessageInsert) (GLenum source, GLenum type, GLuint id,
                              GLenum severity, GLsizei length,
                              const GLchar *buf);
void (*glPushDebugGroup)(GLenum source, GLuint id, GLsizei length,
                         const GLchar *message);
void (*glPopDebugGroup)(void);
void (*glObjectLabel)(GLenum identifier, GLuint name, GLsizei length,
                      const GLchar *label);

#endif

void glextensions_init(void)
{
#ifdef __APPLE__
    glFrameTerminatorGREMEDY =
        glo_get_extension_proc("glFrameTerminatorGREMEDY");
    glDebugMessageInsert = glo_get_extension_proc("glDebugMessageInsert");
    glPushDebugGroup = glo_get_extension_proc("glPushDebugGroup");
    glPopDebugGroup = glo_get_extension_proc("glPopDebugGroup");
    glObjectLabel = glo_get_extension_proc("glObjectLabel");
#endif
}
