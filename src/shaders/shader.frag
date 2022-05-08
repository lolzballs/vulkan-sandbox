#version 450

layout(binding = 0) uniform sampler2D tex_sampler;
layout(location = 0) in vec2 tex_coord;
layout(location = 0) out vec4 out_color;

void main() {
	vec4 color = texture(tex_sampler, tex_coord);
	out_color = color.rgba;
}
