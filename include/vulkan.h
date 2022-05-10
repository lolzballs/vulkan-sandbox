#ifndef VULKAN_H
#define VULKAN_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

struct vulkan_ctx_features {
	bool enable_ycbcr_conversion;
};

struct vulkan_ctx {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;

    uint32_t queue_family_index;
    VkQueue queue;

	VkPhysicalDeviceMemoryProperties memory_properties;
	uint32_t host_visible_memory_index;
	uint32_t device_local_memory_index;
};

struct vulkan_ctx *vulkan_ctx_create(struct vulkan_ctx_features *features);
void vulkan_ctx_destroy(struct vulkan_ctx *ctx);

VkResult vulkan_ctx_create_fence(struct vulkan_ctx *ctx, VkFence *fence, bool init);
VkResult vulkan_ctx_create_semaphore(struct vulkan_ctx *ctx, VkSemaphore *semaphore);
VkResult vulkan_ctx_create_cmd_pool(struct vulkan_ctx *ctx, VkCommandPool *cmd_pool,
		VkCommandPoolCreateFlags flags);
VkResult vulkan_ctx_create_shader_module(struct vulkan_ctx *ctx,
		VkShaderModule *shader_module, size_t code_size, const void *code);

#endif
