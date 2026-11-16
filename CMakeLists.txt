cmake_minimum_required(VERSION 3.21)

project(real-esrguisoft
    VERSION 1.0.0
    DESCRIPTION "Real-ESRGAN 媒体超分处理工具（纯 C++/Qt6）"
    LANGUAGES CXX)

# 打包配置一致性（静态审查 C3）：cxx_pkg.ini 的 [project].version 必须与
# project(VERSION) 一致，避免 zip 名/关于页/CMake 版本三处漂移。打包机与本地
# 配置阶段都会检查；不需要额外脚本/过度设计。
set(RESR_CXX_PKG_INI "${CMAKE_CURRENT_SOURCE_DIR}/cxx_pkg.ini")
if(EXISTS "${RESR_CXX_PKG_INI}")
    file(STRINGS "${RESR_CXX_PKG_INI}" _resr_pkg_version_lines REGEX "^version[ ]*=")
    if(_resr_pkg_version_lines)
        list(GET _resr_pkg_version_lines 0 _resr_pkg_version_line)
        string(REGEX REPLACE "^[^=]*=[ ]*" "" _resr_pkg_version "${_resr_pkg_version_line}")
        string(STRIP "${_resr_pkg_version}" _resr_pkg_version)
        if(NOT "${_resr_pkg_version}" STREQUAL "${PROJECT_VERSION}")
            message(FATAL_ERROR
                "cxx_pkg.ini 的 version (${_resr_pkg_version}) 与 CMake project VERSION "
                "(${PROJECT_VERSION}) 不一致；请同步两处（静态审查 C3）")
        endif()
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RESR_BUILD_TESTS "构建单元测试" ON)
option(RESR_WARNINGS_AS_ERRORS "把编译警告当作错误" OFF)
# 真实 GPU 引擎冒烟用例（默认 OFF）：避免 CI/开发机直接 `ctest` 时误启动
# realesrgan 触发环境 TDR；显式开启后才注册 `resr_engine_tests`（LABELS engine）。
# 运行时还需设置 RESR_TEST_ENGINE=1 且机器有可用 Vulkan GPU（见 tests/test_pipeline.cpp）。
option(RESR_BUILD_ENGINE_TESTS "注册真实引擎 GPU 冒烟用例（默认关闭）" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia MultimediaWidgets)

