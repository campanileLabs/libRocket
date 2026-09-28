#version 450

// Vulkan's NDC has y pointing down (-1 at top, +1 at bottom), which already
// matches Rocket's pixel-space convention (0,0 top-left, y down), so mapping
// window pixels to NDC needs no vertical flip -- unlike an OpenGL renderer.
layout(push_constant) uniform PushConstants
{
	vec2 translation;
	vec2 viewport_size;
} push;

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_colour;
layout(location = 2) in vec2 in_texcoord;

layout(location = 0) out vec4 frag_colour;
layout(location = 1) out vec2 frag_texcoord;

void main()
{
	vec2 position = in_position + push.translation;
	vec2 ndc = (position / push.viewport_size) * 2.0 - 1.0;

	gl_Position = vec4(ndc, 0.0, 1.0);
	frag_colour = in_colour;
	frag_texcoord = in_texcoord;
}
