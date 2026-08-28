#!/usr/bin/env bash
# 交叉编译 libsgzzlb.dll（Windows 版核心动态库）。
# 供 Windows 打包套件使用：产物位于 windows/libsgzzlb.dll。
#
# 依赖：MinGW-w64 交叉编译器（POSIX 线程模型，保证 std::thread 可用）
#   Debian/Ubuntu:  sudo apt install g++-mingw-w64-x86-64-posix
# 可用环境变量：CXX 指定编译器（默认 x86_64-w64-mingw32-g++）。
set -euo pipefail

cd "$(dirname "$0")/.."

CXX="${CXX:-x86_64-w64-mingw32-g++}"
STRIP="${STRIP:-x86_64-w64-mingw32-strip}"
OBJDIR="build/win"
OUT="windows/libsgzzlb.dll"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "[错误] 未找到 $CXX" >&2
    echo "请先安装：sudo apt install g++-mingw-w64-x86-64-posix" >&2
    exit 1
fi

mkdir -p "$OBJDIR" "$(dirname "$OUT")"

# 逐文件编译到独立 obj 目录，避免与 Linux 构建产物冲突
for src in core/*.cpp; do
    obj="$OBJDIR/$(basename "${src%.cpp}").o"
    echo "编译 $src"
    "$CXX" -std=c++17 -O2 -fPIC -Wall -Wextra -Icore -c "$src" -o "$obj"
done

# -static：把 libstdc++ / libgcc / winpthread 静态链进 DLL →
# 交付单文件 DLL，目标机器无需安装 MinGW 运行时。
echo "链接 $OUT"
"$CXX" -shared -static -o "$OUT" "$OBJDIR"/*.o

if command -v "$STRIP" >/dev/null 2>&1; then
    "$STRIP" --strip-unneeded "$OUT" 2>/dev/null || true
fi

echo "完成：$OUT"
file "$OUT"
