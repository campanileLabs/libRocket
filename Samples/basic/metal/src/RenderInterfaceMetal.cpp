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
 * Native Metal renderer. Compiled as Objective-C++ (see the CMake
 * set_source_files_properties for this file) and built under manual
 * reference counting (no -fobjc-arc), consistent with this project's other
 * Objective-C++ files -- Metal object pointers stored across calls are
 * explicitly retained/released rather than relying on ARC.
 */

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>

#include "RenderInterfaceMetal.h"
#include <Rocket/Core.h>
#include <Shell.h>
#include <string.h>
#include <stdio.h>

struct Uniforms
{
	float translation[2];
	float viewport_size[2];
};

RenderInterfaceMetal::RenderInterfaceMetal()
{
	m_width = 0;
	m_height = 0;
	m_rocket_context = NULL;

	m_device = NULL;
	m_command_queue = NULL;
	m_metal_layer = NULL;
	m_pipeline_state = NULL;
	m_sampler_state = NULL;

	m_current_drawable = NULL;
	m_command_buffer = NULL;
	m_render_encoder = NULL;
	m_frame_active = false;

	m_vertex_buffer = NULL;
	m_vertex_buffer_contents = NULL;
	m_vertex_buffer_cursor = 0;

	m_index_buffer = NULL;
	m_index_buffer_contents = NULL;
	m_index_buffer_cursor = 0;

	m_default_texture = NULL;

	m_scissor_enabled = false;
	m_scissor_x = m_scissor_y = m_scissor_width = m_scissor_height = 0;
}

RenderInterfaceMetal::~RenderInterfaceMetal()
{
}

void RenderInterfaceMetal::SetContext(void* context)
{
	m_rocket_context = context;
}

//===================================
// Device / pipeline setup =========
//===================================

bool RenderInterfaceMetal::AttachToNative(void* nativeWindow)
{
	NSWindow* window = (NSWindow*) nativeWindow;
	NSView* view = [window contentView];
	if (view == nil)
		return false;

	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	if (device == nil)
	{
		Shell::DisplayError("Metal: no default device found.");
		return false;
	}
	// MTLCreateSystemDefaultDevice()'s name contains "Create", so per Cocoa's
	// ownership convention it already returns a +1 reference -- no extra
	// retain needed here (matched by one release in DetachFromNative).
	m_device = (void*) device;

	id<MTLCommandQueue> command_queue = [device newCommandQueue];
	if (command_queue == nil)
	{
		Shell::DisplayError("Metal: failed to create command queue.");
		return false;
	}
	m_command_queue = (void*) command_queue;

	[view setWantsLayer:YES];
	CAMetalLayer* metal_layer = [CAMetalLayer layer];
	metal_layer.device = device;
	metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	metal_layer.framebufferOnly = YES;
	// Deliberately left at the default 1x contentsScale (not the screen's
	// backingScaleFactor) so pixel math matches the non-Retina-aware GL and
	// Vulkan sample renderers -- same window_width/window_height everywhere.
	[view setLayer:metal_layer];
	m_metal_layer = (void*) [metal_layer retain];

	if (!CreatePipeline())
		return false;

	MTLSamplerDescriptor* sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
	sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
	sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
	sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
	sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;

	id<MTLSamplerState> sampler_state = [device newSamplerStateWithDescriptor:sampler_descriptor];
	[sampler_descriptor release];
	if (sampler_state == nil)
	{
		Shell::DisplayError("Metal: failed to create sampler state.");
		return false;
	}
	m_sampler_state = (void*) sampler_state;

	id<MTLBuffer> vertex_buffer = [device newBufferWithLength:VERTEX_BUFFER_CAPACITY options:MTLResourceStorageModeShared];
	id<MTLBuffer> index_buffer = [device newBufferWithLength:INDEX_BUFFER_CAPACITY options:MTLResourceStorageModeShared];
	if (vertex_buffer == nil || index_buffer == nil)
	{
		Shell::DisplayError("Metal: failed to create dynamic geometry buffers.");
		return false;
	}
	m_vertex_buffer = (void*) vertex_buffer;
	m_vertex_buffer_contents = [vertex_buffer contents];
	m_index_buffer = (void*) index_buffer;
	m_index_buffer_contents = [index_buffer contents];

	if (!CreateDefaultTexture())
	{
		Shell::DisplayError("Metal: failed to create default white texture.");
		return false;
	}

	return true;
}

