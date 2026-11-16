find_package(Qt6 REQUIRED COMPONENTS Test)

# ---------------------------------------------------------------------------
# 原生处理管线单元 + 冒烟测试（纯逻辑 + 真实引擎单图/批量/视频短片段）
# ---------------------------------------------------------------------------
add_executable(resr_tests
    test_pipeline.cpp
)

target_link_libraries(resr_tests PRIVATE resr_core Qt6::Test)
target_link_libraries(resr_tests PRIVATE resr_warnings)

# 崩溃降级重试回归用假引擎（仅 Windows）：首次调用以 0xC0000409 崩溃，重试成功。
# 它不启动真实引擎/GPU，只用于 tests/test_pipeline.cpp::image_engine_crash_retry()。
if(WIN32)
    add_executable(resr_fake_engine_crash fake_engine_crash.cpp)
    target_link_libraries(resr_fake_engine_crash PRIVATE shell32)
    target_link_libraries(resr_fake_engine_crash PRIVATE resr_warnings)
    add_dependencies(resr_tests resr_fake_engine_crash)
endif()

add_test(NAME resr_tests COMMAND resr_tests)
# 默认只跑非引擎用例：真实 GPU 引擎用例在未设置 RESR_TEST_ENGINE=1 时 QSKIP
# （见 tests/test_pipeline.cpp 的 engine_available()）。这里不注册 engine label，
# 避免 CI/开发机默认 ctest 启动 realesrgan（静态审查 A1）。
set_tests_properties(resr_tests PROPERTIES TIMEOUT 600 LABELS unit)

# 真实 GPU 引擎冒烟用例：默认不注册，显式 -DRESR_BUILD_ENGINE_TESTS=ON 才开启，
# 且运行环境需 RESR_TEST_ENGINE=1 + 可用 Vulkan GPU。ci.toml 默认 -LE engine
# 再兜底一层（静态审查 A1/B4）。
if(RESR_BUILD_ENGINE_TESTS)
    add_test(NAME resr_engine_tests COMMAND resr_tests
        image_single_native_smoke
        image_batch_native_smoke
        video_single_native_smoke
        stop_semantics)
    set_tests_properties(resr_engine_tests PROPERTIES
        TIMEOUT 600
        LABELS engine
        ENVIRONMENT "RESR_TEST_ENGINE=1")
endif()

# ---------------------------------------------------------------------------
# 媒体基础设施（libav*：探测 / 拆帧 / 编码）冒烟测试
# ---------------------------------------------------------------------------
add_executable(resr_media_tests
    test_media.cpp
)

target_link_libraries(resr_media_tests PRIVATE resr_media Qt6::Test)
target_link_libraries(resr_media_tests PRIVATE resr_warnings)

add_test(NAME resr_media_tests COMMAND resr_media_tests)
set_tests_properties(resr_media_tests PROPERTIES TIMEOUT 180)

# ---------------------------------------------------------------------------
# 界面语言基础设施（LanguageManager 纯函数 / QSettings 持久化 / .qm 缺失回退）
#   不依赖 resources.qrc：测试目标无英文翻译资源，正好覆盖回退中文路径。
# ---------------------------------------------------------------------------
add_executable(resr_i18n_tests
    test_i18n.cpp
)

target_link_libraries(resr_i18n_tests PRIVATE resr_core Qt6::Test)
target_link_libraries(resr_i18n_tests PRIVATE resr_warnings)

add_test(NAME resr_i18n_tests COMMAND resr_i18n_tests)
set_tests_properties(resr_i18n_tests PROPERTIES TIMEOUT 120)

# ---------------------------------------------------------------------------
# 四条链路验收工具（用 input/ 真实素材完整跑通，手动执行，不注册进 ctest）
#   build/tests/resr_pipeline_verify.exe [--out <输出根目录>]
# ---------------------------------------------------------------------------
add_executable(resr_pipeline_verify
    pipeline_verify.cpp
)

target_link_libraries(resr_pipeline_verify PRIVATE resr_core)
target_link_libraries(resr_pipeline_verify PRIVATE resr_warnings)
