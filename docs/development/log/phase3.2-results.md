# PANO Phase 3.2 结果记录

日期：2026-11-28  
基线：`c41f00d9055cfd169048960f8e78156c178971f9`（Phase 3.1）  
构建环境：Arch Linux、GCC 16.2.1、xmake 3.1.2、Qt 6.12.0 SDK  
阶段提交：`34716fb refactor: modularize build targets`（A 阶段）；B/C/D 阶段提交见文末。

## 1. 范围

本阶段同时完成两类工作：

1. 修复 Phase 3.1 遗留的关联交互、工具栏、笔记树、草稿和跨模型事务问题。
2. 将正式笔记编辑器升级为 MD4C + JKQTMathText 的专业 Markdown 编辑器，并把根构建重组为模块化静态库。

正式程序仍只使用 Qt Widgets / Qt PDF，不引入 WebEngine、网络依赖或新的笔记协议版本。测试只使用临时书库；未向 `~/Documents/pano` 写入测试数据，未修改任何源 PDF。

## 2. 构建组织（A 阶段）

根 `xmake.lua` 现在是唯一入口，只保留项目级设置、选项和局部配置包含关系。正式目标拆为：

- `pano-common`：`JsonUtils`、`TimeUtils`、`LibraryTypes`
- `pano-pdf`：PDF 加载与页面渲染
- `pano-annotation`：标注数据、几何、模型、命令、渲染
- `pano-note`：独立笔记、笔记树、Markdown/Math 渲染
- `pano-library`：书库索引、导入、删除、书架
- `pano-reader-view`：页面布局与阅读视图
- `pano-app`：主窗口、书库页、阅读工作区、设置、笔记编辑器
- `pano-reader`：`main.cpp` 与资源，仅链接 `pano-app`

测试与诊断目标分离：

- `tests/`：八个功能探针，`--build_tests=y` 才包含。
- `tools/`：两个计时诊断，`--build_tools=y` 才包含。
- `demo/`：PDF Demo 与 Markdown Demo 仍由独立选项包含，目标分别为 `pano-demo-*` 和 `pano-markdown-demo`。

默认目标列表只包含 `pano-common`、`pano-pdf`、`pano-annotation`、`pano-note`、`pano-library`、`pano-reader-view`、`pano-app`、`pano-reader`。修改构建组织前已按要求清理 `build/.deps`、`build/.gens`、`build/.objs`、`build/.build_cache` 和旧 Linux 构建目录，避免旧 target 缓存影响。

## 3. 功能修复（B 阶段）

- **B01**：文字选区和区域选区右键菜单补齐复制、保存、矩形、区域高亮和区域笔记动作；已有标注的命中菜单保留。
- **B02**：笔记树增加过滤、手动上移/下移、按标题/更新时间显示排序、根节点权限限制、空状态提示和分组删除提升子节点。展开状态和滚动位置在刷新后恢复。
- **B03**：增加当前笔记导出 Markdown、按笔记树层级导出目录、导出 JSON。单篇导出复制相对图片资源；目录导出生成分组目录和编号文件；导出使用 `QSaveFile`，拒绝不安全文件名。`pano-note-probe` 已覆盖三种导出。
- **B04**：切换非选择工具时清除对象选择，属性栏显示当前工具预设；选择工具无对象时显示提示，避免陈旧选择。
- **B05**：标注、文字、橡皮悬停使用状态提示，不写模型。
- **B06**：右侧详情/笔记面板默认宽度 380，限宽并自动换行。
- **B07**：导航历史记录来源标签（页码、目录、缩略图、搜索、书签、批注），前进/后退按钮 tooltip 显示目标和来源。
- **B08**：属性栏固定高度，切换工具/对象不改变 PDF viewport；Phase 2 探针覆盖高度与 viewport 稳定性。
- **B09**：缩放/拖动时使用已有的最佳尺寸缓存图像作为临时回退，再异步请求精确分辨率，减少空白闪烁。
- **B10**：大 PDF 打开计时见第 6 节；仍高于 2 s 目标，记录为 SHA-256 全文件校验与 Qt PDF 打开成本，未采用弱化内容身份的方式规避。
- **B11**：`pano-phase2-probe` 继续覆盖属性栏高度与历史；`pano-note-probe` 增加正式渲染、导出、编辑器模式/目标绑定检查。