# 自定义 CMake 模块（FFmpeg 查找等）
# 注意：Qt6 会把自带模块目录（含 Qt 的 FindFFmpeg.cmake）追加进
# CMAKE_MODULE_PATH，因此这里必须 PREPEND，保证工程的 cmake/FindFFmpeg.cmake
# 优先命中（查找逻辑见 docs/design/ffmpeg-integration.md）。
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# 公共警告选项
# ---------------------------------------------------------------------------
add_library(resr_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(resr_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        $<$<BOOL:${RESR_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

set(RESR_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/view/theme
    ${CMAKE_CURRENT_SOURCE_DIR}/src/domain
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure
    ${CMAKE_CURRENT_SOURCE_DIR}/src/infrastructure/media
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controller
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
)

# ---------------------------------------------------------------------------
# 第三方库（以子目录方式引入，保持独立构建与测试）
# ---------------------------------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stacklog/CMakeLists.txt)
    add_subdirectory(third_party/stacklog EXCLUDE_FROM_ALL)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/singleproc/CMakeLists.txt)
    add_subdirectory(third_party/singleproc EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# resr_core：不依赖 Qt Widgets 的业务核心（domain / infrastructure / model）
# ---------------------------------------------------------------------------
add_library(resr_core STATIC
    src/domain/mediasettings.cpp
    src/domain/processstatus.cpp
    src/domain/fileservice.cpp

    src/common/logger.cpp
    src/common/language_manager.cpp

    src/infrastructure/execommand.cpp
    src/infrastructure/exeoutputparser.cpp
    src/infrastructure/exerunner.cpp

    src/model/outputpathresolver.cpp
    src/model/batchprogresswatcher.cpp
    src/model/inputscanner.cpp
    src/model/mediaprocessservice.h
    src/model/imageprocessservice.cpp
    src/model/videoprocessservice.cpp
    src/model/processservice.cpp

    src/utils/tooltips.cpp
)

target_include_directories(resr_core PUBLIC ${RESR_INCLUDE_DIRS})
target_link_libraries(resr_core PUBLIC Qt6::Core Qt6::Gui)
target_link_libraries(resr_core PRIVATE resr_warnings)

# 媒体基础设施层（src/infrastructure/media）是**必需依赖**：cmake/MediaFFmpeg.cmake
# 中 find_package(FFmpeg REQUIRED)，无 FFmpeg 开发库时配置阶段直接失败（文档已说明）。
# 因此这里无条件定义 RESR_HAS_MEDIA；src 中 `#ifndef RESR_HAS_MEDIA` 的历史占位当前
# 不可达（静态审查 C4：要么允许无 media 配置、要么移除占位——清理占位需改 src，
# 不在本配置文件范围，后续单独任务处理）。
target_compile_definitions(resr_core PRIVATE RESR_HAS_MEDIA)

# ---------------------------------------------------------------------------
# 媒体基础设施：libav* 封装层（src/infrastructure/media）
# 查找 FFmpeg（优先系统 pkg-config）、定义 resr_media_ffmpeg / resr_media，
# 并挂到 resr_core 上；详见 docs/design/ffmpeg-integration.md
# ---------------------------------------------------------------------------
include(MediaFFmpeg)

# ---------------------------------------------------------------------------
# resr_ui：视图与控制器
# ---------------------------------------------------------------------------
add_library(resr_ui STATIC
    src/view/mainwindow.cpp
    src/view/mainviewinterface.cpp
    src/view/labeledwidget.cpp
    src/view/panels/functionmodegroup.cpp
    src/view/panels/processmodegroup.cpp
    src/view/panels/videosettingsgroup.cpp
    src/view/panels/mediaplayerwidget.cpp
    src/view/panels/mediapreviewpanel.cpp
    src/view/panels/progresspanel.cpp
    src/view/panels/basicsettingspanel.cpp
    src/view/panels/advancedsettingspanel.cpp

    src/controller/appcontroller.cpp
    src/controller/qtdialogservice.cpp
)

target_include_directories(resr_ui PUBLIC ${RESR_INCLUDE_DIRS})
target_link_libraries(resr_ui PUBLIC resr_core Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
target_link_libraries(resr_ui PRIVATE resr_warnings)

# ---------------------------------------------------------------------------
# 可执行程序
# ---------------------------------------------------------------------------
add_executable(real-esrguisoft WIN32
    main.cpp
    resources/resources.qrc
)
# 应用版本号只在 project(VERSION ...) 维护一处，注入 main.cpp 供界面展示
target_compile_definitions(real-esrguisoft PRIVATE
    RESR_APP_VERSION=\"${PROJECT_VERSION}\")
target_link_libraries(real-esrguisoft PRIVATE resr_ui)
target_link_libraries(real-esrguisoft PRIVATE resr_warnings)

if(WIN32)
    set_target_properties(real-esrguisoft PROPERTIES WIN32_EXECUTABLE ON)
endif()

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(RESR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# UI 壳层 / 主题系统 / 共享控件库（ui-shell 任务追加，勿重排上方内容）
# ---------------------------------------------------------------------------
target_sources(resr_ui PRIVATE
    src/view/theme/theme_tokens.cpp
    src/view/theme/theme_manager.cpp

    src/view/widgets/icon_provider.cpp
    src/view/widgets/section_title.cpp
    src/view/widgets/hint_label.cpp
    src/view/widgets/status_badge.cpp
    src/view/widgets/action_button.cpp
    src/view/widgets/card_widget.cpp
    src/view/widgets/toggle_switch.cpp
    src/view/widgets/segmented_control.cpp
    src/view/widgets/app_page_header.cpp
    src/view/widgets/sidebar_nav.cpp
)

# 应用图标（Windows 可执行文件图标，16/32/48/256 多尺寸 .ico）
if(WIN32)
    enable_language(RC)
    set(RESR_APP_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resr_app.rc" @ONLY)
    target_sources(real-esrguisoft PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/resr_app.rc")
    target_compile_definitions(real-esrguisoft PRIVATE RESR_HAS_APP_ICON=1)
endif()

# ---------------------------------------------------------------------------
# 第三方库接入（libs 任务追加，勿重排上方内容）
#   stacklog：src/common/logger.cpp 以兼容层方式使用
#   singleproc：main.cpp 单实例守卫与二次启动门铃
# ---------------------------------------------------------------------------
if(TARGET stacklog::stacklog)
    target_link_libraries(resr_core PRIVATE stacklog::stacklog)
endif()
if(TARGET singleproc::singleproc)
    target_link_libraries(real-esrguisoft PRIVATE singleproc::singleproc)
endif()

# ---------------------------------------------------------------------------
# 应用级用户设置（settings 任务追加，勿重排上方内容）
#   engine/path 等 QSettings 读写由 Common 层提供；新增 .cpp 单独追加，
#   避免与上方 add_library 列表产生编辑冲突。
# ---------------------------------------------------------------------------
target_sources(resr_core PRIVATE
    src/common/appsettings.cpp
)
