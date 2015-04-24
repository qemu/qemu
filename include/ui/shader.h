#ifdef CONFIG_OPENGL
# include <GLES2/gl2.h>
# include <GLES2/gl2ext.h>
#endif

GLuint qemu_gl_create_compile_shader(GLenum type, const GLchar *src);
GLuint qemu_gl_create_link_program(GLuint vert, GLuint frag);
GLuint qemu_gl_create_compile_link_program(const GLchar *vert_src,
                                           const GLchar *frag_src);
