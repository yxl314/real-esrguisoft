# ============================================================================
# FindFFmpeg.cmake - 查找 FFmpeg 的 libav* 开发库
#
# 查找顺序（同一来源一次找齐，不混用不同安装来源的库）：
#   1. pkg-config：系统已安装 FFmpeg 开发包时零构建直接使用（推荐，MSYS2
#      UCRT64 自带 FFmpeg 9.x 开发包即可满足）；
#   2. 手动安装前缀：-DFFMPEG_ROOT=<prefix>，以及 third_party/ffmpeg 下
#      build-ffmpeg.ps1 / build-ffmpeg.sh 产出的 build-<平台>/install。
#
# 输出变量：
#   FFMPEG_FOUND        - 是否找到
#   FFMPEG_INCLUDE_DIRS - 头文件目录
#   FFMPEG_LIBRARIES    - 需要链接的库（含正确链接顺序）
#   FFMPEG_LIBRARY_DIRS - 额外库目录
#   FFMPEG_VERSION      - libavcodec 版本（pkg-config 来源时可用）
# 输出导入目标：
#   FFmpeg::FFmpeg      - INTERFACE 目标，携带头文件名与链接库
# ============================================================================

include(FindPackageHandleStandardArgs)

set(_RESR_FFMPEG_COMPONENTS avformat avcodec avutil swscale swresample)

# ---------------------------------------------------------------------------
# 1. 优先 pkg-config（系统 FFmpeg 开发包）
# ---------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(RESR_FFMPEG_PC QUIET
        libavformat libavcodec libavutil libswscale libswresample)
endif()

if(RESR_FFMPEG_PC_FOUND)
    set(FFMPEG_INCLUDE_DIRS ${RESR_FFMPEG_PC_INCLUDE_DIRS})
    set(FFMPEG_LIBRARY_DIRS ${RESR_FFMPEG_PC_LIBRARY_DIRS})
    # LINK_LIBRARIES 是带路径的库（CMake >= 3.12），优先使用
    if(DEFINED RESR_FFMPEG_PC_LINK_LIBRARIES
       AND NOT "${RESR_FFMPEG_PC_LINK_LIBRARIES}" STREQUAL "")
        set(FFMPEG_LIBRARIES ${RESR_FFMPEG_PC_LINK_LIBRARIES})
    else()
        set(FFMPEG_LIBRARIES ${RESR_FFMPEG_PC_LIBRARIES})
    endif()
    if(DEFINED RESR_FFMPEG_PC_libavcodec_VERSION)
        set(FFMPEG_VERSION ${RESR_FFMPEG_PC_libavcodec_VERSION})
    endif()
endif()

