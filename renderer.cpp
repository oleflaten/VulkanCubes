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
    const QList<int> sampleCounts = mWindow->supportedSampleCounts();
    if (DBG)
        qDebug() << "Supported sample counts:" << sampleCounts;
    if (sampleCounts.contains(4)) {
        if (DBG)
            qDebug("Requesting 4x MSAA");
        mWindow->setSampleCount(4);
    }
}

void Renderer::initResources()
{
    if (DBG)
        qDebug("Renderer init");

    mAnimating = true;
    mFramePending = false;

    QVulkanInstance *inst = mWindow->vulkanInstance();
    VkDevice dev = mWindow->device();
    const VkPhysicalDeviceLimits *pdevLimits = &mWindow->physicalDeviceProperties()->limits;
    const VkDeviceSize uniAlign = pdevLimits->minUniformBufferOffsetAlignment;

    mDevFuncs = inst->deviceFunctions(dev);

    // Note the std140 packing rules. A vec3 still has an alignment of 16,
    // while a mat3 is like 3 * vec3.
    mItemMaterial.vertUniSize = aligned(2 * 64 + 48, uniAlign); // see color_phong.vert
    mItemMaterial.fragUniSize = aligned(6 * 16 + 12 + 2 * 4, uniAlign); // see color_phong.frag

    if (!mItemMaterial.vs.isValid())
        mItemMaterial.vs.load(inst, dev, QStringLiteral(":/color_phong_vert.spv"));
    if (!mItemMaterial.fs.isValid())
        mItemMaterial.fs.load(inst, dev, QStringLiteral(":/color_phong_frag.spv"));

    if (!mFloorMaterial.vs.isValid())
        mFloorMaterial.vs.load(inst, dev, QStringLiteral(":/color_vert.spv"));
    if (!mFloorMaterial.fs.isValid())
        mFloorMaterial.fs.load(inst, dev, QStringLiteral(":/color_frag.spv"));

    mPipelinesFuture = QtConcurrent::run(&Renderer::createPipelines, this);
}

void Renderer::createPipelines()
{
    VkDevice dev = mWindow->device();

    VkPipelineCacheCreateInfo pipelineCacheInfo{};
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    VkResult err = mDevFuncs->vkCreatePipelineCache(dev, &pipelineCacheInfo, nullptr, &mPipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    createItemPipeline();
    createFloorPipeline();
}

void Renderer::createItemPipeline()
{
    VkDevice dev = mWindow->device();

    // Vertex layout.
    VkVertexInputBindingDescription vertexBindingDesc[] = {
        {
            0, // binding
            8 * sizeof(float),
            VK_VERTEX_INPUT_RATE_VERTEX
        },
        {
            1,
            6 * sizeof(float),
            VK_VERTEX_INPUT_RATE_INSTANCE
        }
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
        { // position
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            0 // offset
        },
        { // normal
            1,
            0,
            VK_FORMAT_R32G32B32_SFLOAT,
            5 * sizeof(float)
        },
        { // instTranslate
            2,
            1,
            VK_FORMAT_R32G32B32_SFLOAT,
            0
        },
        { // instDiffuseAdjust
            3,
            1,
            VK_FORMAT_R32G32B32_SFLOAT,
            3 * sizeof(float)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = sizeof(vertexBindingDesc) / sizeof(vertexBindingDesc[0]);
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Descriptor set layout.
    VkDescriptorPoolSize descPoolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2 }
    };
    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 1; // a single set is enough due to the dynamic uniform buffer
    descPoolInfo.poolSizeCount = sizeof(descPoolSizes) / sizeof(descPoolSizes[0]);
    descPoolInfo.pPoolSizes = descPoolSizes;
    VkResult err = mDevFuncs->vkCreateDescriptorPool(dev, &descPoolInfo, nullptr, &mItemMaterial.descPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    VkDescriptorSetLayoutBinding layoutBindings[] =
    {
        {
            0, // binding
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            1, // descriptorCount
            VK_SHADER_STAGE_VERTEX_BIT,
            nullptr
        },
        {
            1,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            1,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            nullptr
        }
    };
    VkDescriptorSetLayoutCreateInfo descLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        sizeof(layoutBindings) / sizeof(layoutBindings[0]),
        layoutBindings
    };
    err = mDevFuncs->vkCreateDescriptorSetLayout(dev, &descLayoutInfo, nullptr, &mItemMaterial.descSetLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    VkDescriptorSetAllocateInfo descSetAllocInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        mItemMaterial.descPool,
        1,
        &mItemMaterial.descSetLayout
    };
    err = mDevFuncs->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &mItemMaterial.descSet);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate descriptor set: %d", err);

    // Graphics pipeline.
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &mItemMaterial.descSetLayout;

    err = mDevFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &mItemMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            mItemMaterial.vs.data()->shaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            mItemMaterial.fs.data()->shaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.pInputAssemblyState = &ia;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = mWindow->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = mItemMaterial.pipelineLayout;
    pipelineInfo.renderPass = mWindow->defaultRenderPass();

    err = mDevFuncs->vkCreateGraphicsPipelines(dev, mPipelineCache, 1, &pipelineInfo, nullptr, &mItemMaterial.pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);
}

