
#version 300 es

in vec2  in_position;
out vec2 ex_tex_coord;

void main(void) {
    gl_Position = vec4(in_position, 0.0, 1.0);
    ex_tex_coord = vec2(1.0 + in_position.x, 1.0 - in_position.y) * 0.5;
}
