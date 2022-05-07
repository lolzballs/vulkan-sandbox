#ifndef IMAGE_H
#define IMAGE_H

#include "vulkan.h"

enum image_format {
	IMAGE_FORMAT_YU12,
	IMAGE_FORMAT_NV12,
	IMAGE_FORMAT_P420,
};

static VkFormat
image_format_to_vk_format(enum image_format format) {
	switch (format) {
		case IMAGE_FORMAT_YU12:
			return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
		case IMAGE_FORMAT_NV12:
			return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
		case IMAGE_FORMAT_P420:
			return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
	}
	return VK_FORMAT_UNDEFINED;
}

static uint32_t
image_format_plane_count(enum image_format format) {
	switch (format) {
		case IMAGE_FORMAT_YU12:
			return 3;
		case IMAGE_FORMAT_NV12:
			return 2;
		case IMAGE_FORMAT_P420:
			return 3;
	}
	return -1;
}

static size_t
image_format_size(enum image_format format, uint32_t width, uint32_t height) {
	switch (format) {
		case IMAGE_FORMAT_YU12:
			return width * height * 3 / 2;
		case IMAGE_FORMAT_NV12:
			return width * height * 3 / 2;
		case IMAGE_FORMAT_P420:
			return width * height * 2;
	}
	return -1;
}

static void
image_format_plane_size(enum image_format format, 
		uint32_t base_width, uint32_t base_height,
		uint32_t *plane_width, uint32_t *plane_height, uint32_t plane) {
	static uint32_t ratio_yu12[3][2] = { { 1, 1 }, { 2, 2 }, { 2, 2 } };
	static uint32_t ratio_nv12[3][2] = { { 1, 1 }, { 1, 2 } };
	static uint32_t ratio_422p[3][2] = { { 1, 1 }, { 2, 1 }, { 2, 1} };

	switch (format) {
		case IMAGE_FORMAT_YU12:
			*plane_width = base_width / ratio_yu12[plane][0];
			*plane_height = base_height / ratio_yu12[plane][1];
			break;
		case IMAGE_FORMAT_NV12:
			*plane_width = base_width / ratio_nv12[plane][0];
			*plane_height = base_height / ratio_nv12[plane][1];
			break;
		case IMAGE_FORMAT_P420:
			*plane_width = base_width / ratio_422p[plane][0];
			*plane_height = base_height / ratio_422p[plane][1];
			break;
	}
}

struct image {
	uint32_t width;
	uint32_t height;

	enum image_format format;
	uint32_t plane_count;
	VkImage vk_image;
	VkDeviceMemory vk_memories[3];
};

VkResult image_init_from_file(struct image *ini, struct vulkan_ctx *vk,
		const char *file, uint32_t width, uint32_t height,
		enum image_format format, bool disjoint);
VkResult image_init_from_memory(struct image *ini, struct vulkan_ctx *vk,
		void *mem, uint32_t width, uint32_t height,
		enum image_format format, bool disjoint);
void image_finish(struct image *image, struct vulkan_ctx *vk);

VkResult image_create_sampler(struct image *image, struct vulkan_ctx *vk,
		VkSamplerYcbcrConversion *ycbcr_conversion, VkSampler *sampler);

struct image_sampler {
	enum image_format format;
	VkSamplerYcbcrConversion conversion;
	VkSampler sampler;
};

VkResult image_sampler_init(struct image_sampler *ini, struct vulkan_ctx *vk,
		enum image_format format);
void image_sampler_finish(struct image_sampler *sampler, struct vulkan_ctx *vk);

#endif
