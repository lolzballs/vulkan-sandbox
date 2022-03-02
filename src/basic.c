#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "basic.comp.h"
#include "vulkan/context.h"
#include "vulkan/memory.h"

struct input_dim {
    int32_t width;
    int32_t height;
};

int main() {
    VkResult res = VK_ERROR_UNKNOWN;

    struct input_dim input_dim = {
        .width = 1920,
        .height = 1080,
    };

    VkDeviceSize buffer_size = input_dim.height * input_dim.width * sizeof(uint32_t);

    struct vulkan_ctx *ctx = vulkan_ctx_create(buffer_size);

    struct vulkan_buffer buffer_device;
    vulkan_buffer_init(ctx, &buffer_device, VULKAN_BUFFER_TYPE_DEVICE_LOCAL, buffer_size);
    struct vulkan_buffer buffer_host;
    vulkan_buffer_init(ctx, &buffer_host, VULKAN_BUFFER_TYPE_HOST, buffer_size);

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
        .buffer = buffer_device.buffer,
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
        .buffer = buffer_device.buffer,
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
    vkCmdCopyBuffer(cmd_buffer, buffer_device.buffer, buffer_host.buffer, 1, &buffer_copy);

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
    res = vkMapMemory(ctx->device, buffer_host.memory, 0, buffer_size, 0, (void **) &data);
    assert(res == VK_SUCCESS);

    FILE *file = fopen("buffer.bin", "wb");
    fwrite(data, buffer_size, 1, file);
    fclose(file);

    /* TODO: clean up ctx, buffers */
}
