// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "camera.h"

Camera::Camera(const QVector3D &pos)
    : mForward(0.0f, 0.0f, -1.0f),
      mRight(1.0f, 0.0f, 0.0f),
      mUp(0.0f, 1.0f, 0.0f),
      mPos(pos),
      mYaw(0.0f),
      mPitch(0.0f)
{
}

static inline void clamp360(float *v)
{
    if (*v > 360.0f)
        *v -= 360.0f;
    if (*v < -360.0f)
        *v += 360.0f;
}

void Camera::yaw(float degrees)
{
    mYaw += degrees;
    clamp360(&mYaw);
    mYawMatrix.setToIdentity();
    mYawMatrix.rotate(mYaw, 0, 1, 0);

    QMatrix4x4 rotMat = mPitchMatrix * mYawMatrix;
    mForward = (QVector4D(0.0f, 0.0f, -1.0f, 0.0f) * rotMat).toVector3D();
    mRight = (QVector4D(1.0f, 0.0f, 0.0f, 0.0f) * rotMat).toVector3D();
}

void Camera::pitch(float degrees)
{
    mPitch += degrees;
    clamp360(&mPitch);
    mPitchMatrix.setToIdentity();
    mPitchMatrix.rotate(mPitch, 1, 0, 0);

    QMatrix4x4 rotMat = mPitchMatrix * mYawMatrix;
    mForward = (QVector4D(0.0f, 0.0f, -1.0f, 0.0f) * rotMat).toVector3D();
    mUp = (QVector4D(0.0f, 1.0f, 0.0f, 0.0f) * rotMat).toVector3D();
}

void Camera::walk(float amount)
{
    mPos[0] += amount * mForward.x();
    mPos[2] += amount * mForward.z();
}

void Camera::strafe(float amount)
{
    mPos[0] += amount * mRight.x();
    mPos[2] += amount * mRight.z();
}

QMatrix4x4 Camera::viewMatrix() const
{
    QMatrix4x4 m = mPitchMatrix * mYawMatrix;
    m.translate(-mPos);
    return m;
}
