/*
 * This source file is part of libRocket, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://www.librocket.com
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef RENDERINTERFACEMETAL_H
#define RENDERINTERFACEMETAL_H

#include <Rocket/Core/RenderInterface.h>
#include <ShellRenderInterfaceExtensions.h>

/**
	A native Metal render interface for the Rocket samples on macOS.

	Objective-C object references (MTLDevice, MTLCommandQueue, etc.) are kept
	as opaque void* here so this header can be included from ordinary C++
	sample sources; the actual Metal/Cocoa calls all live inside
	RenderInterfaceMetal.cpp, which is compiled as Objective-C++.

	Geometry is never "compiled" (CompileGeometry() is left at its default,
	which returns 0 / not-supported) -- Rocket then always calls
	RenderGeometry() instead, matching this project's OpenGL and Vulkan
	reference renderers. Frames use a single frame in flight (the CPU waits
	for the GPU to finish the previous frame before recording the next),
	trading some throughput for a much simpler synchronization story,
	appropriate for a reference sample.
 */
class RenderInterfaceMetal : public Rocket::Core::RenderInterface, public ShellRenderInterfaceExtensions
{
public:
	RenderInterfaceMetal();
	virtual ~RenderInterfaceMetal();

	// Rocket::Core::RenderInterface
	virtual void RenderGeometry(Rocket::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rocket::Core::TextureHandle texture, const Rocket::Core::Vector2f& translation);
	virtual void EnableScissorRegion(bool enable);
	virtual void SetScissorRegion(int x, int y, int width, int height);
	virtual bool LoadTexture(Rocket::Core::TextureHandle& texture_handle, Rocket::Core::Vector2i& texture_dimensions, const Rocket::Core::String& source);
	virtual bool GenerateTexture(Rocket::Core::TextureHandle& texture_handle, const Rocket::Core::byte* source, const Rocket::Core::Vector2i& source_dimensions);
	virtual void ReleaseTexture(Rocket::Core::TextureHandle texture_handle);

	// ShellRenderInterfaceExtensions
	virtual void SetViewport(int width, int height);
	virtual void SetContext(void* context);
	virtual bool AttachToNative(void* nativeWindow);
	virtual void DetachFromNative();
	virtual void PrepareRenderBuffer();
	virtual void PresentRenderBuffer();

private:
	bool CreatePipeline();
	bool CreateDefaultTexture();

	int m_width;
	int m_height;
	void* m_rocket_context;

	void* m_device;          // id<MTLDevice>
	void* m_command_queue;   // id<MTLCommandQueue>
	void* m_metal_layer;     // CAMetalLayer*
	void* m_pipeline_state;  // id<MTLRenderPipelineState>
	void* m_sampler_state;   // id<MTLSamplerState>

	void* m_current_drawable;      // id<CAMetalDrawable>, valid between PrepareRenderBuffer and PresentRenderBuffer
	void* m_command_buffer;        // id<MTLCommandBuffer>
	void* m_render_encoder;        // id<MTLRenderCommandEncoder>
	bool m_frame_active;

	static const size_t VERTEX_BUFFER_CAPACITY = 1 * 1024 * 1024;
	static const size_t INDEX_BUFFER_CAPACITY = 512 * 1024;

	void* m_vertex_buffer;   // id<MTLBuffer>
	void* m_vertex_buffer_contents;
	size_t m_vertex_buffer_cursor;

	void* m_index_buffer;    // id<MTLBuffer>
	void* m_index_buffer_contents;
	size_t m_index_buffer_cursor;

	void* m_default_texture; // id<MTLTexture>

	bool m_scissor_enabled;
	int m_scissor_x, m_scissor_y, m_scissor_width, m_scissor_height;
};

#endif
