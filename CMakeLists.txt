# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

cmake_minimum_required(VERSION 3.16)
project(VulkanCubes LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Widgets)

qt_standard_project_setup()

qt_add_executable(VulkanCubes
    camera.cpp camera.h
    main.cpp
    mainwindow.cpp mainwindow.h
    mesh.cpp mesh.h
    renderer.cpp renderer.h
    shader.cpp shader.h
    vulkanwindow.cpp vulkanwindow.h
)

set_target_properties(VulkanCubes PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries(VulkanCubes PRIVATE
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Resources:
set_source_files_properties("./resources/block.buf"
    PROPERTIES QT_RESOURCE_ALIAS "block.buf"
)

set_source_files_properties("./resources/qt_logo.buf"
    PROPERTIES QT_RESOURCE_ALIAS "qt_logo.buf"
)

set(VulkanCubes_resource_files
    "./resources/block.buf"
    "./resources/qt_logo.buf"
    "color_frag.spv"
    "color_phong_frag.spv"
    "color_phong_vert.spv"
    "color_vert.spv"
)

qt6_add_resources(VulkanCubes "VulkanCubes"
    PREFIX
        "/"
    FILES
        ${VulkanCubes_resource_files}
)

install(TARGETS VulkanCubes
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET VulkanCubes
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
