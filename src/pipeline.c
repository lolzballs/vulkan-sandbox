#include <stdio.h>

#include "pipeline.h"
#include "shader.frag.h"
#include "shader.vert.h"

VkResult
graphics_pipeline_init(struct graphics_pipeline *ini, struct vulkan_ctx *vk,
		VkDescriptorSetLayout descriptor_set_layout, VkRenderPass render_pass) {
	VkResult res;

	VkPipelineLayout pipeline_layout;
	VkPipelineLayoutCreateInfo pipeline_layout_create = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descriptor_set_layout,
	};
	res = vkCreatePipelineLayout(vk->device, &pipeline_layout_create,
			NULL, &pipeline_layout);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "graphics_pipeline_init - vkCreatePipelineLayout failed\n");
		return res;
	}

	VkShaderModule vert_shader;
	res = vulkan_ctx_create_shader_module(vk, &vert_shader,
			sizeof(shader_vert_data), (const void *) shader_vert_data);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "graphics_pipeline_init - failed to create vert_shader\n");
		return res;
	}

	VkShaderModule frag_shader;
	res = vulkan_ctx_create_shader_module(vk, &frag_shader,
			sizeof(shader_frag_data), (const void *) shader_frag_data);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "graphics_pipeline_init - failed to create frag_shader\n");
		return res;
	}

	/* viewport and scissor don't matter because they will be dynamically set */
	VkViewport viewport = { 0 };
	VkRect2D scissor = { 0 };

	VkPipeline pipeline;
	VkPipelineShaderStageCreateInfo vertex_stage_create = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vert_shader,
		.pName = "main",
	};
	VkPipelineShaderStageCreateInfo frag_stage_create = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = frag_shader,
		.pName = "main",
	};
	VkGraphicsPipelineCreateInfo create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = (const VkPipelineShaderStageCreateInfo[])
			{ vertex_stage_create, frag_stage_create },

		.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		},
		.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		},
		.pViewportState = &(VkPipelineViewportStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.pViewports = &viewport,
			.scissorCount = 1,
			.pScissors = &scissor,
		},
		.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_CLOCKWISE,
			.lineWidth = 1.0f,
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = VK_FALSE,
			.attachmentCount = 1,
			.pAttachments = (VkPipelineColorBlendAttachmentState[]) {
				{
					.colorWriteMask = VK_COLOR_COMPONENT_R_BIT 
						| VK_COLOR_COMPONENT_G_BIT
						| VK_COLOR_COMPONENT_B_BIT 
						| VK_COLOR_COMPONENT_A_BIT,
					.blendEnable = VK_FALSE,
				}
			},
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 2,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
			},
		},

		.layout = pipeline_layout,
		.renderPass = render_pass,
		.subpass = 0,
	};
	res = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1,
			&create_info, NULL, &pipeline);
	if (res != VK_SUCCESS) {
		fprintf(stderr, "graphics_pipeline_init - "
				"failed to create graphics pipeline\n");
		return res;
	}

	ini->vert_shader = vert_shader;
	ini->frag_shader = frag_shader;
	ini->pipeline_layout = pipeline_layout;
	ini->pipeline = pipeline;
	return VK_SUCCESS;
}

void
graphics_pipeline_finish(struct graphics_pipeline *pipeline,
		struct vulkan_ctx *vk) {
	vkDestroyPipeline(vk->device, pipeline->pipeline, NULL);
	vkDestroyPipelineLayout(vk->device, pipeline->pipeline_layout, NULL);
	vkDestroyShaderModule(vk->device, pipeline->frag_shader, NULL);
	vkDestroyShaderModule(vk->device, pipeline->vert_shader, NULL);
}
