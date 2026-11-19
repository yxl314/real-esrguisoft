#!/usr/bin/env python3
"""公开接口与分支覆盖率实测脚本（Linux，gcov JSON）。

流程：
1. 用 --coverage -O0 构建 unit 测试目标（src/singleproc.cpp + fake 平台）；
2. 运行 unit 测试，生成 .gcda；
3. 用 `gcov -b --json-format` 解析 singleproc.cpp（以及公开头文件里的
   inline 函数）的函数/分支数据；
4. 汇总“公开函数”执行数与“公开函数内非异常分支”覆盖数，输出 x/x、y/y；
   未覆盖分支除非在 tests/coverage/branch_whitelist.txt 中给出理由，否则
   脚本非零退出。

公开函数 = include/singleproc/singleproc.hpp 里声明的 guard 公开成员；
其实现位于 src/singleproc.cpp。“异常分支”指 gcov 标记 throw=True 的分支，
按任务约定不计入分母；其余不可达分支必须写进白名单并给理由，不允许静默排除。
"""

from __future__ import annotations

import argparse
import gzip
import json
import shutil
import subprocess
import sys
from pathlib import Path

PUBLIC_PREFIX = "singleproc::guard::"
WHITELIST_NAME = "branch_whitelist.txt"


def log(message: str) -> None:
    """打印进度信息。"""
    print(f"[coverage] {message}", flush=True)


