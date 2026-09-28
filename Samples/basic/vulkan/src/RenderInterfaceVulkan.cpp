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
 * macOS has no native Vulkan; this renders through MoltenVK (a Vulkan-to-Metal
 * translation layer), which is why an Objective-C++ CAMetalLayer is needed to
 * create the VkSurfaceKHR (VK_EXT_metal_surface) below. On Windows, the
 * VkSurfaceKHR is created directly from the sample's HWND (VK_KHR_win32_surface)
 * against a native Vulkan driver, no translation layer required.
 */

#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "RenderInterfaceVulkan.h"
#include <Rocket/Core.h>
#include <Shell.h>
#include <string.h>
#include <stdio.h>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

struct PushConstants
{
	float translation[2];
	float viewport_size[2];
};

RenderInterfaceVulkan::RenderInterfaceVulkan()
{
	m_width = 0;
	m_height = 0;
	m_rocket_context = NULL;

	m_instance = VK_NULL_HANDLE;
	m_surface = VK_NULL_HANDLE;
	m_physical_device = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
	m_queue_family_index = 0;
	m_queue = VK_NULL_HANDLE;

	m_swapchain = VK_NULL_HANDLE;
	m_swapchain_format = VK_FORMAT_UNDEFINED;
	m_swapchain_extent.width = 0;
	m_swapchain_extent.height = 0;
	m_swapchain_valid = false;

	m_render_pass = VK_NULL_HANDLE;
	m_descriptor_set_layout = VK_NULL_HANDLE;
	m_pipeline_layout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
	m_descriptor_pool = VK_NULL_HANDLE;
	m_sampler = VK_NULL_HANDLE;

	m_command_pool = VK_NULL_HANDLE;
	m_command_buffer = VK_NULL_HANDLE;
	m_in_flight_fence = VK_NULL_HANDLE;
	m_image_available_semaphore = VK_NULL_HANDLE;
	m_render_finished_semaphore = VK_NULL_HANDLE;

	m_current_image_index = 0;
	m_frame_active = false;

	m_vertex_buffer = VK_NULL_HANDLE;
	m_vertex_buffer_memory = VK_NULL_HANDLE;
	m_vertex_buffer_mapped = NULL;
	m_vertex_buffer_cursor = 0;

	m_index_buffer = VK_NULL_HANDLE;
	m_index_buffer_memory = VK_NULL_HANDLE;
	m_index_buffer_mapped = NULL;
	m_index_buffer_cursor = 0;

	m_default_texture.image = VK_NULL_HANDLE;
	m_default_texture.memory = VK_NULL_HANDLE;
	m_default_texture.view = VK_NULL_HANDLE;
	m_default_texture.descriptor_set = VK_NULL_HANDLE;

	m_scissor_enabled = false;
	m_scissor_rect.offset = {0, 0};
	m_scissor_rect.extent = {0, 0};
}

RenderInterfaceVulkan::~RenderInterfaceVulkan()
{
}

void RenderInterfaceVulkan::SetContext(void* context)
{
	m_rocket_context = context;
}

//===================================
// Device / pipeline setup =========
//===================================

