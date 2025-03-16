// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef RENDERER_H
#define RENDERER_H

#include "vulkanwindow.h"
#include "mesh.h"
#include "shader.h"
#include "camera.h"
#include <QFutureWatcher>
#include <QMutex>

class Renderer : public QVulkanWindowRenderer
{
public:
    Renderer(VulkanWindow *w, int initialCount);

    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

    bool animating() const { return mAnimating; }
    void setAnimating(bool a) { mAnimating = a; }

    int instanceCount() const { return mInstCount; }
    void addNew();

    void yaw(float degrees);
    void pitch(float degrees);
    void walk(float amount);
    void strafe(float amount);

    void setUseLogo(bool b);

private:
    void createPipelines();
    void createItemPipeline();
    void createFloorPipeline();
    void ensureBuffers();
    void ensureInstanceBuffer();
    void getMatrices(QMatrix4x4 *mvp, QMatrix4x4 *model, QMatrix3x3 *modelNormal, QVector3D *eyePos);
    void writeFragUni(uint8_t *p, const QVector3D &eyePos);
    void buildFrame();
    void buildDrawCallsForItems();
    void buildDrawCallsForFloor();

    void markViewProjDirty() { mVpDirty = mWindow->concurrentFrameCount(); }

    VulkanWindow *mWindow{nullptr};
    QVulkanDeviceFunctions *mDevFuncs{nullptr};

    bool mUseLogo{false};
    Mesh mBlockMesh;
    Mesh mLogoMesh;
    VkBuffer mBlockVertexBuf{VK_NULL_HANDLE};
    VkBuffer mLogoVertexBuf{VK_NULL_HANDLE};
    VkBuffer mFloorVertexBuf{ VK_NULL_HANDLE };

	// Item material = phong shader
    struct {
        VkDeviceSize vertUniSize;
        VkDeviceSize fragUniSize;
        VkDeviceSize uniMemStartOffset;
        Shader vs;
        Shader fs;
        VkDescriptorPool descPool{VK_NULL_HANDLE};
        VkDescriptorSetLayout descSetLayout{VK_NULL_HANDLE};
        VkDescriptorSet descSet;
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    } mItemMaterial;

	// Floor material = color shader
    struct {
        Shader vs;
        Shader fs;
        VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
        VkPipeline pipeline{VK_NULL_HANDLE};
    } mFloorMaterial;

    VkDeviceMemory mBufMem{VK_NULL_HANDLE};
    VkBuffer mUniBuf{VK_NULL_HANDLE};

    VkPipelineCache mPipelineCache{VK_NULL_HANDLE};
    QFuture<void> mPipelinesFuture;

    QVector3D mLightPos;
    Camera mCam;

    QMatrix4x4 mProj;
    int mVpDirty{0};
    QMatrix4x4 mFloorModel;

    bool mAnimating{false};
    float mRotation{0.0f};

    int mInstCount;
    int mPreparedInstCount{0};
    QByteArray mInstData;
    VkBuffer mInstBuf{VK_NULL_HANDLE};
    VkDeviceMemory mInstBufMem{VK_NULL_HANDLE};

    QFutureWatcher<void> mFrameWatcher;
    bool mFramePending{false};

    QMutex mGuiMutex;
};

#endif