bool RenderInterfaceMetal::CreatePipeline()
{
	id<MTLDevice> device = (id<MTLDevice>) m_device;

	Rocket::Core::FileInterface* file_interface = Rocket::Core::GetFileInterface();
	Rocket::Core::FileHandle file_handle = file_interface->Open("shaders/shader.metal");
	if (!file_handle)
	{
		Shell::DisplayError("Metal: could not open shaders/shader.metal.");
		return false;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t source_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	char* source_buffer = new char[source_size + 1];
	file_interface->Read(source_buffer, source_size, file_handle);
	file_interface->Close(file_handle);
	source_buffer[source_size] = '\0';

	NSString* source_string = [[NSString alloc] initWithUTF8String:source_buffer];
	delete[] source_buffer;

	NSError* error = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:source_string options:nil error:&error];
	[source_string release];

	if (library == nil)
	{
		Shell::DisplayError("Metal: shader compilation failed: %s", error != nil ? [[error localizedDescription] UTF8String] : "unknown error");
		return false;
	}

	id<MTLFunction> vertex_function = [library newFunctionWithName:@"vertex_main"];
	id<MTLFunction> fragment_function = [library newFunctionWithName:@"fragment_main"];
	[library release];

	if (vertex_function == nil || fragment_function == nil)
	{
		Shell::DisplayError("Metal: could not find vertex_main / fragment_main in shader.metal.");
		[vertex_function release];
		[fragment_function release];
		return false;
	}

	// Vertex layout matches Rocket::Core::Vertex { Vector2f position; Colourb colour; Vector2f tex_coord; }.
	MTLVertexDescriptor* vertex_descriptor = [[MTLVertexDescriptor alloc] init];
	vertex_descriptor.attributes[0].format = MTLVertexFormatFloat2;
	vertex_descriptor.attributes[0].offset = (NSUInteger) offsetof(Rocket::Core::Vertex, position);
	vertex_descriptor.attributes[0].bufferIndex = 0;

	vertex_descriptor.attributes[1].format = MTLVertexFormatUChar4Normalized;
	vertex_descriptor.attributes[1].offset = (NSUInteger) offsetof(Rocket::Core::Vertex, colour);
	vertex_descriptor.attributes[1].bufferIndex = 0;

	vertex_descriptor.attributes[2].format = MTLVertexFormatFloat2;
	vertex_descriptor.attributes[2].offset = (NSUInteger) offsetof(Rocket::Core::Vertex, tex_coord);
	vertex_descriptor.attributes[2].bufferIndex = 0;

	vertex_descriptor.layouts[0].stride = sizeof(Rocket::Core::Vertex);
	vertex_descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

	MTLRenderPipelineDescriptor* pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
	pipeline_descriptor.vertexFunction = vertex_function;
	pipeline_descriptor.fragmentFunction = fragment_function;
	pipeline_descriptor.vertexDescriptor = vertex_descriptor;
	[vertex_descriptor release];
	[vertex_function release];
	[fragment_function release];

	MTLRenderPipelineColorAttachmentDescriptor* colour_attachment = pipeline_descriptor.colorAttachments[0];
	colour_attachment.pixelFormat = MTLPixelFormatBGRA8Unorm;
	colour_attachment.blendingEnabled = YES;
	colour_attachment.rgbBlendOperation = MTLBlendOperationAdd;
	colour_attachment.alphaBlendOperation = MTLBlendOperationAdd;
	colour_attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
	colour_attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
	colour_attachment.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
	colour_attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

	id<MTLRenderPipelineState> pipeline_state = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];
	[pipeline_descriptor release];

	if (pipeline_state == nil)
	{
		Shell::DisplayError("Metal: failed to create render pipeline state: %s", error != nil ? [[error localizedDescription] UTF8String] : "unknown error");
		return false;
	}

	m_pipeline_state = (void*) pipeline_state;
	return true;
}