bool RenderInterfaceVulkan::AttachToNative(void* nativeWindow)
{
#if defined(__APPLE__)
	NSWindow* window = (NSWindow*) nativeWindow;
	NSView* view = [window contentView];
	if (view == nil)
		return false;

	// Back the content view with a CAMetalLayer; MoltenVK's VK_EXT_metal_surface
	// creates the VkSurfaceKHR directly from this layer.
	[view setWantsLayer:YES];
	CAMetalLayer* metal_layer = [CAMetalLayer layer];
	metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	metal_layer.framebufferOnly = YES;
	// Deliberately left at the default 1x contentsScale (not the screen's
	// backingScaleFactor) so pixel math matches the non-Retina-aware GL
	// sample renderer -- same window_width/window_height everywhere.
	[view setLayer:metal_layer];
#elif defined(_WIN32)
	HWND window_handle = (HWND) nativeWindow;
	if (window_handle == NULL)
		return false;
#endif

	// ---- Instance ----
	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "libRocket Vulkan Sample";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "libRocket";
	app_info.engineVersion = VK_MAKE_VERSION(1, 3, 0);
	app_info.apiVersion = VK_API_VERSION_1_2;

	std::vector<const char*> instance_extensions;
	instance_extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(__APPLE__)
	instance_extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#elif defined(_WIN32)
	instance_extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

	VkInstanceCreateInfo instance_info = {};
	instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if defined(__APPLE__)
	instance_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
	instance_info.pApplicationInfo = &app_info;
	instance_info.enabledExtensionCount = (uint32_t) instance_extensions.size();
	instance_info.ppEnabledExtensionNames = instance_extensions.data();

	if (vkCreateInstance(&instance_info, NULL, &m_instance) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create instance.");
		return false;
	}

	// ---- Surface ----
#if defined(__APPLE__)
	VkMetalSurfaceCreateInfoEXT surface_info = {};
	surface_info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
	surface_info.pLayer = metal_layer;

	if (vkCreateMetalSurfaceEXT(m_instance, &surface_info, NULL, &m_surface) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create Metal surface.");
		return false;
	}
#elif defined(_WIN32)
	VkWin32SurfaceCreateInfoKHR surface_info = {};
	surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface_info.hinstance = GetModuleHandle(NULL);
	surface_info.hwnd = window_handle;

	if (vkCreateWin32SurfaceKHR(m_instance, &surface_info, NULL, &m_surface) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create Win32 surface.");
		return false;
	}
#endif

	// ---- Physical device + queue family ----
	uint32_t physical_device_count = 0;
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, NULL);
	if (physical_device_count == 0)
	{
		Shell::DisplayError("Vulkan: no physical devices found.");
		return false;
	}

	std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, physical_devices.data());

	bool found_device = false;
	for (size_t device_index = 0; device_index < physical_devices.size() && !found_device; device_index++)
	{
		VkPhysicalDevice candidate = physical_devices[device_index];

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, NULL);
		std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
		vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_family_count, queue_families.data());

		for (uint32_t family_index = 0; family_index < queue_family_count; family_index++)
		{
			if (!(queue_families[family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				continue;

			VkBool32 present_supported = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family_index, m_surface, &present_supported);
			if (!present_supported)
				continue;

			m_physical_device = candidate;
			m_queue_family_index = family_index;
			found_device = true;
			break;
		}
	}

	if (!found_device)
	{
		Shell::DisplayError("Vulkan: no queue family with graphics + present support found.");
		return false;
	}

	// ---- Logical device ----
	float queue_priority = 1.0f;
	VkDeviceQueueCreateInfo queue_info = {};
	queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_info.queueFamilyIndex = m_queue_family_index;
	queue_info.queueCount = 1;
	queue_info.pQueuePriorities = &queue_priority;

	std::vector<const char*> device_extensions;
	device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

#if defined(__APPLE__)
	// MoltenVK devices must enable VK_KHR_portability_subset if they support it.
	uint32_t available_extension_count = 0;
	vkEnumerateDeviceExtensionProperties(m_physical_device, NULL, &available_extension_count, NULL);
	std::vector<VkExtensionProperties> available_extensions(available_extension_count);
	vkEnumerateDeviceExtensionProperties(m_physical_device, NULL, &available_extension_count, available_extensions.data());
	for (uint32_t extension_index = 0; extension_index < available_extension_count; extension_index++)
	{
		if (strcmp(available_extensions[extension_index].extensionName, "VK_KHR_portability_subset") == 0)
		{
			device_extensions.push_back("VK_KHR_portability_subset");
			break;
		}
	}
#endif

	VkPhysicalDeviceFeatures device_features = {};

	VkDeviceCreateInfo device_info = {};
	device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue_info;
	device_info.pEnabledFeatures = &device_features;
	device_info.enabledExtensionCount = (uint32_t) device_extensions.size();
	device_info.ppEnabledExtensionNames = device_extensions.data();

	if (vkCreateDevice(m_physical_device, &device_info, NULL, &m_device) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create logical device.");
		return false;
	}

	vkGetDeviceQueue(m_device, m_queue_family_index, 0, &m_queue);

	// ---- Swapchain format (queried now; the swapchain itself is (re)created by SetViewport) ----
	uint32_t format_count = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, NULL);
	std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, surface_formats.data());

	m_swapchain_format = surface_formats[0].format;
	for (uint32_t format_index = 0; format_index < format_count; format_index++)
	{
		if (surface_formats[format_index].format == VK_FORMAT_B8G8R8A8_UNORM)
		{
			m_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
			break;
		}
	}

	// ---- Render pass ----
	VkAttachmentDescription colour_attachment = {};
	colour_attachment.format = m_swapchain_format;
	colour_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colour_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colour_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colour_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colour_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colour_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colour_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colour_attachment_ref = {};
	colour_attachment_ref.attachment = 0;
	colour_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colour_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &colour_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	if (vkCreateRenderPass(m_device, &render_pass_info, NULL, &m_render_pass) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create render pass.");
		return false;
	}

	// ---- Descriptor set layout (one combined image sampler per draw's texture) ----
	VkDescriptorSetLayoutBinding sampler_binding = {};
	sampler_binding.binding = 0;
	sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sampler_binding.descriptorCount = 1;
	sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layout_info = {};
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = 1;
	layout_info.pBindings = &sampler_binding;

	if (vkCreateDescriptorSetLayout(m_device, &layout_info, NULL, &m_descriptor_set_layout) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create descriptor set layout.");
		return false;
	}

	// ---- Pipeline layout ----
	VkPushConstantRange push_constant_range = {};
	push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	push_constant_range.offset = 0;
	push_constant_range.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo pipeline_layout_info = {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &m_descriptor_set_layout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_constant_range;

	if (vkCreatePipelineLayout(m_device, &pipeline_layout_info, NULL, &m_pipeline_layout) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create pipeline layout.");
		return false;
	}

	// ---- Shaders ----
	VkShaderModule vertex_shader = LoadShaderModule("shaders/shader.vert.spv");
	VkShaderModule fragment_shader = LoadShaderModule("shaders/shader.frag.spv");
	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE)
	{
		Shell::DisplayError("Vulkan: failed to load shader modules (did the shaders fail to compile to SPIR-V?).");
		return false;
	}

	VkPipelineShaderStageCreateInfo vertex_stage = {};
	vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertex_stage.module = vertex_shader;
	vertex_stage.pName = "main";

	VkPipelineShaderStageCreateInfo fragment_stage = {};
	fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragment_stage.module = fragment_shader;
	fragment_stage.pName = "main";

	VkPipelineShaderStageCreateInfo shader_stages[2] = {vertex_stage, fragment_stage};

	// ---- Vertex input: matches Rocket::Core::Vertex { Vector2f position; Colourb colour; Vector2f tex_coord; } ----
	VkVertexInputBindingDescription binding_description = {};
	binding_description.binding = 0;
	binding_description.stride = sizeof(Rocket::Core::Vertex);
	binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	VkVertexInputAttributeDescription attribute_descriptions[3] = {};
	attribute_descriptions[0].location = 0;
	attribute_descriptions[0].binding = 0;
	attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[0].offset = (uint32_t) offsetof(Rocket::Core::Vertex, position);

	attribute_descriptions[1].location = 1;
	attribute_descriptions[1].binding = 0;
	attribute_descriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
	attribute_descriptions[1].offset = (uint32_t) offsetof(Rocket::Core::Vertex, colour);

	attribute_descriptions[2].location = 2;
	attribute_descriptions[2].binding = 0;
	attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
	attribute_descriptions[2].offset = (uint32_t) offsetof(Rocket::Core::Vertex, tex_coord);

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 1;
	vertex_input_info.pVertexBindingDescriptions = &binding_description;
	vertex_input_info.vertexAttributeDescriptionCount = 3;
	vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewport_state = {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blend_attachment = {};
	blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blend_attachment.blendEnable = VK_TRUE;
	blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
	blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colour_blending = {};
	colour_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colour_blending.attachmentCount = 1;
	colour_blending.pAttachments = &blend_attachment;

	VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamic_state_info = {};
	dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state_info.dynamicStateCount = 2;
	dynamic_state_info.pDynamicStates = dynamic_states;

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pColorBlendState = &colour_blending;
	pipeline_info.pDynamicState = &dynamic_state_info;
	pipeline_info.layout = m_pipeline_layout;
	pipeline_info.renderPass = m_render_pass;
	pipeline_info.subpass = 0;

	VkResult pipeline_result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &m_pipeline);

	vkDestroyShaderModule(m_device, vertex_shader, NULL);
	vkDestroyShaderModule(m_device, fragment_shader, NULL);

	if (pipeline_result != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create graphics pipeline.");
		return false;
	}

	// ---- Sampler ----
	VkSamplerCreateInfo sampler_info = {};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 0.0f;

	if (vkCreateSampler(m_device, &sampler_info, NULL, &m_sampler) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create sampler.");
		return false;
	}

	// ---- Descriptor pool (one combined-image-sampler set per loaded texture) ----
	VkDescriptorPoolSize pool_size = {};
	pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_size.descriptorCount = 256;

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 256;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &pool_size;

	if (vkCreateDescriptorPool(m_device, &pool_info, NULL, &m_descriptor_pool) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create descriptor pool.");
		return false;
	}

	// ---- Command pool / buffer ----
	VkCommandPoolCreateInfo command_pool_info = {};
	command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	command_pool_info.queueFamilyIndex = m_queue_family_index;

	if (vkCreateCommandPool(m_device, &command_pool_info, NULL, &m_command_pool) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create command pool.");
		return false;
	}

	VkCommandBufferAllocateInfo command_buffer_info = {};
	command_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	command_buffer_info.commandPool = m_command_pool;
	command_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	command_buffer_info.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(m_device, &command_buffer_info, &m_command_buffer) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to allocate command buffer.");
		return false;
	}

	// ---- Sync objects (single frame in flight) ----
	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (vkCreateSemaphore(m_device, &semaphore_info, NULL, &m_image_available_semaphore) != VK_SUCCESS ||
		vkCreateSemaphore(m_device, &semaphore_info, NULL, &m_render_finished_semaphore) != VK_SUCCESS ||
		vkCreateFence(m_device, &fence_info, NULL, &m_in_flight_fence) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create synchronization objects.");
		return false;
	}

	// ---- Dynamic vertex / index buffers (host-visible, persistently mapped) ----
	if (!CreateBuffer(VERTEX_BUFFER_CAPACITY, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_vertex_buffer, m_vertex_buffer_memory) ||
		!CreateBuffer(INDEX_BUFFER_CAPACITY, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_index_buffer, m_index_buffer_memory))
	{
		Shell::DisplayError("Vulkan: failed to create dynamic geometry buffers.");
		return false;
	}

	vkMapMemory(m_device, m_vertex_buffer_memory, 0, VERTEX_BUFFER_CAPACITY, 0, &m_vertex_buffer_mapped);
	vkMapMemory(m_device, m_index_buffer_memory, 0, INDEX_BUFFER_CAPACITY, 0, &m_index_buffer_mapped);

	if (!CreateDefaultTexture())
	{
		Shell::DisplayError("Vulkan: failed to create default white texture.");
		return false;
	}

	return true;
}

