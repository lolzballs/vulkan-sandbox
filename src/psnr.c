#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "psnr.comp.h"
#include "vulkan/context.h"
#include "vulkan/memory.h"

struct buffer_range {
    VkDeviceSize offset;
    VkDeviceSize range;
};

/* stores the ranges for one frame in the device buffer.
 * we will use this to copy input to and output buffer.
 */
struct shader_buffer_ranges {
    struct buffer_range reference_range;
    struct buffer_range distorted_range;
    struct buffer_range output_range;
};

struct input_yuv {
    int32_t width;
    int32_t height;
    int32_t fps_num;
    int32_t fps_denom;

    int fd;
    void *data;

    size_t frame_count;
    uint8_t **frames;
};

struct push_constants {
    int width;
    int height;
};

static VkResult
copy_frame_to_buffer(struct vulkan_ctx *ctx, struct vulkan_buffer *buffer,
        struct shader_buffer_ranges *ranges, struct input_yuv *reference,
        struct input_yuv *distorted, size_t frame_idx) {
    VkResult res = VK_SUCCESS;
    uint8_t *data = NULL;

    /* let's assume the ranges are continuous for this buffer
     * it's a staging buffer anyways */
    VkDeviceSize image_size = ranges->reference_range.range;
    VkDeviceSize output_size = ranges->output_range.range;

    res = vkMapMemory(ctx->device, buffer->memory,
            0, 2 * image_size + output_size,
            0, (void **) &data);
    if (res != VK_SUCCESS) {
        return res;
    }

    /* copy reference and distorted images */
    memcpy(data, reference->frames[frame_idx], ranges->reference_range.range);
    memcpy(data + image_size, distorted->frames[frame_idx], ranges->reference_range.range);
    /* clear output to zero */
    memset(data + 2 * image_size, 0, output_size);

    vkUnmapMemory(ctx->device, buffer->memory);

    return res;
}