## 4. 正式 Markdown/Math（C 阶段）

- `src/note/MarkdownRenderer.cpp` 已替换自研 Markdown 解析器，使用系统 MD4C 0.6.0：
  - `MD_DIALECT_GITHUB`：表格、删除线等 GFM 扩展；
  - `MD_FLAG_LATEXMATHSPANS`：`$...$` / `$$...$$` 数学片段；
  - `MD_FLAG_NOHTML`：原始 HTML 不执行。
- `<x-equation>` 转换为 `JKQTMathText` 生成的透明 ARGB 图像，经 `pano-math:///N` 资源嵌入 `QTextDocument`；失败时显示 `[公式渲染失败：…]` 并保留原始 TeX，不静默输出错误公式。
- 图片资源限制为书籍目录内的相对路径，经 `pano-image:///N` 资源加载；绝对路径、远程 URL 和越界路径拒绝，缺失图片显示占位。
- `MarkdownPreview` 关闭外部链接自动打开，按 `QPalette` 生成浅/深色 CSS；正文、标题、表格、代码、引用、公式和错误占位统一主题。
- 删除正式源码中旧的 `MathRenderer` 自研实现；归档 `demo/markdown` 保持独立，不链接正式源码。
- 依赖：系统 MD4C 0.6.0；JKQTMathText 5.0.0git20251013-2（JKQTPlotter 包），LGPL-2.1-or-later，动态链接；Qt 6.12.0。许可证与部署说明见 `docs/THIRD_PARTY_NOTICES.md`。

编辑器（E 阶段）：

- `NoteEditDialog` 扩展为 1040×760 非模态窗口，记住几何、分栏比例、字体字号和“仅编辑/分栏/仅预览”模式。
- 格式工具栏支持标题、加粗、斜体、列表、代码、公式，以及“更多”菜单中的删除线、有序列表、引用、分隔线、链接、表格、代码块。
- 轻量 `QSyntaxHighlighter` 只做编辑提示，不参与解析或持久化。
- 预览 250 ms 防抖，保留近似滚动位置；侧栏只读预览不显示未提交草稿。
- 剪贴板图片直接写入书籍 `assets/` 并插入相对 Markdown；文字摘录以引用块插入新笔记；区域截图沿用现有阅读器入口。
- 笔记树信息卡显示标题、关联页和创建/修改时间；关联/定位按钮显式操作，不以选择切换隐式提交草稿。

## 5. 自动验证

```bash
# 默认正式构建（最终记录）
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 \
  --build_tests=n --build_tools=n --build_demo=n --build_markdown_demo=n
xmake clean && xmake -j4
# build ok, spent 14.984 s；目标列表只含正式库与 pano-reader

# 功能探针
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 --build_tests=y
xmake -j4
```

八项探针在最终代码上全部通过：

| 探针 | 结果 |
| --- | --- |
| `pano-library-probe` | `library probe passed` |
| `pano-coordinate-probe` | `coordinate probe passed: 4 rotations x 3 zooms x 5 points, with scroll (137,281)` |
| `pano-text-geometry-probe` | `text geometry probe passed` |
| `pano-phase1-probe` | `phase1 probe passed (0 failures)` |
| `pano-phase2-probe` | `phase2 probe passed (0 failures)` |
| `pano-interaction-probe` | 文本/区域浮动操作、区域图像和矩形创建通过 |
| `pano-note-probe` | `note probe passed (0 failures)`，含 schema 2、关联、树、撤销、旧格式拒绝、编辑器模式/目标绑定、剪贴板图片、三种导出、正式公式渲染 |
| `pano-theme-probe` | `theme probe passed (0 failures)`，含设置预览/取消恢复 |

