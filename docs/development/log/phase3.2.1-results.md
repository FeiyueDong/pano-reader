# PANO Phase 3.2.1 笔记数据安全与稳定化结果

## 1. 基线与范围

| 项 | 值 |
| --- | --- |
| 本轮起点 | `2c161d5`（Phase 3.2 第二次提交） |
| 需求文档 | `docs/development/10.PANO_Phase3_2_1_Note_Data_Safety_and_Stabilization.md` |
| 文档分析基线 | `1df0712`（Windows 构建修复），当前检出中没有该提交 |
| 正式构建 | C++20 / Qt 6.12.0 Widgets + PDF + SVG / xmake |
| 测试数据 | `/tmp/pano-note-probe` 临时书库；未写入 `~/Documents/pano` |
| 源 PDF | 只读；测试使用 `examples/sample-paper.pdf` 的拷贝 |

本轮只做修复，没有新增阅读功能、没有改变 schema 2 字段、没有修改 `demo/`。正式目标的
模块划分见 `src/buildcfg/*.lua`。

## 2. 修复内容

### R01 草稿与自动保存隔离

- `DocumentSession::saveNow()` 只保存正式数据，不再调用
  `ReaderWorkspace::commitPendingNote()`。
- 主窗口保存、标签切换、普通自动保存都不会提交 `NoteEditDialog` 草稿。
- 关闭标签、关闭书籍、切换书库、退出程序前统一调用
  `ReaderWorkspace::prepareToClose()`；用户选择保存/放弃/取消，取消会中止生命周期操作。
- 关闭后保存失败不会继续销毁会话或关闭窗口。

### R02 一次提交、先写后发布

- 新增 `DocumentSession::NoteDraft` 和 `commitNoteDraft()`。
- 提交步骤：构造候选 Annotation/Note/NoteTree → 校验 → 检查
  `annotations.json` 指纹 → `LibraryStore::writeReadingData()` → 成功后发布模型和一
  个 `ReadingSnapshotCommand`。
- 写入失败或外部修改时模型、撤销栈和文件都保持原状，编辑器草稿保留。
- 保存无标题笔记时规范为“未命名笔记”；正文允许为空。
- 重复保存相同标题/正文不产生冗余撤销命令。

### R03 编辑器目标绑定

- `ReaderWorkspace` 通过 `ensureNoteDialog()` 只创建一个编辑器，所有入口共用。
- 同一笔记再次打开只激活窗口，不重新加载正文，未保存内容不会丢失。
- 切换到不同笔记/新建笔记前，编辑器显式询问保存、放弃或取消。
- 旧草稿中的关联不会覆盖保存时的最新关联；编辑正文不改变当前关联和树位置。
- 笔记被删除、撤销或外部修改时拒绝保存并保留草稿。

### R04 定位笔记与取消

- `PdfView` 的 Note 工具不再立即创建 NotePoint，而是发出
  `notePointRequested(page, point, style)`。
- 首次保存时才一次性创建 NotePoint、Note、树节点和关联；取消、Esc、关闭窗口或拒绝
  新建不会留下任何正式对象或撤销项。
- 普通标注添加笔记取消时只保留原标注，不创建 Note 或错误关联。

### R05 跨模型事务

- `commitReadingEdit()` 捕获完整 Annotation/Note/NoteTree 前态；变更失败或后态校验
  失败时恢复模型，不推送撤销命令。
- 新增 `deleteAnnotationAndNote()`，删除普通标注及关联笔记时使用一个撤销命令。
- 删除普通标注默认保留笔记并解除关联；删除笔记保留普通标注；删除定位点笔记会清理
  专用 NotePoint。
- 笔记树移动在变更前校验整棵子树深度，失败不改变父节点、顺序或 dirty 状态。

### R06 移除预览覆盖源码

- 删除“重新生成正文”按钮、槽和信号。预览不能反向改写 Markdown 源码。
- 编辑模式、分栏模式、预览模式切换均保留原始 Markdown。

### R07 资源路径与导出安全

- `MarkdownRenderer` 新增 `resolveBookImage()`：URL 解码一次，拒绝绝对路径、盘符、
  UNC、`..` 越界、符号链接逃逸、非普通文件、超过 32 MiB 的附件，以及超过 8000 px
  或 4000 万像素的图片。
- 合法图片限制最大显示宽度；缺失/损坏/被阻止图片显示明确占位和 warning。
- 导出文件名清理 Windows 保留名、尾随点和空格；拒绝符号链接、已存在文件和目录。
- 单篇导出拒绝写入原书籍目录；目录导出拒绝写入原书籍目录或覆盖已有 `assets`。
- 单篇导出复制附件到 `<名称>.assets`，保留 TeX 源码，不把代码块中的示例当成链接。

### R08 摘录保留

