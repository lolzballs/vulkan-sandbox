#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "image.h"
#include "pipeline.h"
#include "window.h"

#define RENDER_FORMAT VK_FORMAT_B8G8R8A8_UNORM

static VkResult
create_command_buffer(struct vulkan_ctx *vk, VkCommandPool pool,
		VkCommandBuffer *cmd_buffer) {
	VkCommandBufferAllocateInfo info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandPool = pool,
		.commandBufferCount = 1,
	};
	return vkAllocateCommandBuffers(vk->device, &info, cmd_buffer);
}

static VkResult
create_renderpass(struct vulkan_ctx *vk, VkRenderPass *render_pass) {
	VkAttachmentDescription attachment_desc = {
		.flags = 0,
		.format = RENDER_FORMAT,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	VkAttachmentReference color_attachment = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass_desc = {
		.flags = 0,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.pInputAttachments = NULL,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment,
		.pResolveAttachments = NULL,
		.pDepthStencilAttachment = NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL,
	};

	VkRenderPassCreateInfo create = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.flags = 0,
		.attachmentCount = 1,
		.pAttachments = &attachment_desc,
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = 0,
		.pDependencies = NULL,
	};
	return vkCreateRenderPass(vk->device, &create, NULL, render_pass);
}

static VkResult
create_descriptor_pool(struct vulkan_ctx *vk, VkDescriptorPool *descriptor_pool) {
	VkDescriptorPoolCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.poolSizeCount = 1,
		.pPoolSizes = (VkDescriptorPoolSize[]) {
			{
				.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
			},
		},
		.maxSets = 1,
	};
	return vkCreateDescriptorPool(vk->device, &create_info, NULL, descriptor_pool);
}

static VkResult
create_descriptor_set_layout(struct vulkan_ctx *vk, VkDescriptorSetLayout *layout,
		VkSampler sampler) {
	VkDescriptorSetLayoutCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = (VkSampler[]) { sampler },
			}
		},
	};
	return vkCreateDescriptorSetLayout(vk->device, &create_info, NULL, layout);
}

static VkResult
allocate_descriptor_set(struct vulkan_ctx *vk, VkDescriptorSet *set,
		VkDescriptorPool pool, VkDescriptorSetLayout layout) {
	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout,
	};
	return vkAllocateDescriptorSets(vk->device, &alloc_info, set);
}

static VkResult
create_image_view(struct vulkan_ctx *vk, VkImageView *image_view,
		const struct image *image, const struct image_sampler *sampler) {
	VkSamplerYcbcrConversionInfo conversion_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
		.pNext = NULL,
		.conversion = sampler->conversion,
	};
	VkImageViewCreateInfo image_view_create = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = &conversion_info,
		.image = image->vk_image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = image_format_to_vk_format(image->format),
		.components = {
			.r = VK_COMPONENT_SWIZZLE_R,
			.g = VK_COMPONENT_SWIZZLE_G,
			.b = VK_COMPONENT_SWIZZLE_B,
			.a = VK_COMPONENT_SWIZZLE_A,
		},
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	return vkCreateImageView(vk->device, &image_view_create, NULL, image_view);
}

static VkResult
transition_image_layout(struct vulkan_ctx *vk, VkCommandPool cmd_pool,
		VkImage image) {
	VkResult res;

	VkCommandBuffer temp_buf;
	res = create_command_buffer(vk, cmd_pool, &temp_buf);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	res = vkBeginCommandBuffer(temp_buf, &begin_info);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkImageMemoryBarrier image_memory_barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image,
		.subresourceRange = (VkImageSubresourceRange) {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};
	vkCmdPipelineBarrier(temp_buf,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, NULL,
			0, NULL,
			1, &image_memory_barrier);

	vkEndCommandBuffer(temp_buf);

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &temp_buf,
	};
	res = vkQueueSubmit(vk->queue, 1, &submit_info, NULL);
	if (res != VK_SUCCESS) {
		return res;
	}

	res = vkQueueWaitIdle(vk->queue);
	if (res != VK_SUCCESS) {
		return res;
	}
	
	vkFreeCommandBuffers(vk->device, cmd_pool, 1, &temp_buf);
	return VK_SUCCESS;
}

static void
update_descriptor_with_image(struct vulkan_ctx *vk, VkDescriptorSet descriptor,
		VkSampler sampler, VkImageView image_view) {
	VkWriteDescriptorSet descriptor_write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = descriptor,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &(VkDescriptorImageInfo) {
				.sampler = sampler,
				.imageView = image_view,
				.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
	};
	vkUpdateDescriptorSets(vk->device, 1, &descriptor_write, 0, NULL);
}

struct swapchain_image {
	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
};

struct swapchain {
	VkSwapchainKHR vk_swapchain;
	VkExtent2D extent;