void RenderInterfaceMetal::DetachFromNative()
{
	if (m_default_texture != NULL)
	{
		[(id<MTLTexture>) m_default_texture release];
		m_default_texture = NULL;
	}
	if (m_vertex_buffer != NULL)
	{
		[(id<MTLBuffer>) m_vertex_buffer release];
		m_vertex_buffer = NULL;
	}
	if (m_index_buffer != NULL)
	{
		[(id<MTLBuffer>) m_index_buffer release];
		m_index_buffer = NULL;
	}
	if (m_sampler_state != NULL)
	{
		[(id<MTLSamplerState>) m_sampler_state release];
		m_sampler_state = NULL;
	}
	if (m_pipeline_state != NULL)
	{
		[(id<MTLRenderPipelineState>) m_pipeline_state release];
		m_pipeline_state = NULL;
	}
	if (m_metal_layer != NULL)
	{
		[(CAMetalLayer*) m_metal_layer release];
		m_metal_layer = NULL;
	}
	if (m_command_queue != NULL)
	{
		[(id<MTLCommandQueue>) m_command_queue release];
		m_command_queue = NULL;
	}
	if (m_device != NULL)
	{
		[(id<MTLDevice>) m_device release];
		m_device = NULL;
	}
}

void RenderInterfaceMetal::SetViewport(int width, int height)
{
	if (m_width != width || m_height != height)
	{
		m_width = width;
		m_height = height;

		if (m_metal_layer != NULL)
		{
			// Unlike the Vulkan renderer's explicit swapchain rebuild, Metal
			// only needs the layer's drawable size updated -- CAMetalLayer
			// manages its own drawable pool internally.
			((CAMetalLayer*) m_metal_layer).drawableSize = CGSizeMake(width, height);
		}
	}

	if (m_rocket_context != NULL)
		((Rocket::Core::Context*) m_rocket_context)->SetDimensions(Rocket::Core::Vector2i(width, height));
}

//===================================
// Frame rendering ==================
//===================================

void RenderInterfaceMetal::PrepareRenderBuffer()
{
	m_frame_active = false;

	if (m_width <= 0 || m_height <= 0)
		return;

	CAMetalLayer* metal_layer = (CAMetalLayer*) m_metal_layer;
	id<CAMetalDrawable> drawable = [metal_layer nextDrawable];
	if (drawable == nil)
		return;

	m_current_drawable = (void*) [drawable retain];

	MTLRenderPassDescriptor* pass_descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
	pass_descriptor.colorAttachments[0].texture = drawable.texture;
	pass_descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
	pass_descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
	pass_descriptor.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

	id<MTLCommandBuffer> command_buffer = [(id<MTLCommandQueue>) m_command_queue commandBuffer];
	id<MTLRenderCommandEncoder> render_encoder = [command_buffer renderCommandEncoderWithDescriptor:pass_descriptor];

	m_command_buffer = (void*) [command_buffer retain];
	m_render_encoder = (void*) [render_encoder retain];

	[render_encoder setRenderPipelineState:(id<MTLRenderPipelineState>) m_pipeline_state];
	[render_encoder setFragmentSamplerState:(id<MTLSamplerState>) m_sampler_state atIndex:0];

	MTLViewport viewport;
	viewport.originX = 0.0;
	viewport.originY = 0.0;
	viewport.width = (double) m_width;
	viewport.height = (double) m_height;
	viewport.znear = 0.0;
	viewport.zfar = 1.0;
	[render_encoder setViewport:viewport];

	[render_encoder setVertexBuffer:(id<MTLBuffer>) m_vertex_buffer offset:0 atIndex:0];

	m_vertex_buffer_cursor = 0;
	m_index_buffer_cursor = 0;
	m_scissor_enabled = false;

	m_frame_active = true;
}

