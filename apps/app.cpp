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

#include "app.h"
#include "platform/platform.h"

namespace vulkr
{

MainApp::MainApp(Platform& platform, std::string name) : Application{ platform, name } {}

 MainApp::~MainApp()
 {
     device->waitIdle();

     semaphorePool.reset();
     fencePool.reset();

     cleanupSwapchain();

     textureSampler.reset();

     for (auto &it : textures)
     {
         it.second->imageview.reset();
         it.second->image.reset();
     }
     textures.clear();

     globalDescriptorSetLayout.reset();
     objectDescriptorSetLayout.reset();
     singleTextureDescriptorSetLayout.reset();

     for (auto &it : meshes)
     {
         it.second->indexBuffer.reset();
         it.second->vertexBuffer.reset();
     }
     meshes.clear();

     for (uint32_t i = 0; i < maxFramesInFlight; ++i)
     {
         frameData.commandPools[i].reset();
     }
     imguiPool.reset();

     ImGui_ImplVulkan_Shutdown();
     ImGui_ImplGlfw_Shutdown();
     ImGui::DestroyContext();

     device.reset();

	 if (surface != VK_NULL_HANDLE)
	 {
		 vkDestroySurfaceKHR(instance->getHandle(), surface, nullptr);
	 }

	 instance.reset();
 }

