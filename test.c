#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>

struct input_dim {
    int32_t width;
    int32_t height;
};

static int32_t
find_compute_queue(VkPhysicalDevice physical_device) {
    uint32_t count = 256;
    VkQueueFamilyProperties properties[256];
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties);

    int32_t candidate = -1;
    for (int32_t i = 0; i < count; i++) {
        if (properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            return i;
        }
    }
    return candidate;
}

static int32_t
find_device_local_memory(VkPhysicalDevice physical_device) {
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (int32_t i = 0; i < memory_properties.memoryTypeCount; i++) {
        if (memory_properties.memoryTypes[i].propertyFlags &
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            return i;
        }
    }

    return -1;
}

static int32_t
load_spirv_shader_module(const char *path, VkShaderModuleCreateInfo *info) {
    info->sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info->pNext = 0;
    info->flags = 0;

    FILE *file = fopen(path, "rb");
    fseeko(file, 0, SEEK_END);
    info->codeSize = ftello(file);
    fseeko(file, 0, SEEK_SET);
    void *code = malloc(info->codeSize);
    fread(code, 1, info->codeSize, file);

    info->pCode = code;

    return 0;
}

int main() {
    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_2,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
    };

    VkInstance instance;
    vkCreateInstance(&create_info, NULL, &instance);

    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);

    VkPhysicalDevice *physical_devices = malloc(sizeof(VkPhysicalDevice) * physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);

    assert(physical_device_count > 0);

    int32_t queue_index = find_compute_queue(physical_devices[0]);
    assert(queue_index >= 0);

    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_index,
        .queueCount = 1,
        .pQueuePriorities = NULL,
    };

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL,
        .pNext = NULL,
    };

    VkDevice device;
    vkCreateDevice(physical_devices[0], &device_create_info, NULL, &device);

    VkQueue queue;
    vkGetDeviceQueue(device, queue_index, 0, &queue);


    VkResult res;

    VkDeviceSize buffer_size = 1920 * 1080 * sizeof(uint32_t);
    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .flags = 0,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = (const uint32_t *) &queue_index,
    };
    VkBuffer buffer;
    res = vkCreateBuffer(device, &buffer_create_info, NULL, &buffer);
    assert(res == VK_SUCCESS);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, buffer, &memory_requirements);

    /* TODO: search for memory based on memory_requirements */
    int32_t memory_index = find_device_local_memory(physical_devices[0]);
    assert(memory_index >= 0);

    VkMemoryAllocateInfo allocate_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex = memory_index,
    };

    VkDeviceMemory memory;
    res = vkAllocateMemory(device, &allocate_info, NULL, &memory);
    assert(res == VK_SUCCESS);

    res = vkBindBufferMemory(device, buffer, memory, 0);
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
    res = vkCreateDescriptorSetLayout(device, &dset_layout_create_info, NULL, &dset_layout);
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
    res = vkCreateDescriptorPool(device, &dpool_create_info, NULL, &dpool);
    assert(res == VK_SUCCESS);

    VkDescriptorSetAllocateInfo dset_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dpool,
        .descriptorSetCount = 1,
        .pSetLayouts = &dset_layout,
    };
    VkDescriptorSet dset;
    res = vkAllocateDescriptorSets(device, &dset_allocate_info, &dset);
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
    vkUpdateDescriptorSets(device, 1, &write_dset, 0, NULL);

    VkShaderModuleCreateInfo shader_module_create_info;
    load_spirv_shader_module("./a.spv", &shader_module_create_info);

    VkShaderModule shader_module;
    vkCreateShaderModule(device, &shader_module_create_info, NULL, &shader_module);

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        .offset = 0,
        .size = sizeof(int32_t) * 2, /* ivec2 */
    };

    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dset_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };

    VkPipelineLayout pipeline_layout;
    res = vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout);
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
    res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_create_info, NULL, &pipeline);
    assert(res == VK_SUCCESS);

    VkCommandPoolCreateInfo cmd_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_index,
    };
    VkCommandPool cmd_pool;
    vkCreateCommandPool(device, &cmd_pool_create_info, NULL, &cmd_pool);

    VkCommandBufferAllocateInfo cmd_buffer_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd_buffer;
    vkAllocateCommandBuffers(device, &cmd_buffer_allocate_info, &cmd_buffer);

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
    res = vkCreateFence(device, &fence_create_info, NULL, &fence);
    assert(res == VK_SUCCESS);

    res = vkQueueSubmit(queue, 1, &submit_info, fence);
    assert(res == VK_SUCCESS);

    res = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    assert(res == VK_SUCCESS);

    vkDestroyFence(device, fence, NULL);
}
