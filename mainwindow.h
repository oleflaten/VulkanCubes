// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>

QT_FORWARD_DECLARE_CLASS(QLCDNumber)
QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QCheckBox)

class VulkanWindow;

class MainWindow : public QWidget
{
public:
    MainWindow(VulkanWindow *vulkanWindow);

private:
    QLabel *createLabel(const QString &text);

    QLabel* infoLabel{ nullptr };
    QCheckBox *meshSwitch{ nullptr };
    QLCDNumber *counterLcd{ nullptr };
    QPushButton *newButton{ nullptr };
    QPushButton *quitButton{ nullptr };
    QPushButton *pauseButton{ nullptr };

    int mCount{ 128 };
};

#endif
