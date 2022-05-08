#version 450

vec2 vertices[4] = vec2[](
    vec2(-1, -1),
    vec2(1, -1),
    vec2(1, 1),
    vec2(-1, 1)
);

vec2 texture_map[4] = vec2[](
	vec2(0, 0),
	vec2(1, 0),
	vec2(1, 1),
	vec2(0, 1)
);

int indices[6] = int[](
	0, 1, 2,
	2, 3, 0
);

layout(location = 0) out vec2 out_tex_coord;

void main() {
	int index = indices[gl_VertexIndex];
    gl_Position = vec4(vertices[index], 0.0, 1.0);
	out_tex_coord = texture_map[index];
}
