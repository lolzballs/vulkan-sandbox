#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "basic.comp.h"
#include "vulkan/context.h"

struct input_dim {
    int32_t width;
    int32_t height;
};

static int32_t
find_device_memory(VkPhysicalDevice physical_device, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (int32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if (memory_properties.memoryTypes[i].propertyFlags & properties) {
            return i;
        }
    }

    return -1;
}

static VkResult
create_buffer(VkBuffer *buffer, VkDevice device,
        VkDeviceSize size, VkBufferUsageFlags usage) {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    return vkCreateBuffer(device, &create_info, NULL, buffer);
}

int main() {
    VkResult res = VK_ERROR_UNKNOWN;

    struct vulkan_ctx *ctx = vulkan_ctx_create();

    VkDeviceSize buffer_size = 1920 * 1080 * sizeof(uint32_t);
    VkBuffer buffer;
    res = create_buffer(&buffer, ctx->device, buffer_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    assert(res == VK_SUCCESS);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(ctx->device, buffer, &memory_requirements);

    /* TODO: search for memory based on memory_requirements */
    int32_t memory_index = find_device_memory(ctx->physical_device,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    assert(memory_index >= 0);

    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_index,
    };

    VkDeviceMemory memory;
    res = vkAllocateMemory(ctx->device, &allocate_info, NULL, &memory);
    assert(res == VK_SUCCESS);

    res = vkBindBufferMemory(ctx->device, buffer, memory, 0);
    assert(res == VK_SUCCESS);

    int32_t host_memory_index = find_device_memory(ctx->physical_device,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    assert(host_memory_index >= 0);

    /* allocate host memory for host buffer */
    VkMemoryAllocateInfo host_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = host_memory_index,
    };

    VkDeviceMemory host_memory;
    res = vkAllocateMemory(ctx->device, &host_allocate_info, NULL, &host_memory);
    assert(res == VK_SUCCESS);

    VkBuffer host_buffer;
    res = create_buffer(&host_buffer, ctx->device, buffer_size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    assert(res == VK_SUCCESS);

    res = vkBindBufferMemory(ctx->device, host_buffer, host_memory, 0);
    assert(res == VK_SUCCESS);

    VkDescriptorSetLayoutBinding dset_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo dset_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &dset_layout_binding,
    };
    VkDescriptorSetLayout dset_layout;
    res = vkCreateDescriptorSetLayout(ctx->device, &dset_layout_create_info, NULL, &dset_layout);
    assert(res == VK_SUCCESS);

    VkDescriptorPoolSize dpool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
    };
    VkDescriptorPoolCreateInfo dpool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &dpool_size,
    };

    VkDescriptorPool dpool;
    res = vkCreateDescriptorPool(ctx->device, &dpool_create_info, NULL, &dpool);
    assert(res == VK_SUCCESS);

    VkDescriptorSetAllocateInfo dset_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dset_layout,
    };
    VkDescriptorSet dset;
    res = vkAllocateDescriptorSets(ctx->device, &dset_allocate_info, &dset);
    assert(res == VK_SUCCESS);

    VkDescriptorBufferInfo descriptor_buffer_info = {
        .buffer = buffer,
        .offset = 0,
        .range = buffer_size,
    };
    VkWriteDescriptorSet write_dset = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &descriptor_buffer_info,
    };
    vkUpdateDescriptorSets(ctx->device, 1, &write_dset, 0, NULL);

    VkShaderModuleCreateInfo shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(basic_comp_data),
        .pCode = basic_comp_data,
    };

    VkShaderModule shader_module;
    vkCreateShaderModule(ctx->device, &shader_module_create_info, NULL, &shader_module);

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(struct input_dim), /* ivec2 */
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dset_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout;
    res = vkCreatePipelineLayout(ctx->device, &pipeline_layout_create_info, NULL, &pipeline_layout);
    assert(res == VK_SUCCESS);

    VkComputePipelineCreateInfo pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = 0,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shader_module,
            .pName = "main",
        },
        .layout = pipeline_layout,
    };
    VkPipeline pipeline;
    res = vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &pipeline);
    assert(res == VK_SUCCESS);

    VkCommandPoolCreateInfo cmd_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = ctx->queue_family_index,
    };
    VkCommandPool cmd_pool;
    vkCreateCommandPool(ctx->device, &cmd_pool_create_info, NULL, &cmd_pool);

    VkCommandBufferAllocateInfo cmd_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd_buffer;
    vkAllocateCommandBuffers(ctx->device, &cmd_buffer_allocate_info, &cmd_buffer);

    struct input_dim input_dim = {
        .width = 1920,
        .height = 1080,
    };

    VkCommandBufferBeginInfo cmd_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_begin_info);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
            0, 1, &dset, 0, NULL);
    vkCmdPushConstants(cmd_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(struct input_dim), &input_dim);

    vkCmdDispatch(cmd_buffer, (1920 + 15) / 16, (1080 + 15) / 16, 1);

    /* wait for compute shader to finish, then copy to host memory buffer */
    VkBufferMemoryBarrier memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd_buffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, NULL,
            1, &memory_barrier,
            0, NULL);

    VkBufferCopy buffer_copy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = buffer_size,
    };
    vkCmdCopyBuffer(cmd_buffer, buffer, host_buffer, 1, &buffer_copy);

    res = vkEndCommandBuffer(cmd_buffer);
    assert(res == VK_SUCCESS);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buffer,
    };

    VkFenceCreateInfo fence_create_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkFence fence;
    res = vkCreateFence(ctx->device, &fence_create_info, NULL, &fence);
    assert(res == VK_SUCCESS);

    res = vkQueueSubmit(ctx->queue, 1, &submit_info, fence);
    assert(res == VK_SUCCESS);

    res = vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX);
    assert(res == VK_SUCCESS);

    vkDestroyFence(ctx->device, fence, NULL);

    uint8_t *data;
    res = vkMapMemory(ctx->device, host_memory, 0, buffer_size, 0, (void **) &data);
    assert(res == VK_SUCCESS);

    FILE *file = fopen("buffer.bin", "wb");
    fwrite(data, buffer_size, 1, file);
    fclose(file);

    /* TODO: call vulkan_ctx_destroy(ctx); */
}
