#!/bin/sh
# 项目检查入口：build/test、格式检查、审计、覆盖率实测、Windows 交叉编译检查。
# 用法：./check.sh [all|ci|style|audit|coverage|windows]
#
# coverage 阶段用 gcov JSON 实测单元测试对公开接口/公开函数分支的覆盖率，
# 低于 100% 时非零退出（详见 tests/coverage/run_coverage.py）。
#
# windows 阶段只在显式调用时运行，不进入 all：它用 mingw-w64 交叉编译器以
# 严格告警集编译并链接 Windows 平台实现、Windows 集成测试、单元测试（假端口）
# 与冒烟测试，证明“可编译且无警告”；它不证明运行期正确（运行期只有真实
# Windows 能验证）。没有 mingw-w64 时打印跳过信息并返回 0。
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
# 并行度自适应本机核数；显式给 JOBS=... 可覆盖
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
STAGE=${1:-all}
STATUS=0
# Windows 交叉编译器；可用环境变量覆盖。
MINGW_CXX=${MINGW_CXX:-x86_64-w64-mingw32-g++}

run() {
    echo "[check] $*"
    "$@"
    rc=$?
    if [ "$rc" -eq 130 ]; then
        exit 130
    fi
    if [ "$rc" -ne 0 ]; then
        # 配置/工具错误(2)比普通失败(1)更需要保留，不能被后续步骤覆盖。
        if [ "$rc" -eq 2 ] || [ "$STATUS" -eq 2 ]; then
            STATUS=2
        else
            STATUS=1
        fi
    fi
}

# Windows 交叉编译检查：严格告警 + 链接，但不执行。
# 注意：Windows 平台文件在 Linux 构建里根本不参与编译，clang-tidy/ASan/
# Valgrind 都不覆盖它们；这个阶段是 Windows 代码静态质量的最低保障。
windows_compile_check() (
    set -e
    out_dir=${WINDOWS_CROSS_DIR:-build/windows-cross}
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    flags="-std=c++11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
-Wsign-conversion -Wold-style-cast -Wcast-qual -Werror -Iinclude -Isrc -Itests"

    echo "[windows] compiler: $("$MINGW_CXX" --version | head -n 1)"
    "$MINGW_CXX" $flags -c src/singleproc.cpp -o "$out_dir/singleproc.o"
    "$MINGW_CXX" $flags -c src/platform_win32.cpp -o "$out_dir/platform_win32.o"
    "$MINGW_CXX" $flags -c tests/unit/test_singleproc.cpp -o "$out_dir/test_singleproc.o"
    "$MINGW_CXX" $flags -c tests/support/fake_platform.cpp -o "$out_dir/fake_platform.o"
    "$MINGW_CXX" $flags -c tests/integration/test_win32.cpp -o "$out_dir/test_win32.o"
    "$MINGW_CXX" $flags -c tests/integration/test_win32_connect_race.cpp -o "$out_dir/test_win32_connect_race.o"
    "$MINGW_CXX" $flags -c tests/smoke/test_guard_lifecycle.cpp -o "$out_dir/test_guard_lifecycle.o"
    "$MINGW_CXX" $flags -c tests/smoke/test_second_instance.cpp -o "$out_dir/test_second_instance.o"

    # 链接成 Windows 可执行文件：只证明符号可解析，不在 Linux 上运行。
    "$MINGW_CXX" "$out_dir/singleproc.o" "$out_dir/fake_platform.o" "$out_dir/test_singleproc.o" \
        -o "$out_dir/singleproc_unit_tests.exe"
    "$MINGW_CXX" "$out_dir/singleproc.o" "$out_dir/platform_win32.o" "$out_dir/test_win32.o" \
        -o "$out_dir/singleproc_win32_integration.exe" -ladvapi32
    "$MINGW_CXX" "$out_dir/test_win32_connect_race.o" \
        -o "$out_dir/singleproc_win32_connect_race_integration.exe" -ladvapi32
    "$MINGW_CXX" "$out_dir/singleproc.o" "$out_dir/platform_win32.o" "$out_dir/test_guard_lifecycle.o" \
        -o "$out_dir/singleproc_guard_lifecycle_smoke.exe" -ladvapi32
    "$MINGW_CXX" "$out_dir/singleproc.o" "$out_dir/platform_win32.o" "$out_dir/test_second_instance.o" \
        -o "$out_dir/singleproc_second_instance_smoke.exe" -ladvapi32
    echo "[windows] compile + link OK (runtime not executed on Linux)"
)

windows_stage() {
    if ! command -v "$MINGW_CXX" >/dev/null 2>&1; then
        echo "mingw-w64 not installed; skipping Windows compile check"
        return 0
    fi
    windows_compile_check
}

coverage_stage() {
    run python3 tests/coverage/run_coverage.py --root "$ROOT" --jobs "$JOBS"
}

case "$STAGE" in
    all)
        run python3 ../cxx_ci/check.py all --root "$ROOT" --jobs "$JOBS"
        run env PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml --jobs "$JOBS" all
        ;;
    ci)
        run python3 ../cxx_ci/check.py ci --root "$ROOT" --jobs "$JOBS"
        ;;
    style)
        run python3 ../cxx_ci/check.py style --root "$ROOT" --jobs "$JOBS"
        ;;
    audit)
        run env PYTHONPATH=../cxx_audit/src python3 -m cpp_audit -c audit.toml --jobs "$JOBS" all
        ;;
    coverage)
        coverage_stage
        ;;
    windows)
        run windows_stage
        ;;
    *)
        echo "usage: $0 [all|ci|style|audit|coverage|windows]" >&2
        exit 2
        ;;
esac

exit "$STATUS"
