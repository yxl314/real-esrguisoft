#!/usr/bin/env python3
"""用 gcov JSON + gcov 文本分支数据实测 stacklog 公开接口覆盖率。

退出码：0 = 公开函数与公开分支均 100%；1 = 有缺口或测试失败。
平台适配器（src/platform_*.cpp）是内部端口实现，不在公开头 manifest 内；
其正确性由集成/冒烟测试与真实平台替身共同验证。
"""

import argparse
import gzip
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# 公开接口 manifest：文件（相对仓库根）+ 反解名正则。
PUBLIC_FUNCTIONS = [
    ("src/stacklog.cpp", r"^stacklog::init\("),
    ("src/stacklog.cpp", r"^stacklog::shutdown\(\)"),
    ("src/stacklog.cpp", r"^stacklog::set_level\("),
    ("src/stacklog.cpp", r"^stacklog::trace_scope::trace_scope\("),
    ("src/stacklog.cpp", r"^stacklog::trace_scope::~trace_scope\(\)"),
    ("include/stacklog/stacklog.hpp", r"^stacklog::trace_frame::trace_frame\(\)"),
    ("src/stacklog.cpp", r"^stacklog::trace_frame::valid\("),
    ("src/stacklog.cpp", r"^stacklog::capture_trace\(\)"),
    ("src/stacklog.cpp", r"^stacklog::restore_scope::restore_scope\("),
    ("src/stacklog.cpp", r"^stacklog::restore_scope::~restore_scope\(\)"),
    ("src/stacklog.cpp", r"^stacklog::detail::restore_trace_scope::restore_trace_scope\("),
    ("src/stacklog.cpp", r"^stacklog::detail::restore_trace_scope::~restore_trace_scope\(\)"),
    ("src/stacklog.cpp", r"^stacklog::detail::log_line::log_line\("),
    ("src/stacklog.cpp", r"^stacklog::detail::log_line::~log_line\(\)"),
    ("include/stacklog/stacklog.hpp", r"^stacklog::detail::log_line& stacklog::detail::log_line::operator<< <"),
    ("include/stacklog/stacklog.hpp", r"^stacklog::detail::log_line::operator<<\(std::ostream"),
]

LINE_RE = re.compile(r"^\s*([-0-9]+):\s*([0-9]+):(.*)$")
BRANCH_TAKEN_RE = re.compile(r"^branch\s+(\d+)\s+taken\s+(\d+)")
BRANCH_NEVER_RE = re.compile(r"^branch\s+(\d+)\s+never executed")


def run(command, cwd=None, capture=False):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=capture)
    if result.returncode != 0:
        if capture:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
        raise SystemExit("command failed: {}".format(" ".join(command)))
    return result


def relative(repo, path):
    try:
        return Path(path).resolve().relative_to(repo).as_posix()
    except ValueError:
        # 系统头/工具链文件不在仓库内，不参与公开接口统计。
        return None


def load_whitelist(repo):
    path = repo / "tests" / "coverage" / "whitelist.json"
    if not path.is_file():
        return set(), set()
    data = json.loads(path.read_text(encoding="utf-8"))
    lines = {(item["file"], int(item["line"])) for item in data.get("lines", [])}
    branches = {(item["file"], int(item["line"]), int(item["index"])) for item in data.get("branches", [])}
    return lines, branches