`pano-reader --help` 正常输出；离屏启动退出码 124 来自 `timeout`，日志只有 `propagateSizeHints()` 插件提示，没有崩溃或数据写入。

## 6. 大 PDF 打开计时（B10）

`pano-open-timing`，临时书库、参考导入、离屏 GUI 路径：

| 样本 | 页面 | 大小 | 导入 | 源 SHA-256 | PDF 打开 | 会话加载 | 窗口显示 | 打开+首屏 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `~/Documents/Primer C++(第5版).pdf` | 864 | 199 MB | ~1121 ms | ~1015 ms | ~1149 ms | ~2183 ms | ~2200 ms | ~2330 ms |
| `examples/sample-paper.pdf` | 3 | 126 KB | ~27 ms | 0 ms | ~2 ms | ~30 ms | ~47 ms | ~68 ms |

结论：当前打开大文件约 2.3 s，主要成本是全文件 SHA-256 校验（约 1 s）和 Qt PDF 打开（约 1.2 s）。本次未改为 mtime/size 等弱身份校验，也未引入异步哈希；P01/P02 的进一步优化留待后续阶段。

## 7. V01–V38 状态

状态说明：**通过**为本次实际执行；**部分**为实现/截图的局部验证；**未验证**为环境或交互无法在离屏探针中覆盖，需人工复核。

| 编号 | 状态 | 说明 |
| --- | --- | --- |
| V01 默认根构建 | 通过 | 最终默认目标仅正式库/程序；`pano-reader --help` 与离屏启动正常 |
| V02 按需构建测试 | 通过 | 八探针均构建、执行通过 |
| V03 独立诊断目标 | 通过（A 阶段配置） | `--build_tools=y` 曾构建并运行两个计时目标；最终默认配置不包含 |
| V04 Demo 组合 | 通过（A 阶段配置） | `pano-demo-*` 与 `pano-markdown-demo` 独立/组合构建已验证 |
| V05 模块边界 | 通过 | 库依赖无环，正式源码未重复编译；qrc/MOC/链接正常 |
| V06 跨平台 | 部分 | Linux/GCC 实际验证；Windows/MSVC 未验证 |
| V07 关联状态 | 部分 | 两侧使用真实 ID，树中关联笔记显示页码；关联图标未单独截图核对 |
| V08 双向建立关联 | 部分 | 标注→笔记与笔记→标注均可；自动化只覆盖编辑目标绑定 |
| V09 双向解除/重建 | 部分 | 解除/重建路径存在，未做完整 GUI 双向循环 |
| V10 双向定位 | 部分 | 树/编辑器定位路径存在，未逐项人工验证过滤/折叠状态 |
| V11 工具栏 | 部分 | 图标、禁用、菜单已实现；窄宽度人工检查有限 |
| V12 根节点权限 | 部分 | 根重命名/移动/删除在菜单与方法层拒绝；未做 GUI 点击核对 |
| V13 树状态 | 部分 | 展开与滚动位置已恢复；关联刷新后的选中项需人工复核 |
| V14 同名分组移动 | 部分 | 按节点 ID 移动、排除自身/后代；深度上限与失败回滚未自动化 |
| V15 草稿隔离 | 部分 | 新建笔记使用临时 ID，保存前不提交；长等待自动保存交互未覆盖 |
| V16 显式保存 | 部分 | 保存继续/确定/取消存在；人工流程未完整执行 |
| V17 保存失败 | 未验证 | 未模拟磁盘写入失败/外部修改后的草稿保留 |
| V18 重复打开 | 部分 | 脏窗口阻止切换目标；重复打开同一笔记未自动化 |
| V19 非模态会话 | 部分 | 非模态窗口可继续阅读；关闭书籍/程序的完整路径未逐项验证 |
| V20 外部变更冲突 | 未验证 | 笔记编辑期间删除/重命名/撤销的冲突流程未测试 |
| V21 取消定位笔记 | 部分 | 取消时清理新建 NotePoint/标注的代码路径存在；未覆盖全部入口 |
| V22 创建及撤销 | 部分 | 笔记探针覆盖删除/撤销；定位笔记创建-撤销全链路未单独断言 |
| V23 删除事务 | 部分 | 删除标注保留笔记、NotePoint 清理已覆盖；同时删除的双对象撤销未单独覆盖 |
| V24 关联回滚 | 部分 | 重复关联拒绝已覆盖；移动失败回滚未测试 |
| V25 协议与持久化 | 通过 | schema 2 重开三集合/顺序正确；schema 1 拒绝且不改写；源 PDF 未变 |
| V26 正式依赖 | 通过 | 默认构建链接 MD4C 与 JKQTMathText；`ldd` 可解析 SO |
| V27 常用 Markdown | 通过 | 标题、列表、引用、表格、代码、链接、中文均在截图与探针中验证 |
| V28 数学公式 | 通过 | 行内、分式、积分、矩阵、分段、多行公式；失败保留源文 |
| V29 解析边界 | 部分 | MD4C 禁用原始 HTML；代码/转义美元未单独自动化 |
| V30 安全资源 | 部分 | 远程/绝对图片拒绝、缺失占位；符号链接/UNC 未测试 |
| V31 主题及窄面板 | 部分 | 浅色与编辑器宽度已截图；深色编辑器、DPI、250–350 px 侧栏未逐项验证 |
| V32 编辑器三模式 | 通过 | 探针切换仅编辑/分栏/仅预览，验证源码控件可见性 |
| V33 格式工具 | 部分 | 工具栏和 More 菜单存在；各格式生成的 Markdown 未逐项断言 |
| V34 实时预览 | 部分 | 250 ms 防抖与滚动比例保留已实现；长文位置未专项测试 |
| V35 图文插入 | 部分 | 剪贴板图片已自动验证；本地图片/区域截图/扫描页人工路径未全测 |
| V36 附件取消/重开 | 未验证 | 草稿取消后的附件生命周期未测试 |
| V37 阅读器回归 | 通过 | phase1/phase2/interaction 探针通过；属性栏、导航、橡皮、Shift、Esc 未退化 |
| V38 文档/构建说明 | 通过 | 本记录、README、AGENTS、许可证和格式规范已更新 |

