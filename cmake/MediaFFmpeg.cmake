# ============================================================================
# MediaFFmpeg.cmake - 媒体基础设施构建模块
#
# 职责：
#   1. 通过 cmake/FindFFmpeg.cmake 定位 libav*（系统 pkg-config 优先）；
#   2. 暴露 resr_media_ffmpeg INTERFACE 目标（FFmpeg 头文件 + 链接库）；
#   3. 编译 src/infrastructure/media/ 下的媒体封装层为 resr_media 静态库，
#      并挂到业务核心库 resr_core 上（上层无需单独链接）。
#
# 在根 CMakeLists.txt 的 resr_core 定义之后 include 本模块。
# ============================================================================

find_package(FFmpeg REQUIRED)

# 对外暴露的 FFmpeg 依赖目标：上层链接它即获得头文件与 libav* 链接
add_library(resr_media_ffmpeg INTERFACE)
target_link_libraries(resr_media_ffmpeg INTERFACE FFmpeg::FFmpeg)

# 媒体封装层（探测 / 拆帧 / 编码）
add_library(resr_media STATIC
    src/infrastructure/media/mediaerror.cpp
    src/infrastructure/media/mediaframe.cpp
    src/infrastructure/media/ffmpegutils.cpp
    src/infrastructure/media/mediaprobe.cpp
    src/infrastructure/media/framedecoder.cpp
    src/infrastructure/media/videoencoder.cpp
)

target_include_directories(resr_media PUBLIC ${RESR_INCLUDE_DIRS})
target_link_libraries(resr_media PUBLIC resr_media_ffmpeg Qt6::Core Qt6::Gui)
target_link_libraries(resr_media PRIVATE resr_warnings)

# 挂到业务核心库：Model 层通过 resr_core 即可使用媒体能力
if(TARGET resr_core)
    target_link_libraries(resr_core PUBLIC resr_media)
endif()
