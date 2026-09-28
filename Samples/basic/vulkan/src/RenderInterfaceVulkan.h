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

#ifndef RENDERINTERFACEVULKAN_H
#define RENDERINTERFACEVULKAN_H

#include <Rocket/Core/RenderInterface.h>
#include <ShellRenderInterfaceExtensions.h>
#include <vulkan/vulkan.h>
#include <vector>

/**
	A Vulkan render interface for the Rocket samples, used on macOS (via
	MoltenVK) and Windows (via a native Vulkan driver).

	Only exposes plain Vulkan C-API types, so this header can be included from
	ordinary C++ sample sources; the platform-specific surface setup (the
	Metal/Cocoa surface on macOS, which needs Objective-C++; the Win32 surface
	on Windows) lives entirely inside RenderInterfaceVulkan.cpp.

	Geometry is never "compiled" (CompileGeometry() is left at its default,
	which returns 0 / not-supported) -- Rocket then always calls
	RenderGeometry() instead, exactly like this project's OpenGL reference
	renderer. Frames are rendered with a single frame in flight (the CPU
	waits for the GPU to finish the previous frame before recording the
	next), trading some throughput for a much simpler, easier-to-verify
	synchronization story, appropriate for a reference sample.
 */
class RenderInterfaceVulkan : public Rocket::Core::RenderInterface, public ShellRenderInterfaceExtensions
{
public:
	RenderInterfaceVulkan();
	virtual ~RenderInterfaceVulkan();

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
	struct Texture
	{
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
		VkDescriptorSet descriptor_set;
	};

	bool CreateSwapchain(int width, int height);
	void DestroySwapchain();
	uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties);
	bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory);
	VkCommandBuffer BeginOneTimeCommands();
	void EndOneTimeCommands(VkCommandBuffer command_buffer);
	bool CreateDefaultTexture();
	VkShaderModule LoadShaderModule(const Rocket::Core::String& source_path);

	int m_width;
	int m_height;
	void* m_rocket_context;

	VkInstance m_instance;
	VkSurfaceKHR m_surface;
	VkPhysicalDevice m_physical_device;
	VkDevice m_device;
	uint32_t m_queue_family_index;
	VkQueue m_queue;

	VkSwapchainKHR m_swapchain;
	VkFormat m_swapchain_format;
	VkExtent2D m_swapchain_extent;
	std::vector<VkImage> m_swapchain_images;
	std::vector<VkImageView> m_swapchain_image_views;
	std::vector<VkFramebuffer> m_swapchain_framebuffers;
	bool m_swapchain_valid;

	VkRenderPass m_render_pass;
	VkDescriptorSetLayout m_descriptor_set_layout;
	VkPipelineLayout m_pipeline_layout;
	VkPipeline m_pipeline;
	VkDescriptorPool m_descriptor_pool;
	VkSampler m_sampler;

	VkCommandPool m_command_pool;
	VkCommandBuffer m_command_buffer;
	VkFence m_in_flight_fence;
	VkSemaphore m_image_available_semaphore;
	VkSemaphore m_render_finished_semaphore;

	uint32_t m_current_image_index;
	bool m_frame_active;

	static const VkDeviceSize VERTEX_BUFFER_CAPACITY = 1 * 1024 * 1024;
	static const VkDeviceSize INDEX_BUFFER_CAPACITY = 512 * 1024;

	VkBuffer m_vertex_buffer;
	VkDeviceMemory m_vertex_buffer_memory;
	void* m_vertex_buffer_mapped;
	VkDeviceSize m_vertex_buffer_cursor;

	VkBuffer m_index_buffer;
	VkDeviceMemory m_index_buffer_memory;
	void* m_index_buffer_mapped;
	VkDeviceSize m_index_buffer_cursor;

	Texture m_default_texture;

	bool m_scissor_enabled;
	VkRect2D m_scissor_rect;
};

#endif
