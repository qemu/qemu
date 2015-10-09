#ifndef QEMU_SHADER_H
#define QEMU_SHADER_H

#include <epoxy/gl.h>

GLuint qemu_gl_init_texture_blit(GLint texture_blit_prog);
void qemu_gl_run_texture_blit(GLint texture_blit_prog,
                              GLint texture_blit_vao);

GLuint qemu_gl_create_compile_shader(GLenum type, const GLchar *src);
GLuint qemu_gl_create_link_program(GLuint vert, GLuint frag);
GLuint qemu_gl_create_compile_link_program(const GLchar *vert_src,
                                           const GLchar *frag_src);

#endif /* QEMU_SHADER_H */
