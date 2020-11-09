/* Copyright (c) 2020 Adithya Venkatarao
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

#include "descriptor_set.h"
#include "descriptor_set_layout.h"
#include "descriptor_pool.h"
#include "device.h"

#include "common/helpers.h"

namespace vulkr
{

DescriptorSet::DescriptorSet(
    Device& device,
    DescriptorSetLayout& descriptorSetLayout,
    DescriptorPool& descriptorPool) :
    device{ device },
    descriptorSetLayout{ descriptorSetLayout },
    handle{ descriptorPool.allocate() }
{}

const VkDescriptorSet &DescriptorSet::getHandle() const
{
    return handle;
}

void DescriptorSet::update(std::vector<VkWriteDescriptorSet> writeDescriptorSets) const
{
    vkUpdateDescriptorSets(device.getHandle(), to_u32(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

} // namespace vulkr