void RenderInterfaceVulkan::DetachFromNative()
{
	if (m_device == VK_NULL_HANDLE)
		return;

	vkDeviceWaitIdle(m_device);

	DestroySwapchain();

	if (m_default_texture.descriptor_set != VK_NULL_HANDLE)
		vkFreeDescriptorSets(m_device, m_descriptor_pool, 1, &m_default_texture.descriptor_set);
	if (m_default_texture.view != VK_NULL_HANDLE)
		vkDestroyImageView(m_device, m_default_texture.view, NULL);
	if (m_default_texture.image != VK_NULL_HANDLE)
		vkDestroyImage(m_device, m_default_texture.image, NULL);
	if (m_default_texture.memory != VK_NULL_HANDLE)
		vkFreeMemory(m_device, m_default_texture.memory, NULL);

	if (m_vertex_buffer_mapped != NULL)
		vkUnmapMemory(m_device, m_vertex_buffer_memory);
	if (m_index_buffer_mapped != NULL)
		vkUnmapMemory(m_device, m_index_buffer_memory);
	if (m_vertex_buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(m_device, m_vertex_buffer, NULL);
	if (m_vertex_buffer_memory != VK_NULL_HANDLE)
		vkFreeMemory(m_device, m_vertex_buffer_memory, NULL);
	if (m_index_buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(m_device, m_index_buffer, NULL);
	if (m_index_buffer_memory != VK_NULL_HANDLE)
		vkFreeMemory(m_device, m_index_buffer_memory, NULL);

	if (m_in_flight_fence != VK_NULL_HANDLE)
		vkDestroyFence(m_device, m_in_flight_fence, NULL);
	if (m_image_available_semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(m_device, m_image_available_semaphore, NULL);
	if (m_render_finished_semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(m_device, m_render_finished_semaphore, NULL);

	if (m_command_pool != VK_NULL_HANDLE)
		vkDestroyCommandPool(m_device, m_command_pool, NULL);

	if (m_sampler != VK_NULL_HANDLE)
		vkDestroySampler(m_device, m_sampler, NULL);
	if (m_descriptor_pool != VK_NULL_HANDLE)
		vkDestroyDescriptorPool(m_device, m_descriptor_pool, NULL);
	if (m_pipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_pipeline, NULL);
	if (m_pipeline_layout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_pipeline_layout, NULL);
	if (m_descriptor_set_layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, NULL);
	if (m_render_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(m_device, m_render_pass, NULL);

	vkDestroyDevice(m_device, NULL);
	m_device = VK_NULL_HANDLE;

	if (m_surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(m_instance, m_surface, NULL);
	if (m_instance != VK_NULL_HANDLE)
		vkDestroyInstance(m_instance, NULL);

	m_instance = VK_NULL_HANDLE;
	m_surface = VK_NULL_HANDLE;
}

//===================================
// Swapchain ========================
//===================================

bool RenderInterfaceVulkan::CreateSwapchain(int width, int height)
{
	DestroySwapchain();

	if (width <= 0 || height <= 0)
	{
		// Window minimized or mid-resize to zero size; nothing to render to yet.
		m_swapchain_valid = false;
		return true;
	}

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &capabilities);

	VkExtent2D extent;
	extent.width = (uint32_t) width;
	extent.height = (uint32_t) height;
	if (capabilities.currentExtent.width != 0xFFFFFFFFu)
		extent = capabilities.currentExtent;

	if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
	if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
	if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
	if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;

	uint32_t image_count = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
		image_count = capabilities.maxImageCount;

	VkSwapchainCreateInfoKHR swapchain_info = {};
	swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_info.surface = m_surface;
	swapchain_info.minImageCount = image_count;
	swapchain_info.imageFormat = m_swapchain_format;
	swapchain_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchain_info.imageExtent = extent;
	swapchain_info.imageArrayLayers = 1;
	swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchain_info.preTransform = capabilities.currentTransform;
	swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchain_info.clipped = VK_TRUE;

	if (vkCreateSwapchainKHR(m_device, &swapchain_info, NULL, &m_swapchain) != VK_SUCCESS)
	{
		Shell::DisplayError("Vulkan: failed to create swapchain.");
		return false;
	}

	m_swapchain_extent = extent;

	uint32_t actual_image_count = 0;
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual_image_count, NULL);
	m_swapchain_images.resize(actual_image_count);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &actual_image_count, m_swapchain_images.data());

	m_swapchain_image_views.resize(actual_image_count);
	m_swapchain_framebuffers.resize(actual_image_count);

	for (uint32_t image_index = 0; image_index < actual_image_count; image_index++)
	{
		VkImageViewCreateInfo view_info = {};
		view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		view_info.image = m_swapchain_images[image_index];
		view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view_info.format = m_swapchain_format;
		view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
		view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view_info.subresourceRange.levelCount = 1;
		view_info.subresourceRange.layerCount = 1;

		if (vkCreateImageView(m_device, &view_info, NULL, &m_swapchain_image_views[image_index]) != VK_SUCCESS)
		{
			Shell::DisplayError("Vulkan: failed to create swapchain image view.");
			return false;
		}

		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = m_render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = &m_swapchain_image_views[image_index];
		framebuffer_info.width = extent.width;
		framebuffer_info.height = extent.height;
		framebuffer_info.layers = 1;

		if (vkCreateFramebuffer(m_device, &framebuffer_info, NULL, &m_swapchain_framebuffers[image_index]) != VK_SUCCESS)
		{
			Shell::DisplayError("Vulkan: failed to create framebuffer.");
			return false;
		}
	}

	m_swapchain_valid = true;
	return true;
}

void RenderInterfaceVulkan::DestroySwapchain()
{
	if (m_device == VK_NULL_HANDLE)
		return;

	for (size_t index = 0; index < m_swapchain_framebuffers.size(); index++)
		vkDestroyFramebuffer(m_device, m_swapchain_framebuffers[index], NULL);
	m_swapchain_framebuffers.clear();

	for (size_t index = 0; index < m_swapchain_image_views.size(); index++)
		vkDestroyImageView(m_device, m_swapchain_image_views[index], NULL);
	m_swapchain_image_views.clear();

	m_swapchain_images.clear();

	if (m_swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_device, m_swapchain, NULL);
		m_swapchain = VK_NULL_HANDLE;
	}

	m_swapchain_valid = false;
}

void RenderInterfaceVulkan::SetViewport(int width, int height)
{
	if (m_width != width || m_height != height)
	{
		m_width = width;
		m_height = height;

		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
			CreateSwapchain(width, height);
		}
	}

	if (m_rocket_context != NULL)
		((Rocket::Core::Context*) m_rocket_context)->SetDimensions(Rocket::Core::Vector2i(width, height));
}

//===================================
// Frame rendering ==================
//===================================

void RenderInterfaceVulkan::PrepareRenderBuffer()
{
	m_frame_active = false;

	if (!m_swapchain_valid)
		return;

	vkWaitForFences(m_device, 1, &m_in_flight_fence, VK_TRUE, UINT64_MAX);

	VkResult acquire_result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_image_available_semaphore, VK_NULL_HANDLE, &m_current_image_index);
	if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		CreateSwapchain(m_width, m_height);
		return;
	}
	else if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
	{
		return;
	}

	vkResetFences(m_device, 1, &m_in_flight_fence);

	m_vertex_buffer_cursor = 0;
	m_index_buffer_cursor = 0;
	m_scissor_enabled = false;

	vkResetCommandBuffer(m_command_buffer, 0);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(m_command_buffer, &begin_info);

	VkClearValue clear_colour = {};
	clear_colour.color.float32[0] = 0.0f;
	clear_colour.color.float32[1] = 0.0f;
	clear_colour.color.float32[2] = 0.0f;
	clear_colour.color.float32[3] = 1.0f;

	VkRenderPassBeginInfo render_pass_begin = {};
	render_pass_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin.renderPass = m_render_pass;
	render_pass_begin.framebuffer = m_swapchain_framebuffers[m_current_image_index];
	render_pass_begin.renderArea.offset = {0, 0};
	render_pass_begin.renderArea.extent = m_swapchain_extent;
	render_pass_begin.clearValueCount = 1;
	render_pass_begin.pClearValues = &clear_colour;

	vkCmdBeginRenderPass(m_command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float) m_swapchain_extent.width;
	viewport.height = (float) m_swapchain_extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(m_command_buffer, 0, 1, &viewport);

	VkDeviceSize vertex_buffer_offset = 0;
	vkCmdBindVertexBuffers(m_command_buffer, 0, 1, &m_vertex_buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(m_command_buffer, m_index_buffer, 0, VK_INDEX_TYPE_UINT32);

	m_frame_active = true;
}