# ---------------------------------------------------------------------------
# 2. 手动安装前缀（third_party/ffmpeg 构建产物或自定义 FFMPEG_ROOT）
# ---------------------------------------------------------------------------
if(NOT FFMPEG_LIBRARIES)
    set(_RESR_FFMPEG_ROOTS "")
    if(FFMPEG_ROOT)
        list(APPEND _RESR_FFMPEG_ROOTS "${FFMPEG_ROOT}")
    endif()
    file(GLOB _RESR_FFMPEG_BUILD_ROOTS
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/build-*/install"
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/build/install"
        "${CMAKE_SOURCE_DIR}/third_party/ffmpeg/install")
    list(APPEND _RESR_FFMPEG_ROOTS ${_RESR_FFMPEG_BUILD_ROOTS})

    foreach(_root IN LISTS _RESR_FFMPEG_ROOTS)
        # 优先使用构建产物自带的 .pc 文件：--static 能自动带上静态库的全部依赖
        # （zlib、lzma、ws2_32 等），避免静态链接时漏库。
        # 注意：pkg-config 只搜索 PKG_CONFIG_PATH/PKG_CONFIG_LIBDIR，必须把
        # ${_root}/lib/pkgconfig 临时注入搜索路径，否则在系统未安装 FFmpeg
        # 开发包时会错误地报告“未找到”（回退路径形同虚设）。
        if(PKG_CONFIG_FOUND AND EXISTS "${_root}/lib/pkgconfig")
            if(CMAKE_HOST_WIN32)
                set(_RESR_FFMPEG_PC_PATH_SEPARATOR ";")
            else()
                set(_RESR_FFMPEG_PC_PATH_SEPARATOR ":")
            endif()
            set(_RESR_FFMPEG_SAVED_PKG_CONFIG_PATH "$ENV{PKG_CONFIG_PATH}")
            set(ENV{PKG_CONFIG_PATH}
                "${_root}/lib/pkgconfig${_RESR_FFMPEG_PC_PATH_SEPARATOR}$ENV{PKG_CONFIG_PATH}")
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs
                        libavformat libavcodec libavutil libswscale libswresample
                OUTPUT_VARIABLE _RESR_FFMPEG_STATIC_LIBS
                RESULT_VARIABLE _RESR_FFMPEG_PC_RESULT
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            set(ENV{PKG_CONFIG_PATH} "${_RESR_FFMPEG_SAVED_PKG_CONFIG_PATH}")
            if(_RESR_FFMPEG_PC_RESULT EQUAL 0
               AND NOT "${_RESR_FFMPEG_STATIC_LIBS}" STREQUAL "")
                separate_arguments(_RESR_FFMPEG_STATIC_LIST UNIX_COMMAND
                    "${_RESR_FFMPEG_STATIC_LIBS}")
                set(FFMPEG_INCLUDE_DIRS "${_root}/include")
                set(FFMPEG_LIBRARIES ${_RESR_FFMPEG_STATIC_LIST})
                set(FFMPEG_LIBRARY_DIRS "")
                message(STATUS "FFmpeg: 使用构建产物 ${_root}（pkg-config 静态链接）")
                break()
            endif()
        endif()

        find_path(FFMPEG_AVCODEC_INCLUDE_DIR libavcodec/avcodec.h
            PATHS "${_root}"
            PATH_SUFFIXES include
            NO_DEFAULT_PATH)
        if(NOT FFMPEG_AVCODEC_INCLUDE_DIR)
            continue()
        endif()

        set(_candidate_libraries "")
        set(_missing_component FALSE)
        foreach(_component IN LISTS _RESR_FFMPEG_COMPONENTS)
            find_library(FFMPEG_${_component}_LIBRARY
                NAMES ${_component}
                PATHS "${_root}"
                PATH_SUFFIXES lib lib64
                NO_DEFAULT_PATH)
            if(NOT FFMPEG_${_component}_LIBRARY)
                set(_missing_component TRUE)
                break()
            endif()
            list(APPEND _candidate_libraries "${FFMPEG_${_component}_LIBRARY}")
        endforeach()
        if(_missing_component)
            continue()
        endif()

        set(FFMPEG_INCLUDE_DIRS "${FFMPEG_AVCODEC_INCLUDE_DIR}")
        set(FFMPEG_LIBRARIES ${_candidate_libraries})
        set(FFMPEG_LIBRARY_DIRS "")
        break()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 3. 结果处理与导入目标
# ---------------------------------------------------------------------------
set(_RESR_FFMPEG_REASON "未找到 FFmpeg 开发库（libavformat / libavcodec / libavutil / libswscale / libswresample）。")
string(APPEND _RESR_FFMPEG_REASON
    "\n  · 推荐：安装系统 FFmpeg 开发包并确保 pkg-config 可发现，例如 MSYS2 UCRT64：\n"
    "      pacman -S mingw-w64-ucrt-x86_64-ffmpeg\n"
    "  · 或：运行 third_party/ffmpeg/build-ffmpeg.ps1（Windows）/ build-ffmpeg.sh（Linux）\n"
    "    构建静态库后，用 -DFFMPEG_ROOT=<构建产物 install 目录> 重新配置。\n"
    "  · 详见 docs/design/ffmpeg-integration.md。")

find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS FFMPEG_INCLUDE_DIRS FFMPEG_LIBRARIES
    VERSION_VAR FFMPEG_VERSION
    REASON_FAILURE_MESSAGE "${_RESR_FFMPEG_REASON}")

if(FFMPEG_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFMPEG_LIBRARIES}")
    if(FFMPEG_LIBRARY_DIRS)
        set_target_properties(FFmpeg::FFmpeg PROPERTIES
            INTERFACE_LINK_DIRECTORIES "${FFMPEG_LIBRARY_DIRS}")
    endif()
endif()

mark_as_advanced(
    FFMPEG_INCLUDE_DIRS
    FFMPEG_LIBRARIES
    FFMPEG_LIBRARY_DIRS
    FFMPEG_AVCODEC_INCLUDE_DIR)
