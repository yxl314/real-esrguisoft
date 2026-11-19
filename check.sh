#!/bin/sh
# 项目检查入口（POSIX）：ci/style 委托 cxx_ci/check.py，audit 委托 cxx_audit。
# 用法：./check.sh [all|ci|style|audit] [--jobs N]
#   all（默认）= ci + style + audit；Windows 无 sh，直接跑 cxx_ci/check.py。
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT" || exit 2

STAGE=all
JOBS=0
while [ $# -gt 0 ]; do
	case "$1" in
		all|ci|style|audit)
			STAGE=$1
			;;
		--jobs)
			[ $# -ge 2 ] || { echo "check.sh: --jobs 需要参数" >&2; exit 2; }
			JOBS=$2
			shift
			;;
		-h|--help)
			sed -n '2,4p' "$0"
			exit 0
			;;
		*)
			echo "check.sh: 未知参数 $1（可用：all|ci|style|audit|--jobs N）" >&2
			exit 2
			;;
	esac
	shift
done

STATUS=0
merge()
{
	rc=$1
	[ "$rc" -eq 0 ] && return 0
	if [ "$STATUS" -eq 0 ] || [ "$rc" -gt "$STATUS" ]; then
		STATUS=$rc
	fi
	return 0
}

run_ci() { python3 ../cxx_ci/check.py ci --root . --jobs "$JOBS"; }
run_style() { python3 ../cxx_ci/check.py style --root . --jobs "$JOBS"; }
run_audit() { PYTHONPATH=../cxx_audit/src python3 -m cxx_audit -c audit.toml all; }

case "$STAGE" in
	ci)
		run_ci
		merge $?
		;;
	style)
		run_style
		merge $?
		;;
	audit)
		run_audit
		merge $?
		;;
	all)
		run_ci
		merge $?
		run_style
		merge $?
		run_audit
		merge $?
		;;
esac

exit "$STATUS"