void RenderInterfaceMetal::RenderGeometry(Rocket::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rocket::Core::TextureHandle texture, const Rocket::Core::Vector2f& translation)
{
	if (!m_frame_active)
		return;

	size_t vertices_bytes = (size_t) num_vertices * sizeof(Rocket::Core::Vertex);
	size_t indices_bytes = (size_t) num_indices * sizeof(uint32_t);

	if (m_vertex_buffer_cursor + vertices_bytes > VERTEX_BUFFER_CAPACITY ||
		m_index_buffer_cursor + indices_bytes > INDEX_BUFFER_CAPACITY)
	{
		// Per-frame dynamic buffers are fixed size for simplicity; a UI this
		// large in one frame is outside what this reference sample supports.
		Shell::Log("Metal: dynamic geometry buffer exhausted, dropping a draw call this frame.");
		return;
	}

	size_t base_vertex = m_vertex_buffer_cursor / sizeof(Rocket::Core::Vertex);
	size_t base_index = m_index_buffer_cursor / sizeof(uint32_t);

	memcpy((char*) m_vertex_buffer_contents + m_vertex_buffer_cursor, vertices, vertices_bytes);

	// Rocket hands us signed 32-bit indices; the buffer is bound as UInt32.
	uint32_t* index_dest = (uint32_t*) ((char*) m_index_buffer_contents + m_index_buffer_cursor);
	for (int index = 0; index < num_indices; index++)
		index_dest[index] = (uint32_t) indices[index];

	m_vertex_buffer_cursor += vertices_bytes;
	m_index_buffer_cursor += indices_bytes;

	id<MTLRenderCommandEncoder> render_encoder = (id<MTLRenderCommandEncoder>) m_render_encoder;

	MTLScissorRect scissor;
	if (m_scissor_enabled)
	{
		// Metal's scissor rect is top-left origin, same as Rocket -- no flip
		// needed -- but it must stay fully inside the drawable, unlike
		// Vulkan/GL which clamp it for us.
		int x = m_scissor_x < 0 ? 0 : m_scissor_x;
		int y = m_scissor_y < 0 ? 0 : m_scissor_y;
		int width = m_scissor_width;
		int height = m_scissor_height;
		if (x > m_width) x = m_width;
		if (y > m_height) y = m_height;
		if (x + width > m_width) width = m_width - x;
		if (y + height > m_height) height = m_height - y;
		if (width < 0) width = 0;
		if (height < 0) height = 0;

		scissor.x = (NSUInteger) x;
		scissor.y = (NSUInteger) y;
		scissor.width = (NSUInteger) width;
		scissor.height = (NSUInteger) height;
	}
	else
	{
		scissor.x = 0;
		scissor.y = 0;
		scissor.width = (NSUInteger) m_width;
		scissor.height = (NSUInteger) m_height;
	}
	[render_encoder setScissorRect:scissor];

	id<MTLTexture> bound_texture = texture != 0 ? (id<MTLTexture>) texture : (id<MTLTexture>) m_default_texture;
	[render_encoder setFragmentTexture:bound_texture atIndex:0];

	Uniforms uniforms;
	uniforms.translation[0] = translation.x;
	uniforms.translation[1] = translation.y;
	uniforms.viewport_size[0] = (float) m_width;
	uniforms.viewport_size[1] = (float) m_height;
	[render_encoder setVertexBytes:&uniforms length:sizeof(Uniforms) atIndex:1];

	[render_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
								indexCount:(NSUInteger) num_indices
								 indexType:MTLIndexTypeUInt32
							   indexBuffer:(id<MTLBuffer>) m_index_buffer
						 indexBufferOffset:base_index * sizeof(uint32_t)
							 instanceCount:1
								baseVertex:(NSInteger) base_vertex
							  baseInstance:0];
}

void RenderInterfaceMetal::PresentRenderBuffer()
{
	if (!m_frame_active)
		return;

	id<MTLRenderCommandEncoder> render_encoder = (id<MTLRenderCommandEncoder>) m_render_encoder;
	id<MTLCommandBuffer> command_buffer = (id<MTLCommandBuffer>) m_command_buffer;
	id<CAMetalDrawable> drawable = (id<CAMetalDrawable>) m_current_drawable;

	[render_encoder endEncoding];
	[command_buffer presentDrawable:drawable];
	[command_buffer commit];

	// Single frame in flight: block until the GPU is done so the dynamic
	// vertex/index buffers can be safely overwritten by the next frame's
	// RenderGeometry() calls without extra synchronization.
	[command_buffer waitUntilCompleted];

	[render_encoder release];
	[command_buffer release];
	[drawable release];

	m_render_encoder = NULL;
	m_command_buffer = NULL;
	m_current_drawable = NULL;
	m_frame_active = false;
}

//===================================
// Scissoring =======================
//===================================

void RenderInterfaceMetal::EnableScissorRegion(bool enable)
{
	m_scissor_enabled = enable;
}

void RenderInterfaceMetal::SetScissorRegion(int x, int y, int width, int height)
{
	m_scissor_x = x;
	m_scissor_y = y;
	m_scissor_width = width;
	m_scissor_height = height;
}

//===================================
// Textures =========================
//===================================

