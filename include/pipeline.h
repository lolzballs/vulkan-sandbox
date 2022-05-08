#ifndef PIPELINE_H
#define PIPELINE_H

#include "vulkan.h"

struct graphics_pipeline {
	VkShaderModule vert_shader;
	VkShaderModule frag_shader;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
};

VkResult graphics_pipeline_init(struct graphics_pipeline *ini,
		struct vulkan_ctx *vk, VkDescriptorSetLayout descriptor_set_layout,
		VkRenderPass render_pass);
void graphics_pipeline_finish(struct graphics_pipeline *pipeline,
		struct vulkan_ctx *vk);

#endif