	uint32_t nimages;
	struct swapchain_image *images;
};

static void
destroy_swapchain_related_resources(struct vulkan_ctx *vk, struct swapchain *swapchain) {
	for (uint32_t i = 0; i < swapchain->nimages; i++) {
		vkDestroyImageView(vk->device, swapchain->images[i].image_view, NULL);
		swapchain->images[i].image_view = NULL;
		vkDestroyFramebuffer(vk->device, swapchain->images[i].framebuffer, NULL);
		swapchain->images[i].framebuffer = NULL;
	}
	swapchain->nimages = 0;
	free(swapchain->images);
	swapchain->images = NULL;
}

static VkResult
create_swapchain(struct vulkan_ctx *vk, VkSurfaceKHR surface,
		VkRenderPass render_pass, struct swapchain *swapchain) {
	VkResult res = VK_SUCCESS;

	res = vkDeviceWaitIdle(vk->device);
	assert(res == VK_SUCCESS);

	uint32_t nformats = 16;
	VkSurfaceFormatKHR surface_formats[16];
	res = vkGetPhysicalDeviceSurfaceFormatsKHR(vk->physical_device,
			surface, &nformats, surface_formats);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkSurfaceCapabilitiesKHR surface_caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk->physical_device, surface,
			&surface_caps);

	destroy_swapchain_related_resources(vk, swapchain);

	VkSwapchainCreateInfoKHR create_info = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = surface_caps.minImageCount,
		.imageFormat = RENDER_FORMAT,
		.imageExtent = surface_caps.currentExtent,
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &vk->queue_family_index,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR,
		.clipped = false,
		.oldSwapchain = swapchain->vk_swapchain,
	};
	res = vkCreateSwapchainKHR(vk->device, &create_info, NULL, &swapchain->vk_swapchain);
	if (res != VK_SUCCESS) {
		return res;
	}
	swapchain->extent = create_info.imageExtent;

	/* need to destroy old swapchain if we are recreating */
	if (create_info.oldSwapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(vk->device, create_info.oldSwapchain, NULL);
	}

	uint32_t nimages = 0;
	res = vkGetSwapchainImagesKHR(vk->device, swapchain->vk_swapchain, &nimages, NULL);
	if (res != VK_SUCCESS) {
		return res;
	}

	struct swapchain_image *images = calloc(nimages, sizeof(struct swapchain_image));

	VkImage *vk_images = calloc(nimages, sizeof(VkImage));
	res = vkGetSwapchainImagesKHR(vk->device, swapchain->vk_swapchain, &nimages, vk_images);
	if (res != VK_SUCCESS) {
		free(images);
		return res;
	}

	swapchain->nimages = nimages;
	swapchain->images = images;

	for (uint32_t i = 0; i < nimages; i++) {
		VkImageViewCreateInfo image_view_create = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.flags = 0,
			.image = vk_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = RENDER_FORMAT,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_R,
				.g = VK_COMPONENT_SWIZZLE_G,
				.b = VK_COMPONENT_SWIZZLE_B,
				.a = VK_COMPONENT_SWIZZLE_A,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};
		VkImageView image_view;
		res = vkCreateImageView(vk->device, &image_view_create, NULL, &image_view);
		if (res != VK_SUCCESS) {
			return res;
		}

		VkFramebufferCreateInfo framebuffer_create = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = render_pass,
			.attachmentCount = 1,
			.pAttachments = &image_view,
			.width = create_info.imageExtent.width,
			.height = create_info.imageExtent.height,
			.layers = 1,
		};
		VkFramebuffer framebuffer;
		vkCreateFramebuffer(vk->device, &framebuffer_create, NULL, &framebuffer);
		if (res != VK_SUCCESS) {
			return res;
		}

		swapchain->images[i].image = vk_images[i];
		swapchain->images[i].image_view = image_view;
		swapchain->images[i].framebuffer = framebuffer;
	}

	free(vk_images);
	return res;
}

struct app_params {
	uint32_t width;
	uint32_t height;
	bool disjoint;
	enum image_format format;
	char *image_path;
};

