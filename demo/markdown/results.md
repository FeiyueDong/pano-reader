# PANO Phase 3.0 Markdown + 数学渲染验证结果

- 验证日期：2026-11-28
- 平台：Arch Linux x86_64，g++ 16.2.1，xmake 3.1.2
- Qt：6.12.0 SDK（`/home/yue/Qt/6.12.0/gcc_64`）
- MD4C：0.6.0-2
- JKQTPlotter：5.0.0git20251013-2（LGPL-2.1-or-later，动态链接）
- 工程：`demo/markdown/`，target `pano-markdown-demo`（以下 `-P` 命令为 Phase 3.0 历史记录；Phase 3.1 起由根 `xmake.lua` 统一包含）

## 构建与运行

```bash
cd demo/markdown
export JKQT_ROOT=/mnt/workspace/CodeFiles/Tools/pano-reader/build/phase3-deps/jkqt/usr
xmake f -P . -c -m debug
xmake -P . -j4
./build/linux/x86_64/debug/pano-markdown-demo
```

从干净目标重建（已安装依赖、3 个源文件，`xmake -P . -r -j4`）：

```
build ok, spent 2.551s
```

“默认依赖路径”也已验证：在临时副本中把 JKQT 包放到 `build/deps`、执行 `tools/fetch-deps.sh`，随后不带 `JKQT_ROOT` 执行 `xmake f -P . -c -m debug && xmake -P . -j4`，构建成功。

## 验证结论

**有条件采用。**

MD4C + JKQTMathText + QTextDocument 的管线在本地 Qt Widgets 中可稳定工作：CommonMark/GFM 基础排版、表格、删除线、代码、本地图片、行内/显示公式、矩阵/分段/对齐公式、错误回退均可运行；公式图片透明、可随主题换色，失败可见且保留原始 TeX。

但“有条件”：

1. 正式集成需固定 Qt 小版本与 JKQT 构建 ABI；本次 JKQT 由 Qt 6.12.1 构建、实际加载 6.12.0，仅验证可运行。
2. JKQTMathText 是 LaTeX 子集，不能承诺完整 TeX；复杂宏、颜色、完整 `align` 等不保证。
3. 行内公式基线和行高、表格内公式的窄列适配仍是视觉折扣；窄窗口依赖横向滚动。
4. LGPL-2.1+ 动态链接和许可证声明必须在正式发布物中保留。
5. Windows/MSVC、真实高 DPI 和长时间大文档输入仍需后续验收。
6. 正式笔记预览应保留“公式失败显示源码”的回退，不应让用户数据因渲染失败而丢失。

## V01–V09 状态

| 编号 | 项目 | 状态 | 证据 / 说明 |
| --- | --- | --- | --- |
| V01 | 依赖与构建 | 通过 | MD4C 0.6.0、JKQT 5.0.0git 动态链接；独立 xmake clean/rebuild 成功；`fetch-deps.sh` 路径验证 |
| V02 | 基础 Markdown | 通过 | 标题、粗体/斜体、删除线、行内代码、链接、嵌套列表、引用、GFM 表格、代码块、硬换行、UTF-8 |
| V03 | 公式识别 | 通过 | 样本 11 个公式全部识别；代码块内 `$`/`$$`、行内代码与 `\$100` 不误判 |
| V04 | 基础公式 | 通过 | `E=mc^2`、欧拉恒等式、希腊字母、分式/根号、阻尼振动方程 |
| V05 | 专业公式 | 通过（目视） | 矩阵、分段函数、`aligned`、括号/分式/根号渲染正确；对齐是接近而非 TeX 级精确 |
| V06 | 混合显示 | 部分 | 正文/公式/表格/图片同屏可用；行内公式基线与行高可接受但不完全一致；表格窄列公式偏小 |
| V07 | 阅读适配 | 部分 | 9–26 pt、浅/深色、520/1360 宽可用；低于约 464 px 时编辑/预览最小宽度会阻止继续缩窄；窄窗口公式缩放并告警，表格横向滚动；真实高 DPI 未验证 |
| V08 | 错误与性能 | 通过 | 缺图、外链阻止、未闭合 `$$`、未知命令、超长公式均不崩溃；错误红字 + 原始 TeX + 状态计数；样本 11 公式约 19 ms（12 pt），边缘样本 1–8 ms |
| V09 | 跨平台/许可/协议 | 部分 | Linux 通过；Windows 未验证；MD4C MIT、JKQT LGPL-2.1+ 动态链接；未改动正式数据协议 |

