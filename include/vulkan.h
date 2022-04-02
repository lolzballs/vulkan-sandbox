#ifndef VULKAN_H
#define VULKAN_H

#include <vulkan/vulkan.h>

struct vulkan_ctx {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;

    uint32_t queue_family_index;
    VkQueue queue;
};

struct vulkan_ctx *vulkan_ctx_create();
void vulkan_ctx_destroy(struct vulkan_ctx *ctx);

#endif
