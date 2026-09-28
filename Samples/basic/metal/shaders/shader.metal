#include <metal_stdlib>
using namespace metal;

struct Uniforms
{
	float2 translation;
	float2 viewport_size;
};

struct VertexIn
{
	float2 position [[attribute(0)]];
	float4 colour [[attribute(1)]];
	float2 texcoord [[attribute(2)]];
};

struct VertexOut
{
	float4 position [[position]];
	float4 colour;
	float2 texcoord;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]], constant Uniforms& uniforms [[buffer(1)]])
{
	float2 position = in.position + uniforms.translation;
	float2 ndc = (position / uniforms.viewport_size) * 2.0 - 1.0;

	VertexOut out;
	// Metal's NDC has y pointing up (+1 at top), unlike Vulkan's y-down NDC,
	// so -- unlike the Vulkan renderer's shader -- this needs an explicit
	// flip to match Rocket's top-left-origin, y-down pixel space.
	out.position = float4(ndc.x, -ndc.y, 0.0, 1.0);
	out.colour = in.colour;
	out.texcoord = in.texcoord;
	return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler tex_sampler [[sampler(0)]])
{
	// Untextured geometry is bound to a 1x1 white texture by the renderer,
	// so this multiply is always correct.
	return in.colour * tex.sample(tex_sampler, in.texcoord);
}
