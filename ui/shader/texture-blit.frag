
#version 300 es

uniform sampler2D image;
in  mediump vec2 ex_tex_coord;
out mediump vec4 out_frag_color;

void main(void) {
     out_frag_color = texture(image, ex_tex_coord);
}
