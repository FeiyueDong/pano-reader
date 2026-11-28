# PANO Phase 1/2 数据格式规范

本规范描述 PANO 正式版书库、书籍、标注与本机配置的持久化格式。实现代码位于
`src/storage/`、`src/library/`、`src/settings/` 和 `src/annotation/`。

## 1. 通用约定

- 所有文本文件为 UTF-8 编码的 JSON，带缩进和末尾换行，便于人工检查与版本管理。
- 每个文件都有 `format` 字段标识用途，并有 `schema_version`。Phase 1 只接受版本 `1`；
  未知版本或未知 `format` 必须拒绝写入，不能猜测或静默转换。
- 写入使用 `QSaveFile` 的提交语义，避免半写入文件。书库级索引写入前还会检查文件是否
  在本次打开后被外部修改。
- 书库目录以 `.pano.lock` 做单实例保护；锁只保护写入，不改变 PDF 源文件。
- 对象 ID 为 UUID 字符串；时间戳为 UTC ISO 8601 字符串，例如
  `2026-11-27T08:30:00.000Z`。
- 书库内相对路径使用 `/`，禁止绝对路径、`..`、反斜杠和逃逸书库根目录的路径。
- 标注坐标是 Qt PDF 页面可见坐标（未旋转视图），单位 pt，原点在左上角，X 向右，
  Y 向下。`page_index` 从 0 开始。DPR 不进入数据文件。

## 2. 书库目录结构

```text
<library root>/
  library.json
  books/
    <book directory>/
      document.json
      annotations.json
      bookmarks.json        # Phase 2；首次添加书签时创建
      source.pdf            # 仅拷贝模式
      cover.png
      assets/               # 笔记图片、截图
  bookshelf/
    shelves.json
  state/
    reading.json
    history.json
  .pano.lock
  .pending-delete/          # Phase 2 永久删除隔离与恢复标记
```

书库根目录由用户选择；缓存和日志不得写入书库。本机配置位于 Qt 的配置目录
（`QStandardPaths::AppConfigLocation`），测试可通过 `PANO_CONFIG_DIR` 覆盖。

## 3. `library.json`

书库索引，只保存书籍的身份、目录与显示状态；标题、作者等元信息以
`document.json` 为准。

```json
{
  "format": "pano-library",
  "schema_version": 1,
  "id": "3a0b2c7e-9d55-4b5b-8a91-6a4f4b8a1c2d",
  "books": [
    {
      "id": "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1",
      "directory": "books/sample-paper-a1b2c3d4",
      "favorite": false,
      "deleted_at": null
    }
  ]
}
```

- `books[].directory` 必须位于 `books/` 下。
- `deleted_at` 为 `null` 表示未删除；字符串表示软删除时间。软删除不删除书籍目录、
  标注、笔记或拷贝的 PDF。
- 目录缺失只产生诊断信息，不自动删除索引条目。
- 永久删除仅接受回收站中的书；实现先把书籍目录移入书库根目录下的
  `.pending-delete/` 隔离区，并在其中写入操作标记。索引、书架、阅读状态与历史全部
  成功写入后才清理隔离目录；任一步失败会回滚。启动/打开书库时扫描 .pending-delete
  标记，未提交的删除恢复原目录，已提交的删除清理隔离内容。该目录是内部恢复数据，
  不属于用户可见书库内容。

## 4. `document.json`

每本书的元信息与源文件绑定。

```json
{
  "format": "pano-document",
  "schema_version": 1,
  "id": "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1",
  "title": "PANO Phase 0 Sample Document",
  "author": "",
  "imported_at": "2026-11-27T08:20:00.000Z",
  "source": {
    "mode": "copy",
    "path": "source.pdf",
    "sha256": "905ce4da9f8e5c10bf64ed815b36cbca1c61cdaddd929395c058275b0093f899",
    "size_bytes": 126396
  }
}
```

- `id` 与 `library.json` 中的书籍 ID 一致。
- `source.mode` 为 `copy` 或 `reference`。
- `copy` 的 `source.path` 是书籍目录内的相对路径（通常是 `source.pdf`）。
- `reference` 的 `source.path` 是绝对值路径，作为可移植的默认值；本机重定位优先使用
  `sources.json` 中的覆盖路径。
- `sha256` 为小写十六进制 SHA-256；导入、打开和更换源文件时必须校验。哈希不一致时
  必须拒绝并提示，不能静默绑定。
- `size_bytes` 为源 PDF 字节数。

## 5. `bookshelf/shelves.json`

```json
{
  "format": "pano-bookshelves",
  "schema_version": 1,
  "shelves": [
    {
      "id": "c2a6d1b0-6f6a-4d3a-9f8e-2c3d4e5f6a7b",
      "name": "论文",
      "parent_id": null,
      "book_ids": ["0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1"]
    }
  ]
}
```