static void
parse_args(struct app_params *params, int argc, char *argv[]) {
	params->width = -1;
	params->height = -1;
	params->format = -1;
	params->disjoint = false;

	int opt;
	while ((opt = getopt(argc, argv, "w:h:f:d")) != -1) {
		switch (opt) {
			case 'w':
				params->width = atoi(optarg);
				break;
			case 'h':
				params->height = atoi(optarg);
				break;
			case 'f':
				if (strcmp(optarg, "nv12") == 0) {
					params->format = IMAGE_FORMAT_NV12;
				} else if (strcmp(optarg, "yu12") == 0) {
					params->format = IMAGE_FORMAT_YU12;
				} else if (strcmp(optarg, "422p") == 0) {
					params->format = IMAGE_FORMAT_422P;
				} else {
					fprintf(stderr, "%s is not a supported format.\n"
							"supported formats are:\n"
							" - nv12\n"
							" - yu12\n"
							" - 422p\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case 'd':
				params->disjoint = true;
				break;
			default:
				goto fail;
		}
	}

	if (optind >= argc) {
		goto fail;
	}

	params->image_path = argv[optind];
	return;

fail:
	fprintf(stderr, "usage: %s [-w width] [-h height] [-f format] [-d] file\n"
			"  -d\tenable disjoint planes\n",
			argv[0]);
	exit(EXIT_FAILURE);
}

struct app {
	struct window *window;
	struct vulkan_ctx *vk;

	VkSurfaceKHR surface;
	VkRenderPass render_pass;
	struct swapchain swapchain;

	VkCommandPool cmd_pool;
	VkCommandBuffer cmd;

	VkSemaphore image_acquisition_semaphore;
	VkSemaphore rendering_semaphore;
	VkFence inflight_fence;

	struct image_sampler sampler;
	struct image image;
	VkImageView image_view;

	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorSet descriptor_set;

	struct graphics_pipeline pipeline;
};

static VkResult
acquire_next_image(struct app *app, uint32_t *image_ind) {
	VkResult res = VK_TIMEOUT;
	while (res == VK_NOT_READY || res == VK_TIMEOUT) {
		res = vkAcquireNextImageKHR(app->vk->device, app->swapchain.vk_swapchain,
			30, app->image_acquisition_semaphore, NULL, image_ind);
	}
	return res;
}

static VkResult
build_cmd_buffer_for_fb(struct app *app, VkCommandBuffer cmd, VkFramebuffer fb) {
	VkResult res = VK_SUCCESS;

	res = vkResetCommandBuffer(cmd, 0);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkCommandBufferBeginInfo cmd_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	res = vkBeginCommandBuffer(cmd, &cmd_begin_info);
	if (res != VK_SUCCESS) {
		return res;
	}

	VkClearValue clear_value = {
		.color = {
			.float32 = { 1.0f, 0, 1.0f, 1.0f },
		}
	};
	VkRenderPassBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = app->render_pass,
		.framebuffer = fb,
		.renderArea = {
			.extent = app->swapchain.extent,
			.offset = { 0, 0 },
		},
		.clearValueCount = 1,
		.pClearValues = &clear_value,
	};
	vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			app->pipeline.pipeline_layout, 0,
			1, &app->descriptor_set,
			0, NULL);
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline.pipeline);

	VkViewport viewport = {
		.x = 0,
		.y = 0,
		.width = app->swapchain.extent.width,
		.height = app->swapchain.extent.height,
	};
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	VkRect2D scissor = {
		.offset = { 0 },
		.extent = app->swapchain.extent,
	};
	vkCmdSetScissor(cmd, 0, 1, &scissor);
	
	vkCmdDraw(cmd, 6, 1, 0, 0);
	vkCmdEndRenderPass(cmd);

	res = vkEndCommandBuffer(cmd);
	if (res != VK_SUCCESS) {
		return res;
	}

	return VK_SUCCESS;
}

static void
app_render(struct app *app) {
	struct vulkan_ctx *vk = app->vk;
	VkResult res = VK_SUCCESS;

	res = vkWaitForFences(vk->device, 1, &app->inflight_fence, VK_TRUE, UINT64_MAX);
	assert(res == VK_SUCCESS);
	res = vkResetFences(vk->device, 1, &app->inflight_fence);
	assert(res == VK_SUCCESS);

	uint32_t image_ind = 0;
	res = acquire_next_image(app, &image_ind);
	if (res == VK_ERROR_OUT_OF_DATE_KHR) {
		return;
	}
	assert(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR);

	res = build_cmd_buffer_for_fb(app, app->cmd, app->swapchain.images[image_ind].framebuffer);
	assert(res == VK_SUCCESS);

	VkPipelineStageFlags dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &app->image_acquisition_semaphore,
		.pWaitDstStageMask = &dst_stage_mask,
		.commandBufferCount = 1,
		.pCommandBuffers = &app->cmd,
		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &app->rendering_semaphore,
	};
	vkQueueSubmit(vk->queue, 1, &submit_info, app->inflight_fence);

	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &app->rendering_semaphore,
		.swapchainCount = 1,
		.pSwapchains = &app->swapchain.vk_swapchain,
		.pImageIndices = &image_ind,
		.pResults = NULL,
	};
	res = vkQueuePresentKHR(vk->queue, &present_info);
}