void RenderInterfaceVulkan::RenderGeometry(Rocket::Core::Vertex* vertices, int num_vertices, int* indices, int num_indices, Rocket::Core::TextureHandle texture, const Rocket::Core::Vector2f& translation)
{
	if (!m_frame_active)
		return;

	VkDeviceSize vertices_bytes = (VkDeviceSize) num_vertices * sizeof(Rocket::Core::Vertex);
	VkDeviceSize indices_bytes = (VkDeviceSize) num_indices * sizeof(uint32_t);

	if (m_vertex_buffer_cursor + vertices_bytes > VERTEX_BUFFER_CAPACITY ||
		m_index_buffer_cursor + indices_bytes > INDEX_BUFFER_CAPACITY)
	{
		// Per-frame dynamic buffers are fixed size for simplicity; a UI this
		// large in one frame is outside what this reference sample supports.
		Shell::Log("Vulkan: dynamic geometry buffer exhausted, dropping a draw call this frame.");
		return;
	}

	uint32_t base_vertex = (uint32_t) (m_vertex_buffer_cursor / sizeof(Rocket::Core::Vertex));
	uint32_t base_index = (uint32_t) (m_index_buffer_cursor / sizeof(uint32_t));

	memcpy((char*) m_vertex_buffer_mapped + m_vertex_buffer_cursor, vertices, (size_t) vertices_bytes);

	// Rocket hands us signed 32-bit indices; the buffer is bound as UINT32.
	uint32_t* index_dest = (uint32_t*) ((char*) m_index_buffer_mapped + m_index_buffer_cursor);
	for (int index = 0; index < num_indices; index++)
		index_dest[index] = (uint32_t) indices[index];

	m_vertex_buffer_cursor += vertices_bytes;
	m_index_buffer_cursor += indices_bytes;

	VkRect2D scissor;
	if (m_scissor_enabled)
	{
		scissor = m_scissor_rect;
	}
	else
	{
		scissor.offset = {0, 0};
		scissor.extent = m_swapchain_extent;
	}
	vkCmdSetScissor(m_command_buffer, 0, 1, &scissor);

	Texture* bound_texture = texture != 0 ? (Texture*) texture : &m_default_texture;
	vkCmdBindDescriptorSets(m_command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0, 1, &bound_texture->descriptor_set, 0, NULL);

	PushConstants push_constants;
	push_constants.translation[0] = translation.x;
	push_constants.translation[1] = translation.y;
	push_constants.viewport_size[0] = (float) m_swapchain_extent.width;
	push_constants.viewport_size[1] = (float) m_swapchain_extent.height;
	vkCmdPushConstants(m_command_buffer, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);

	vkCmdDrawIndexed(m_command_buffer, (uint32_t) num_indices, 1, base_index, (int32_t) base_vertex, 0);
}