def run(command: list[str], cwd: Path) -> None:
    """执行命令，失败时抛出 SystemExit 并带出退出码。"""
    log("run: " + " ".join(command))
    completed = subprocess.run(command, cwd=str(cwd), check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def configure_and_build(root: Path, build_dir: Path, jobs: int, clean: bool) -> None:
    """配置并构建 unit 覆盖目标。"""
    if clean and build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    run(
        [
            "cmake",
            "-S",
            str(root),
            "-B",
            str(build_dir),
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DCMAKE_CXX_FLAGS=--coverage -O0 -g",
            "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
            "-DSINGLEPROC_BUILD_EXAMPLES=OFF",
        ],
        cwd=root,
    )
    run(["cmake", "--build", str(build_dir), "--target", "singleproc_unit_tests", "-j", str(jobs)], cwd=root)
    run(["ctest", "--test-dir", str(build_dir), "-L", "unit", "--output-on-failure"], cwd=root)


def find_gcda(build_dir: Path, source_name: str) -> Path:
    """在构建目录里查找指定源文件的 .gcda。"""
    for path in sorted(build_dir.rglob(source_name + ".gcda")):
        return path
    raise SystemExit(f"未找到 {source_name}.gcda：unit 目标可能没有带 --coverage 构建")


def load_gcov_json(gcda: Path) -> dict:
    """对 .gcda 运行 gcov JSON 并解析结果。"""
    gcda_dir = gcda.parent
    json_gz = gcda_dir / (gcda.name[: -len(".gcda")] + ".gcov.json.gz")
    if json_gz.exists():
        json_gz.unlink()
    run(["gcov", "-b", "--json-format", gcda.name], cwd=gcda_dir)
    if not json_gz.exists():
        raise SystemExit(f"gcov 没有生成 {json_gz.name}")
    with gzip.open(json_gz, "rt", encoding="utf-8") as handle:
        return json.load(handle)


def load_whitelist(path: Path, root: Path) -> dict[tuple[str, int], str]:
    """读取分支白名单：`相对路径:行号  # 理由`。"""
    entries: dict[tuple[str, int], str] = {}
    if not path.exists():
        return entries
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "#" not in line:
            raise SystemExit(f"{path.name}:{number}: 白名单项必须带理由（# ...）")
        location, reason = line.split("#", 1)
        location = location.strip()
        reason = reason.strip()
        if not reason:
            raise SystemExit(f"{path.name}:{number}: 白名单项必须带理由（# ...）")
        if ":" not in location:
            raise SystemExit(f"{path.name}:{number}: 位置必须形如 src/singleproc.cpp:123")
        file_part, line_part = location.rsplit(":", 1)
        try:
            line_number = int(line_part)
        except ValueError:
            raise SystemExit(f"{path.name}:{number}: 行号不是整数：{line_part}")
        entries[(file_part.replace("\\", "/"), line_number)] = reason
    return entries


def relative_source(path_text: str, root: Path) -> str:
    """把 gcov 记录的源文件路径归一成相对仓库根的形式。"""
    normalized = path_text.replace("\\", "/")
    try:
        return str(Path(normalized).resolve().relative_to(root.resolve())).replace("\\", "/")
    except ValueError:
        return normalized


def is_public_source(relative: str) -> bool:
    """判断该源文件是否包含公开接口实现。"""
    return relative == "src/singleproc.cpp" or relative.startswith("include/singleproc/")


def collect(data: dict, root: Path) -> tuple[int, int, int, int, list[tuple[str, int]]]:
    """统计公开函数/分支覆盖，返回 (函数已覆盖, 函数总数, 分支已覆盖, 分支总数, 未覆盖位置)。

    异常（throw）分支按约定从分母中剔除，单独计数不参与统计。
    """
    public_functions_covered = 0
    public_functions_total = 0
    public_branches_covered = 0
    public_branches_total = 0
    uncovered: list[tuple[str, int]] = []

    for file_entry in data.get("files", []):
        relative = relative_source(file_entry.get("file", ""), root)
        if not is_public_source(relative):
            continue
        demangled = {item.get("name", ""): item.get("demangled_name", "") for item in file_entry.get("functions", [])}
        for mangled, name in demangled.items():
            if not name.startswith(PUBLIC_PREFIX):
                continue
            public_functions_total += 1
            function = next(
                (item for item in file_entry.get("functions", []) if item.get("name", "") == mangled),
                None,
            )
            if function is not None and int(function.get("execution_count", 0)) > 0:
                public_functions_covered += 1

        for line_entry in file_entry.get("lines", []):
            function_name = demangled.get(line_entry.get("function_name", ""), "")
            if not function_name.startswith(PUBLIC_PREFIX):
                continue
            for branch in line_entry.get("branches", []):
                if branch.get("throw"):
                    continue
                public_branches_total += 1
                if int(branch.get("count", 0)) > 0:
                    public_branches_covered += 1
                else:
                    uncovered.append((relative, int(line_entry.get("line_number", 0))))

    return (
        public_functions_covered,
        public_functions_total,
        public_branches_covered,
        public_branches_total,
        uncovered,
    )


def main() -> int:
    """入口：构建 → 运行 → 统计 → 输出并返回退出码。"""
    parser = argparse.ArgumentParser(description="singleproc 公开接口/分支覆盖率实测（gcov JSON）")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2], help="仓库根目录")
    parser.add_argument("--build-dir", type=Path, default=None, help="覆盖构建目录（默认 <root>/build/coverage）")
    parser.add_argument("--jobs", type=int, default=4, help="构建并行度")
    parser.add_argument("--no-clean", action="store_true", help="复用已有覆盖构建目录")
    parser.add_argument("--build-only", action="store_true", help="只配置/构建/运行，不统计")
    args = parser.parse_args()

    root = args.root.resolve()
    build_dir = args.build_dir.resolve() if args.build_dir else root / "build" / "coverage"

    configure_and_build(root, build_dir, args.jobs, clean=not args.no_clean)
    if args.build_only:
        return 0

    gcda = find_gcda(build_dir, "singleproc.cpp")
    data = load_gcov_json(gcda)
    covered_functions, total_functions, covered_branches, total_branches, uncovered = collect(data, root)

    whitelist = load_whitelist(root / "tests" / "coverage" / WHITELIST_NAME, root)
    whitelisted: list[tuple[str, int, str]] = []
    failures: list[tuple[str, int]] = []
    for location in uncovered:
        reason = whitelist.get(location)
        if reason is None:
            failures.append(location)
        else:
            whitelisted.append((location[0], location[1], reason))

    print()
    log(f"public functions: {covered_functions}/{total_functions}")
    log(f"public branches:  {covered_branches}/{total_branches}")
    if whitelisted:
        log("whitelisted branches (未计入失败):")
        for file_name, line_number, reason in whitelisted:
            log(f"  {file_name}:{line_number}  # {reason}")
    if failures:
        log("uncovered public branches (非白名单):")
        for file_name, line_number in sorted(set(failures)):
            log(f"  {file_name}:{line_number}")
        return 1
    if covered_functions != total_functions:
        log("存在未执行的公开函数")
        return 1
    log("public functions 100%, public branches 100%（非白名单）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
