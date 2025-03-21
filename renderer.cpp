// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "renderer.h"
#include "qrandom.h"
#include <QVulkanFunctions>
#include <QtConcurrentRun>
#include <QTime>
#include "utilities.h"

Renderer::Renderer(VulkanWindow *w, int initialCount)
    : mWindow(w),
      // Have the light positioned just behind the default camera position, looking forward.
      mLightPos(0.0f, 0.0f, 25.0f),
      mCam(QVector3D(0.0f, 0.0f, 20.0f)), // starting camera position
      mInstCount(initialCount)
{
    mFloorModel.translate(0, -5, 0);
    mFloorModel.rotate(-90, 1, 0, 0);
    mFloorModel.scale(20, 100, 1);

    mBlockMesh.load(QStringLiteral(":/block.buf"));
    mLogoMesh.load(QStringLiteral(":/qt_logo.buf"));

    QObject::connect(&mFrameWatcher, &QFutureWatcherBase::finished, mWindow, [this] {
        if (mFramePending) {
            mFramePending = false;
            mWindow->frameReady();
            mWindow->requestUpdate();
        }
    });
}

void Renderer::preInitResources()
{
	//Just request 4x MSAA if available.
    const QList<int> sampleCounts = mWindow->supportedSampleCounts();
    if (DBG)
        qDebug() << "Supported sample counts:" << sampleCounts;
    if (sampleCounts.contains(4)) {
        if (DBG)
            qDebug("Requesting 4x MSAA");
        mWindow->setSampleCount(4);
    }
}

//Automatically called by the window when the Vulkan device is created.
void Renderer::initResources()
{
    if (DBG)
        qDebug("Renderer init");

    mAnimating = true;
    mFramePending = false;

    QVulkanInstance *vulkanInstance = mWindow->vulkanInstance();
    VkDevice logicalDevice = mWindow->device(); //cannot be made a class member

    const VkPhysicalDeviceLimits *physicalDeviceLimits = &mWindow->physicalDeviceProperties()->limits;
    const VkDeviceSize uniformAlignment = physicalDeviceLimits->minUniformBufferOffsetAlignment;

    mDeviceFunctions = vulkanInstance->deviceFunctions(logicalDevice);

    /************* Shaders ****************/
    // Note the std140 packing rules. A vec3 still has an alignment of 16,
    // while a mat3 is like 3 * vec3.
    mItemMaterial.vertUniSize = aligned(2 * 64 + 48, uniformAlignment);         // 2x mat4, 1x mat3
    mItemMaterial.fragUniSize = aligned(6 * 16 + 12 + 2 * 4, uniformAlignment); // 7x vec3, 2x float <-???

	//Phong shader for the blocks
    if (!mItemMaterial.vs.isValid())
        mItemMaterial.vs.load(vulkanInstance, logicalDevice, QStringLiteral(":/color_phong_vert.spv"));
    if (!mItemMaterial.fs.isValid())
        mItemMaterial.fs.load(vulkanInstance, logicalDevice, QStringLiteral(":/color_phong_frag.spv"));

	//Color shader for the floor
    if (!mFloorMaterial.vs.isValid())
        mFloorMaterial.vs.load(vulkanInstance, logicalDevice, QStringLiteral(":/color_vert.spv"));
    if (!mFloorMaterial.fs.isValid())
        mFloorMaterial.fs.load(vulkanInstance, logicalDevice, QStringLiteral(":/color_frag.spv"));

    //Runs createPipelines() in a separate thread
    //Returns a QFuture - the result of an asynchronous computation
    mPipelinesFuture = QtConcurrent::run(&Renderer::createPipelines, this);
}