// Set to byte packing, or the compiler will expand our struct, which means it won't read correctly from file
#pragma pack(1)
struct TGAHeader
{
	char idLength;
	char colourMapType;
	char dataType;
	short int colourMapOrigin;
	short int colourMapLength;
	char colourMapDepth;
	short int xOrigin;
	short int yOrigin;
	short int width;
	short int height;
	char bitsPerPixel;
	char imageDescriptor;
};
// Restore packing
#pragma pack()

bool RenderInterfaceMetal::LoadTexture(Rocket::Core::TextureHandle& texture_handle, Rocket::Core::Vector2i& texture_dimensions, const Rocket::Core::String& source)
{
	Rocket::Core::FileInterface* file_interface = Rocket::Core::GetFileInterface();
	Rocket::Core::FileHandle file_handle = file_interface->Open(source);
	if (!file_handle)
		return false;

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	if (buffer_size <= sizeof(TGAHeader))
	{
		file_interface->Close(file_handle);
		return false;
	}

	char* buffer = new char[buffer_size];
	file_interface->Read(buffer, buffer_size, file_handle);
	file_interface->Close(file_handle);

	TGAHeader header;
	memcpy(&header, buffer, sizeof(TGAHeader));

	int color_mode = header.bitsPerPixel / 8;
	int image_size = header.width * header.height * 4;

	if (header.dataType != 2)
	{
		Rocket::Core::Log::Message(Rocket::Core::Log::LT_ERROR, "Only 24/32bit uncompressed TGAs are supported.");
		delete[] buffer;
		return false;
	}

	if (color_mode < 3)
	{
		Rocket::Core::Log::Message(Rocket::Core::Log::LT_ERROR, "Only 24 and 32bit textures are supported");
		delete[] buffer;
		return false;
	}

	const char* image_src = buffer + sizeof(TGAHeader);
	unsigned char* image_dest = new unsigned char[image_size];

	// Targa is BGR, swap to RGBA and flip Y axis.
	for (long y = 0; y < header.height; y++)
	{
		long read_index = y * header.width * color_mode;
		long write_index = ((header.imageDescriptor & 32) != 0) ? read_index : (header.height - y - 1) * header.width * color_mode;
		for (long x = 0; x < header.width; x++)
		{
			image_dest[write_index] = image_src[read_index + 2];
			image_dest[write_index + 1] = image_src[read_index + 1];
			image_dest[write_index + 2] = image_src[read_index];
			if (color_mode == 4)
				image_dest[write_index + 3] = image_src[read_index + 3];
			else
				image_dest[write_index + 3] = 255;

			write_index += 4;
			read_index += color_mode;
		}
	}

	texture_dimensions.x = header.width;
	texture_dimensions.y = header.height;

	bool success = GenerateTexture(texture_handle, image_dest, texture_dimensions);

	delete[] image_dest;
	delete[] buffer;

	return success;
}

bool RenderInterfaceMetal::GenerateTexture(Rocket::Core::TextureHandle& texture_handle, const Rocket::Core::byte* source, const Rocket::Core::Vector2i& source_dimensions)
{
	id<MTLDevice> device = (id<MTLDevice>) m_device;

	MTLTextureDescriptor* texture_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
																									 width:(NSUInteger) source_dimensions.x
																									height:(NSUInteger) source_dimensions.y
																								 mipmapped:NO];
	texture_descriptor.usage = MTLTextureUsageShaderRead;
	texture_descriptor.storageMode = MTLStorageModeShared;

	id<MTLTexture> texture = [device newTextureWithDescriptor:texture_descriptor];
	if (texture == nil)
		return false;

	MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger) source_dimensions.x, (NSUInteger) source_dimensions.y);
	NSUInteger bytes_per_row = (NSUInteger) source_dimensions.x * 4;
	[texture replaceRegion:region mipmapLevel:0 withBytes:source bytesPerRow:bytes_per_row];

	texture_handle = (Rocket::Core::TextureHandle) texture;
	return true;
}

void RenderInterfaceMetal::ReleaseTexture(Rocket::Core::TextureHandle texture_handle)
{
	id<MTLTexture> texture = (id<MTLTexture>) texture_handle;
	if (texture == nil)
		return;

	[texture release];
}

bool RenderInterfaceMetal::CreateDefaultTexture()
{
	unsigned char white_pixel[4] = {255, 255, 255, 255};
	Rocket::Core::TextureHandle handle = 0;
	if (!GenerateTexture(handle, white_pixel, Rocket::Core::Vector2i(1, 1)))
		return false;

	m_default_texture = (void*) handle;
	return true;
}
