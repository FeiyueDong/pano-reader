# PANO 项目约定

本文件面向在本仓库工作的编码代理与贡献者。它补充 `README.md`，不替代产品文档。

## 构建、运行与视觉检查

用户已授权在本机对 PANO 进行构建、运行、截图和真实视觉检查：

```bash
# 默认：正式程序及内部静态库，不包含测试、诊断或 Demo
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 \
  --build_tests=n --build_tools=n --build_demo=n --build_markdown_demo=n
xmake -j4

# 功能探针 / 性能诊断
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 --build_tests=y
xmake -j4
xmake f -m debug --build_tools=y
```

Demo 默认关闭，由两个独立根选项按需包含：
`--build_demo=y`（Phase 0 PDF Demo，targets `pano-demo-*`）和
`--build_markdown_demo=y`（Phase 3.0 Markdown Demo，target `pano-markdown-demo`）。
局部 `demo/*/xmake.lua` 不是独立构建入口；修改 xmake 组织前后先用
`xmake f -c` 清理配置，必要时删除 `build/.deps`、`build/.gens`、`build/.objs`、
`build/.build_cache`，避免旧 target 缓存残留。

- 更改 Qt/C++ 代码后至少运行一次 `xmake -j4`；探针位于 `tests/`，需要
  `--build_tests=y`；涉及数据层时运行 `pano-library-probe`；涉及阅读器坐标时运行
  `pano-coordinate-probe`；涉及文字选区合并时运行 `pano-text-geometry-probe`；
  涉及主界面/会话时运行 `pano-phase1-probe`。性能诊断位于 `tools/`，需要
  `--build_tools=y`。
- 视觉检查使用真实运行或离屏探针生成的截图；不要把“编译通过”当作视觉验收。
- 探针是稀疏检查，不替代人工 GUI 测试。添加探针时优先覆盖数据安全、格式契约、
  坐标变换和持久化闭环，避免为每个控件写脆弱的 UI 测试。
- 修改 `demo/` 时注意它是归档代码，由根工程按选项包含；根正式 target 不链接
  Demo 源码。Phase 3.2 起 MD4C/JKQT 是正式笔记模块的正常依赖；关闭 Markdown Demo
  只排除 Demo 自身目标，不排除正式渲染模块。

## 数据安全（最高优先级）

- 源 PDF 始终只读；不修改、不覆盖、不把标注写回 PDF。
- 用户书库是真实数据：测试使用 `/tmp` 或 `build/probe/` 下的临时书库，不在仓库内
  或用户书库中写入测试数据。Phase 3.1 起新版笔记协议不兼容旧数据，禁止直接在
  `~/Documents/pano` 写入新格式；使用临时书库或经确认的新测试书库。
- 不自动迁移或重写旧 Demo `.pano` 数据；不静默修复格式错误。
- 写入 JSON 使用 `QSaveFile`；书库索引写入必须经过外部修改检查。
- 遇到哈希不一致、锁冲突、未知 `schema_version`、缺失清单时拒绝并提示，不能猜测。
- 删除书籍、带笔记的标注必须有明确确认；撤销必须恢复完整对象和笔记。

## 数据契约

- 持久化格式以 `docs/specification/phase1-data-formats.md` 为准。修改字段时同时更新
  实现、规范文档和探针。
- 标注坐标是未旋转页面 pt 坐标，左上原点；页面索引 0-based。不要把 viewport 坐标、
  旋转后坐标或 DPR 写入文件。
- 阅读/标注/书籍数据放在书库；主题、字体、窗口与源路径覆盖放在本机配置。

## 代码与提交

- C++20，命名空间 `pano`；Qt 6 Widgets + Qt PDF；不引入 WebEngine 或网络依赖。
- 保持模块边界：`app/` 界面，`library/` 书库，`document/` 会话，`pdf/` 渲染，
  `reader/` 视图，`annotation/` 标注，`note/` 笔记，`storage/` 持久化，`settings/` 配置。
- 修复问题优先定位根因，不用统一偏移、全局缩放或数据重写掩盖坐标错误。
- 不提交 `build/`、临时书库、截图缓存或个人路径。
- 文档使用中文；代码标识符和 JSON 字段使用英文。


## Phase 3 探针

- `pano-note-probe` 覆盖笔记 schema 2、一对一关联、笔记树、跨模型撤销、旧 schema
  拒绝、编辑器目标绑定，以及正式 MD4C + JKQTMathText 的矩阵/分段公式渲染。

## Phase 2 探针

- `pano-phase2-probe` 覆盖四种页面布局、缩放模式、复制粘贴、书签、搜索、全屏、连续
  橡皮、滚轮翻页冷却、Shift 两点路径和属性栏高度；涉及 `bookmarks.json`、
  `reading.json` 布局字段或阅读器交互时必须运行。
- 缩略图/全屏/查找相关改动还应目视检查 `build/probe/phase2-layout.png`、
  `phase2-thumbnails.png`、`phase2-search.png`、`phase2-fullscreen.png`。
- `pano-pdf-timing` 用于大 PDF 打开/首屏测量，例如
  `./build/linux/x86_64/debug/pano-pdf-timing "/path/to/book.pdf"`。

## Phase 3.1 笔记模型

- `annotations.json` 为 `schema_version: 2`，同时保存 `annotations`、`notes`、`note_tree`。
  旧 schema 1 必须拒绝读取并保持文件不变，不做自动迁移。
- 涉及笔记模型、笔记树、关联、撤销或书库统计时运行 `pano-note-probe`；涉及阅读器/标注
  回归时同时运行 Phase 1/2 探针。
- 新版笔记协议不兼容 `~/Documents/pano` 中已有旧数据；不要直接向用户书库写入新格式。
  测试使用临时书库或经确认的新测试书库，旧格式拒绝测试使用临时副本。
