# PANO Phase 3.1 结果记录：独立笔记模型与笔记树

- 基线分析提交：`3c6a404389e78961027df4249cb63f26f8d71266`
- 开发方式：在 Phase 2.2 工作树上继续修改；本轮改动尚未提交。
- 范围：B01 统一 xmake 入口；N01–N05 独立笔记、关联、schema 2、笔记树与生命周期；
  U01–U03 右侧笔记页、独立编辑器和入口整合；V01–V28 验证与文档。

## 构建与入口（B01）

根目录 `xmake.lua` 现在是唯一正式构建入口。Demo 不参与默认构建，由两个独立选项控制：

```bash
# 默认：正式程序 + 正式探针，不含 Demo
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 \
  --build_demo=n --build_markdown_demo=n
xmake -j4

# Phase 0 PDF Demo
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 --build_demo=y
xmake -j4

# Phase 3.0 Markdown Demo
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64 --build_markdown_demo=y
xmake -j4
```

- Phase 0 目标已改名：`pano-demo-reader`、`pano-demo-coordinate-probe`、
  `pano-demo-preview-probe`、`pano-demo-math-probe`、`pano-demo-ui-probe`、
  `pano-demo-flow-probe`。
- Markdown Demo 目标：`pano-markdown-demo`，只在 `build_markdown_demo=y` 时探测
  MD4C 和 JKQTMathText，不会拖累正式构建。
- 本地 `demo/*/xmake.lua` 不再声明 `set_project`、编译标准或 Qt 规则，由根工程统一
  配置；路径基于 `os.scriptdir()`。
- 改造前已清理 `build/.deps`、`.gens`、`.objs`、`.build_cache`、`build/linux` 以及
  `demo/markdown` 的旧 xmake 缓存。

已执行的构建验证：

| 配置 | 结果 |
| --- | --- |
| 默认（不含 Demo） | `xmake -j4`：build ok，124.232 s |
| 仅 `build_demo=y` | `pano-demo-reader`：build ok，18.482 s；离屏启动正常 |
| 仅 `build_markdown_demo=y` | `pano-markdown-demo`：build ok，6.526 s；离屏截图成功 |
| 两个 Demo 同时启用 | 目标列表同时包含正式目标、`pano-demo-*`、`pano-markdown-demo`，无重名 |

最终默认配置的清洁构建时间为 107.56 s（见文末“最终验证”）。

## 数据模型与格式

### 独立对象

- `src/note/Note.h`：`Note{id,title,markdown,annotationId,createdAt,modifiedAt}`。
  笔记标题为空时显示为“未命名笔记”；正文允许为空。
- `src/note/NoteModel.*`：创建顺序保存笔记，校验 ID 唯一、一个标注最多一条笔记。
- `src/note/NoteTree.*`：分组/笔记叶子的扁平模型，支持多级分组、同级顺序、移动、
  重命名、删除提升子节点、深度 32/节点 10000 限制和引用校验。
- `Annotation` 不再持有 `AnnotationNote`；`annotation.note` 和 `note_tree` 的旧内嵌
  笔记语义已移除。

### `annotations.json` schema 2

同一文件保存三部分，整体原子写入：

```json
{
  "format": "pano-annotations",
  "schema_version": 2,
  "book_id": "<book-id>",
  "annotations": [],
  "notes": [
    {
      "id": "<note-id>",
      "title": "阅读笔记",
      "markdown": "## 标题\n\n正文",
      "annotation_id": null,
      "created_at": "2026-11-28T10:00:00.000Z",
      "modified_at": "2026-11-28T10:00:00.000Z"
    }
  ],
  "note_tree": [
    {"type": "group", "id": "<group-id>", "title": "论文", "children": [
      {"type": "note", "note_id": "<note-id>"}
    ]}
  ]
}
```

约束与行为：

- 笔记与标注最多一对一关联，只持久化 `note.annotation_id`，不新增 `annotation.note_id`。
- 树中每个笔记恰好出现一次；分组 ID 唯一、无循环、引用必须存在。
- 读取校验失败时拒绝加载并保留原文件；`QSaveFile` 写入失败时保留内存中的未保存状态。
- `schema_version` 1 或未知版本明确拒绝，不做自动迁移，不写空模型覆盖旧文件。
- `LibraryManager::bookInfo` 的 `note_count` 来自 `notes` 数组，标注数来自
  `annotations` 数组。

完整字段见 [`docs/specification/phase1-data-formats.md`](../../specification/phase1-data-formats.md)。

## 界面与生命周期

- 右侧页签改为“标注 / 笔记 / 详情”，默认“标注”；笔记页包含树、预览和
  新建笔记/分组、重命名、移动、上下排序、删除、关联/解除关联、定位按钮。
- 标注列表的“仅有笔记”过滤依据 `DocumentSession::hasLinkedNote`，独立笔记不会出现
  在标注列表中。
- 新建/编辑笔记使用一个非模态 `NoteEditDialog`，绑定 `book_id + note_id`，不跟随
  当前页面或选择切换；支持保存继续、确定关闭、取消、Ctrl+S 和关闭前保存提示。
- 新建草稿在保存成功前不创建 Note/树节点/关联；保存失败保留草稿和未保存状态。
- 删除普通标注默认保留并解除关联笔记；删除 NotePoint 的笔记会同时删除定位点。
  关联笔记的标注在橡皮中默认跳过，显式删除通过确认框处理。
- 笔记树、笔记、关联和标注修改由 `DocumentSession` 的跨模型快照命令统一撤销/重做。

## 探针与验证

### 自动/离屏证据

