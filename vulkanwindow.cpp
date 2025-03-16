// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "vulkanwindow.h"
#include "renderer.h"
#include <QMouseEvent>
#include <QKeyEvent>

VulkanWindow::VulkanWindow(bool dbg)
    : mDebug(dbg)
{
}

QVulkanWindowRenderer *VulkanWindow::createRenderer()
{
    mRenderer = new Renderer(this, 128);
    return mRenderer;
}

void VulkanWindow::addNew()
{
    mRenderer->addNew();
}

void VulkanWindow::togglePaused()
{
    mRenderer->setAnimating(!mRenderer->animating());
}

void VulkanWindow::meshSwitched(bool enable)
{
    mRenderer->setUseLogo(enable);
}

void VulkanWindow::mousePressEvent(QMouseEvent *e)
{
    mPressed = true;
    mLastPos = e->position().toPoint();
}

void VulkanWindow::mouseReleaseEvent(QMouseEvent *)
{
    mPressed = false;
}

void VulkanWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (!mPressed)
        return;

    int dx = e->position().toPoint().x() - mLastPos.x();
    int dy = e->position().toPoint().y() - mLastPos.y();

    if (dy)
        mRenderer->pitch(dy / 10.0f);

    if (dx)
        mRenderer->yaw(dx / 10.0f);

    mLastPos = e->position().toPoint();
}

void VulkanWindow::keyPressEvent(QKeyEvent *e)
{
    const float amount = e->modifiers().testFlag(Qt::ShiftModifier) ? 1.0f : 0.1f;
    switch (e->key()) {
    case Qt::Key_W:
        mRenderer->walk(amount);
        break;
    case Qt::Key_S:
        mRenderer->walk(-amount);
        break;
    case Qt::Key_A:
        mRenderer->strafe(-amount);
        break;
    case Qt::Key_D:
        mRenderer->strafe(amount);
        break;
    default:
        break;
    }
}

int VulkanWindow::instanceCount() const
{
    return mRenderer->instanceCount();
}