## 关键公式

- 行内：`E=mc^2`、`e^{i\pi}+1=0`、`\alpha+\beta=\gamma`、表格中的 `\nabla\cdot\vec{E}=\rho/\varepsilon_0`
- 显示：`m\ddot u+c\dot u+ku=0`
- 积分/求和：`\int_0^1 x^2\,dx`、`\sum_{i=1}^n i^2`
- 矩阵：`\begin{bmatrix}...\end{bmatrix}`
- 分段：`\begin{cases}...\end{cases}`
- 对齐：`\begin{aligned}...\end{aligned}`
- 括号：`\left(\frac{a}{b}\right)^2+\sqrt{x^2+y^2}`

矩阵样本中，`=` 与 `\begin{bmatrix}` 必须放在同一行；否则 MD4C 会把单独一行 `=` 当作 setext 标题分隔符，导致 `$$` 内容被拆开。这是 MD4C 行为，不是数学渲染器缺陷，样例已按此修正。

## 性能

| 场景 | 解析 | 数学 | 公式数 | 失败 | 缺图 |
| --- | --- | --- | --- | --- | --- |
| `sample.md` 12 pt 浅色 | 0 ms | 19 ms | 11 | 0 | 0 |
| `sample.md` 12 pt 深色 | 0 ms | 19–32 ms | 11 | 0 | 0 |
| `sample.md` 18 pt | 0 ms | 约 49 ms | 11 | 0 | 0 |
| `edge-cases.md` | 0 ms | 1–8 ms | 2 | 2 | 2（含外链阻止） |

时间来自窗口状态栏，含该次渲染的全部公式；不同机器/字体缓存会有波动。长文档尚未做压力测试。

## 截图

- [浅色主界面](docs/demo-light.png)
- [深色主题](docs/demo-dark.png)
- [矩阵/分段/对齐/图片](docs/demo-formulas.png)
- [错误与边界](docs/demo-edge.png)
- [520 px 窄窗口](docs/demo-narrow.png)
- [18 pt 字号](docs/demo-font18.png)

## 错误与边界实测

- 缺失本地图片：红色 `图片缺失: ...`，状态栏 `缺图` 增加。
- 外部图片：阻止加载，红色 `图片已阻止: https://...`，计入缺图/警告。
- 未闭合 `$$`：MD4C 当作普通文本，保留原始内容，不崩溃。
- 未知 TeX：JKQT `getErrorList()` 非空，显示 `[公式渲染失败：...]` 和原始 TeX。
- 超长公式：按可用宽度缩放并记录“公式过宽”警告；520 px 窗口仍可读。
- HTML 注入：`MD_FLAG_NOHTML` 使 `<script>` 作为纯文本显示。

## 已知限制

- 行内公式为图片，基线与纯文本不完全对齐；高倍字号/某些字体下会更明显。
- 表格内公式受列宽限制，窄窗口依赖表格横向滚动。
- 无 Markdown 语法高亮、无公式编辑辅助、无目录/搜索。
- 不读取远程图片，不执行脚本，不做 HTML 渲染。
- 未验证 Windows、MSVC、真实 125%/150% 缩放屏、超大文档和极端长公式。
- JKQT 版本需随发行物固定；LGPL 动态链接和声明是采用前提。

## 复现命令

```bash
cd demo/markdown
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-markdown-demo \
  --screenshot build/final-light.png
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-markdown-demo \
  --dark --screenshot build/final-dark.png
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-markdown-demo \
  --file examples/edge-cases.md --screenshot build/edge2.png
```

截图和结果只使用演示样本与本地图片，未读写正式书库或用户 PDF。