| 探针 | 结果 |
| --- | --- |
| `pano-library-probe` | `library probe passed` |
| `pano-coordinate-probe` | 4 旋转 × 3 缩放 × 5 点通过 |
| `pano-text-geometry-probe` | 词距/行/栏/上标/退化路径通过 |
| `pano-phase1-probe` | `phase1 probe passed (0 failures)`，含笔记持久化 |
| `pano-interaction-probe` | 智能选择、浮动操作、区域图像通过 |
| `pano-phase2-probe` | `phase2 probe passed (0 failures)` |
| `pano-note-probe` | `note probe passed (0 failures)` |
| `pano-theme-probe` | 主题、深色阅读器、设置预览/取消恢复通过 |

`pano-note-probe` 覆盖：schema 2 写入/读回、笔记树、独立空笔记、关联、拒绝重复关联、
移动/排序、删除标注保留笔记、NotePoint 删除/撤销、旧 schema 拒绝、保存后重新打开和
笔记树/关联恢复；并生成 `phase3-note-tree.png`、`phase3-note-editor.png` 两幅截图。

### V01–V28 状态

| 编号 | 状态 | 说明 |
| --- | --- | --- |
| V01 | 通过（非视觉） | 默认构建不含 Demo，正式目标与探针正常 |
| V02 | 通过（非视觉） | `build_demo=y` 只包含 Phase 0 Demo，target 运行正常 |
| V03 | 通过（非视觉） | `build_markdown_demo=y` 只包含 Markdown Demo 及依赖 |
| V04 | 部分 | 两个 Demo 选项目标列表已验证；未在同一构建中完整运行所有 Demo |
| V05 | 部分 | Linux 实际构建/运行通过；Windows 未验证 |
| V06 | 通过（非视觉） | 新书库无笔记/标注时写入空 schema 2 |
| V07 | 部分 | 普通标注无笔记路径由现有探针覆盖，未单独做 UI 保存检查 |
| V08 | 通过（非视觉） | 独立空笔记可保存并重新打开 |
| V09 | 通过（非视觉） | 关联已有笔记、一对一冲突拒绝 |
| V10 | 通过（非视觉） | 解除关联、删除标注保留笔记、撤销恢复关联 |
| V11 | 部分 | NotePoint 创建/删除/撤销通过；编辑器取消草稿未专门测试 |
| V12 | 部分 | 文字/区域摘录创建笔记路径已接入；未对扫描页做人工验证 |
| V13 | 通过（非视觉） | 根/多级分组与树结构保存 |
| V14 | 部分 | 移动对话框排除自身/后代；代码审查通过，未做自动化移动树测试 |
| V15 | 通过（非视觉） | 同级排序与重新打开后的顺序 |
| V16 | 部分 | 删除分组提升直接子节点已实现；未做专门探针 |
| V17 | 通过（非视觉） | 删除带笔记普通标注默认保留笔记并解除关联 |
| V18 | 通过（非视觉） | 删除笔记保留普通标注；NotePoint 笔记删除定位点 |
| V19 | 部分 | 橡皮/复制回归探针通过；关联笔记跳过需人工确认 |
| V20 | 通过（非视觉） | 跨标注/笔记/树的撤销与重做 |
| V21 | 部分 | 新笔记草稿和保存失败保留逻辑已实现；完整关闭流程未自动化 |
| V22 | 部分 | 单窗口、选中笔记后编辑器目标绑定探针通过；跨文档切换未做 GUI 验证 |
| V23 | 部分 | 关闭提示、保存失败、外部冲突路径已实现；未逐项故障注入 |
| V24 | 通过（视觉） | 右侧笔记树、预览、关联/定位按钮截图检查；未做循环导航压测 |
| V25 | 通过（非视觉） | 保存/重新打开完整闭环 |
| V26 | 部分 | 旧 schema、重复关联已拒绝；畸形引用/外部修改仍需更多故障注入 |
| V27 | 通过（非视觉） | Phase 1/2 阅读器、标注、布局回归探针通过 |
| V28 | 通过（受控） | 使用临时书库和示例 PDF，未写入用户书库或源 PDF |

## 截图

- `docs/development/log/phase3.1-note-tree.png`
- `docs/development/log/phase3.1-note-editor.png`

## 已知限制

- 旧 schema 1 书库不能直接在新版本打开；必须建立新的测试数据，迁移不在本阶段范围。
- 笔记编辑器使用纯 Markdown 编辑 + 既有预览，不提供富文本工具栏/实时分栏。
- 笔记树不支持拖拽排序/多选/思维导图视图；移动通过对话框和上下按钮完成。
- 现有 `NotePanel` 源文件已不再被 ReaderWorkspace 使用，作为临时遗留文件待后续清理。
- 真实 GUI 的关闭草稿、保存失败、删除确认、扫描页文字摘录、Windows/高 DPI 仍需人工验收。

## 结论

Phase 3.1 的独立笔记模型、schema 2、笔记树、关联和编辑器闭环已在 Linux 离屏探针与
人工截图检查下工作；数据安全边界（旧格式拒绝、临时书库、源 PDF 只读）已保留。
Windows、真实 GUI 故障注入和部分 V 项仍为“部分/未验证”，不能视为完整产品验收。

## 最终验证

在完成笔记模型、笔记树、编辑器和文档修改后，以默认配置（两个 Demo 关闭）重新清洁构建：

```text
xmake clean
xmake -j4
build ok, spent 107.56s
```

随后运行全部正式探针与 `pano-note-probe`，结果见上表；所有探针退出码为 0。
