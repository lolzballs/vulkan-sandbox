#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "image.h"

static VkResult
copy_to_memory(struct vulkan_ctx *vk, VkDeviceMemory dst,
		const VkSubresourceLayout *layout,
		uint32_t width, uint32_t height, const void *data) {
	VkResult res;

	void *dst_ptr;
	res = vkMapMemory(vk->device, dst, layout->offset, layout->size, 0, &dst_ptr);
	if (res != VK_SUCCESS) {
		return res;
	}

	if (layout->rowPitch != width) {
		for (uint32_t row = 0; row < height; row++) {
			memcpy(dst_ptr + row * layout->rowPitch, data + row * width, width);
		}
	} else {
		memcpy(dst_ptr, data, width * height);
	}
	
	vkUnmapMemory(vk->device, dst);
	return VK_SUCCESS;
}

static VkResult
create_vulkan_image(struct vulkan_ctx *vk, uint32_t width, uint32_t height,
		VkFormat format, bool disjoint, VkImage *image) {
	const VkImageCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = NULL,
		.flags = disjoint ? VK_IMAGE_CREATE_DISJOINT_BIT : 0,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = {
			.width = width,
			.height = height,
			.depth = 1,
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_LINEAR,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &vk->queue_family_index,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	return vkCreateImage(vk->device, &create_info, NULL, image);
}

static VkResult
allocate_memory_with_requirements(struct vulkan_ctx *vk,
		VkMemoryRequirements requirements, VkDeviceMemory *memory) {
	assert(requirements.memoryTypeBits & (1 << vk->host_visible_memory_index));
	VkMemoryAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = NULL,
		.memoryTypeIndex = vk->host_visible_memory_index,
		.allocationSize = requirements.size,
	};
	return vkAllocateMemory(vk->device, &info, NULL, memory);
}

static void
get_image_memory_requirements(struct vulkan_ctx *vk, VkImage image,
		VkMemoryRequirements2 *requirements) {
	VkImageMemoryRequirementsInfo2 info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = NULL,
		.image = image,
	};
	requirements->sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	requirements->pNext = NULL;
	vkGetImageMemoryRequirements2(vk->device, &info, requirements);
}

static void
get_plane_memory_requirements(struct vulkan_ctx *vk, VkImage image,
		VkImageAspectFlagBits plane, VkMemoryRequirements2 *requirements) {
	VkImagePlaneMemoryRequirementsInfo plane_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO,
		.pNext = NULL,
		.planeAspect = plane,
	};
	VkImageMemoryRequirementsInfo2 info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.pNext = &plane_info,
		.image = image,
	};
	vkGetImageMemoryRequirements2(vk->device, &info, requirements);
}

static int
mmap_file(const char *file, size_t size, void **mapped_ptr) {
	int fd = open(file, O_RDONLY);
	if (fd == -1) {
		perror("mmap_file - open");
		return -1;
	}

	void *out = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (out == MAP_FAILED) {
		perror("mmap_file - mmap");
		return -1;
	}

	*mapped_ptr = out;
	return 0;
}

VkResult
image_init_from_file(struct image *ini, struct vulkan_ctx *vk,
		const char *file, uint32_t width, uint32_t height,
		enum image_format format, bool disjoint) {
	VkResult res;
	size_t size = image_format_size(format, width, height);

	void *mapped_ptr;
	if (mmap_file(file, size, &mapped_ptr) == -1) {
		return VK_ERROR_UNKNOWN;
	}

	res = image_init_from_memory(ini, vk, mapped_ptr,
			width, height, format, disjoint);
	if (res != VK_SUCCESS) {
		return res;
	}

	if (munmap(mapped_ptr, size) == -1) {
		perror("image_init_from_file - munmap");
		return VK_ERROR_UNKNOWN;
	}

	return VK_SUCCESS;
}

