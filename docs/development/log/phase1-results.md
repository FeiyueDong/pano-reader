# PANO Phase 1 实施结果

日期：2026-11-27
依据：[`3.PANO_Phase1_Application_Foundation_and_Library.md`](../3.PANO_Phase1_Application_Foundation_and_Library.md)
数据格式：[`docs/specification/phase1-data-formats.md`](../../specification/phase1-data-formats.md)

## 1. 构建与运行

```text
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64
xmake -j4
```

结果：`build ok`，生成 `build/linux/x86_64/debug/pano-reader`、四个探针和示例目标。
归档的 Phase 0 工程在隔离目录中也能独立构建：

```text
cp -a demo /tmp/pano-demo-verify
cd /tmp/pano-demo-verify && xmake f -c -m debug --qt=... && xmake -j4
build ok
```

根工程只构建 `src/`，不链接 `demo/`。

运行：

```text
./build/linux/x86_64/debug/pano-reader
./build/linux/x86_64/debug/pano-reader <library>
```

## 2. 里程碑状态

| 里程碑 | 状态 | 说明 |
| --- | --- | --- |
| M00 Demo 归档 | 完成 | `src/tools/xmake.lua/README/文档/示例` 移到 `demo/`；根工程不依赖 Demo；Demo 在隔离目录构建通过 |
| M01 正式架构 | 完成 | `app/library/document/pdf/reader/annotation/note/storage/settings` 模块与正式 MainWindow 建立 |
| M02 数据格式 | 完成 | 书库、书籍、书架、标注、阅读状态、历史、本机配置 JSON；规范文档已补 |
| M03 启动与配置 | 完成 | 本机配置目录、上次书库/标签恢复、书库选择欢迎页 |
| M04 导入 | 完成 | 拷贝/映射、哈希识别、重复提示、封面、元信息；批量导入与拖拽入口 |
| M05 源文件保护 | 完成 | 源文件只读、哈希校验、映射重定位、缺失源保留元数据 |
| M06 多级书架 | 完成 | 单父级、多归属、循环拒绝、删除中间书架提升子书架 |
| M07 收藏与回收站 | 完成 | 收藏、软删除、恢复、永久删除及确认 |
| M08 书库界面 | 完成 | 三栏布局、导航树、搜索/排序、空状态、详情与操作 |
| M09 会话与标签页 | 基本完成 | 每书一个会话、重复打开复用、库标签不可关闭、关闭/切换保存 |
| M10 阅读基线 | 完成 | 连续分页、页码、缩放、旋转、统一坐标、异步渲染与缓存、全部标注工具 |
| M11 保存策略 | 基本完成 | 2 s 防抖、30 s 上限、库操作立即写、关闭前 flush；未模拟磁盘写失败 |
| M12 设置窗口 | 完成 | 界面/数据/阅读/标注/快捷键页；主题与字号即时生效 |
| M13 主题 | 完成 | 现代浅色、经典、暖色、现代深色；阅读器状态不随主题重建 |
| M14 数据可靠性 | 完成 | QSaveFile、QLockFile、外部修改检测、严格 schema 校验 |
| M15 笔记基线 | 完成 | Markdown 预览/编辑、图片、截图、本地公式、标注锚点、撤销 |

## 3. 自动检查

### 3.1 数据与书库

```text
./build/linux/x86_64/debug/pano-library-probe
```

覆盖并通过：创建书库、拷贝导入、封面、标题/哈希、源文件可用、哈希查找、重复导入、
多级书架与成员、删除中间书架、收藏/回收站/恢复、外部 `library.json` 篡改拒绝且文件
不变、第二实例锁拒绝、重开后的外部修改检查、映射导入、缺失源保留元数据、异哈希
源拒绝、永久删除。

最终输出：`library probe passed`。

### 3.2 坐标与旋转

```text
./build/linux/x86_64/debug/pano-coordinate-probe
```

最终输出：

```text
coordinate probe passed: 4 rotations x 3 zooms x 5 points, with scroll (137,281)
```

覆盖 4 种旋转 × 3 种缩放 × 5 个页内点（四角 + 内部点），使用非零滚动，独立计算
页面 → 内容 → 视图坐标并检查逆变换。

### 3.3 主界面、会话与持久化

```text
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-phase1-probe \
  examples/sample-paper.pdf build/probe/phase1-ui.png
```

覆盖并通过：创建书库、导入、封面/托管源、重复导入、书架、收藏、打开主窗口、打开书、
PDF 加载、页码、标注、笔记、保存、重开、标注/笔记/收藏恢复、阅读状态（第 2 页、
150% 缩放、90°）恢复。

最终输出：`phase1 probe passed (0 failures)`。

### 3.4 主题

```text
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-theme-probe
```