## 8. 截图

- `docs/development/log/phase3.2-note-tree.png`：右侧笔记树、过滤/排序、信息卡与只读预览。
- `docs/development/log/phase3.2-note-editor.png`：非模态编辑器、格式工具栏、分栏预览与 MathJax 风格公式。

## 9. 已知限制

- Windows/MSVC 构建、DLL 部署、高 DPI 未验证。
- B10 大 PDF 打开约 2.3 s，高于 2 s 目标；全文件哈希与 Qt PDF 打开成本仍在。
- V17/V20/V21/V36 等保存失败、外部冲突和附件生命周期仅实现路径，未自动化或人工全量验证。
- 公式基线使用 Qt 富文本行内图片，复杂公式的基线/行高仍有轻微差异；超宽公式按预览宽度缩放。
- 导出为 UI 路径，`pano-note-probe` 已覆盖单篇/目录/JSON；资源复制的跨平台符号链接边界未测试。
- 正式程序依赖系统 MD4C 与本地 JKQTMathText 前缀；分发前需复核 LGPL 动态链接与可替换库要求。

## 10. 结论

A 阶段模块化构建、B 阶段遗留交互修复、C 阶段 MD4C + JKQTMathText 正式集成和 D 阶段笔记编辑器/导出已落地；默认正式构建、八项探针和主要截图通过。剩余风险集中在 Windows/高 DPI、保存失败/外部冲突、附件取消和非模态会话的完整人工回归，不以“编译通过”替代这些验收。
