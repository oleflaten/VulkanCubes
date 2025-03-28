// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef VULKANWINDOW_H
#define VULKANWINDOW_H

#include <QVulkanWindow>

class Renderer;

class VulkanWindow : public QVulkanWindow
{
public:
    VulkanWindow(bool dbg);

    QVulkanWindowRenderer *createRenderer() override;

    bool isDebugEnabled() const { return mDebug; }
    int instanceCount() const;

public slots:
    void addNew();
    void togglePaused();
    void meshSwitched(bool enable);

private:
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

    bool mDebug;
    Renderer* mRenderer{nullptr};
    bool mPressed{false};
    QPoint mLastPos;
};

#endif