切换暖色/现代深色并检查 `qApp->styleSheet()`，保存主题截图；最终输出
`theme probe passed (0 failures)`。

## 4. 视觉检查

以下截图由真实主窗口离屏渲染后人工查看，不是“编译通过”的替代：

- `docs/development/log/phase1-library.png`：三栏书库、真实封面、书名/作者/大小/模式/
  源路径、标注与笔记计数、操作按钮。
- `docs/development/log/phase1-reader.png`：PDF 页面、选中矩形、标注/属性面板、笔记
  预览（标题、正文、行内公式）、页码/缩放/旋转/缓存状态。
- `docs/development/log/phase1-theme-warm.png`、`phase1-theme-dark.png`、
  `phase1-theme-reader.png`：暖色与深色主题下的书库和阅读界面。

检查结论：

- 书库三栏、封面、详情和阅读标签页布局可用。
- 阅读页在 86% 缩放下居中显示，标注与文字/笔记面板不遮挡页面。
- 深色主题下正文、列表和书库文字可读；Markdown 预览的代码/表格背景已改为跟随
  palette，不再固定浅色。
- 左侧标注栏在 1440×900 下仍偏窄，长标题会截断；右侧笔记栏约 380 px，可接受但
  未做窄窗口专门适配。

## 5. 验收项状态（V01–V16）

| 项 | 状态 | 证据/说明 |
| --- | --- | --- |
| V01 首次启动/取消/新建/重启 | 部分 | 欢迎页与新建/打开入口已实现；新建流程未做逐步自动测试 |
| V02 加载/切换/无权限/无效库 | 部分 | `library` 探针覆盖无效库、锁、外部修改；UI 切换失败保留旧库的逻辑已加，未逐步手动验收 |
| V03 单个/批量拷贝、映射导入 | 通过（数据层） | `library` 探针覆盖 copy/reference、封面、哈希 |
| V04 移动源、重定位、异哈希 | 通过（数据层） | 缺失源、异哈希、`sources.json` 覆盖路径均有探针 |
| V05 多级书架/多归属/删除 | 通过（数据层） | 三层书架、成员、删除中间书架提升子级 |
| V06 收藏/软删除/恢复/永久删除 | 通过（数据层） | 探针覆盖；UI 确认框未逐步手动验收 |
| V07 封面/列表/筛选/排序/详情 | 部分 | 视觉检查封面/详情；搜索、排序、路径导航未做自动交互测试 |
| V08 多书会话/切换/重复打开 | 部分 | 标签复用、阅读状态探针通过；两书同时切换未自动测试 |
| V09 多页/滚动/缩放/旋转/定位 | 部分 | 坐标探针与截图通过；真实鼠标滚动/旋转组合仍需人工 |
| V10 扫描 PDF 绘制/Shift/样式/删除保护 | 未验证 | 绘制工具从 Phase 0 继承；本轮未用扫描 PDF 做视觉验收 |
| V11 笔记/图片/截图/公式/切换 | 部分 | 笔记新建/编辑/持久化探针通过；截图与插图未逐步自动测试 |
| V12 延迟/关闭/写入失败 | 部分 | 2 s 防抖、30 s 上限、关闭 flush 已实现；未模拟磁盘满/只读 |
| V13 第二实例/外部 JSON | 通过 | `library` 探针覆盖锁与外部篡改 |
| V14 主题/字体/侧栏/窗口 | 部分 | 暖色/深色视觉检查；字体调节和窗口尺寸组合未穷举 |
| V15 大 PDF/快速切换/DPI | 未验证 | 示例 PDF 仅 3 页；高 DPI/大文档需人工 |
| V16 重启恢复 | 部分 | 阅读状态与打开标签恢复已实现并有探针；完整重启流程未逐步手动验收 |

## 6. 已知限制

- 只在 Linux 上构建和运行，Windows 目标未实际验证。
- 文字选择限单页；扫描版 PDF 没有 OCR；不能导出 PDF。
- 公式渲染为常用 TeX 子集，不支持矩阵、颜色、对齐环境等。
- 橡皮为点击式删除，连续擦除未实现。
- 左侧标注栏在 1440×900 下偏窄，长标题会截断；未做更小窗口适配。
- `note_tree` 目前保留为空数组；笔记正文附在标注上。
- 未做完整 V01–V16 自动化和真实鼠标验收；本记录只声明已执行的检查。

## 7. 结论

Phase 1 的正式工程、书库数据闭环、书库界面、多标签阅读、标注/笔记基线、主题与
本机配置已经建立并通过数据/坐标/UI/主题探针。剩余工作主要是人工视觉验收、Windows/
高 DPI/大文档验证，以及磁盘写入失败等边界场景的专项测试。
