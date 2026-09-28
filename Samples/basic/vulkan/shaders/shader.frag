#version 450

layout(binding = 0) uniform sampler2D tex_sampler;

layout(location = 0) in vec4 frag_colour;
layout(location = 1) in vec2 frag_texcoord;

layout(location = 0) out vec4 out_colour;

void main()
{
	// Untextured geometry is bound to a 1x1 white texture by the renderer,
	// so this multiply is always correct.
	out_colour = frag_colour * texture(tex_sampler, frag_texcoord);
}