def build_and_run(repo, build_dir):
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)
    run([
        "cmake", "-S", str(repo), "-B", str(build_dir), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_CXX_FLAGS=--coverage -O0 -g",
        "-DCMAKE_EXE_LINKER_FLAGS=--coverage",
        "-DSTACKLOG_BUILD_TESTS=ON",
    ])
    run(["cmake", "--build", str(build_dir), "--target", "stacklog_unit"])
    unit = build_dir / "tests" / "stacklog_unit"
    if not unit.is_file():
        raise SystemExit("找不到单元测试可执行文件: {}".format(unit))
    result = subprocess.run([str(unit)], text=True, capture_output=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit("单元测试失败，无法统计覆盖率")
    return unit


def collect_json(repo, build_dir, json_dir):
    gcda_files = sorted((build_dir / "tests" / "CMakeFiles" / "stacklog_unit.dir").rglob("*.gcda"))
    if not gcda_files:
        raise SystemExit("没有找到 .gcda；覆盖率构建失败")
    if json_dir.exists():
        shutil.rmtree(json_dir)
    json_dir.mkdir(parents=True)
    for gcda in gcda_files:
        run(["gcov", "--json-format", str(gcda)], cwd=str(json_dir))
    file_lines = {}
    functions = []
    for archive in json_dir.glob("*.gcov.json.gz"):
        data = json.load(gzip.open(archive, "rt", encoding="utf-8"))
        for entry in data.get("files", []):
            rel = relative(repo, entry["file"])
            if rel is None:
                continue
            for line in entry.get("lines", []):
                key = (rel, int(line["line_number"]))
                file_lines[key] = max(file_lines.get(key, 0), int(line["count"]))
            for function in entry.get("functions", []):
                functions.append({
                    "file": rel,
                    "name": function.get("demangled_name") or function["name"],
                    "start": int(function["start_line"]),
                    "end": int(function["end_line"]),
                })
    return file_lines, functions


def collect_branches(repo, build_dir, branch_dir):
    gcda_files = sorted((build_dir / "tests" / "CMakeFiles" / "stacklog_unit.dir").rglob("*.gcda"))
    if branch_dir.exists():
        shutil.rmtree(branch_dir)
    branch_dir.mkdir(parents=True)
    for gcda in gcda_files:
        run(["gcov", "-b", "-c", str(gcda)], cwd=str(branch_dir))
    branches = {}
    for gcov_file in branch_dir.glob("*.gcov"):
        source = None
        current_line = None
        for raw in gcov_file.read_text(encoding="utf-8", errors="replace").splitlines():
            if source is None and "Source:" in raw:
                parts = raw.split("Source:", 1)
                source = relative(repo, parts[1].strip())
                continue
            match = LINE_RE.match(raw)
            if match:
                current_line = int(match.group(2))
                continue
            text = raw.strip()
            if current_line is None or source is None:
                continue
            taken = BRANCH_TAKEN_RE.match(text)
            never = BRANCH_NEVER_RE.match(text)
            if taken:
                index = int(taken.group(1))
                is_throw = "(throw)" in text
                key = (source, current_line, index)
                branches[key] = (max(branches.get(key, (0, False))[0], int(taken.group(2))), is_throw)
            elif never:
                index = int(never.group(1))
                is_throw = "(throw)" in text
                key = (source, current_line, index)
                branches[key] = (max(branches.get(key, (0, False))[0], 0), is_throw)
    return branches


def check_public(repo, file_lines, functions, branches, whitelist_lines, whitelist_branches):
    total_functions = len(PUBLIC_FUNCTIONS)
    covered_functions = 0
    total_branches = 0
    covered_branches = 0
    missing_report = []
    for public_file, pattern in PUBLIC_FUNCTIONS:
        compiled = re.compile(pattern)
        matches = [item for item in functions if item["file"] == public_file and compiled.match(item["name"])]
        if not matches:
            missing_report.append("{} 未在覆盖率数据中找到（函数未链接/未执行）".format(pattern))
            continue
        spans = sorted({(item["start"], item["end"]) for item in matches})
        missing_lines = set()
        missing_branches = []
        function_branches = 0
        function_branches_covered = 0
        for start, end in spans:
            for (path, line), count in file_lines.items():
                if path != public_file or line < start or line > end:
                    continue
                if count == 0 and (path, line) not in whitelist_lines:
                    missing_lines.add(line)
            for (path, line, index), (taken, is_throw) in branches.items():
                if path != public_file or line < start or line > end or is_throw:
                    continue
                if (path, line, index) in whitelist_branches:
                    continue
                function_branches += 1
                if taken > 0:
                    function_branches_covered += 1
                else:
                    missing_branches.append((line, index))
        total_branches += function_branches
        covered_branches += function_branches_covered
        if not missing_lines and not missing_branches:
            covered_functions += 1
        else:
            detail = matches[0]["name"]
            if missing_lines:
                missing_report.append("{} 未执行行: {}".format(detail, sorted(missing_lines)))
            if missing_branches:
                missing_report.append("{} 未覆盖分支: {}".format(detail, sorted(missing_branches)))
    print("public functions: {}/{}".format(covered_functions, total_functions))
    print("public branches: {}/{}".format(covered_branches, total_branches))
    if missing_report:
        for item in missing_report:
            print("  MISSING", item)
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description="stacklog 公开接口/gcov 分支覆盖率门禁")
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--build-dir", default=None)
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    build_dir = Path(args.build_dir).resolve() if args.build_dir else repo / "build" / "coverage"
    json_dir = repo / "build" / "coverage-json"
    branch_dir = repo / "build" / "coverage-gcov"
    build_and_run(repo, build_dir)
    file_lines, functions = collect_json(repo, build_dir, json_dir)
    branches = collect_branches(repo, build_dir, branch_dir)
    whitelist_lines, whitelist_branches = load_whitelist(repo)
    ok = check_public(repo, file_lines, functions, branches, whitelist_lines, whitelist_branches)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
