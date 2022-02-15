#include "vulkan/memory.h"

#include <stdio.h>

static int32_t
find_device_memory(VkPhysicalDevice physical_device,
        const VkMemoryRequirements *requirements, enum vulkan_buffer_type buffer_type) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    VkMemoryPropertyFlags supported_flags = requirements->memoryTypeBits;
    VkMemoryPropertyFlags required_flags = 0;
    switch (buffer_type) {
        case VULKAN_BUFFER_TYPE_HOST:
            required_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            break;
        case VULKAN_BUFFER_TYPE_DEVICE_LOCAL:
            required_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
    }

    for (int32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        VkMemoryPropertyFlags flags = memory_properties.memoryTypes[i].propertyFlags;
        if ((flags & required_flags) == required_flags
                && (flags & supported_flags)) {
            return i;
        }
    }
    return -1;
}

VkResult
vulkan_buffer_init(struct vulkan_ctx *ctx, struct vulkan_buffer *ini,
        enum vulkan_buffer_type buffer_type, VkDeviceSize size) {
    VkResult res;

    ini->size = size;

    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    res = vkCreateBuffer(ctx->device, &create_info, NULL, &ini->buffer);
    if (res != VK_SUCCESS) {
        return res;
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(ctx->device, ini->buffer, &memory_requirements);

    int32_t memory_index = find_device_memory(ctx->physical_device,
            &memory_requirements, buffer_type);
    if (memory_index == -1) {
        fprintf(stderr, "error: no suitable memory found\n");
        vkDestroyBuffer(ctx->device, ini->buffer, NULL); 
        return VK_ERROR_UNKNOWN;
    }

    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_index,
    };
    res = vkAllocateMemory(ctx->device, &allocate_info, NULL, &ini->memory);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "error: failed to allocate memory\n");
        vkDestroyBuffer(ctx->device, ini->buffer, NULL); 
        return res;
    }

    res = vkBindBufferMemory(ctx->device, ini->buffer, ini->memory, 0);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "error: failed to bind buffer to memory\n");
        vkDestroyBuffer(ctx->device, ini->buffer, NULL); 
        vkFreeMemory(ctx->device, ini->memory, NULL);
        return res;
    }

    return VK_SUCCESS;
}

void
vulkan_buffer_finish(struct vulkan_ctx *ctx, struct vulkan_buffer *buffer) {
    vkDestroyBuffer(ctx->device, buffer->buffer, NULL);
    buffer->buffer = VK_NULL_HANDLE;
    vkFreeMemory(ctx->device, buffer->memory, NULL);
    buffer->memory = VK_NULL_HANDLE;
}