- `parent_id` 为 `null` 表示顶层书架，否则必须是另一书架的 ID。
- 不允许自引用或循环；每本书可以属于多个书架。
- 删除书架只删除书架及其成员关系，不删除书籍文件或书籍索引；子书架上移到被删书架
  的父级。

## 6. `annotations.json`

```json
{
  "format": "pano-annotations",
  "schema_version": 1,
  "book_id": "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1",
  "annotations": [
    {
      "id": "d1f0d0a4-3a94-4d7a-9a1b-4f1b7f0f2b11",
      "page_index": 0,
      "type": "rectangle",
      "geometry": {"x": 80, "y": 120, "width": 300, "height": 90},
      "style": {
        "stroke_color": "#ccd94f4f",
        "stroke_width_pt": 2.0,
        "fill_color": null,
        "opacity": 0.8,
        "blend_mode": "normal"
      },
      "created_at": "2026-11-27T08:35:00.000Z",
      "modified_at": "2026-11-27T08:35:00.000Z",
      "z_order": 0
    }
  ],
  "note_tree": []
}
```

### 6.1 类型与几何

| `type` | 含义 | `geometry` 必填字段 |
| --- | --- | --- |
| `text_highlight` | 文本高亮 | `polygons`, `text_start`, `text_end`, `text` |
| `underline` | 文本下划线 | 同上 |
| `strikeout` | 文本删除线 | 同上 |
| `line` | 直线 | `start`, `end` |
| `rectangle` | 矩形 | `x`, `y`, `width`, `height` |
| `ellipse` | 椭圆 | `x`, `y`, `width`, `height` |
| `pencil` | 铅笔路径 | `points` |
| `highlighter` | 荧光笔路径 | `points` |
| `note_point` | 定位笔记 | `point` |

- `polygons` 是文字选区的原始多边形数组，每个多边形至少 3 个点；每个文字选区保留
  PDF 文本索引 `text_start`/`text_end` 与摘录 `text`，用于重新打开后保持语义。
  写入时保留 PDF 引擎返回的原始选区多边形，不把合并后的显示几何写回文件；渲染、
  命中和定位时由 `TextSelectionGeometry` 在内存中按行合并，避免把词间距误判为永久
  几何。
- `points`、`polygons` 中每个点为 `[x, y]`。
- 不再保存重复的 `rect`；矩形由几何字段推导，避免同一坐标出现两个权威来源。
- 兼容读取旧字段别名：`highlight`、`text_underline`、`text_strikeout`、
  `strike_out`、`note`、`excerpt`、`p1`/`p2`；正式写入只使用上表名称。

### 6.2 样式

- `stroke_color`：`#AARRGGBB`。
- `stroke_width_pt`：页面 pt，`0 < width <= 200`。
- `fill_color`：`#AARRGGBB` 或 `null`。
- `opacity`：`0..1`。
- `blend_mode`：`normal` 或 `multiply`。
- `line_style`：可选，`solid`（默认）、`dash`、`dot`、`dash_dot`、`dash_dot_dot`；
  旧文件缺省按 `solid` 读取。
- `stroke_enabled`：可选布尔值，默认 `true`；仅当为 `false` 时写入，表示不描边
  （填充仍可保留）。

### 6.3 笔记

标注可带附加笔记：

```json
"note": {"title": "阅读笔记", "markdown": "## 标题\n\n正文"}
```

没有笔记时省略 `note`（等价于空笔记）。笔记 Markdown 中的相对图片路径相对于书籍
目录下的 `assets/`；图片文件与标注 JSON 一起保存。

`z_order` 为同页绘制顺序，数组顺序与创建顺序一致；Phase 1 写入该字段，读取旧数据时
缺省为 0。

`note_tree` 为保留字段，当前为空数组；笔记正文存放在标注的 `note` 中。

## 7. 阅读状态与历史

`state/reading.json`：

```json
{
  "format": "pano-reading-state",
  "schema_version": 1,
  "books": {
    "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1": {
      "page_index": 1,
      "page_point": [297.64, 421.13],
      "zoom": 1.5,
      "zoom_mode": "fixed",
      "layout_mode": "continuous",
      "first_page_alone": true,
      "rotation": 90,
      "updated_at": "2026-11-27T08:40:00.000Z"
    }
  }
}
```

`zoom_mode` 为 `fixed`、`fit_width` 或 `fit_page`；`layout_mode` 为 `single`、
`continuous`、`facing` 或 `continuous_facing`，旧数据缺省为 `continuous`；
`first_page_alone` 仅影响双页布局，旧数据缺省为 `true`；`rotation` 只能为
0/90/180/270。`page_point` 是可选的阅读锚点，单位为**未旋转页面 pt**，表示打开时
视口中心所在的页面位置；省略或无效时回退到 `page_index` 的页首，兼容 Phase 1 旧
数据。无状态或状态无效时回到第一页、连续单页、固定比例。

