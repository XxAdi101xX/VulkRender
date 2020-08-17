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

#pragma once

#include <set>

#include "common/vk_common.h"

namespace vulkr
{

/* Forward declaration */
class Device;

struct SwapchainProperties
{
	uint32_t imageCount{ 3 };
	VkSurfaceFormatKHR surfaceFormat{};
	VkExtent2D imageExtent{};
	uint32_t imageArraylayers{ 1 };
	VkImageUsageFlags imageUsage;
	VkSurfaceTransformFlagBitsKHR preTransform;
	VkCompositeAlphaFlagBitsKHR compositeAlpha;
	VkPresentModeKHR presentMode;
	VkBool32 clipped;
	VkSwapchainKHR oldSwapchain{ VK_NULL_HANDLE };
};

class Swapchain
{
public:
	Swapchain();
	~Swapchain();

	SwapchainProperties getProperties() const;
	SwapchainProperties getMutableProperties();
private:
	/* The swapchain handle */
	VkSwapchainKHR handle{ VK_NULL_HANDLE };

	/* The surface handle */
	VkSurfaceKHR surface{ VK_NULL_HANDLE };

	/* The logical device associated with the swapchain */
	Device &device;

	/* The associating properties of the swapchain */
	SwapchainProperties properties{};

	/* A list of present modes in decreasing priority */
	const std::vector<VkPresentModeKHR> presentModePriorityList = {
		VK_PRESENT_MODE_FIFO_KHR, /* Ideal option */
		VK_PRESENT_MODE_MAILBOX_KHR
	};

	/* A list of surface formats in descreasing priority */
	const std::vector<VkSurfaceFormatKHR> surfaceFormatPriorityList = {
		{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, /* Ideal option */
		{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
		{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
	};

	/* A list of default image usage flags to fall back on */
	const std::vector<VkImageUsageFlagBits> imageUsagePriorityFlags = {
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_IMAGE_USAGE_STORAGE_BIT,
		VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT
	};

	/* A list of default composite alpha modes */
	const std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaPriorityFlags = {
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
	};

	/* Swapchain properties selection helper functions */
	uint32_t chooseImageCount(uint32_t minImageCount, uint32_t maxImageCount);
	VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, const std::vector<VkSurfaceFormatKHR> surfaceFormatPriorityList);
	VkExtent2D chooseImageExtent(const VkSurfaceCapabilitiesKHR& capabilities);
	uint32_t chooseImageArrayLayers(uint32_t requestedImageArrayLayers, uint32_t maxImageArrayLayers);
	bool validateFormatFeature(VkImageUsageFlagBits imageUsage, VkFormatFeatureFlags supportedFormatFeatures); /* used in chooseImageUsage */
	VkImageUsageFlags chooseImageUsage(const std::set<VkImageUsageFlagBits>& requestedImageUsageFlags, VkImageUsageFlags supportedImageUsage, VkFormatFeatureFlags supportedFormatFeatures);
	VkSurfaceTransformFlagBitsKHR choosePreTransform(VkSurfaceTransformFlagBitsKHR requestedTransform, VkSurfaceTransformFlagsKHR supportedTransform, VkSurfaceTransformFlagBitsKHR currentTransform);
	VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagBitsKHR requestedCompositeAlpha, VkCompositeAlphaFlagsKHR supportedCompositeAlpha);
	VkPresentModeKHR choosePresentMode(VkPresentModeKHR requestedPresentMode, const std::vector<VkPresentModeKHR>& availablePresentModes);
	// clipped
	// old swapchain
};

} // namespace vulkr