VkResult
image_init_from_memory(struct image *ini, struct vulkan_ctx *vk,
		void *mem, uint32_t width, uint32_t height,
		enum image_format format, bool disjoint) {
	static const VkImageAspectFlagBits plane_aspects[3] = {
		VK_IMAGE_ASPECT_PLANE_0_BIT,
		VK_IMAGE_ASPECT_PLANE_1_BIT,
		VK_IMAGE_ASPECT_PLANE_2_BIT,
	};

	VkResult res;
	uint32_t plane_count = image_format_plane_count(format);

	VkImage image;
	res = create_vulkan_image(vk, width, height,
			image_format_to_vk_format(format), disjoint, &image);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "image_init_from_memory - failed to create_vulkan_image\n");
		return res;
	}

	VkBindImageMemoryInfo bind_infos[3];
	VkBindImagePlaneMemoryInfo bind_plane_infos[3];
	VkDeviceMemory memories[3];
	VkMemoryRequirements2 requirements;
	VkImageSubresource subresource = {
		.arrayLayer = 0,
		.mipLevel = 0,
	};
	VkSubresourceLayout subresource_layout;
	if (disjoint) {
		size_t mem_offset = 0;
		for (uint32_t plane = 0; plane < plane_count; plane++) {
			get_plane_memory_requirements(vk, image, plane, &requirements);
			res = allocate_memory_with_requirements(vk,
					requirements.memoryRequirements, &memories[plane]);
			assert(res == VK_SUCCESS);

			uint32_t plane_width, plane_height;
			image_format_plane_size(format, width, height,
					&plane_width, &plane_height, plane);

			subresource.aspectMask = plane_aspects[plane];
			vkGetImageSubresourceLayout(vk->device, image, &subresource,
					&subresource_layout);
			res = copy_to_memory(vk, memories[plane], &subresource_layout,
					plane_width, plane_height, mem + mem_offset);
			assert(res == VK_SUCCESS);

			mem_offset += plane_width * plane_height;

			bind_plane_infos[plane] = (const VkBindImagePlaneMemoryInfo) {
				.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO,
				.pNext = NULL,
				.planeAspect = plane_aspects[plane],
			};
			bind_infos[plane] = (const VkBindImageMemoryInfo) {
				.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
				.pNext = &bind_plane_infos[plane],
				.image = image,
				.memory = memories[plane],
				.memoryOffset = 0,
			};
		}
	} else {
		get_image_memory_requirements(vk, image, &requirements);
		res = allocate_memory_with_requirements(vk,
				requirements.memoryRequirements, &memories[0]);
		assert(res == VK_SUCCESS);

		size_t mem_offset = 0;
		for (uint32_t plane = 0; plane < plane_count; plane++) {
			uint32_t plane_width, plane_height;
			image_format_plane_size(format, width, height,
					&plane_width, &plane_height, plane);

			subresource.aspectMask = plane_aspects[plane];
			vkGetImageSubresourceLayout(vk->device, image, &subresource,
					&subresource_layout);
			res = copy_to_memory(vk, memories[0], &subresource_layout,
					plane_width, plane_height, mem + mem_offset);
			assert(res == VK_SUCCESS);

			mem_offset += plane_width * plane_height;
		}

		bind_infos[0] = (const VkBindImageMemoryInfo) {
			.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
			.pNext = NULL,
			.image = image,
			.memory = memories[0],
			.memoryOffset = 0,
		};

		plane_count = 1;
	}

	res = vkBindImageMemory2(vk->device, plane_count, bind_infos);
	if (res != VK_SUCCESS) {
		return res;
	}

	ini->width = width;
	ini->height = height;
	ini->format = format;
	ini->plane_count = plane_count;
	ini->vk_image = image;
	memcpy(ini->vk_memories, memories, sizeof(ini->vk_memories));

	return VK_SUCCESS;
}

void
image_finish(struct image *image, struct vulkan_ctx *vk) {
	vkDestroyImage(vk->device, image->vk_image, NULL);

	for (uint32_t plane = 0; plane < image->plane_count; plane++) {
		vkFreeMemory(vk->device, image->vk_memories[plane], NULL);
	}
}

VkResult image_sampler_init(struct image_sampler *ini, struct vulkan_ctx *vk,
		enum image_format format) {
	VkResult res;

	VkSamplerYcbcrConversion ycbcr_conversion;
	VkSamplerYcbcrConversionCreateInfo ycbcr_conversion_create = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
		.format = image_format_to_vk_format(format),
		.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
		.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
		.xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
		.yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
		.chromaFilter = VK_FILTER_NEAREST,
	};
	res = vkCreateSamplerYcbcrConversion(vk->device, &ycbcr_conversion_create,
			NULL, &ycbcr_conversion);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkSampler sampler;
	VkSamplerYcbcrConversionInfo ycbcr_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
		.conversion = ycbcr_conversion,
	};
	VkSamplerCreateInfo sampler_create = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = &ycbcr_info,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		.anisotropyEnable = VK_FALSE,
		.compareEnable = VK_FALSE,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};
	res = vkCreateSampler(vk->device, &sampler_create, NULL, &sampler);
	if (res != VK_SUCCESS) {
		vkDestroySamplerYcbcrConversion(vk->device, ycbcr_conversion, NULL);
		return res;
	}

	ini->format = format;
	ini->conversion = ycbcr_conversion;
	ini->sampler = sampler;
	return VK_SUCCESS;
}

void
image_sampler_finish(struct image_sampler *sampler, struct vulkan_ctx *vk) {
	vkDestroySampler(vk->device, sampler->sampler, NULL);
	sampler->sampler = VK_NULL_HANDLE;
	vkDestroySamplerYcbcrConversion(vk->device, sampler->conversion, NULL);
	sampler->conversion = VK_NULL_HANDLE;
}
