#!/usr/bin/env bash
set -euo pipefail
demo_root="$(cd "$(dirname "$0")/.." && pwd)"
repo_root="$(cd "$demo_root/../.." && pwd)"
deps="${PANO_MARKDOWN_DEPS:-$repo_root/build/deps}"
pkg="$deps/jkqtplotter.pkg.tar.zst"
out="$deps/jkqt"

if ! pkg-config --exists md4c-html; then
    echo "缺少 MD4C：请安装 md4c（Arch: sudo pacman -S md4c）" >&2
    exit 1
fi
if [ -f "$out/usr/include/jkqtmathtext/jkqtmathtext.h" ]; then
    echo "JKQTMathText 已存在: $out"
    exit 0
fi
mkdir -p "$deps"
url="${JKQT_PKG_URL:-}"
if [ -z "$url" ] && command -v pacman >/dev/null 2>&1; then
    url="$(pacman -Sp jkqtplotter 2>/dev/null | head -n1 || true)"
fi
if [ -z "$url" ]; then
    echo "无法确定 JKQTPlotter 包地址；请设置 JKQT_PKG_URL 指向 Arch 的 jkqtplotter 包。" >&2
    exit 1
fi
echo "下载 $url"
curl -L --fail -o "$pkg" "$url"
mkdir -p "$out"
tar --zstd -xf "$pkg" -C "$out"
echo "JKQTMathText 已解压到 $out"
echo "依赖目录：$out（根构建默认使用该路径；也可设置 JKQT_ROOT）"