static struct input_yuv *
read_y4m(char *path) {
    FILE *file = fopen(path, "rb");
    assert(file != NULL);

    struct input_yuv *ini = calloc(1, sizeof(struct input_yuv));
    assert(ini != NULL);

    /* read header */
    char header[256];
    fgets(header, sizeof(header), file);
    size_t header_len = strnlen(header, sizeof(header));

    char *token = strtok(header, " ");
    assert(strcmp(token, "YUV4MPEG2") == 0);

    while ((token = strtok(NULL, " ")) != NULL) {
        switch (token[0]) {
            case 'W':
                ini->width = strtol(token + 1, NULL, 10);
                break;
            case 'H':
                ini->height = strtol(token + 1, NULL, 10);
                break;
            case 'F':
                sscanf(token + 1, "%d:%d", &ini->fps_num, &ini->fps_denom);
                break;
            default:
                fprintf(stderr, "read_y4m: ignoring header element %s\n", token);
        }
    }

    size_t frame_size = ini->width * ini->height * 3 / 2 + sizeof("FRAME");

    /* use unix stuff to read the y4m */
    int res = 0;
    ini->fd = fileno(file);

    struct stat stat;
    res = fstat(ini->fd, &stat);
    assert(res == 0);

    ini->frame_count = (stat.st_size - header_len) / frame_size;
    ini->frames = calloc(sizeof(uint8_t *), ini->frame_count);

    void *mapped = mmap(NULL, frame_size * ini->frame_count, PROT_READ, MAP_PRIVATE, ini->fd, 0);
    if (mapped == MAP_FAILED) {
        perror("mmap");
        exit(-1);
    }
    ini->data = mapped;

    /* forward this pointer past the file header */
    mapped += header_len;

    /* init the frames array */
    for (size_t i = 0; i < ini->frame_count; i++) {
        assert(strncmp(mapped, "FRAME\n", sizeof("FRAME")) == 0);
        ini->frames[i] = mapped + sizeof("FRAME");
        mapped += frame_size;
    }

    return ini;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <in_y4m> <distorted_y4m>\n", argv[0]);
        return -1;
    }
    VkResult res = VK_ERROR_UNKNOWN;

    struct input_yuv *reference_yuv = read_y4m(argv[1]);
    struct input_yuv *distorted_yuv = read_y4m(argv[2]);
    struct push_constants image_dim = {
        .width = reference_yuv->width,
        .height = reference_yuv->height,
    };

    VkDeviceSize image_size = reference_yuv->height * reference_yuv->width * sizeof(uint8_t);
    VkDeviceSize output_size = sizeof(float);

    struct shader_buffer_ranges buffer_ranges = {
        .reference_range = {
            .offset = 0,
            .range = image_size,
        },
        .distorted_range = {
            .offset = image_size,
            .range = image_size,
        },
        .output_range = {
            .offset = 2 * image_size,
            .range = sizeof(float),
        },
    };

    VkDeviceSize buffer_size = 2 * image_size + output_size;

    struct vulkan_ctx *ctx = vulkan_ctx_create(buffer_size);

    struct vulkan_buffer buffer_device;
    vulkan_buffer_init(ctx, &buffer_device, VULKAN_BUFFER_TYPE_DEVICE_LOCAL, buffer_size);
    struct vulkan_buffer buffer_host;
    vulkan_buffer_init(ctx, &buffer_host, VULKAN_BUFFER_TYPE_HOST, buffer_size);
    
    /* plan to have two descriptors:
     * 0. Buffer -> src yuv
     * 1. Buffer -> distorted yuv
     * 2. Buffer -> output float
     **/
    VkDescriptorPoolSize dpool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 3, /* may up this later so we aren't waiting on disk */
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

    VkDescriptorSetLayoutBinding reference_yuv_layout_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutBinding distorted_yuv_layout_binding = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutBinding result_layout_binding = {
        .binding = 2,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    VkDescriptorSetLayoutBinding dset_layout_bindings[] = {
        reference_yuv_layout_binding,
        distorted_yuv_layout_binding,
        result_layout_binding,
    };
    VkDescriptorSetLayoutCreateInfo dset_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = sizeof(dset_layout_bindings) / sizeof(VkDescriptorSetLayoutBinding),
        .pBindings = dset_layout_bindings,
    };
    VkDescriptorSetLayout dset_layout;
    res = vkCreateDescriptorSetLayout(ctx->device, &dset_layout_create_info, NULL, &dset_layout);
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

    VkDescriptorBufferInfo reference_yuv_descriptor_info = {
        .buffer = buffer_device.buffer,
        .offset = buffer_ranges.reference_range.offset,
        .range = buffer_ranges.reference_range.range,
    };
    VkWriteDescriptorSet reference_yuv_dset = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &reference_yuv_descriptor_info,
    };
    VkDescriptorBufferInfo distorted_yuv_descriptor_info = {
        .buffer = buffer_device.buffer,
        .offset = buffer_ranges.distorted_range.offset,
        .range = buffer_ranges.distorted_range.range,
    };
    VkWriteDescriptorSet distorted_yuv_dset = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset,
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &distorted_yuv_descriptor_info,
    };
    VkDescriptorBufferInfo result_descriptor_buffer_info = {
        .buffer = buffer_device.buffer,
        .offset = buffer_ranges.output_range.offset,
        .range = buffer_ranges.output_range.range,
    };
    VkWriteDescriptorSet result_dset = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &result_descriptor_buffer_info,
    };
    VkWriteDescriptorSet dsets[] = { reference_yuv_dset, distorted_yuv_dset, result_dset };
    vkUpdateDescriptorSets(ctx->device,
            sizeof(dsets) / sizeof(VkWriteDescriptorSet),
            dsets, 0, NULL);

    VkShaderModuleCreateInfo shader_module_create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(psnr_comp_data),
        .pCode = psnr_comp_data,
    };

    VkShaderModule shader_module;
    vkCreateShaderModule(ctx->device, &shader_module_create_info, NULL, &shader_module);

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(struct push_constants),
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

    /* later we should make use of buffer_ranges */
    VkBufferCopy yuv_copy = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = buffer_size,
    };
    vkCmdCopyBuffer(cmd_buffer, buffer_host.buffer, buffer_device.buffer, 1, &yuv_copy);

    /* wait for compute shader to finish, then copy to host memory buffer */
    VkBufferMemoryBarrier copy_to_device_memory_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer_device.buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,
            0, NULL,
            1, &copy_to_device_memory_barrier,
            0, NULL);

    vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
            0, 1, &dset, 0, NULL);
    vkCmdPushConstants(cmd_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(struct push_constants), &image_dim);

    vkCmdDispatch(cmd_buffer, (1920 + 15) / 16, (1080 + 15) / 16, 1);

    /* wait for compute shader to finish, then copy to host memory buffer */
    VkBufferMemoryBarrier copy_to_host_memory_barrier = {
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
            1, &copy_to_host_memory_barrier,
            0, NULL);

    VkBufferCopy buffer_copy = {
        .srcOffset = 2 * image_size,
        .dstOffset = buffer_ranges.output_range.offset,
        .size = buffer_ranges.output_range.range,
    };
    vkCmdCopyBuffer(cmd_buffer, buffer_device.buffer, buffer_host.buffer, 1, &buffer_copy);

    res = vkEndCommandBuffer(cmd_buffer);
    assert(res == VK_SUCCESS);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buffer,
    };

    res = copy_frame_to_buffer(ctx, &buffer_host, &buffer_ranges,
            reference_yuv, distorted_yuv, 0);
    assert(res == VK_SUCCESS);

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

    uint32_t *data;
    res = vkMapMemory(ctx->device, buffer_host.memory, 2 * image_size,
            sizeof(float), 0, (void **) &data);
    assert(res == VK_SUCCESS);

    uint32_t squared_error = *data;
    vkUnmapMemory(ctx->device, buffer_host.memory);

    double mse = (double) squared_error / (double) (reference_yuv->width * reference_yuv->height);

    printf("output: %d\n", squared_error);
    printf("psnr: %f\n", 20 * log10(255) - 10 * log10(mse));


    /* TODO: clean up ctx, buffers */
}
