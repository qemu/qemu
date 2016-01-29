/*
 * QEMU opengl shader helper functions
 *
 * Copyright (c) 2014 Red Hat
 *
 * Authors:
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "ui/shader.h"

/* ---------------------------------------------------------------------- */

GLuint qemu_gl_init_texture_blit(GLint texture_blit_prog)
{
    static const GLfloat in_position[] = {
        -1, -1,
        1,  -1,
        -1,  1,
        1,   1,
    };
    GLint l_position;
    GLuint vao, buffer;

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    /* this is the VBO that holds the vertex data */
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(in_position), in_position,
                 GL_STATIC_DRAW);

    l_position = glGetAttribLocation(texture_blit_prog, "in_position");
    glVertexAttribPointer(l_position, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(l_position);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    return vao;
}

void qemu_gl_run_texture_blit(GLint texture_blit_prog,
                              GLint texture_blit_vao)
{
    glUseProgram(texture_blit_prog);
    glBindVertexArray(texture_blit_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* ---------------------------------------------------------------------- */

GLuint qemu_gl_create_compile_shader(GLenum type, const GLchar *src)
{
    GLuint shader;
    GLint status, length;
    char *errmsg;

    shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, 0);
    glCompileShader(shader);

    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        errmsg = malloc(length);
        glGetShaderInfoLog(shader, length, &length, errmsg);
        fprintf(stderr, "%s: compile %s error\n%s\n", __func__,
                (type == GL_VERTEX_SHADER) ? "vertex" : "fragment",
                errmsg);
        free(errmsg);
        return 0;
    }
    return shader;
}

GLuint qemu_gl_create_link_program(GLuint vert, GLuint frag)
{
    GLuint program;
    GLint status, length;
    char *errmsg;

    program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        errmsg = malloc(length);
        glGetProgramInfoLog(program, length, &length, errmsg);
        fprintf(stderr, "%s: link program: %s\n", __func__, errmsg);
        free(errmsg);
        return 0;
    }
    return program;
}

GLuint qemu_gl_create_compile_link_program(const GLchar *vert_src,
                                           const GLchar *frag_src)
{
    GLuint vert_shader, frag_shader, program;

    vert_shader = qemu_gl_create_compile_shader(GL_VERTEX_SHADER, vert_src);
    frag_shader = qemu_gl_create_compile_shader(GL_FRAGMENT_SHADER, frag_src);
    if (!vert_shader || !frag_shader) {
        return 0;
    }

    program = qemu_gl_create_link_program(vert_shader, frag_shader);
    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    return program;
}
