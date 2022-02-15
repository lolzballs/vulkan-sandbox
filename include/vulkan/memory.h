#ifndef VULKAN_MEMORY_H
#define VULKAN_MEMORY_H

#include "vulkan/context.h"

enum vulkan_buffer_type {
    VULKAN_BUFFER_TYPE_HOST,
    VULKAN_BUFFER_TYPE_DEVICE_LOCAL,
};

struct vulkan_buffer {
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceSize size;
};

VkResult vulkan_buffer_init(struct vulkan_ctx *ctx, struct vulkan_buffer *ini,
        enum vulkan_buffer_type buffer_type, VkDeviceSize size);
void vulkan_buffer_finish(struct vulkan_ctx *ctx, struct vulkan_buffer *buffer);

#endif
