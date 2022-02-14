#include "vulkan/context.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t
find_unified_queue(VkPhysicalDevice device) {
    uint32_t count = 8;
    VkQueueFamilyProperties properties[8];
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);

    int32_t candidate = -1;
    for (int32_t i = 0; i < count; i++) {
        if (properties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
            return i;
        }
    }
    return candidate;

}

static VkResult
create_vulkan_instance(VkInstance *instance) {
    VkResult res = VK_ERROR_UNKNOWN;

    uint32_t layer_count = 32;
    VkLayerProperties layers[32];

    res = vkEnumerateInstanceLayerProperties(&layer_count, layers);
    if (res == VK_INCOMPLETE) {
        fprintf(stderr, "warning: there were more instance layers "
                "than the max supported (32)\n");
    } else if (res != VK_SUCCESS) {
        return res;
    }

    VkApplicationInfo application_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_2,
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application_info,
    };

    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    bool validation_layer_present = false;
    for (uint32_t layer = 0; layer < layer_count; layer++) {
        if (strncmp(layers[layer].layerName,
                    validation_layer, VK_MAX_EXTENSION_NAME_SIZE) == 0) {
            validation_layer_present = true;
            break;
        }
    }

    if (validation_layer_present) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = &validation_layer;
    } else {
        fprintf(stderr, "warning: validation layer is not present\n");
    }

    res = vkCreateInstance(&create_info, NULL, instance);
    if (res != VK_SUCCESS) {
        return res;
    }

    return res;
}

static VkResult
create_vulkan_device(struct vulkan_ctx *ini) {
    int32_t queue_index = find_unified_queue(ini->physical_device);
    float queue_priority = 1.0;
    assert(queue_index >= 0);

    /* cast to uint32_t is safe due to assert */
    ini->queue_family_index = (uint32_t) queue_index;

    VkDeviceQueueCreateInfo queue_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = queue_index,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_create_info,
    };

    VkResult res = VK_ERROR_UNKNOWN;
    res = vkCreateDevice(ini->physical_device, &device_create_info, NULL, &ini->device);
    if (res != VK_SUCCESS) {
        return res;
    }

    vkGetDeviceQueue(ini->device, queue_index, 0, &ini->queue);
    return res;
}

struct vulkan_ctx *
vulkan_ctx_create() {
    VkResult res = VK_ERROR_UNKNOWN;

    struct vulkan_ctx *ini = calloc(1, sizeof(struct vulkan_ctx));

    res = create_vulkan_instance(&ini->instance);
    assert(res == VK_SUCCESS);

    uint32_t physical_device_count = 1;
    res = vkEnumeratePhysicalDevices(ini->instance, &physical_device_count, &ini->physical_device);
    if (res == VK_INCOMPLETE) {
        fprintf(stderr, "warning: more than one VkPhysicalDevice present\n");
        res = VK_SUCCESS;
    }
    assert(res == VK_SUCCESS);

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(ini->physical_device, &physical_device_properties);
    printf("using physical device 0: %s\n", physical_device_properties.deviceName);

    res = create_vulkan_device(ini);
    assert(res == VK_SUCCESS);

    return ini;
}

void
vulkan_ctx_destroy(struct vulkan_ctx *ctx) {
    ctx->queue = VK_NULL_HANDLE;
    ctx->physical_device = VK_NULL_HANDLE;

    vkDestroyDevice(ctx->device, NULL);
    ctx->device = VK_NULL_HANDLE;

    vkDestroyInstance(ctx->instance, NULL);
    ctx->instance = VK_NULL_HANDLE;

    free(ctx);
    ctx = NULL;
}
