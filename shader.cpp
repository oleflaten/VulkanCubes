// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "shader.h"
#include <QtConcurrentRun>
#include <QFile>
#include <QVulkanDeviceFunctions>

void Shader::load(QVulkanInstance *inst, VkDevice dev, const QString &fileName)
{
    reset();
    mMaybeRunning = true;
    mFuture = QtConcurrent::run([inst, dev, fileName]() {
        ShaderData sd;
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning("Failed to open %s", qPrintable(fileName));
            return sd;
        }
        QByteArray blob = file.readAll();
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = blob.size();
        shaderInfo.pCode = reinterpret_cast<const uint32_t *>(blob.constData());

        VkResult err = inst->deviceFunctions(dev)->vkCreateShaderModule(dev, &shaderInfo, nullptr, &sd.shaderModule);
        if (err != VK_SUCCESS) {
            qWarning("Failed to create shader module: %d", err);
            return sd;
        }
        return sd;
    }
    );
}

ShaderData *Shader::data()
{
    if (mMaybeRunning && !mData.isValid())
        mData = mFuture.result();   //blocks and waits till mFuture is ready

    return &mData;
}

void Shader::reset()
{
    *data() = ShaderData();
    mMaybeRunning = false;
}
