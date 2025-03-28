// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef SHADER_H
#define SHADER_H

#include <QVulkanInstance>
#include <QFuture>

struct ShaderData
{
    bool isValid() const { return shaderModule != VK_NULL_HANDLE; }
    VkShaderModule shaderModule = VK_NULL_HANDLE;
};

class Shader
{
public:
    void load(QVulkanInstance *inst, VkDevice dev, const QString & fileName);
    ShaderData *data();
    bool isValid() { return data()->isValid(); }
    void reset();

private:
    bool mMaybeRunning{ false };
    QFuture<ShaderData> mFuture;
    ShaderData mData;
};

#endif
