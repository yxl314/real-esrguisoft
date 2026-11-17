#!/bin/sh
# 项目检查入口：build/test、格式检查、审计、可选 Windows 交叉编译检查。
# 用法：./check.sh [all|ci|style|audit|windows]
#   windows：用 mingw-w64 的严格警告集交叉编译 Windows 分支（库 + 全部测试）；
#            未安装 mingw-w64 时打印跳过信息并返回 0，不算失败（不进 all）。
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT"
# 并行度自适应本机核数；显式给 JOBS=... 可覆盖
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
STAGE=${1:-all}
STATUS=0

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
        run python3 tests/coverage/run_coverage.py --repo "$ROOT"
        ;;
    windows)
        # Linux 常规构建不会编译平台端口；这里用 mingw-w64 交叉编译 Windows 端口、
        # 核心、unit（fake 端口）与 integration/smoke，只证明“能编译且无警告”。
        MINGW_CXX=${MINGW_CXX:-x86_64-w64-mingw32-g++}
        if ! command -v "$MINGW_CXX" >/dev/null 2>&1; then
            echo "mingw-w64 not installed; skipping Windows compile check"
        else
            WIN_DIR=build/windows-check
            rm -rf "$WIN_DIR"
            mkdir -p "$WIN_DIR"
            # 与项目自有告警口径一致；-Werror 把告警当失败，锁住“Windows 分支 0 告警”。
            WIN_WARN="-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-qual -Werror"
            run "$MINGW_CXX" -std=c++11 $WIN_WARN -Iinclude -Isrc -c src/stacklog.cpp -o "$WIN_DIR/stacklog.o"
            run "$MINGW_CXX" -std=c++11 $WIN_WARN -Iinclude -Isrc -c src/platform_win32.cpp -o "$WIN_DIR/platform_win32.o"
            run "$MINGW_CXX" -std=c++11 $WIN_WARN -Iinclude -Isrc -c tests/support/fake_platform.cpp -o "$WIN_DIR/fake_platform.o"
            # unit：核心 + fake 端口，不链接真实平台适配器。
            run "$MINGW_CXX" -std=c++11 $WIN_WARN -Iinclude -Isrc tests/unit/test_stacklog.cpp "$WIN_DIR/stacklog.o" "$WIN_DIR/fake_platform.o" -o "$WIN_DIR/test_stacklog.exe"
            # integration/smoke：核心 + Windows 平台适配器。
            for TEST_SRC in tests/integration/test_*.cpp tests/smoke/test_*.cpp; do
                TEST_NAME=$(basename "$TEST_SRC" .cpp)
                run "$MINGW_CXX" -std=c++11 $WIN_WARN -Iinclude -Isrc "$TEST_SRC" "$WIN_DIR/stacklog.o" "$WIN_DIR/platform_win32.o" -o "$WIN_DIR/$TEST_NAME.exe"
            done
        fi
        ;;
    *)
        echo "usage: $0 [all|ci|style|audit|coverage|windows]" >&2
        exit 2
        ;;
esac

exit "$STATUS"
