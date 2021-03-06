/* Copyright (c) 2021 Adithya Venkatarao
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "common/helpers.h"
#include "pipeline_layout.h"
#include "device.h"
#include "descriptor_set_layout.h"

namespace vulkr
{

PipelineLayout::PipelineLayout(Device &device, const std::vector<ShaderModule> &shaderModules, std::vector<VkDescriptorSetLayout> &descriptorSetLayoutHandles, std::vector<VkPushConstantRange> &pushConstantRangeHandles) :
	device{ device },
	shaderModules{ shaderModules }
{
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.setLayoutCount = to_u32(descriptorSetLayoutHandles.size());
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayoutHandles.data();
	pipelineLayoutInfo.pushConstantRangeCount = to_u32(pushConstantRangeHandles.size());
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRangeHandles.data();

	VK_CHECK(vkCreatePipelineLayout(device.getHandle(), &pipelineLayoutInfo, nullptr, &handle));
}

PipelineLayout::~PipelineLayout()
{
	if (handle != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device.getHandle(), handle, nullptr);
	}
}

VkPipelineLayout PipelineLayout::getHandle() const
{
	return handle;
}

const std::vector<ShaderModule> &PipelineLayout::getShaderModules() const
{
	return shaderModules;
}


} // namespace vulkr