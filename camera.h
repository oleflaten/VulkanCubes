// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef CAMERA_H
#define CAMERA_H

#include <QVector3D>
#include <QMatrix4x4>

class Camera
{
public:
    Camera(const QVector3D &pos);

    void yaw(float degrees);
    void pitch(float degrees);
    void walk(float amount);
    void strafe(float amount);

    QMatrix4x4 viewMatrix() const;

private:
    QVector3D mForward;
    QVector3D mRight;
    QVector3D mUp;
    QVector3D mPos;
    float mYaw;
    float mPitch;
    QMatrix4x4 mYawMatrix;
    QMatrix4x4 mPitchMatrix;
};

#endif