void RenderInterfaceVulkan::PresentRenderBuffer()
{
	if (!m_frame_active)
		return;

	vkCmdEndRenderPass(m_command_buffer);
	vkEndCommandBuffer(m_command_buffer);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &m_image_available_semaphore;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &m_command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &m_render_finished_semaphore;

	vkQueueSubmit(m_queue, 1, &submit_info, m_in_flight_fence);

	VkPresentInfoKHR present_info = {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &m_render_finished_semaphore;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &m_swapchain;
	present_info.pImageIndices = &m_current_image_index;

	VkResult present_result = vkQueuePresentKHR(m_queue, &present_info);
	if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR)
		CreateSwapchain(m_width, m_height);

	m_frame_active = false;
}

//===================================
// Scissoring =======================
//===================================

void RenderInterfaceVulkan::EnableScissorRegion(bool enable)
{
	m_scissor_enabled = enable;
}

void RenderInterfaceVulkan::SetScissorRegion(int x, int y, int width, int height)
{
	// Vulkan's scissor rect is top-left origin, same as Rocket's -- no flip needed.
	m_scissor_rect.offset.x = x;
	m_scissor_rect.offset.y = y;
	m_scissor_rect.extent.width = (uint32_t) (width > 0 ? width : 0);
	m_scissor_rect.extent.height = (uint32_t) (height > 0 ? height : 0);
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

bool RenderInterfaceVulkan::LoadTexture(Rocket::Core::TextureHandle& texture_handle, Rocket::Core::Vector2i& texture_dimensions, const Rocket::Core::String& source)
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

bool RenderInterfaceVulkan::GenerateTexture(Rocket::Core::TextureHandle& texture_handle, const Rocket::Core::byte* source, const Rocket::Core::Vector2i& source_dimensions)
{
	VkDeviceSize image_size = (VkDeviceSize) source_dimensions.x * source_dimensions.y * 4;

	VkBuffer staging_buffer;
	VkDeviceMemory staging_memory;
	if (!CreateBuffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_memory))
		return false;

	void* mapped = NULL;
	vkMapMemory(m_device, staging_memory, 0, image_size, 0, &mapped);
	memcpy(mapped, source, (size_t) image_size);
	vkUnmapMemory(m_device, staging_memory);

	Texture* new_texture = new Texture();
	new_texture->image = VK_NULL_HANDLE;
	new_texture->memory = VK_NULL_HANDLE;
	new_texture->view = VK_NULL_HANDLE;
	new_texture->descriptor_set = VK_NULL_HANDLE;

	VkImageCreateInfo image_info = {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent.width = (uint32_t) source_dimensions.x;
	image_info.extent.height = (uint32_t) source_dimensions.y;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if (vkCreateImage(m_device, &image_info, NULL, &new_texture->image) != VK_SUCCESS)
	{
		vkDestroyBuffer(m_device, staging_buffer, NULL);
		vkFreeMemory(m_device, staging_memory, NULL);
		delete new_texture;
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(m_device, new_texture->image, &memory_requirements);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = FindMemoryType(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	if (vkAllocateMemory(m_device, &alloc_info, NULL, &new_texture->memory) != VK_SUCCESS)
	{
		vkDestroyImage(m_device, new_texture->image, NULL);
		vkDestroyBuffer(m_device, staging_buffer, NULL);
		vkFreeMemory(m_device, staging_memory, NULL);
		delete new_texture;
		return false;
	}
	vkBindImageMemory(m_device, new_texture->image, new_texture->memory, 0);

	VkCommandBuffer copy_commands = BeginOneTimeCommands();

	VkImageMemoryBarrier barrier_to_transfer = {};
	barrier_to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier_to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier_to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier_to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier_to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier_to_transfer.image = new_texture->image;
	barrier_to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier_to_transfer.subresourceRange.levelCount = 1;
	barrier_to_transfer.subresourceRange.layerCount = 1;
	barrier_to_transfer.srcAccessMask = 0;
	barrier_to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(copy_commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier_to_transfer);

	VkBufferImageCopy copy_region = {};
	copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy_region.imageSubresource.layerCount = 1;
	copy_region.imageExtent.width = (uint32_t) source_dimensions.x;
	copy_region.imageExtent.height = (uint32_t) source_dimensions.y;
	copy_region.imageExtent.depth = 1;

	vkCmdCopyBufferToImage(copy_commands, staging_buffer, new_texture->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);

	VkImageMemoryBarrier barrier_to_shader_read = barrier_to_transfer;
	barrier_to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier_to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier_to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier_to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(copy_commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier_to_shader_read);

	EndOneTimeCommands(copy_commands);

	vkDestroyBuffer(m_device, staging_buffer, NULL);
	vkFreeMemory(m_device, staging_memory, NULL);

	VkImageViewCreateInfo view_info = {};
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = new_texture->image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_device, &view_info, NULL, &new_texture->view) != VK_SUCCESS)
	{
		vkDestroyImage(m_device, new_texture->image, NULL);
		vkFreeMemory(m_device, new_texture->memory, NULL);
		delete new_texture;
		return false;
	}

	VkDescriptorSetAllocateInfo set_alloc_info = {};
	set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_alloc_info.descriptorPool = m_descriptor_pool;
	set_alloc_info.descriptorSetCount = 1;
	set_alloc_info.pSetLayouts = &m_descriptor_set_layout;

	if (vkAllocateDescriptorSets(m_device, &set_alloc_info, &new_texture->descriptor_set) != VK_SUCCESS)
	{
		vkDestroyImageView(m_device, new_texture->view, NULL);
		vkDestroyImage(m_device, new_texture->image, NULL);
		vkFreeMemory(m_device, new_texture->memory, NULL);
		delete new_texture;
		return false;
	}

	VkDescriptorImageInfo descriptor_image_info = {};
	descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	descriptor_image_info.imageView = new_texture->view;
	descriptor_image_info.sampler = m_sampler;

	VkWriteDescriptorSet descriptor_write = {};
	descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptor_write.dstSet = new_texture->descriptor_set;
	descriptor_write.dstBinding = 0;
	descriptor_write.descriptorCount = 1;
	descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptor_write.pImageInfo = &descriptor_image_info;

	vkUpdateDescriptorSets(m_device, 1, &descriptor_write, 0, NULL);

	texture_handle = (Rocket::Core::TextureHandle) new_texture;
	return true;
}

void RenderInterfaceVulkan::ReleaseTexture(Rocket::Core::TextureHandle texture_handle)
{
	Texture* texture = (Texture*) texture_handle;
	if (texture == NULL)
		return;

	vkDeviceWaitIdle(m_device);

	if (texture->descriptor_set != VK_NULL_HANDLE)
		vkFreeDescriptorSets(m_device, m_descriptor_pool, 1, &texture->descriptor_set);
	if (texture->view != VK_NULL_HANDLE)
		vkDestroyImageView(m_device, texture->view, NULL);
	if (texture->image != VK_NULL_HANDLE)
		vkDestroyImage(m_device, texture->image, NULL);
	if (texture->memory != VK_NULL_HANDLE)
		vkFreeMemory(m_device, texture->memory, NULL);

	delete texture;
}

bool RenderInterfaceVulkan::CreateDefaultTexture()
{
	unsigned char white_pixel[4] = {255, 255, 255, 255};
	Rocket::Core::TextureHandle handle = 0;
	if (!GenerateTexture(handle, white_pixel, Rocket::Core::Vector2i(1, 1)))
		return false;

	m_default_texture = *(Texture*) handle;
	delete (Texture*) handle;
	return true;
}

//===================================
// Helpers ==========================
//===================================

uint32_t RenderInterfaceVulkan::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memory_properties);

	for (uint32_t index = 0; index < memory_properties.memoryTypeCount; index++)
	{
		if ((type_filter & (1 << index)) && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties)
			return index;
	}

	Shell::DisplayError("Vulkan: failed to find a suitable memory type.");
	return 0;
}