void Renderer::createFloorPipeline()
{
    VkDevice dev = mWindow->device();

    // Vertex layout.
    VkVertexInputBindingDescription vertexBindingDesc = {
        0, // binding
        3 * sizeof(float),
        VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
        { // position
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            0 // offset
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = sizeof(vertexAttrDesc) / sizeof(vertexAttrDesc[0]);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    // Do not bother with uniform buffers and descriptors, all the data fits
    // into the spec mandated minimum of 128 bytes for push constants.
    VkPushConstantRange pcr[] = {
        // mvp
        {
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            64
        },
        // color
        {
            VK_SHADER_STAGE_FRAGMENT_BIT,
            64,
            12
        }
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = sizeof(pcr) / sizeof(pcr[0]);
    pipelineLayoutInfo.pPushConstantRanges = pcr;

    VkResult err = mDevFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &mFloorMaterial.pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            mFloorMaterial.vs.data()->shaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            mFloorMaterial.fs.data()->shaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelineInfo.pInputAssemblyState = &ia;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = mWindow->sampleCountFlagBits();
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = 0xF;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = mFloorMaterial.pipelineLayout;
    pipelineInfo.renderPass = mWindow->defaultRenderPass();

    err = mDevFuncs->vkCreateGraphicsPipelines(dev, mPipelineCache, 1, &pipelineInfo, nullptr, &mFloorMaterial.pipeline);
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

    if (mItemMaterial.descSetLayout) {
        mDevFuncs->vkDestroyDescriptorSetLayout(dev, mItemMaterial.descSetLayout, nullptr);
        mItemMaterial.descSetLayout = VK_NULL_HANDLE;
    }

    if (mItemMaterial.descPool) {
        mDevFuncs->vkDestroyDescriptorPool(dev, mItemMaterial.descPool, nullptr);
        mItemMaterial.descPool = VK_NULL_HANDLE;
    }

    if (mItemMaterial.pipeline) {
        mDevFuncs->vkDestroyPipeline(dev, mItemMaterial.pipeline, nullptr);
        mItemMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (mItemMaterial.pipelineLayout) {
        mDevFuncs->vkDestroyPipelineLayout(dev, mItemMaterial.pipelineLayout, nullptr);
        mItemMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (mFloorMaterial.pipeline) {
        mDevFuncs->vkDestroyPipeline(dev, mFloorMaterial.pipeline, nullptr);
        mFloorMaterial.pipeline = VK_NULL_HANDLE;
    }

    if (mFloorMaterial.pipelineLayout) {
        mDevFuncs->vkDestroyPipelineLayout(dev, mFloorMaterial.pipelineLayout, nullptr);
        mFloorMaterial.pipelineLayout = VK_NULL_HANDLE;
    }

    if (mPipelineCache) {
        mDevFuncs->vkDestroyPipelineCache(dev, mPipelineCache, nullptr);
        mPipelineCache = VK_NULL_HANDLE;
    }

    if (mBlockVertexBuf) {
        mDevFuncs->vkDestroyBuffer(dev, mBlockVertexBuf, nullptr);
        mBlockVertexBuf = VK_NULL_HANDLE;
    }

    if (mLogoVertexBuf) {
        mDevFuncs->vkDestroyBuffer(dev, mLogoVertexBuf, nullptr);
        mLogoVertexBuf = VK_NULL_HANDLE;
    }

    if (mFloorVertexBuf) {
        mDevFuncs->vkDestroyBuffer(dev, mFloorVertexBuf, nullptr);
        mFloorVertexBuf = VK_NULL_HANDLE;
    }

    if (mUniBuf) {
        mDevFuncs->vkDestroyBuffer(dev, mUniBuf, nullptr);
        mUniBuf = VK_NULL_HANDLE;
    }

    if (mBufMem) {
        mDevFuncs->vkFreeMemory(dev, mBufMem, nullptr);
        mBufMem = VK_NULL_HANDLE;
    }

    if (mInstBuf) {
        mDevFuncs->vkDestroyBuffer(dev, mInstBuf, nullptr);
        mInstBuf = VK_NULL_HANDLE;
    }

    if (mInstBufMem) {
        mDevFuncs->vkFreeMemory(dev, mInstBufMem, nullptr);
        mInstBufMem = VK_NULL_HANDLE;
    }

    if (mItemMaterial.vs.isValid()) {
        mDevFuncs->vkDestroyShaderModule(dev, mItemMaterial.vs.data()->shaderModule, nullptr);
        mItemMaterial.vs.reset();
    }
    if (mItemMaterial.fs.isValid()) {
        mDevFuncs->vkDestroyShaderModule(dev, mItemMaterial.fs.data()->shaderModule, nullptr);
        mItemMaterial.fs.reset();
    }

    if (mFloorMaterial.vs.isValid()) {
        mDevFuncs->vkDestroyShaderModule(dev, mFloorMaterial.vs.data()->shaderModule, nullptr);
        mFloorMaterial.vs.reset();
    }
    if (mFloorMaterial.fs.isValid()) {
        mDevFuncs->vkDestroyShaderModule(dev, mFloorMaterial.fs.data()->shaderModule, nullptr);
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
    VkResult err = mDevFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &mBlockVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements blockVertMemReq;
    mDevFuncs->vkGetBufferMemoryRequirements(dev, mBlockVertexBuf, &blockVertMemReq);

    // Vertex buffer for the logo.
    const int logoMeshByteCount = mLogoMesh.data()->vertexCount * 8 * sizeof(float);
    bufInfo.size = logoMeshByteCount;
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    err = mDevFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &mLogoVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements logoVertMemReq;
    mDevFuncs->vkGetBufferMemoryRequirements(dev, mLogoVertexBuf, &logoVertMemReq);

    // Vertex buffer for the floor.
    bufInfo.size = sizeof(quadVert);
    err = mDevFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &mFloorVertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create vertex buffer: %d", err);

    VkMemoryRequirements floorVertMemReq;
    mDevFuncs->vkGetBufferMemoryRequirements(dev, mFloorVertexBuf, &floorVertMemReq);

    // Uniform buffer. Instead of using multiple descriptor sets, we take a
    // different approach: have a single dynamic uniform buffer and specify the
    // active-frame-specific offset at the time of binding the descriptor set.
    bufInfo.size = (mItemMaterial.vertUniSize + mItemMaterial.fragUniSize) * concurrentFrameCount;
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    err = mDevFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &mUniBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create uniform buffer: %d", err);

    VkMemoryRequirements uniMemReq;
    mDevFuncs->vkGetBufferMemoryRequirements(dev, mUniBuf, &uniMemReq);

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
    err = mDevFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &mBufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate memory: %d", err);

    err = mDevFuncs->vkBindBufferMemory(dev, mBlockVertexBuf, mBufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDevFuncs->vkBindBufferMemory(dev, mLogoVertexBuf, mBufMem, logoVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDevFuncs->vkBindBufferMemory(dev, mFloorVertexBuf, mBufMem, floorVertStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind vertex buffer memory: %d", err);
    err = mDevFuncs->vkBindBufferMemory(dev, mUniBuf, mBufMem, mItemMaterial.uniMemStartOffset);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind uniform buffer memory: %d", err);

    // Copy vertex data.
    quint8 *p;
    err = mDevFuncs->vkMapMemory(dev, mBufMem, 0, mItemMaterial.uniMemStartOffset, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, mBlockMesh.data()->geom.constData(), blockMeshByteCount);
    memcpy(p + logoVertStartOffset, mLogoMesh.data()->geom.constData(), logoMeshByteCount);
    memcpy(p + floorVertStartOffset, quadVert, sizeof(quadVert));
    mDevFuncs->vkUnmapMemory(dev, mBufMem);

    // Write descriptors for the uniform buffers in the vertex and fragment shaders.
    VkDescriptorBufferInfo vertUni = { mUniBuf, 0, mItemMaterial.vertUniSize };
    VkDescriptorBufferInfo fragUni = { mUniBuf, mItemMaterial.vertUniSize, mItemMaterial.fragUniSize };

    VkWriteDescriptorSet descWrite[2]{};
    descWrite[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite[0].dstSet = mItemMaterial.descSet;
    descWrite[0].dstBinding = 0;
    descWrite[0].descriptorCount = 1;
    descWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrite[0].pBufferInfo = &vertUni;

    descWrite[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descWrite[1].dstSet = mItemMaterial.descSet;
    descWrite[1].dstBinding = 1;
    descWrite[1].descriptorCount = 1;
    descWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    descWrite[1].pBufferInfo = &fragUni;

    mDevFuncs->vkUpdateDescriptorSets(dev, 2, descWrite, 0, nullptr);
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

        VkResult err = mDevFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &mInstBuf);
        if (err != VK_SUCCESS)
            qFatal("Failed to create instance buffer: %d", err);

        VkMemoryRequirements memReq;
        mDevFuncs->vkGetBufferMemoryRequirements(dev, mInstBuf, &memReq);
        if (DBG)
            qDebug("Allocating %u bytes for instance data", uint32_t(memReq.size));

        VkMemoryAllocateInfo memAllocInfo = {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            nullptr,
            memReq.size,
            mWindow->hostVisibleMemoryIndex()
        };
        err = mDevFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &mInstBufMem);
        if (err != VK_SUCCESS)
            qFatal("Failed to allocate memory: %d", err);

        err = mDevFuncs->vkBindBufferMemory(dev, mInstBuf, mInstBufMem, 0);
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

    quint8 *p;
    VkResult err = mDevFuncs->vkMapMemory(dev, mInstBufMem, 0, mInstCount * PER_INSTANCE_DATA_SIZE, 0,
                                           reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, mInstData.constData(), mInstData.size());
    mDevFuncs->vkUnmapMemory(dev, mInstBufMem);
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

void Renderer::writeFragUni(quint8 *p, const QVector3D &eyePos)
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
    clearValues[0].color = clearValues[2].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo{};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = mWindow->defaultRenderPass();
    rpBeginInfo.framebuffer = mWindow->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = mWindow->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    rpBeginInfo.pClearValues = clearValues;
    VkCommandBuffer cmdBuf = mWindow->currentCommandBuffer();
    mDevFuncs->vkCmdBeginRenderPass(cmdBuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {
        0, 0,
        float(sz.width()), float(sz.height()),
        0, 1
    };
    mDevFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor = {
        { 0, 0 },
        { uint32_t(sz.width()), uint32_t(sz.height()) }
    };
    mDevFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    buildDrawCallsForFloor();
    buildDrawCallsForItems();

    mDevFuncs->vkCmdEndRenderPass(cmdBuf);
}

void Renderer::buildDrawCallsForItems()
{
    VkDevice dev = mWindow->device();
    VkCommandBuffer cb = mWindow->currentCommandBuffer();

    mDevFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mItemMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    mDevFuncs->vkCmdBindVertexBuffers(cb, 0, 1, mUseLogo ? &mLogoVertexBuf : &mBlockVertexBuf, &vbOffset);
    mDevFuncs->vkCmdBindVertexBuffers(cb, 1, 1, &mInstBuf, &vbOffset);

    // Now provide offsets so that the two dynamic buffers point to the
    // beginning of the vertex and fragment uniform data for the current frame.
    uint32_t frameUniOffset = mWindow->currentFrame() * (mItemMaterial.vertUniSize + mItemMaterial.fragUniSize);
    uint32_t frameUniOffsets[] = { frameUniOffset, frameUniOffset };
    mDevFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mItemMaterial.pipelineLayout, 0, 1,
                                        &mItemMaterial.descSet, 2, frameUniOffsets);

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
        quint8 *p;
        VkResult err = mDevFuncs->vkMapMemory(dev, mBufMem,
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

        mDevFuncs->vkUnmapMemory(dev, mBufMem);
    }

    mDevFuncs->vkCmdDraw(cb, (mUseLogo ? mLogoMesh.data() : mBlockMesh.data())->vertexCount, mInstCount, 0, 0);
}

void Renderer::buildDrawCallsForFloor()
{
    VkCommandBuffer cb = mWindow->currentCommandBuffer();

    mDevFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, mFloorMaterial.pipeline);

    VkDeviceSize vbOffset = 0;
    mDevFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &mFloorVertexBuf, &vbOffset);

    QMatrix4x4 mvp = mProj * mCam.viewMatrix() * mFloorModel;
    mDevFuncs->vkCmdPushConstants(cb, mFloorMaterial.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp.constData());
    float color[] = { 0.67f, 1.0f, 0.2f };
    mDevFuncs->vkCmdPushConstants(cb, mFloorMaterial.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 64, 12, color);

    mDevFuncs->vkCmdDraw(cb, 4, 1, 0, 0);
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