- 新建笔记先构造 Markdown 引用块（转义 `\`、`$` 等），再加载编辑器；状态刷新只更新
  标题/关联信息，不重新加载正文。
- 文字标注摘录在打开编辑器后仍保留；区域截图继续写入书库 `assets/` 并使用相对路径。

### R09 单次预览

- `MarkdownPreview::setRenderedMarkdown()` 接受已经渲染的结果；编辑器不再先渲染一次、
  再由预览重复渲染。
- 标题/关联变化只刷新状态，不替换正在编辑的正文。

### R10 未提交附件生命周期

- `NoteEditDialog` 记录 `m_draftAssets`；保存成功后并入已提交集合。
- 取消/放弃/关闭未保存草稿时，通过 `DocumentSession::discardDraftAssets()` 删除本次
  独占且未被任何正式笔记引用的附件。
- 已保存附件不会被误删；`isPathInside(asset, assetsRoot)` 做了方向校验。

### R11 双向关联

- 标注页工具栏增加“关联已有笔记…”和“解除关联”；候选列表附带笔记 ID 后缀，避免同名
  笔记歧义。
- 笔记页工具栏增加“关联到所选标注”和“解除关联”；两侧解除语义一致。
- 普通标注解除关联只改关系；定位点解除关联会删除专用 NotePoint；已有占用关系不会被
  静默覆盖。
- 关联、解除、删除均通过会话事务进入同一个撤销栈。

### R12 虚拟根、同名分组和稳定 ID

- 笔记树增加虚拟根节点（书籍标题），根不可重命名、移动、删除；根不写入
  `annotations.json`，UserRole 为空。
- 展开状态只在首次构建后记录，刷新不会把用户折叠的树重新展开。
- 移动目标对话框使用真实节点 ID，并排除自身/后代；同名分组移动按 ID 定位。
- 树节点移动、重命名、排序、删除失败时不改变正式顺序。

### R13 模块化 xmake

- `src/xmake.lua` 只包含 `src/buildcfg/*.lua`；`common`、`pdf`、`annotation`、`note`、
  `library`、`reader-view`、`app`、`reader` 各自定义 target。
- 目标名、依赖、Qt 模块、rpath、MOC/qrc 和四个根选项保持不变。
- 默认构建仍只产出正式程序与七个内部库，不包含 tests/tools/demo。

### R14 依赖与平台说明

- 正式 `pano-note` 使用系统 MD4C 0.6 和本地 JKQTMathText（默认
  `build/deps/jkqt/usr`，可用 `JKQT_ROOT` 覆盖）。
- `docs/THIRD_PARTY_NOTICES.md` 记录 MIT/LGPL 归属；`AGENTS.md` 记录新数据安全约束。
- Windows 本轮未实际构建，相关项明确标为未验证，不用旧阶段结果代替。

## 3. 构建与探针

### 默认构建

```bash
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 \
  --build_tests=n --build_tools=n --build_demo=n --build_markdown_demo=n
xmake clean
xmake -j4
```

结果：`build ok, spent 14.717s`，仅正式库和 `pano-reader`。`pano-reader --help` 在
offscreen Qt 平台下退出码 0；带 PDF 的 4 秒离屏 smoke 为预期超时 124，未崩溃。

### 探针构建

```bash
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 --build_tests=y
xmake -j4
```

八项探针在最终修复后全部通过：

| 探针 | 实际结果 |
| --- | --- |
| `pano-library-probe` | `library probe passed` |
| `pano-coordinate-probe` | `coordinate probe passed: 4 rotations x 3 zooms x 5 points, with scroll (137,281)` |
| `pano-text-geometry-probe` | `text geometry probe passed` |
| `pano-phase1-probe` | `phase1 probe passed (0 failures)` |
| `pano-phase2-probe` | `phase2 probe passed (0 failures)` |
| `pano-interaction-probe` | 生成文本/区域选区截图，退出码 0 |
| `pano-note-probe` | `note probe passed (0 failures)` |
| `pano-theme-probe` | `theme probe passed (0 failures)` |

`pano-note-probe` 新增覆盖：

- 空正文草稿写入后模型才发布；外部修改指纹时拒绝并保持文件字节不变。
- 定位笔记的 NotePoint + Note 原子创建；`deleteAnnotationAndNote` 一次删除并可撤销。
- 取消定位草稿不留下 Note、NotePoint 或附件。
- 同一笔记再次打开保留未保存正文；切换模式保留 Markdown 源码。
- 无变化的草稿保存成功但不新增撤销命令，也不重复创建笔记。
- 摘录转义后保留；“重新生成正文”入口不存在。
- Markdown 图片 `../` 越界和符号链接逃逸被阻止。
- 导出拒绝覆盖；导出保留 TeX。
- 虚拟根唯一且不可编辑；31 层树深度移动拒绝整棵子树；同名分组按 ID 移动。
- 双向 link/unlink 及一对一约束。

## 4. V01–V34 状态

| 编号 | 状态 | 说明 |
| --- | --- | --- |
| V01 | 部分 | 正式自动保存不提交草稿的代码路径已改；未做真实等待自动保存的 GUI 计时 |
| V02 | 部分 | 主窗口保存不再提交草稿；未用真实菜单逐项回放 |
| V03 | 通过（离屏） | 保存继续、取消、无变化保存；“确定成功才关闭”由代码路径与探针覆盖 |
| V04 | 未验证 | 未模拟磁盘满/权限失败；使用外部指纹冲突覆盖同类失败分支 |
| V05 | 通过（离屏） | 临时库外部追加字节后保存被拒绝，文件和模型不变 |
| V06 | 未验证 | 未构造阅读数据成功而书签写入失败的组合故障 |
| V07 | 部分 | 同笔记再打开保留正文；脏草稿切换目标弹窗未自动化点击 |
| V08 | 部分 | 保存只提交标题/正文、保留最新关联；未在编辑中实际解除/更换关联后保存 |
| V09 | 部分 | 删除/撤销后保存会在会话层拒绝；未逐项做 GUI 冲突矩阵 |
| V10 | 部分 | 关闭标签/书籍/书库/退出统一 prepareToClose；真实窗口关闭未逐项点击 |
| V11 | 通过（离屏） | 取消、关闭定位草稿无新 NotePoint/Note/树节点/undo |
| V12 | 通过（离屏） | 取消普通标注笔记不改变原标注 |
| V13 | 通过（离屏） | NotePoint、Note、树、关联一次提交；失败/取消无半成品 |
| V14 | 通过（离屏） | 31 层树移动整棵子树被拒绝，父节点和顺序不变 |
| V15 | 通过（离屏） | 删除普通标注+笔记后一次撤销恢复几何、正文、关联和图位置 |
| V16 | 通过（离屏） | 普通标注保留笔记、笔记保留标注、定位点解除清理 NotePoint |
| V17 | 通过（离屏） | 无“重新生成正文”；模式切换保留公式/代码/图片/表格源码 |
| V18 | 部分 | 合法本地图片路径和缺失占位逻辑存在；中文/空格附件未单独构造 |
| V19 | 通过（Linux） | `../`、符号链接逃逸被拒绝；盘符/UNC 仅代码分支，未在 Windows 实测 |
| V20 | 通过（离屏） | 禁止写入原书籍目录、拒绝覆盖已有文件/目录；未覆盖只读介质 |
| V21 | 部分 | 三种导出可用且保留 TeX；附件可打开与代码块链接回归未逐项人工检查 |
| V22 | 部分 | 文字摘录保留；区域截图保存路径未与编辑器取消组合测试 |
| V23 | 部分 | 关联/标题刷新不覆盖正文、单次渲染代码路径已改；未统计渲染次数 |
| V24 | 通过（离屏） | 未提交剪贴板图片取消后删除 |
| V25 | 未验证 | 保存后追加附件再取消的组合未自动化 |
| V26 | 通过（离屏，API） | 两侧选择器均已实现；link/unlink、一对一拒绝通过，真实点击未自动化 |
| V27 | 部分 | 关联标识、定位按钮、图标已实现；过滤/折叠下逐项工具状态未全测 |
| V28 | 通过（离屏） | 虚拟根唯一、不可编辑、不持久化；全部折叠后的刷新保持未单独测试 |
| V29 | 通过（离屏） | 同名分组按 ID 移动；UI 选择框未用真实点击验证 |
| V30 | 通过（离屏） | schema 2 三集合、关联、树、旧 schema 拒绝、重开一致 |
| V31 | 通过 | 默认目标仅正式库+程序；tests 开关工作；模块 target 无重复编译 |
| V32 | 部分 | Linux 构建运行通过；Windows/MSVC 未验证 |
| V33 | 通过 | 八项探针全部执行成功；阅读器回归探针通过 |
| V34 | 通过 | 本文件、README、AGENTS 与实现同步 |

## 5. 视觉检查

- `docs/development/log/phase3.2.1-note-tree.png`：右侧笔记树、虚拟根、过滤、信息卡和
  Markdown 预览；右栏宽度约 380 px，内容换行正常。
- `docs/development/log/phase3.2.1-note-editor.png`：编辑器 1040×760，标题、格式工具栏、
  Markdown 源、公式预览、保存/确定/取消均可见；预览随主题/字号应用。
- 离屏截图只用于核对布局，不代替真实鼠标操作。真实拖拽/分区/输入法/高 DPI 仍需人工。

## 6. 已知限制

- Windows/MSVC 构建、Windows 保留名/UNC/盘符路径未实际验证。
- 磁盘写入失败、书签部分失败、保存后附件撤销等故障注入未完成。
- 笔记草稿没有跨进程崩溃恢复；应用被强杀时未提交正文和草稿附件不会保留。
- 未对超大附件复制、极长笔记、深层树渲染做压力测试。
- 阅读器大 PDF 打开仍约 2.3 秒，主要成本为全文件 SHA-256；本阶段未弱化内容哈希。
- 仍是本地单用户应用，不含同步、账号或协作。