//Called from initResources() in a separate thread.
void Renderer::createPipelines()
{
    VkDevice logicalDevice = mWindow->device();

    VkPipelineCacheCreateInfo pipelineCacheInfo{};
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VkResult err = mDeviceFunctions->vkCreatePipelineCache(logicalDevice, &pipelineCacheInfo, nullptr, &mPipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    createItemPipeline();
    createFloorPipeline();
}

//Called from createPipelines() in a separate thread.
//Phong shader for the blocks
void Renderer::createItemPipeline()
{
    VkDevice logicalDevice = mWindow->device();

	// 0 = vertex
    VkVertexInputBindingDescription vertexBindingDesc[2]{};
	vertexBindingDesc[0].binding = 0;
	vertexBindingDesc[0].stride = 8 * sizeof(float);    //position, uv, normal = 3 + 2 + 3 = 8
	vertexBindingDesc[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // 1 = instance
    // Seems like instance translate and diffuse color
	vertexBindingDesc[1].binding = 1;
	vertexBindingDesc[1].stride = 6 * sizeof(float);
	vertexBindingDesc[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription vertexAttrDesc[4]{};
	// 0 = position
	vertexAttrDesc[0].location = 0;
	vertexAttrDesc[0].binding = 0;
	vertexAttrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttrDesc[0].offset = 0;

    // 1 = normal 
	vertexAttrDesc[1].location = 1;
	vertexAttrDesc[1].binding = 0;
	vertexAttrDesc[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttrDesc[1].offset = 5 * sizeof(float);   // because uv is 3 and 4

    // 2 = instTranslate
	vertexAttrDesc[2].location = 2;
	vertexAttrDesc[2].binding = 1;
	vertexAttrDesc[2].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttrDesc[2].offset = 0;
    // 3 = instDiffuseAdjust
	vertexAttrDesc[3].location = 3;
	vertexAttrDesc[3].binding = 1;
	vertexAttrDesc[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttrDesc[3].offset = 3 * sizeof(float);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = sizeof(vertexBindingDesc) / sizeof(vertexBindingDesc[0]);
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Descriptor set layout.
    VkDescriptorPoolSize descriptorPoolSizes[1]{};
    descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descriptorPoolSizes[0].descriptorCount = 2;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.maxSets = 1; // a single set is enough due to the dynamic uniform buffer
    descriptorPoolInfo.poolSizeCount = sizeof(descriptorPoolSizes) / sizeof(descriptorPoolSizes[0]);
    descriptorPoolInfo.pPoolSizes = descriptorPoolSizes;

    VkResult err = mDeviceFunctions->vkCreateDescriptorPool(logicalDevice, &descriptorPoolInfo, nullptr, &mItemMaterial.descriptorPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    // OEF: Similar to what I have done, but I only use 1 for now for the Vertex shader
    VkDescriptorSetLayoutBinding layoutBindings[2]{};
	layoutBindings[0].binding = 0;
	layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBindings[0].descriptorCount = 1;
	layoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBindings[0].pImmutableSamplers = nullptr;

	layoutBindings[1].binding = 1;
    layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutBindings[1].descriptorCount = 1;
    layoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layoutBindings[1].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.pNext = nullptr;
    descriptorSetLayoutInfo.flags = 0;
    descriptorSetLayoutInfo.bindingCount = sizeof(layoutBindings) / sizeof(layoutBindings[0]);
    descriptorSetLayoutInfo.pBindings = layoutBindings;
    
    err = mDeviceFunctions->vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutInfo, nullptr, &mItemMaterial.descriptorSetLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);
    //----------------------------------------

    VkDescriptorSetAllocateInfo descritprSetAllocateInfo{};
    descritprSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descritprSetAllocateInfo.pNext = nullptr;
    descritprSetAllocateInfo.descriptorPool = mItemMaterial.descriptorPool;
    descritprSetAllocateInfo.descriptorSetCount = 1;
    descritprSetAllocateInfo.pSetLayouts = &mItemMaterial.descriptorSetLayout;

    err = mDeviceFunctions->vkAllocateDescriptorSets(logicalDevice, &descritprSetAllocateInfo, &mItemMaterial.descriptorSet);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate descriptor set: %d", err);

    // Graphics pipeline.
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mItemMaterial.descriptorSetLayout;

    err = mDeviceFunctions->vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &mItemMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkPipelineShaderStageCreateInfo vertShaderCreateInfo{};
    vertShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderCreateInfo.module = mItemMaterial.vs.data()->shaderModule;
    vertShaderCreateInfo.pName = "main";                // start function in shader

    VkPipelineShaderStageCreateInfo fragShaderCreateInfo{};
    fragShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderCreateInfo.module = mItemMaterial.fs.data()->shaderModule;
    fragShaderCreateInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[2] = { vertShaderCreateInfo, fragShaderCreateInfo };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.pInputAssemblyState = &inputAssembly;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    pipelineInfo.pViewportState = &viewport;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rasterization;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mWindow->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &multisample;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;
    pipelineInfo.pColorBlendState = &colorBlend;

    VkDynamicState dynamicEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = sizeof(dynamicEnable) / sizeof(VkDynamicState);
    dynamic.pDynamicStates = dynamicEnable;
    pipelineInfo.pDynamicState = &dynamic;

    pipelineInfo.layout = mItemMaterial.pipelineLayout;
    pipelineInfo.renderPass = mWindow->defaultRenderPass();

    err = mDeviceFunctions->vkCreateGraphicsPipelines(logicalDevice, mPipelineCache, 1, &pipelineInfo, nullptr, &mItemMaterial.pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);
}

//Called from createPipelines() in a separate thread.
//Color shader for the floor
void Renderer::createFloorPipeline()
{
    VkDevice logicalDevice = mWindow->device();

    // Vertex layout.
    VkVertexInputBindingDescription vertexBindingDesc{};
	vertexBindingDesc.binding = 0;
	vertexBindingDesc.stride = 3 * sizeof(float);
	vertexBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexAttrDesc[1]{};
	vertexAttrDesc[0].binding = 0;
	vertexAttrDesc[0].location = 0;
	vertexAttrDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	vertexAttrDesc[0].offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Do not bother with uniform buffers and descriptors, all the data fits
    // into the spec mandated minimum of 128 bytes for push constants.
	VkPushConstantRange pcr[2]{};
    // mvp
	pcr[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcr[0].offset = 0;
	pcr[0].size = 64;   //one mat4
	// color
	pcr[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pcr[1].offset = 64;
	pcr[1].size = 12;   //one vec3

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = sizeof(pcr) / sizeof(pcr[0]);
    pipelineLayoutInfo.pPushConstantRanges = pcr;

    VkResult err = mDeviceFunctions->vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &mFloorMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkPipelineShaderStageCreateInfo vertShaderCreateInfo{};
    vertShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderCreateInfo.module = mFloorMaterial.vs.data()->shaderModule;
    vertShaderCreateInfo.pName = "main";                // start function in shader

    VkPipelineShaderStageCreateInfo fragShaderCreateInfo{};
    fragShaderCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderCreateInfo.module = mFloorMaterial.fs.data()->shaderModule;
    fragShaderCreateInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[2] = { vertShaderCreateInfo, fragShaderCreateInfo };

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelineInfo.pInputAssemblyState = &inputAssembly;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    pipelineInfo.pViewportState = &viewport;

    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rasterization;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = mWindow->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &multisample;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &depthStencil;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorBlendAttachment;
    pipelineInfo.pColorBlendState = &colorBlend;

    VkDynamicState dynamicEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = sizeof(dynamicEnable) / sizeof(VkDynamicState);
    dynamic.pDynamicStates = dynamicEnable;
    pipelineInfo.pDynamicState = &dynamic;

    pipelineInfo.layout = mFloorMaterial.pipelineLayout;
    pipelineInfo.renderPass = mWindow->defaultRenderPass();

    err = mDeviceFunctions->vkCreateGraphicsPipelines(logicalDevice, mPipelineCache, 1, &pipelineInfo, nullptr, &mFloorMaterial.pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);
}

void Renderer::initSwapChainResources()
{
    mProj = mWindow->clipCorrectionMatrix();
    const QSize sz = mWindow->swapChainImageSize();
    mProj.perspective(45.0f, sz.width() / (float) sz.height(), 0.01f, 1000.0f);
    markViewProjDirty();
}

void Renderer::releaseSwapChainResources()
{
    // It is important to finish the pending frame right here since this is the
    // last opportunity to act with all resources intact.
    mFrameWatcher.waitForFinished();
    // Cannot count on the finished() signal being emitted before returning
    // from here.
    if (mFramePending) {
        mFramePending = false;
        mWindow->frameReady();
    }
}

void Renderer::releaseResources()
{
    if (DBG)
        qDebug("Renderer release");

    mPipelinesFuture.waitForFinished();

    VkDevice dev = mWindow->device();

    if (mItemMaterial.descriptorSetLayout) {
        mDeviceFunctions->vkDestroyDescriptorSetLayout(dev, mItemMaterial.descriptorSetLayout, nullptr);
        mItemMaterial.descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (mItemMaterial.descriptorPool) {
        mDeviceFunctions->vkDestroyDescriptorPool(dev, mItemMaterial.descriptorPool, nullptr);
        mItemMaterial.descriptorPool = VK_NULL_HANDLE;
    }

    if (mItemMaterial.pipeline) {
        mDeviceFunctions->vkDestroyPipeline(dev, mItemMaterial.pipeline, nullptr);
        mItemMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (mItemMaterial.pipelineLayout) {
        mDeviceFunctions->vkDestroyPipelineLayout(dev, mItemMaterial.pipelineLayout, nullptr);
        mItemMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (mFloorMaterial.pipeline) {
        mDeviceFunctions->vkDestroyPipeline(dev, mFloorMaterial.pipeline, nullptr);
        mFloorMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (mFloorMaterial.pipelineLayout) {
        mDeviceFunctions->vkDestroyPipelineLayout(dev, mFloorMaterial.pipelineLayout, nullptr);
        mFloorMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (mPipelineCache) {
        mDeviceFunctions->vkDestroyPipelineCache(dev, mPipelineCache, nullptr);
        mPipelineCache = VK_NULL_HANDLE;
    }

    if (mBlockVertexBuf) {
        mDeviceFunctions->vkDestroyBuffer(dev, mBlockVertexBuf, nullptr);
        mBlockVertexBuf = VK_NULL_HANDLE;
    }

    if (mLogoVertexBuf) {
        mDeviceFunctions->vkDestroyBuffer(dev, mLogoVertexBuf, nullptr);
        mLogoVertexBuf = VK_NULL_HANDLE;
    }

    if (mFloorVertexBuf) {
        mDeviceFunctions->vkDestroyBuffer(dev, mFloorVertexBuf, nullptr);
        mFloorVertexBuf = VK_NULL_HANDLE;
    }

    if (mUniBuf) {
        mDeviceFunctions->vkDestroyBuffer(dev, mUniBuf, nullptr);
        mUniBuf = VK_NULL_HANDLE;
    }

    if (mBufMem) {
        mDeviceFunctions->vkFreeMemory(dev, mBufMem, nullptr);
        mBufMem = VK_NULL_HANDLE;
    }

    if (mInstBuf) {
        mDeviceFunctions->vkDestroyBuffer(dev, mInstBuf, nullptr);
        mInstBuf = VK_NULL_HANDLE;
    }

    if (mInstBufMem) {
        mDeviceFunctions->vkFreeMemory(dev, mInstBufMem, nullptr);
        mInstBufMem = VK_NULL_HANDLE;
    }

    if (mItemMaterial.vs.isValid()) {
        mDeviceFunctions->vkDestroyShaderModule(dev, mItemMaterial.vs.data()->shaderModule, nullptr);
        mItemMaterial.vs.reset();
    }
    if (mItemMaterial.fs.isValid()) {
        mDeviceFunctions->vkDestroyShaderModule(dev, mItemMaterial.fs.data()->shaderModule, nullptr);
        mItemMaterial.fs.reset();
    }

    if (mFloorMaterial.vs.isValid()) {
        mDeviceFunctions->vkDestroyShaderModule(dev, mFloorMaterial.vs.data()->shaderModule, nullptr);
        mFloorMaterial.vs.reset();
    }
    if (mFloorMaterial.fs.isValid()) {
        mDeviceFunctions->vkDestroyShaderModule(dev, mFloorMaterial.fs.data()->shaderModule, nullptr);
        mFloorMaterial.fs.reset();
    }
}

void Renderer::ensureBuffers()
{
    if (mBlockVertexBuf)
        return;

    VkDevice dev = mWindow->device();
    const int concurrentFrameCount = mWindow->concurrentFrameCount();

    // Vertex buffer for the block.
    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    const int blockMeshByteCount = mBlockMesh.data()->vertexCount * 8 * sizeof(float);
    bufInfo.size = blockMeshByteCount;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VkResult err = mDeviceFunctions->vkCreateBuffer(dev, &bufInfo, nullptr, &mBlockVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements blockVertMemReq;
    mDeviceFunctions->vkGetBufferMemoryRequirements(dev, mBlockVertexBuf, &blockVertMemReq);

    // Vertex buffer for the logo.
    const int logoMeshByteCount = mLogoMesh.data()->vertexCount * 8 * sizeof(float);
    bufInfo.size = logoMeshByteCount;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    err = mDeviceFunctions->vkCreateBuffer(dev, &bufInfo, nullptr, &mLogoVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements logoVertMemReq;
    mDeviceFunctions->vkGetBufferMemoryRequirements(dev, mLogoVertexBuf, &logoVertMemReq);

    // Vertex buffer for the floor.
    bufInfo.size = sizeof(quadVert);
    err = mDeviceFunctions->vkCreateBuffer(dev, &bufInfo, nullptr, &mFloorVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements floorVertMemReq;
    mDeviceFunctions->vkGetBufferMemoryRequirements(dev, mFloorVertexBuf, &floorVertMemReq);

    // Uniform buffer. Instead of using multiple descriptor sets, we take a
    // different approach: have a single dynamic uniform buffer and specify the
    // active-frame-specific offset at the time of binding the descriptor set.
    bufInfo.size = (mItemMaterial.vertUniSize + mItemMaterial.fragUniSize) * concurrentFrameCount;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    err = mDeviceFunctions->vkCreateBuffer(dev, &bufInfo, nullptr, &mUniBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create uniform buffer: %d", err);

    VkMemoryRequirements uniMemReq;
    mDeviceFunctions->vkGetBufferMemoryRequirements(dev, mUniBuf, &uniMemReq);

    // Allocate memory for everything at once.
    VkDeviceSize logoVertStartOffset = aligned(0 + blockVertMemReq.size, logoVertMemReq.alignment);
    VkDeviceSize floorVertStartOffset = aligned(logoVertStartOffset + logoVertMemReq.size, floorVertMemReq.alignment);
    mItemMaterial.uniMemStartOffset = aligned(floorVertStartOffset + floorVertMemReq.size, uniMemReq.alignment);
    VkMemoryAllocateInfo memAllocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        mItemMaterial.uniMemStartOffset + uniMemReq.size,
        mWindow->hostVisibleMemoryIndex()
    };
    err = mDeviceFunctions->vkAllocateMemory(dev, &memAllocInfo, nullptr, &mBufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate memory: %d", err);

    err = mDeviceFunctions->vkBindBufferMemory(dev, mBlockVertexBuf, mBufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDeviceFunctions->vkBindBufferMemory(dev, mLogoVertexBuf, mBufMem, logoVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDeviceFunctions->vkBindBufferMemory(dev, mFloorVertexBuf, mBufMem, floorVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDeviceFunctions->vkBindBufferMemory(dev, mUniBuf, mBufMem, mItemMaterial.uniMemStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind uniform buffer memory: %d", err);

    // Copy vertex data.
    uint8_t* p{ nullptr };
    err = mDeviceFunctions->vkMapMemory(dev, mBufMem, 0, mItemMaterial.uniMemStartOffset, 0, reinterpret_cast<void**>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, mBlockMesh.data()->geom.constData(), blockMeshByteCount);
    memcpy(p + logoVertStartOffset, mLogoMesh.data()->geom.constData(), logoMeshByteCount);
    memcpy(p + floorVertStartOffset, quadVert, sizeof(quadVert));
    mDeviceFunctions->vkUnmapMemory(dev, mBufMem);

    // Write descriptors for the uniform buffers in the vertex and fragment shaders.
    // OEF: I have done it the same way but only have it for Vertex shader for now.
    VkDescriptorBufferInfo vertUniformBufferInfo{};
    vertUniformBufferInfo.buffer = mUniBuf;
    vertUniformBufferInfo.offset = 0;
    vertUniformBufferInfo.range = mItemMaterial.vertUniSize;

    VkDescriptorBufferInfo fragUniformBufferInfo{};
    fragUniformBufferInfo.buffer = mUniBuf;
    fragUniformBufferInfo.offset = mItemMaterial.vertUniSize;
    fragUniformBufferInfo.range = mItemMaterial.fragUniSize;

    VkWriteDescriptorSet writeDescriptorSet[2]{};
    writeDescriptorSet[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[0].dstSet = mItemMaterial.descriptorSet;
    writeDescriptorSet[0].dstBinding = 0;
    writeDescriptorSet[0].descriptorCount = 1;
    writeDescriptorSet[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writeDescriptorSet[0].pBufferInfo = &vertUniformBufferInfo;

    writeDescriptorSet[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet[1].dstSet = mItemMaterial.descriptorSet;
    writeDescriptorSet[1].dstBinding = 1;
    writeDescriptorSet[1].descriptorCount = 1;
    writeDescriptorSet[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    writeDescriptorSet[1].pBufferInfo = &fragUniformBufferInfo;

    mDeviceFunctions->vkUpdateDescriptorSets(dev, 2, writeDescriptorSet, 0, nullptr);
}

void Renderer::ensureInstanceBuffer()
{
    if (mInstCount == mPreparedInstCount && mInstBuf)
        return;

    Q_ASSERT(mInstCount <= MAX_INSTANCES);

    VkDevice dev = mWindow->device();

    // allocate only once, for the maximum instance count
    if (!mInstBuf) {
        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = MAX_INSTANCES * PER_INSTANCE_DATA_SIZE;
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        // Keep a copy of the data since we may lose all graphics resources on
        // unexpose, and reinitializing to new random positions afterwards
        // would not be nice.
        mInstData.resize(bufInfo.size);

        VkResult err = mDeviceFunctions->vkCreateBuffer(dev, &bufInfo, nullptr, &mInstBuf);
        if (err != VK_SUCCESS)
            qFatal("Failed to create instance buffer: %d", err);

        VkMemoryRequirements memReq;
        mDeviceFunctions->vkGetBufferMemoryRequirements(dev, mInstBuf, &memReq);
        if (DBG)
            qDebug("Allocating %u bytes for instance data", uint32_t(memReq.size));

        VkMemoryAllocateInfo memAllocInfo{};
		memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memAllocInfo.allocationSize = memReq.size;
		memAllocInfo.memoryTypeIndex = mWindow->hostVisibleMemoryIndex();
        
        err = mDeviceFunctions->vkAllocateMemory(dev, &memAllocInfo, nullptr, &mInstBufMem);
        if (err != VK_SUCCESS)
            qFatal("Failed to allocate memory: %d", err);

        err = mDeviceFunctions->vkBindBufferMemory(dev, mInstBuf, mInstBufMem, 0);
        if (err != VK_SUCCESS)
            qFatal("Failed to bind instance buffer memory: %d", err);
    }

    if (mInstCount != mPreparedInstCount) {
        if (DBG)
            qDebug("Preparing instances %d..%d", mPreparedInstCount, mInstCount - 1);

        char *p = mInstData.data();
        p += mPreparedInstCount * PER_INSTANCE_DATA_SIZE;
        auto gen = [](int a, int b) {
            return float(QRandomGenerator::global()->bounded(double(b - a)) + a);
        };

        for (int i = mPreparedInstCount; i < mInstCount; ++i) {
            // Apply a random translation to each instance of the mesh.
            float t[] = { gen(-5, 5), gen(-4, 6), gen(-30, 5) };
            memcpy(p, t, 12);
            // Apply a random adjustment to the diffuse color for each instance. (default is 0.7)
            float d[] = { gen(-6, 3) / 10.0f, gen(-6, 3) / 10.0f, gen(-6, 3) / 10.0f };
            memcpy(p + 12, d, 12);
            p += PER_INSTANCE_DATA_SIZE;
        }
        mPreparedInstCount = mInstCount;
    }

    uint8_t* p{ nullptr };
    VkResult err = mDeviceFunctions->vkMapMemory(dev, mInstBufMem, 0, mInstCount * PER_INSTANCE_DATA_SIZE, 0,
                                           reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, mInstData.constData(), mInstData.size());
    mDeviceFunctions->vkUnmapMemory(dev, mInstBufMem);
}

void Renderer::getMatrices(QMatrix4x4 *vp, QMatrix4x4 *model, QMatrix3x3 *modelNormal, QVector3D *eyePos)
{
    model->setToIdentity();
    if (mUseLogo)
        model->rotate(90, 1, 0, 0);
    model->rotate(mRotation, 1, 1, 0);

    *modelNormal = model->normalMatrix();

    QMatrix4x4 view = mCam.viewMatrix();
    *vp = mProj * view;

    *eyePos = view.inverted().column(3).toVector3D();
}

void Renderer::writeFragUni(uint8_t *p, const QVector3D &eyePos)
{
    float ECCameraPosition[] = { eyePos.x(), eyePos.y(), eyePos.z() };
    memcpy(p, ECCameraPosition, 12);
    p += 16;

    // Material
    float ka[] = { 0.05f, 0.05f, 0.05f };
    memcpy(p, ka, 12);
    p += 16;

    float kd[] = { 0.7f, 0.7f, 0.7f };
    memcpy(p, kd, 12);
    p += 16;

    float ks[] = { 0.66f, 0.66f, 0.66f };
    memcpy(p, ks, 12);
    p += 16;

    // Light parameters
    float ECLightPosition[] = { mLightPos.x(), mLightPos.y(), mLightPos.z() };
    memcpy(p, ECLightPosition, 12);
    p += 16;

    float att[] = { 1, 0, 0 };
    memcpy(p, att, 12);
    p += 16;

    float color[] = { 1.0f, 1.0f, 1.0f };
    memcpy(p, color, 12);
    p += 12; // next we have two floats which have an alignment of 4, hence 12 only

    float intensity = 0.8f;
    memcpy(p, &intensity, 4);
    p += 4;

    float specularExp = 150.0f;
    memcpy(p, &specularExp, 4);
    p += 4;
}

void Renderer::startNextFrame()
{
    // For demonstration purposes offload the command buffer generation onto a
    // worker thread and continue with the frame submission only when it has
    // finished.
    Q_ASSERT(!mFramePending);
    mFramePending = true;
    QFuture<void> future = QtConcurrent::run(&Renderer::buildFrame, this);
    mFrameWatcher.setFuture(future);
}

void Renderer::buildFrame()
{
    QMutexLocker locker(&mGuiMutex);

    ensureBuffers();
    ensureInstanceBuffer();
    mPipelinesFuture.waitForFinished();

    VkCommandBuffer cb = mWindow->currentCommandBuffer();
    const QSize sz = mWindow->swapChainImageSize();

    VkClearColorValue clearColor = {{ 0.67f, 0.84f, 0.9f, 1.0f }};
    VkClearDepthStencilValue clearDS = { 1, 0 };
    VkClearValue clearValues[3]{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = clearDS;
    clearValues[2].color = clearColor;

    VkRenderPassBeginInfo rpBeginInfo{};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = mWindow->defaultRenderPass();
    rpBeginInfo.framebuffer = mWindow->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = mWindow->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rpBeginInfo.pClearValues = clearValues;
    VkCommandBuffer cmdBuf = mWindow->currentCommandBuffer();
    mDeviceFunctions->vkCmdBeginRenderPass(cmdBuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        0, 0,
        float(sz.width()), float(sz.height()),
        0, 1
    };
    mDeviceFunctions->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {
        { 0, 0 },
        { uint32_t(sz.width()), uint32_t(sz.height()) }
    };
    mDeviceFunctions->vkCmdSetScissor(cb, 0, 1, &scissor);

    buildDrawCallsForFloor();
    buildDrawCallsForItems();

    mDeviceFunctions->vkCmdEndRenderPass(cmdBuf);
}

void Renderer::buildDrawCallsForItems()
{
    VkDevice dev = mWindow->device();
    VkCommandBuffer cb = mWindow->currentCommandBuffer();

    mDeviceFunctions->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mItemMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    mDeviceFunctions->vkCmdBindVertexBuffers(cb, 0, 1, mUseLogo ? &mLogoVertexBuf : &mBlockVertexBuf, &vbOffset);
    mDeviceFunctions->vkCmdBindVertexBuffers(cb, 1, 1, &mInstBuf, &vbOffset);

    // Now provide offsets so that the two dynamic buffers point to the
    // beginning of the vertex and fragment uniform data for the current frame.
    uint32_t frameUniOffset = mWindow->currentFrame() * (mItemMaterial.vertUniSize + mItemMaterial.fragUniSize);
    uint32_t frameUniOffsets[] = { frameUniOffset, frameUniOffset };
    mDeviceFunctions->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mItemMaterial.pipelineLayout, 0, 1,
                                        &mItemMaterial.descriptorSet, 2, frameUniOffsets);

    if (mAnimating)
        mRotation += 0.5;

    if (mAnimating || mVpDirty) {
        if (mVpDirty)
            --mVpDirty;
        QMatrix4x4 vp, model;
        QMatrix3x3 modelNormal;
        QVector3D eyePos;
        getMatrices(&vp, &model, &modelNormal, &eyePos);

        // Map the uniform data for the current frame, ignore the geometry data at
        // the beginning and the uniforms for other frames.
        uint8_t* p{ nullptr };
        VkResult err = mDeviceFunctions->vkMapMemory(dev, mBufMem,
                                               mItemMaterial.uniMemStartOffset + frameUniOffset,
                                               mItemMaterial.vertUniSize + mItemMaterial.fragUniSize,
                                               0, reinterpret_cast<void **>(&p));
        if (err != VK_SUCCESS)
            qFatal("Failed to map memory: %d", err);

        // Vertex shader uniforms
        memcpy(p, vp.constData(), 64);
        memcpy(p + 64, model.constData(), 64);
        const float *mnp = modelNormal.constData();
        memcpy(p + 128, mnp, 12);
        memcpy(p + 128 + 16, mnp + 3, 12);
        memcpy(p + 128 + 32, mnp + 6, 12);

        // Fragment shader uniforms
        p += mItemMaterial.vertUniSize;
        writeFragUni(p, eyePos);

        mDeviceFunctions->vkUnmapMemory(dev, mBufMem);
    }

    mDeviceFunctions->vkCmdDraw(cb, (mUseLogo ? mLogoMesh.data() : mBlockMesh.data())->vertexCount, mInstCount, 0, 0);
}

void Renderer::buildDrawCallsForFloor()
{
    VkCommandBuffer cb = mWindow->currentCommandBuffer();

    mDeviceFunctions->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mFloorMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    mDeviceFunctions->vkCmdBindVertexBuffers(cb, 0, 1, &mFloorVertexBuf, &vbOffset);

    QMatrix4x4 mvp = mProj * mCam.viewMatrix() * mFloorModel;
    mDeviceFunctions->vkCmdPushConstants(cb, mFloorMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp.constData());
    float color[] = { 0.67f, 1.0f, 0.2f };
    mDeviceFunctions->vkCmdPushConstants(cb, mFloorMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 12, color);

    mDeviceFunctions->vkCmdDraw(cb, 4, 1, 0, 0);
}

void Renderer::addNew()
{
    QMutexLocker locker(&mGuiMutex);
    mInstCount = qMin(mInstCount + 16, MAX_INSTANCES);
}

void Renderer::yaw(float degrees)
{
    QMutexLocker locker(&mGuiMutex);
    mCam.yaw(degrees);
    markViewProjDirty();
}

void Renderer::pitch(float degrees)
{
    QMutexLocker locker(&mGuiMutex);
    mCam.pitch(degrees);
    markViewProjDirty();
}

void Renderer::walk(float amount)
{
    QMutexLocker locker(&mGuiMutex);
    mCam.walk(amount);
    markViewProjDirty();
}

void Renderer::strafe(float amount)
{
    QMutexLocker locker(&mGuiMutex);
    mCam.strafe(amount);
    markViewProjDirty();
}

void Renderer::setUseLogo(bool b)
{
    QMutexLocker locker(&mGuiMutex);
    mUseLogo = b;
    if (!mAnimating)
        mWindow->requestUpdate();
}