 void MainApp::cleanupSwapchain()
 {
     for (uint32_t i = 0; i < maxFramesInFlight; ++i)
     {
         frameData.commandBuffers[i].reset();
     }

     depthImage.reset();

     depthImageView.reset();

     for (uint32_t i = 0; i < swapchainFramebuffers.size(); ++i)
     {
         swapchainFramebuffers[i].reset();
     }
     swapchainFramebuffers.clear();

     for (auto &it : materials)
     {
         it.second->pipeline.reset();
         it.second->pipelineState.reset();
     }
     materials.clear();
     renderables.clear();

     renderPass.reset();
     subpasses.clear();

     for (uint32_t i = 0; i < swapChainImageViews.size(); ++i)
     {
         swapChainImageViews[i].reset();
     }
     swapChainImageViews.clear();
     inputAttachments.clear();
     colorAttachments.clear();
     resolveAttachments.clear();
     depthStencilAttachments.clear();
     preserveAttachments.clear();

     swapchain.reset();

     for (uint32_t i = 0; i < maxFramesInFlight; ++i)
     {
         frameData.globalDescriptorSets[i].reset();
         frameData.objectDescriptorSets[i].reset();
         frameData.globalBuffers[i].reset();
         frameData.objectBuffers[i].reset();
     }

     descriptorPool.reset();

     cameraController.reset();
 }

void MainApp::prepare()
{
    Application::prepare();

    setupTimer();

    createInstance();
    createSurface();
    createDevice();

    graphicsQueue = device->getOptimalGraphicsQueue().getHandle();

    if (device->getOptimalGraphicsQueue().canSupportPresentation())
    {
        presentQueue = graphicsQueue;
    }
    else
    {
        presentQueue = device->getQueueByPresentation().getHandle();
    }

    createSwapchain();
    createSwapchainImageViews();
    setupCamera();
    createRenderPass();
    createDescriptorSetLayouts();
    createGraphicsPipelines();
    createDepthResources();
    createFramebuffers();
    createCommandPools();
    createCommandBuffers();
    loadTextures();
    createTextureSampler();
    createUniformBuffers();
    createSSBOs();
    createDescriptorPool();
    createDescriptorSets();
    loadMeshes();
    createScene();
    createSemaphoreAndFencePools();
    setupSynchronizationObjects();
    initializeImGui();
}

void MainApp::update()
{
    fencePool->wait(&frameData.inFlightFences[currentFrame]);
    fencePool->reset(&frameData.inFlightFences[currentFrame]);

    //now that we are sure that the commands finished executing, we can safely reset the command buffer to begin recording again.
    frameData.commandBuffers[currentFrame]->reset();

    uint32_t swapchainImageIndex;
    VkResult result = vkAcquireNextImageKHR(device->getHandle(), swapchain->getHandle(), std::numeric_limits<uint64_t>::max(), frameData.imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &swapchainImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        LOGEANDABORT("Failed to acquire swap chain image");
    }

    if (imagesInFlight[swapchainImageIndex] != VK_NULL_HANDLE)
    {
        // TODO: enabling this check causes errors on swapchain recreation, will need to eventually resolve this for correctness
        // fencePool->wait(&imagesInFlight[swapchainImageIndex]);
    }
    imagesInFlight[swapchainImageIndex] = frameData.inFlightFences[currentFrame];

    drawImGuiInterface();

    std::vector<VkClearValue> clearValues;
    clearValues.resize(2);
    clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
    clearValues[1].depthStencil = { 1.0f, 0u };

    frameData.commandBuffers[currentFrame]->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);
    frameData.commandBuffers[currentFrame]->beginRenderPass(*renderPass, *(swapchainFramebuffers[swapchainImageIndex]), swapchain->getProperties().imageExtent, clearValues, VK_SUBPASS_CONTENTS_INLINE);
    // Render scene
    drawObjects();
    // Render UI
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameData.commandBuffers[currentFrame]->getHandle());
    frameData.commandBuffers[currentFrame]->endRenderPass();
    frameData.commandBuffers[currentFrame]->end();

    // I have setup a subpass dependency to ensure that the render pass waits for the swapchain to finish reading from the image before accessing it
    // hence I don't need to set the wait stages to VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT 
    std::array<VkPipelineStageFlags, 1> waitStages{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    std::array<VkSemaphore, 1> waitSemaphores{ frameData.imageAvailableSemaphores[currentFrame] };
    std::array<VkSemaphore, 1> signalSemaphores{ frameData.renderingFinishedSemaphores[currentFrame] };

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = to_u32(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frameData.commandBuffers[currentFrame]->getHandle();
    submitInfo.signalSemaphoreCount = to_u32(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameData.inFlightFences[currentFrame]));

    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = to_u32(signalSemaphores.size());
    presentInfo.pWaitSemaphores = signalSemaphores.data();

    std::array<VkSwapchainKHR, 1> swapchains{ swapchain->getHandle() };
    presentInfo.swapchainCount = to_u32(swapchains.size());
    presentInfo.pSwapchains = swapchains.data();

    presentInfo.pImageIndices = &swapchainImageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        LOGEANDABORT("Failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
}

void MainApp::recreateSwapchain()
{
    // TODO: update window width and high variables on window resize callback??
    // TODO: enable the imagesInFlight check in the update() function and resolve the swapchain recreation bug
    device->waitIdle();
    cleanupSwapchain();

    createSwapchain();
    createSwapchainImageViews();
    setupCamera();
    createRenderPass();
    createGraphicsPipelines();
    createDepthResources();
    createFramebuffers();
    createCommandBuffers();
    createUniformBuffers();
    createSSBOs();
    createDescriptorPool();
    createDescriptorSets();
    createScene();

    imagesInFlight.resize(swapChainImageViews.size(), VK_NULL_HANDLE);
}

void MainApp::handleInputEvents(const InputEvent &inputEvent)
{
    ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;

    cameraController->handleInputEvents(inputEvent);
}

/* Private methods start here */

void MainApp::drawImGuiInterface()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    bool changed{ false };
    glm::vec3 position = cameraController->getCamera()->getPosition();
    glm::vec3 center = cameraController->getCamera()->getCenter();
    float fovy = cameraController->getCamera()->getFovY();

    // Creating UI
    // TODO stop using default debug window and use begin/end to control the window; we would need to create a separate class altogether
    // ImGui::Begin();
    if (ImGui::BeginTabBar("Main Tab"))
    {
        if (ImGui::BeginTabItem("Camera"))
        {
            ImGui::Text("Position");
            ImGui::SameLine();
            ImGui::InputFloat3("##Position", &position.x);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (changed)
            {
                cameraController->getCamera()->setPosition(position);
                changed = false;
            }
            ImGui::Text("Center");
            ImGui::SameLine();
            ImGui::InputFloat3("##Center", &center.x);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (changed)
            {
                cameraController->getCamera()->setCenter(center);
                changed = false;
            }
            ImGui::EndTabItem();
            ImGui::Text("FOV");
            ImGui::SameLine();
            changed |= ImGui::DragFloat("##FOV", &fovy, 1.0f, 1.0f, 179.0f, "%.3f", 0);
            if (changed)
            {
                cameraController->getCamera()->setFovY(fovy);
                changed = false;
            }
        }

        if (ImGui::BeginTabItem("Extra"))
        {
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // ImGui::End();

    // ImGui::ShowDemoWindow();
}

// TODO: as opposed to doing slot based binding of descriptor sets which leads to multiple vkCmdBindDescriptorSets calls per drawcall, you can use
// frequency based descriptor sets and use dynamicOffsetCount: see https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/, or just bindless
// decriptors altogether
void MainApp::drawObjects()
{
    // Update camera buffer
    CameraData cameraData{};
    cameraData.view = cameraController->getCamera()->getView();
    cameraData.proj = cameraController->getCamera()->getProjection();

    void *mappedData = frameData.globalBuffers[currentFrame]->map();
    memcpy(mappedData, &cameraData, sizeof(cameraData));
    frameData.globalBuffers[currentFrame]->unmap();

    // Update object buffer
    mappedData = frameData.objectBuffers[currentFrame]->map();
    ObjectData *objectSSBO = (ObjectData *)mappedData;
    for (int index = 0; index < renderables.size(); index++)
    {
        objectSSBO[index].model = renderables[index].transformMatrix;
    }
    frameData.objectBuffers[currentFrame]->unmap();

    // Draw renderables
    std::shared_ptr<Mesh> lastMesh = nullptr;
    std::shared_ptr<Material> lastMaterial = nullptr;
    for (int index = 0; index < renderables.size(); index++)
    {
        RenderObject object = renderables[index];

        // Bind the pipeline if it doesn't match with the already bound one
        if (object.material != lastMaterial)
        {

            vkCmdBindPipeline(frameData.commandBuffers[currentFrame]->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline->getHandle());
            lastMaterial = object.material;

            // Camera data descriptor
            vkCmdBindDescriptorSets(frameData.commandBuffers[currentFrame]->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineState->getPipelineLayout().getHandle(), 0, 1, &frameData.globalDescriptorSets[currentFrame]->getHandle(), 0, nullptr);

            // Object data descriptor
            vkCmdBindDescriptorSets(frameData.commandBuffers[currentFrame]->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineState->getPipelineLayout().getHandle(), 1, 1, &frameData.objectDescriptorSets[currentFrame]->getHandle(), 0, nullptr);

            if (object.material->textureDescriptorSet != VK_NULL_HANDLE)
            {
                // Texture descriptor
                vkCmdBindDescriptorSets(frameData.commandBuffers[currentFrame]->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipelineState->getPipelineLayout().getHandle(), 2, 1, &object.material->textureDescriptorSet->getHandle(), 0, nullptr);

            }
        }

        // Bind the mesh if it's a different one from last one
        if (object.mesh != lastMesh)
        {
            VkBuffer vertexBuffers[] = { object.mesh->vertexBuffer->getHandle() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(frameData.commandBuffers[currentFrame]->getHandle(), 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(frameData.commandBuffers[currentFrame]->getHandle(), object.mesh->indexBuffer->getHandle(), 0, VK_INDEX_TYPE_UINT32);

            lastMesh = object.mesh;
        }

        vkCmdDrawIndexed(frameData.commandBuffers[currentFrame]->getHandle(), to_u32(object.mesh->indices.size()), 1, 0, 0, index);
    }
}

void MainApp::setupTimer()
{
    drawingTimer = std::make_unique<Timer>();
    drawingTimer->start();
}

void MainApp::createInstance()
{
    instance = std::make_unique<Instance>(getName());
    g_instance = instance->getHandle();
}

void MainApp::createSurface()
{
    platform.createSurface(instance->getHandle());
    surface = platform.getSurface();
}

void MainApp::createDevice()
{
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    std::unique_ptr<PhysicalDevice> physicalDevice = instance->getSuitablePhysicalDevice();
    physicalDevice->setRequestedFeatures(deviceFeatures);

    device = std::make_unique<Device>(std::move(physicalDevice), surface, deviceExtensions);
}

void MainApp::createSwapchain()
{
    const std::set<VkImageUsageFlagBits> imageUsageFlags{ VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT };
    swapchain = std::make_unique<Swapchain>(*device, surface, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, imageUsageFlags);
}

void MainApp::createSwapchainImageViews()
{
    swapChainImageViews.reserve(swapchain->getImages().size());
    for (uint32_t i = 0; i < swapchain->getImages().size(); ++i)
    {
        swapChainImageViews.emplace_back(std::make_unique<ImageView>(*(swapchain->getImages()[i]), VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, swapchain->getProperties().surfaceFormat.format));
    }
}

void MainApp::setupCamera()
{
    cameraController = std::make_unique<CameraController>(swapchain->getProperties().imageExtent.width, swapchain->getProperties().imageExtent.height);
    cameraController->getCamera()->setPerspectiveProjection(45.0f, swapchain->getProperties().imageExtent.width / (float)swapchain->getProperties().imageExtent.height, 0.1f, 100.0f);
    cameraController->getCamera()->setView(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void MainApp::createRenderPass()
{
    std::vector<Attachment> attachments;
    Attachment colorAttachment{};
    colorAttachment.format = swapchain->getProperties().surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments.push_back(colorAttachment);

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachments.push_back(colorAttachmentRef);

    Attachment depthAttachment{};
    depthAttachment.format = getSupportedDepthFormat(device->getPhysicalDevice().getHandle());
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments.push_back(depthAttachment);

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthStencilAttachments.push_back(depthAttachmentRef);

    subpasses.emplace_back(inputAttachments, colorAttachments, resolveAttachments, depthStencilAttachments, preserveAttachments, VK_PIPELINE_BIND_POINT_GRAPHICS);

    std::vector<VkSubpassDependency> dependencies;
    dependencies.resize(1);

    // Only need a dependency coming in to ensure that the first layout transition happens at the right time.
    // Second external dependency is implied by having a different finalLayout and subpass layout.
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0; // References the subpass index in the subpasses array
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0; // We don't have anything that we need to flush
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Normally, we would need an external dependency at the end as well since we are changing layout in finalLayout,
    // but since we are signalling a semaphore, we can rely on Vulkan's default behavior,
    // which injects an external dependency here with dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, dstAccessMask = 0. 

    renderPass = std::make_unique<RenderPass>(*device, attachments, subpasses, dependencies);
}

void MainApp::createDescriptorSetLayouts()
{
    // Global descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

    std::vector<VkDescriptorSetLayoutBinding> globalDescriptorSetLayoutBindings{ uboLayoutBinding };
    globalDescriptorSetLayout = std::make_unique<DescriptorSetLayout>(*device, globalDescriptorSetLayoutBindings);

    // Object descriptor set layout
    VkDescriptorSetLayoutBinding objectLayoutBinding{};
    objectLayoutBinding.binding = 0;
    objectLayoutBinding.descriptorCount = 1;
    objectLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    objectLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    objectLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> objectDescriptorSetLayoutBindings{ objectLayoutBinding };
    objectDescriptorSetLayout = std::make_unique<DescriptorSetLayout>(*device, objectDescriptorSetLayoutBindings);

    // Texture descriptor set layout
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> textureDescriptorSetLayoutBindings{ samplerLayoutBinding };
    singleTextureDescriptorSetLayout = std::make_unique<DescriptorSetLayout>(*device, textureDescriptorSetLayoutBindings);
}

std::shared_ptr<Material> MainApp::createMaterial(std::shared_ptr<GraphicsPipeline> pipeline, std::shared_ptr<PipelineState> pipelineState, const std::string &name)
{
    std::shared_ptr<Material> material = std::make_shared<Material>();
    material->pipeline = pipeline;
    material->pipelineState = pipelineState;
    materials[name] = material;
    return material;
}

void MainApp::createGraphicsPipelines()
{
    // Setup pipeline
    VertexInputState vertexInputState{};
    vertexInputState.bindingDescriptions.reserve(1);
    vertexInputState.attributeDescriptions.reserve(3);

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexInputState.bindingDescriptions.emplace_back(bindingDescription);

    // Position at location 0
    VkVertexInputAttributeDescription positionAttributeDescription;
    positionAttributeDescription.binding = 0;
    positionAttributeDescription.location = 0;
    positionAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    positionAttributeDescription.offset = offsetof(Vertex, position);
    // Normal at location 1
    VkVertexInputAttributeDescription normalAttributeDescription;
    normalAttributeDescription.binding = 0;
    normalAttributeDescription.location = 1;
    normalAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    normalAttributeDescription.offset = offsetof(Vertex, normal);
    // Color at location 2
    VkVertexInputAttributeDescription colorAttributeDescription;
    colorAttributeDescription.binding = 0;
    colorAttributeDescription.location = 2;
    colorAttributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    colorAttributeDescription.offset = offsetof(Vertex, color);
    // TexCoord at location 3
    VkVertexInputAttributeDescription textureCoordinateAttributeDescription;
    textureCoordinateAttributeDescription.binding = 0;
    textureCoordinateAttributeDescription.location = 3;
    textureCoordinateAttributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
    textureCoordinateAttributeDescription.offset = offsetof(Vertex, textureCoordinate);

    vertexInputState.attributeDescriptions.emplace_back(positionAttributeDescription);
    vertexInputState.attributeDescriptions.emplace_back(normalAttributeDescription);
    vertexInputState.attributeDescriptions.emplace_back(colorAttributeDescription);
    vertexInputState.attributeDescriptions.emplace_back(textureCoordinateAttributeDescription);

    InputAssemblyState inputAssemblyState{};
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    ViewportState viewportState{};
    VkViewport viewport{ 0.0f, 0.0f, swapchain->getProperties().imageExtent.width, swapchain->getProperties().imageExtent.height, 0.0f, 1.0f };
    viewportState.viewports.emplace_back(viewport);
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapchain->getProperties().imageExtent;
    viewportState.scissors.emplace_back(scissor);

    RasterizationState rasterizationState{};
    rasterizationState.depthClampEnable = VK_FALSE;
    rasterizationState.rasterizerDiscardEnable = VK_FALSE;
    rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationState.lineWidth = 1.0f;
    rasterizationState.depthBiasEnable = VK_FALSE;

    MultisampleState multisampleState{};
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleState.sampleShadingEnable = VK_FALSE;

    DepthStencilState depthStencilState{};
    depthStencilState.depthTestEnable = VK_TRUE;
    depthStencilState.depthWriteEnable = VK_TRUE;
    // TODO: change this to VK_COMPARE_OP_GREATER: https://developer.nvidia.com/content/depth-precision-visualized , will need to also change the depthStencil clearValue to 0.0f instead of 1.0f
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilState.depthBoundsTestEnable = VK_FALSE;
    depthStencilState.stencilTestEnable = VK_FALSE;

    ColorBlendAttachmentState colorBlendAttachmentState{};
    colorBlendAttachmentState.blendEnable = VK_FALSE;
    colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    ColorBlendState colorBlendState{};
    colorBlendState.logicOpEnable = VK_FALSE;
    colorBlendState.logicOp = VK_LOGIC_OP_COPY;
    colorBlendState.attachments.emplace_back(colorBlendAttachmentState);
    colorBlendState.blendConstants[0] = 0.0f;
    colorBlendState.blendConstants[1] = 0.0f;
    colorBlendState.blendConstants[2] = 0.0f;
    colorBlendState.blendConstants[3] = 0.0f;

    std::shared_ptr<ShaderSource> vertexShader = std::make_shared<ShaderSource>("../../../src/shaders/main.vert.spv");
    std::shared_ptr<ShaderSource> defaultFragmentShader = std::make_shared<ShaderSource>("../../../src/shaders/default.frag.spv");
    std::shared_ptr<ShaderSource> texturedFragmentShader = std::make_shared<ShaderSource>("../../../src/shaders/textured.frag.spv");

    std::vector<ShaderModule> shaderModules;
    shaderModules.emplace_back(*device, VK_SHADER_STAGE_VERTEX_BIT, vertexShader);
    shaderModules.emplace_back(*device, VK_SHADER_STAGE_FRAGMENT_BIT, defaultFragmentShader);

    std::vector<VkDescriptorSetLayout> descriptorSetLayoutHandles {
        globalDescriptorSetLayout->getHandle(),
        objectDescriptorSetLayout->getHandle()
    };
    std::vector<VkPushConstantRange> pushConstantRangeHandles;

    // Create default mesh materials
    std::shared_ptr<PipelineState> defaultMeshPipelineState = std::make_shared<PipelineState>(
        std::make_unique<PipelineLayout>(*device, shaderModules, descriptorSetLayoutHandles, pushConstantRangeHandles),
        *renderPass,
        vertexInputState,
        inputAssemblyState,
        viewportState,
        rasterizationState,
        multisampleState,
        depthStencilState,
        colorBlendState
    );

    std::shared_ptr<GraphicsPipeline> defaultMeshPipeline = std::make_shared<GraphicsPipeline>(*device, *defaultMeshPipelineState, nullptr);

    createMaterial(defaultMeshPipeline, defaultMeshPipelineState, "defaultmesh");

    // Create textured mesh materials
    shaderModules.clear();
    shaderModules.emplace_back(*device, VK_SHADER_STAGE_VERTEX_BIT, vertexShader);
    shaderModules.emplace_back(*device, VK_SHADER_STAGE_FRAGMENT_BIT, texturedFragmentShader);

    descriptorSetLayoutHandles.clear();
    descriptorSetLayoutHandles.push_back(globalDescriptorSetLayout->getHandle());
    descriptorSetLayoutHandles.push_back(objectDescriptorSetLayout->getHandle());
    descriptorSetLayoutHandles.push_back(singleTextureDescriptorSetLayout->getHandle());

    std::shared_ptr<PipelineState> texturedMeshPipelineState = std::make_shared<PipelineState>(
        std::make_unique<PipelineLayout>(*device, shaderModules, descriptorSetLayoutHandles, pushConstantRangeHandles),
        *renderPass,
        vertexInputState,
        inputAssemblyState,
        viewportState,
        rasterizationState,
        multisampleState,
        depthStencilState,
        colorBlendState
        );

    std::shared_ptr<GraphicsPipeline> texturedMeshPipeline = std::make_shared<GraphicsPipeline>(*device, *texturedMeshPipelineState, nullptr);

    createMaterial(texturedMeshPipeline, texturedMeshPipelineState, "texturedmesh");
}

void MainApp::createFramebuffers()
{
    swapchainFramebuffers.reserve(swapchain->getImages().size());
    for (uint32_t i = 0; i < swapchain->getImages().size(); ++i)
    {
        std::vector<VkImageView> attachments{ swapChainImageViews[i]->getHandle(), depthImageView->getHandle() };

        swapchainFramebuffers.emplace_back(std::make_unique<Framebuffer>(*device, *swapchain, *renderPass, attachments));
    }
}

void MainApp::createCommandPools()
{
    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        frameData.commandPools[i] = std::make_unique<CommandPool>(*device, device->getOptimalGraphicsQueue().getFamilyIndex(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    }
}

void MainApp::createCommandBuffers()
{
    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        frameData.commandBuffers[i] = std::make_unique<CommandBuffer>(*frameData.commandPools[i], VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    }
}

// TODO create a new command pool and allocate the command buffer using requestCommandBuffer
void MainApp::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    std::unique_ptr<CommandBuffer> commandBuffer = std::make_unique<CommandBuffer>(*frameData.commandPools[currentFrame], VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        LOGEANDABORT("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer->getHandle(),
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    commandBuffer->end();

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer->getHandle();

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
}

// TODO create a new command pool and allocate the command buffer using requestCommandBuffer
void MainApp::copyBufferToImage(const Buffer &srcBuffer, const Image &dstImage, uint32_t width, uint32_t height)
{
    std::unique_ptr<CommandBuffer> commandBuffer = std::make_unique<CommandBuffer>(*frameData.commandPools[currentFrame], VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer->getHandle(), srcBuffer.getHandle(), dstImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    commandBuffer->end();

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer->getHandle();

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
}

void MainApp::createDepthResources()
{
    VkFormat depthFormat = getSupportedDepthFormat(device->getPhysicalDevice().getHandle());

    VkExtent3D extent{ swapchain->getProperties().imageExtent.width, swapchain->getProperties().imageExtent.height, 1 };
    // TODO: a single depth buffer may not be correct... https://stackoverflow.com/questions/62371266/why-is-a-single-depth-buffer-sufficient-for-this-vulkan-swapchain-render-loop
    // TODO: understand why I don't need to use a staging buffer here to access the depth buffer
    depthImage = std::make_unique<Image>(*device, depthFormat, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY /* default values for remaining params */);
    depthImageView = std::make_unique<ImageView>(*depthImage, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_DEPTH_BIT, depthFormat);
}

std::unique_ptr<Image> MainApp::createTextureImage(const char *filename)
{
    int texWidth, texHeight, texChannels;

    stbi_uc *pixels = stbi_load(filename, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        LOGEANDABORT("failed to load texture image!");
    }

    VkDeviceSize imageSize{ static_cast<VkDeviceSize>(texWidth * texHeight * 4) };
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    std::unique_ptr<Buffer> stagingBuffer = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);

    void *mappedData = stagingBuffer->map();
    memcpy(mappedData, pixels, static_cast<size_t>(imageSize));
    stagingBuffer->unmap();

    stbi_image_free(pixels);

    VkExtent3D extent{ to_u32(texWidth), to_u32(texHeight), 1u };
    std::unique_ptr<Image> textureImage = std::make_unique<Image>(*device, VK_FORMAT_R8G8B8A8_SRGB, extent, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY /* default values for remaining params */);

    transitionImageLayout(textureImage->getHandle(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(*stagingBuffer, *textureImage, to_u32(texWidth), to_u32(texHeight));
    transitionImageLayout(textureImage->getHandle(), VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return textureImage;
}

std::unique_ptr<ImageView> MainApp::createTextureImageView(const Image &textureImage)
{
    return std::make_unique<ImageView>(textureImage, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_SRGB);
}

void MainApp::loadTextures()
{
    std::shared_ptr<Texture> empireTexture = std::make_shared<Texture>();
    empireTexture->image = createTextureImage(TEXTURE_PATH.c_str());
    empireTexture->imageview = createTextureImageView(*(empireTexture->image));

    textures["empire_diffuse"] = empireTexture;
}

void MainApp::createTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = device->getPhysicalDevice().getProperties().limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    textureSampler = std::make_unique<Sampler>(*device, samplerInfo);
}

void MainApp::copyBufferToBuffer(const Buffer &srcBuffer, const Buffer &dstBuffer, VkDeviceSize size)
{
    // TODO: move this elsewhere?
    std::unique_ptr<CommandBuffer> commandBuffer = std::make_unique<CommandBuffer>(*frameData.commandPools[currentFrame], VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer->getHandle(), srcBuffer.getHandle(), dstBuffer.getHandle(), 1, &copyRegion);

    commandBuffer->end();

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer->getHandle();

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
}

void MainApp::createVertexBuffer(std::shared_ptr<Mesh> mesh)
{
    VkDeviceSize bufferSize{ sizeof(mesh->vertices[0]) * mesh->vertices.size() };

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    std::unique_ptr<Buffer> stagingBuffer = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    memoryInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    mesh->vertexBuffer = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);

    void *mappedData = stagingBuffer->map();
    memcpy(mappedData, mesh->vertices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer->unmap();
    copyBufferToBuffer(*stagingBuffer, *(mesh->vertexBuffer), bufferSize);
}

void MainApp::createIndexBuffer(std::shared_ptr<Mesh> mesh)
{
    VkDeviceSize bufferSize{ sizeof(mesh->indices[0]) * mesh->indices.size() };

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    std::unique_ptr<Buffer> stagingBuffer = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);

    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    memoryInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    mesh->indexBuffer = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);

    void *mappedData = stagingBuffer->map();
    memcpy(mappedData, mesh->indices.data(), static_cast<size_t>(bufferSize));
    stagingBuffer->unmap();
    copyBufferToBuffer(*stagingBuffer, *(mesh->indexBuffer), bufferSize);
}

// TODO use push constants to pass in mvp matrix information to the vertext shader
void MainApp::createUniformBuffers()
{
    VkDeviceSize bufferSize{ sizeof(CameraData) };

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        frameData.globalBuffers[i] = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);
    }
}

void MainApp::createSSBOs()
{
    VkDeviceSize bufferSize{ sizeof(ObjectData) * MAX_OBJECT_COUNT };

    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo memoryInfo{};
    memoryInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        frameData.objectBuffers[i] = std::make_unique<Buffer>(*device, bufferInfo, memoryInfo);
    }
}

void MainApp::createDescriptorPool()
{
    std::vector<VkDescriptorPoolSize> poolSizes{};
    poolSizes.resize(3);
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = maxFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = maxFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[2].descriptorCount = 1;

    descriptorPool = std::make_unique<DescriptorPool>(*device, poolSizes, 10u, 0);
}

void MainApp::createDescriptorSets()
{
    for (uint32_t i = 0; i < maxFramesInFlight; ++i)
    {
        // Global Descriptor Set
        VkDescriptorSetAllocateInfo globalDescriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        globalDescriptorSetAllocateInfo.descriptorPool = descriptorPool->getHandle();
        globalDescriptorSetAllocateInfo.descriptorSetCount = 1;
        globalDescriptorSetAllocateInfo.pSetLayouts = &globalDescriptorSetLayout->getHandle();
        frameData.globalDescriptorSets[i] = std::make_unique<DescriptorSet>(*device, globalDescriptorSetAllocateInfo);

        VkDescriptorBufferInfo cameraBufferInfo{};
        cameraBufferInfo.buffer = frameData.globalBuffers[i]->getHandle();
        cameraBufferInfo.offset = 0;
        cameraBufferInfo.range = sizeof(CameraData); // can also use VK_WHOLE_SIZE in this case

        VkWriteDescriptorSet descriptorWriteUniformBuffer{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWriteUniformBuffer.dstSet = frameData.globalDescriptorSets[i]->getHandle();
        descriptorWriteUniformBuffer.dstBinding = 0;
        descriptorWriteUniformBuffer.dstArrayElement = 0;
        descriptorWriteUniformBuffer.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWriteUniformBuffer.descriptorCount = 1;
        descriptorWriteUniformBuffer.pBufferInfo = &cameraBufferInfo;
        descriptorWriteUniformBuffer.pImageInfo = nullptr; // Optional
        descriptorWriteUniformBuffer.pTexelBufferView = nullptr; // Optional

        // Object Descriptor Set
        VkDescriptorSetAllocateInfo objectDescriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        objectDescriptorSetAllocateInfo.descriptorPool = descriptorPool->getHandle();
        objectDescriptorSetAllocateInfo.descriptorSetCount = 1;
        objectDescriptorSetAllocateInfo.pSetLayouts = &objectDescriptorSetLayout->getHandle();
        frameData.objectDescriptorSets[i] = std::make_unique<DescriptorSet>(*device, objectDescriptorSetAllocateInfo);

        VkDescriptorBufferInfo objectBufferInfo{};
        objectBufferInfo.buffer = frameData.objectBuffers[i]->getHandle();
        objectBufferInfo.offset = 0;
        objectBufferInfo.range = sizeof(ObjectData) * MAX_OBJECT_COUNT; // can also use VK_WHOLE_SIZE in this case

        VkWriteDescriptorSet objectWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        objectWrite.dstSet = frameData.objectDescriptorSets[i]->getHandle();
        objectWrite.dstBinding = 0;
        objectWrite.dstArrayElement = 0;
        objectWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objectWrite.descriptorCount = 1;
        objectWrite.pBufferInfo = &objectBufferInfo;
        objectWrite.pImageInfo = nullptr; // Optional
        objectWrite.pTexelBufferView = nullptr; // Optional

        // Write descriptor sets
        std::array<VkWriteDescriptorSet, 2> writeDescriptorSets{ descriptorWriteUniformBuffer, objectWrite };
        vkUpdateDescriptorSets(device->getHandle(), to_u32(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
    }

    // TODO refactor this hardcoded bit so that we can actually support more than one texture
    // Texture Descriptor Set
    std::shared_ptr<Material> texturedMeshMaterial = getMaterial("texturedmesh");

    VkDescriptorSetAllocateInfo textureDescriptorSetAllocateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    textureDescriptorSetAllocateInfo.descriptorPool = descriptorPool->getHandle();
    textureDescriptorSetAllocateInfo.descriptorSetCount = 1;
    textureDescriptorSetAllocateInfo.pSetLayouts = &singleTextureDescriptorSetLayout->getHandle();
    texturedMeshMaterial->textureDescriptorSet = std::make_unique<DescriptorSet>(*device, textureDescriptorSetAllocateInfo);

    VkDescriptorImageInfo textureImageInfo{};
    textureImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    textureImageInfo.imageView = textures["empire_diffuse"]->imageview->getHandle();
    textureImageInfo.sampler = textureSampler->getHandle();

    VkWriteDescriptorSet descriptorWriteCombinedImageSampler{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    descriptorWriteCombinedImageSampler.dstSet = texturedMeshMaterial->textureDescriptorSet->getHandle();
    descriptorWriteCombinedImageSampler.dstBinding = 0;
    descriptorWriteCombinedImageSampler.dstArrayElement = 0;
    descriptorWriteCombinedImageSampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWriteCombinedImageSampler.descriptorCount = 1;
    descriptorWriteCombinedImageSampler.pImageInfo = &textureImageInfo;
    descriptorWriteCombinedImageSampler.pBufferInfo = nullptr; // Optional
    descriptorWriteCombinedImageSampler.pTexelBufferView = nullptr; // Optional

    std::array<VkWriteDescriptorSet, 1> writeDescriptorSets{ descriptorWriteCombinedImageSampler };
    vkUpdateDescriptorSets(device->getHandle(), to_u32(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
}

void MainApp::createSemaphoreAndFencePools()
{
    semaphorePool = std::make_unique<SemaphorePool>(*device);
    fencePool = std::make_unique<FencePool>(*device);
}

void MainApp::setupSynchronizationObjects()
{
    imagesInFlight.resize(swapchain->getImages().size(), VK_NULL_HANDLE);

    for (size_t i = 0; i < maxFramesInFlight; ++i) {
        frameData.imageAvailableSemaphores[i] = semaphorePool->requestSemaphore();
        frameData.renderingFinishedSemaphores[i] = semaphorePool->requestSemaphore();
        frameData.inFlightFences[i] = fencePool->requestFence();
    }
}

void MainApp::loadMeshes()
{
    // TODO resolve warning messages saying that mtl files are not found
    std::shared_ptr<Mesh> monkeyMesh = std::make_shared<Mesh>();
    monkeyMesh->loadFromObjFile("../../../assets/models/monkey_smooth.obj");

    std::shared_ptr<Mesh> empireMesh = std::make_shared<Mesh>();
    empireMesh->loadFromObjFile("../../../assets/models/lost_empire.obj");

    createVertexBuffer(monkeyMesh);
    createIndexBuffer(monkeyMesh);
    createVertexBuffer(empireMesh);
    createIndexBuffer(empireMesh);

    meshes["monkey"] = monkeyMesh;
    meshes["empire"] = empireMesh;
}

void MainApp::createScene()
{
    RenderObject monkey;
    monkey.mesh = getMesh("monkey");
    monkey.material = getMaterial("defaultmesh");
    monkey.transformMatrix = glm::translate(glm::mat4{ 1.0 }, glm::vec3(1, 0, 0));
    renderables.push_back(monkey);

    RenderObject map;
    map.mesh = getMesh("empire");
    map.material = getMaterial("texturedmesh");
    map.transformMatrix = glm::translate(glm::mat4{ 1.0 }, glm::vec3{ 5,-10,0 });
    renderables.push_back(map);
    return; // TODO delete this

    for (int x = -20; x <= 20; x++)
    {
        for (int y = -20; y <= 20; y++)
        {

            RenderObject tri;
            tri.mesh = getMesh("triangle");
            tri.material = getMaterial("defaultmesh");
            glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 0, y));
            glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
            tri.transformMatrix = translation * scale;

            renderables.push_back(tri);
        }
    }
}

std::shared_ptr<Material> MainApp::getMaterial(const std::string &name)
{
    auto it = materials.find(name);
    if (it == materials.end())
    {
        return nullptr;
    }

    return it->second;

}

std::shared_ptr<Mesh> MainApp::getMesh(const std::string &name)
{
    auto it = meshes.find(name);
    if (it == meshes.end())
    {
        return nullptr;
    }

    return it->second;
}

void Mesh::loadFromObjFile(const char *filename)
{
    // Attrib will contain the vertex arrays of the file
    tinyobj::attrib_t attrib;
    // Shapes contains the info for each separate object in the file
    std::vector<tinyobj::shape_t> shapes;
    // Materials contains the information about the material of each shape, but we wont use it.
    std::vector<tinyobj::material_t> materials;

    // Error and warning output from the load function
    std::string warn;
    std::string err;

    // Load the OBJ file
    tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename, nullptr);
    if (!warn.empty())
    {
        LOGW(warn);
    }

    // This happens if the file cant be found or is malformed
    if (!err.empty())
    {
        LOGEANDABORT(err);
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};

    // Loop over shapes
    for (size_t s = 0; s < shapes.size(); s++)
    {
        // Loop over faces(polygon)
        size_t index_offset = 0;
        for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
        {
            // Hardcode loading to triangles
            int fv = 3;

            // Loop over vertices in the face.
            for (size_t v = 0; v < fv; v++)
            {
                // Access to vertex
                tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                // Vertex position
                tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
                tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
                tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];
                // Vertex normal
                tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
                tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
                tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];
                // Texture coordinates
                tinyobj::real_t ux = attrib.texcoords[2 * idx.texcoord_index + 0];
                tinyobj::real_t uy = attrib.texcoords[2 * idx.texcoord_index + 1];

                // Create a new vertex entry
                Vertex newVertex;
                newVertex.position = { vx, vy, vz };
                newVertex.normal = { nx, ny, nz };
                newVertex.color = { nx, ny, nz }; // Set the colour as the normal values for now
                newVertex.textureCoordinate = { ux, 1 - uy };

                if (uniqueVertices.count(newVertex) == 0)
                {
                    uniqueVertices[newVertex] = to_u32(vertices.size());
                    vertices.push_back(newVertex);
                }

                indices.push_back(uniqueVertices[newVertex]);
            }
            index_offset += fv;
        }
    }
}

void MainApp::initializeImGui()
{
    // Create descriptor pool for imgui
    std::vector<VkDescriptorPoolSize> poolSizes =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    imguiPool = std::make_unique<DescriptorPool>(*device, poolSizes, 1000 * 11, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    ImGui::StyleColorsClassic();

    // Initialize imgui for glfw
    ImGui_ImplGlfw_InitForVulkan(platform.getWindow().getHandle(), true);

    // Initilialize imgui for Vulkan
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = instance->getHandle();
    initInfo.PhysicalDevice = device->getPhysicalDevice().getHandle();
    initInfo.Device = device->getHandle();
    initInfo.QueueFamily = device->getOptimalGraphicsQueue().getFamilyIndex();
    initInfo.Queue = graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiPool->getHandle();
    initInfo.Subpass = 0u;
    initInfo.MinImageCount = swapchain->getProperties().imageCount;
    initInfo.ImageCount = swapchain->getProperties().imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr; // TODO pass VMA here
    initInfo.CheckVkResultFn = checkVkResult;

    ImGui_ImplVulkan_LoadFunctions(loadFunction); // TODO figure out how to integrate with volk
    ImGui_ImplVulkan_Init(&initInfo, renderPass->getHandle());

    std::unique_ptr<CommandBuffer> commandBuffer = std::make_unique<CommandBuffer>(*frameData.commandPools[currentFrame], VK_COMMAND_BUFFER_LEVEL_PRIMARY);
    commandBuffer->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr);

    ImGui_ImplVulkan_CreateFontsTexture(commandBuffer->getHandle());

    commandBuffer->end();

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer->getHandle();

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Clear font data on CPU
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

} // namespace vulkr

int main()
{
    vulkr::Platform platform;
    std::unique_ptr<vulkr::MainApp> app = std::make_unique<vulkr::MainApp>(platform, "Vulkan App");

    platform.initialize(std::move(app));
    platform.prepareApplication();

    platform.runMainProcessingLoop();
    platform.terminate();

    return EXIT_SUCCESS;
}

