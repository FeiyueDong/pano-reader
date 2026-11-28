# PANO 第三方组件说明

PANO 正式程序在笔记预览中链接以下第三方组件；二者均以动态库方式使用。

## MD4C

- 版本：0.6.0（Linux 发行版包；开发头文件 `md4c.h`、`md4c-html.h`）
- 许可证：MIT
- 项目：https://github.com/mity/md4c
- 用途：将笔记 Markdown 解析为 CommonMark/GFM HTML，支持表格、删除线和 LaTeX 数学跨度。
- 源码中的 MIT 许可文本以上游仓库和发行版许可证文件为准。

## JKQTPlotter / JKQTMathText

- 版本：5.0.0git20251013-2（JKQTPlotter 发行包中的 `JKQTMathText` 子库）
- 许可证：LGPL-2.1-or-later
- 项目：https://github.com/jkriege2/JKQtPlotter
- 用途：公式离线排版，输出透明 ARGB 图像后嵌入 `QTextDocument`。仅链接
  `libJKQTMathText6_RelWithDebInfo.so` / `libJKQTCommon6_RelWithDebInfo.so`，不链接
  绘图组件。
- 完整 LGPL 文本随发行包提供，通常位于
  `/usr/share/licenses/jkqtplotter/LICENSE`。

构建时默认在 `build/deps/jkqt/usr` 查找 JKQT 前缀；也可以通过 `JKQT_ROOT` 指向其他
已安装前缀。依赖准备脚本：`demo/markdown/tools/fetch-deps.sh`。

## Qt

Qt 6（Widgets、Gui、Core、Pdf、Svg）按 Qt 自身许可证使用；本项目不修改 Qt 源码，
也不静态链接 Qt。

## Lucide 图标

`resources/icons/` 中的图标来自 Lucide，ISC 许可证与 Feather MIT 归属见
`resources/icons/LICENSE-lucide`。