页面切换、缩放、旋转和同页滚动会更新阅读状态；滚动只更新阅读状态，不重写标注文件。

`state/history.json` 保存每本书最近一次阅读时间：

```json
{
  "format": "pano-reading-history",
  "schema_version": 1,
  "books": {
    "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1": "2026-11-27T08:40:00.000Z"
  }
}
```

### 7.1 书签

`books/<book>/bookmarks.json`：

```json
{
  "format": "pano-bookmarks",
  "schema_version": 1,
  "book_id": "0e9b4d3a-6f6f-4c86-9d5c-1f0b2f46f1a1",
  "bookmarks": [
    {
      "id": "4f1c2e5a-0b7a-4d0e-9b2a-3c5d6e7f8a90",
      "title": "第 3 页",
      "page_index": 2,
      "position": {"x": 297.64, "y": 421.13},
      "created_at": "2026-11-28T08:00:00.000Z"
    }
  ]
}
```

`page_index` 从 0 开始，`position` 为未旋转页面 pt 坐标；书签文件按书籍目录独立保存，
`book_id` 必须与 `document.json` 一致。文件不存在视为空书签；外部修改检测和
`QSaveFile` 原子写入与标注相同。Phase 1 旧书库没有该文件时保持为空，不自动创建，
直到用户第一次添加书签。

## 8. 本机配置

本机配置不放进书库，避免随书库同步或分享：

- `general.json`：`format: pano-settings-general`，包含主题键、界面字号、恢复上次书库、
  恢复打开标签、自动保存秒数、默认导入方式、书库封面/列表视图、默认布局、默认缩放
  模式、首页单独、页间距和滚轮翻页。`wheel_turns_page` 新安装默认 `true`，用户显式保存的
  `false` 必须保持不变；滚轮翻页只影响非连续布局。
- `workspace.json`：`format: pano-settings-workspace`，包含上次书库路径、上次书籍 ID、
  打开书籍 ID 列表、书库三栏和阅读器左右栏的宽度与可见性。
- `tools.json`：`format: pano-tool-presets`，按工具保存新标注预设和橡皮半径；不属于书库，不随
  书库分享。字段与该工具的 `AnnotationStyle` 一致（含 `line_style` 与
  `stroke_enabled`），缺省工具使用内置默认值。
- `sources.json`：`format: pano-local-sources`，按书库 ID 保存映射模式书籍的本机源文件
  覆盖路径：

```json
{
  "format": "pano-local-sources",
  "schema_version": 1,
  "libraries": {
    "<library id>": {
      "source_overrides": {
        "<book id>": "/home/user/papers/moved.pdf"
      }
    }
  }
}
```

覆盖路径只改变本机解析结果，不写回 `document.json`，因此共享书库时不会把本机绝对
路径带给其他设备。

工具预设示例：

```json
{
  "format": "pano-tool-presets",
  "schema_version": 1,
  "presets": {
    "pencil": {
      "stroke_color": "#ff344054",
      "stroke_width_pt": 1.5,
      "fill_color": null,
      "opacity": 1.0,
      "blend_mode": "normal"
    },
    "highlighter": {
      "stroke_color": "#cc45b9e6",
      "stroke_width_pt": 12,
      "fill_color": null,
      "opacity": 0.8,
      "blend_mode": "multiply"
    }
  }
}
```

未知 `schema_version` 或损坏的 `tools.json` 只告警并保留原文件，不静默覆盖；缺失时
使用内置默认值。

## 9. 完整性、锁与外部修改

- 打开书库时校验 `library.json` 的 `format`、`schema_version`、书籍目录、书架父级与
  循环、重复 ID；错误必须拒绝打开，保留原文件不变。
- 书库打开期间持有 `.pano.lock`；另一个实例打开同一书库必须失败并给出明确提示。
- 书库 JSON 写入前比较打开时记录的哈希；外部修改或删除后拒绝写入，不覆盖外部内容。
- `annotations.json` 额外记录打开时的 SHA-256；保存前若文件被外部修改，本次保存失败、
  保留未保存状态，要求重新打开书籍，避免覆盖外部内容。
- 源 PDF 始终只读；PANO 不修改源文件，也不把标注写回 PDF。
- 删除书籍前必须确认；永久删除只删除书库内拷贝的书籍目录，映射模式的外部 PDF 保留。
- 旧 Demo 的 `.pano` 数据不会自动迁移；需要时由用户显式处理。

## 10. 兼容性与阶段边界

- Phase 1.1 沿用上述格式并增加可选的阅读锚点与本机工具预设；未知字段在读取时忽略，
  未知 `format`/`schema_version` 拒绝。
- 标注文本选区限制在单页；不支持跨页选择、扫描 PDF OCR、PDF 导出。
- 公式渲染采用本地子集实现；不支持的 TeX 命令以源码或安全占位显示。