void
app_init(struct app *ini, struct app_params *params, struct vulkan_ctx *vk) {
	ini->vk = vk;

	struct window *window = window_create();
	ini->window = window;

	VkResult res = VK_SUCCESS;

	VkXcbSurfaceCreateInfoKHR xcb_surface_create_info = {
		.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
		.connection = window->xcb_connection,
		.window = window->window_id,
	};
	res = vkCreateXcbSurfaceKHR(vk->instance, &xcb_surface_create_info, NULL, &ini->surface);
	assert(res == VK_SUCCESS);

	res = create_renderpass(vk, &ini->render_pass);
	assert(res == VK_SUCCESS);

	res = create_swapchain(vk, ini->surface, ini->render_pass, &ini->swapchain);
	assert(res == VK_SUCCESS);

	res = vulkan_ctx_create_semaphore(vk, &ini->image_acquisition_semaphore);
	assert(res == VK_SUCCESS);

	res = vulkan_ctx_create_semaphore(vk, &ini->rendering_semaphore);
	assert(res == VK_SUCCESS);

	res = vulkan_ctx_create_fence(vk, &ini->inflight_fence, true);
	assert(res == VK_SUCCESS);

	res = vulkan_ctx_create_cmd_pool(vk, &ini->cmd_pool,
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	assert(res == VK_SUCCESS);

	res = create_command_buffer(vk, ini->cmd_pool, &ini->cmd);
	assert(res == VK_SUCCESS);

	res = image_init_from_file(&ini->image, vk, params->image_path,
			params->width, params->height, params->format, params->disjoint);
	assert(res == VK_SUCCESS);

	res = image_sampler_init(&ini->sampler, vk, params->format);
	assert(res == VK_SUCCESS);

	res = create_image_view(vk, &ini->image_view, &ini->image, &ini->sampler);
	assert(res == VK_SUCCESS);
	res = transition_image_layout(vk, ini->cmd_pool, ini->image.vk_image);
	assert(res == VK_SUCCESS);

	res = create_descriptor_pool(vk, &ini->descriptor_pool);
	assert(res == VK_SUCCESS);

	res = create_descriptor_set_layout(vk, &ini->descriptor_set_layout,
			ini->sampler.sampler);
	assert(res == VK_SUCCESS);

	res = allocate_descriptor_set(vk, &ini->descriptor_set,
			ini->descriptor_pool, ini->descriptor_set_layout);
	assert(res == VK_SUCCESS);

	update_descriptor_with_image(vk, ini->descriptor_set, ini->sampler.sampler,
			ini->image_view);

	res = graphics_pipeline_init(&ini->pipeline, vk,
			ini->descriptor_set_layout, ini->render_pass);
	assert(res == VK_SUCCESS);
}

void
app_finish(struct app *app) {
	graphics_pipeline_finish(&app->pipeline, app->vk);

	vkDestroyDescriptorSetLayout(app->vk->device, app->descriptor_set_layout, NULL);
	vkDestroyDescriptorPool(app->vk->device, app->descriptor_pool, NULL);

	vkDestroyImageView(app->vk->device, app->image_view, NULL);
	image_finish(&app->image, app->vk);
	image_sampler_finish(&app->sampler, app->vk);

	vkDestroyFence(app->vk->device, app->inflight_fence, NULL);
	vkDestroySemaphore(app->vk->device, app->rendering_semaphore, NULL);
	vkDestroySemaphore(app->vk->device, app->image_acquisition_semaphore, NULL);

	vkFreeCommandBuffers(app->vk->device, app->cmd_pool, 1, &app->cmd);
	vkDestroyCommandPool(app->vk->device, app->cmd_pool, NULL);

	destroy_swapchain_related_resources(app->vk, &app->swapchain);
	vkDestroySwapchainKHR(app->vk->device, app->swapchain.vk_swapchain, NULL);

	vkDestroyRenderPass(app->vk->device, app->render_pass, NULL);
	vkDestroySurfaceKHR(app->vk->instance, app->surface, NULL);

	window_destroy(app->window);
	vulkan_ctx_destroy(app->vk);
}

void
app_run(struct app *app) {
	struct window *window = app->window;
	struct vulkan_ctx *vk = app->vk;

	window_show(window);

	VkResult res = VK_SUCCESS;

	while (!window->close_requested) {
		window_poll_event(window);

		/* recreate swapchain on resize */
		if (window->resized) {
			res = create_swapchain(vk, app->surface, app->render_pass, &app->swapchain);
			assert(res == VK_SUCCESS);
			window->resized = false;
		}

		app_render(app);
	}

	vkDeviceWaitIdle(vk->device);
}

int main(int argc, char *argv[]) {
	struct app app = { 0 };
	struct app_params params;
	parse_args(&params, argc, argv);

	struct vulkan_ctx_features features = {
		.enable_ycbcr_conversion = true,
	};
	struct vulkan_ctx *vk = vulkan_ctx_create(&features);

	app_init(&app, &params, vk);
	app_run(&app);
	app_finish(&app);
}