bool RenderInterfaceVulkan::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& memory)
{
	VkBufferCreateInfo buffer_info = {};
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = size;
	buffer_info.usage = usage;
	buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_device, &buffer_info, NULL, &buffer) != VK_SUCCESS)
		return false;

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(m_device, buffer, &memory_requirements);

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.allocationSize = memory_requirements.size;
	alloc_info.memoryTypeIndex = FindMemoryType(memory_requirements.memoryTypeBits, properties);

	if (vkAllocateMemory(m_device, &alloc_info, NULL, &memory) != VK_SUCCESS)
	{
		vkDestroyBuffer(m_device, buffer, NULL);
		return false;
	}

	vkBindBufferMemory(m_device, buffer, memory, 0);
	return true;
}

VkCommandBuffer RenderInterfaceVulkan::BeginOneTimeCommands()
{
	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandPool = m_command_pool;
	alloc_info.commandBufferCount = 1;

	VkCommandBuffer command_buffer;
	vkAllocateCommandBuffers(m_device, &alloc_info, &command_buffer);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(command_buffer, &begin_info);

	return command_buffer;
}

void RenderInterfaceVulkan::EndOneTimeCommands(VkCommandBuffer command_buffer)
{
	vkEndCommandBuffer(command_buffer);

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &command_buffer;

	vkQueueSubmit(m_queue, 1, &submit_info, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_queue);

	vkFreeCommandBuffers(m_device, m_command_pool, 1, &command_buffer);
}

VkShaderModule RenderInterfaceVulkan::LoadShaderModule(const Rocket::Core::String& source_path)
{
	Rocket::Core::FileInterface* file_interface = Rocket::Core::GetFileInterface();
	Rocket::Core::FileHandle file_handle = file_interface->Open(source_path);
	if (!file_handle)
	{
		Shell::DisplayError("Vulkan: could not open shader '%s'.", source_path.CString());
		return VK_NULL_HANDLE;
	}

	file_interface->Seek(file_handle, 0, SEEK_END);
	size_t buffer_size = file_interface->Tell(file_handle);
	file_interface->Seek(file_handle, 0, SEEK_SET);

	std::vector<char> buffer(buffer_size);
	file_interface->Read(buffer.data(), buffer_size, file_handle);
	file_interface->Close(file_handle);

	VkShaderModuleCreateInfo module_info = {};
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = buffer_size;
	module_info.pCode = (const uint32_t*) buffer.data();

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(m_device, &module_info, NULL, &module) != VK_SUCCESS)
		return VK_NULL_HANDLE;

	return module;
}
