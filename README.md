# PANO Reader — Phase 0 Demo

PANO 是一个以 PDF 原文为不可变底稿、把标注与 Markdown 笔记作为独立开放数据保存的笔记阅读器。本仓库是 Phase 0 的可操作桌面 Demo，覆盖阅读、文字/图形标注、笔记、公式预览和本地保存的早期闭环；它不是正式产品版本。

![PANO Phase 0 界面](docs/development/phase0-ui.png)

## 已实现

- 连续单页 PDF 阅读：滚动、页码跳转、缩放 5%–1200%、适合宽度/整页、90° 旋转。
- 文字工具：页面内选择、复制、高亮、下划线、删除线；文字标注保存选区多边形和字符索引。
- 图形工具：直线、矩形、椭圆、铅笔、荧光笔、对象橡皮擦；支持命中检测、拖动、八向缩放和 Shift 45° 吸附。
- 属性与撤销：普通/正片叠底混合、透明度、线宽、描边/填充色；基于 `QUndoStack` 的添加、删除、修改和笔记编辑撤销。
- 笔记：页面定位图钉 + 附属 Markdown；预览支持标题、列表、表格、代码、链接、本地图片、行内/独立公式；笔记区域截图和本地图片插入。
- 存储：每个 PDF 对应同名 `.pano/` 目录，使用 SHA-256 绑定原文件；`QSaveFile` 原子写入；不修改源 PDF。
- 诊断：Qt/平台、PDF 路径/哈希/大小、页数、标注数、缓存策略、当前页/缩放。

## 已验证环境

| 项 | 值 |
| --- | --- |
| 系统 | Arch Linux x86_64 |
| 编译器 | g++ 16.2.1 |
| 构建 | xmake 3.1.2 |
| Qt | Qt 6.12.0 SDK，`/home/yue/Qt/6.12.0/gcc_64`，含 Qt PDF / Qt Widgets |
| 语言标准 | C++20 |

Windows 目标已列入计划，但尚未在本阶段实际构建验证。高 DPI 真机、扫描 PDF、大型 500+ 页 PDF 尚未完成人工验收。

## 构建

Qt PDF 必须来自同一套 Qt 安装，不要混用系统 Qt 与 SDK 的 QtPdf。使用 xmake 的 Qt 支持：

```bash
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64
xmake -j4
```

若 `qmake` 已在 `PATH` 中，也可以只执行 `xmake f -c -m debug`。项目会为 Linux 可执行文件设置 Qt SDK 的 rpath；若移动 Qt 安装位置，需要重新配置。

产物：

```text
build/linux/x86_64/debug/pano-reader
```

## 启动

```bash
./build/linux/x86_64/debug/pano-reader examples/sample-paper.pdf
```

也可以先启动再通过工具栏“打开 PDF”选择文件：

```bash
./build/linux/x86_64/debug/pano-reader
```

## 使用

1. **阅读**：鼠标滚轮纵向滚动，`Ctrl+滚轮` 以鼠标位置为中心缩放，`Shift+滚轮` 横向滚动；工具栏页码框为 1-based，状态栏显示 0-based 页索引。
2. **选择/移动**：左侧“选择”工具。单击标注选中，拖动移动，拖动八向手柄缩放；`Delete` 删除，`Esc` 取消选择。
3. **文字标注**：切到“文本选择”，在页面内拖动选择；`Ctrl+C` 复制，选择高亮/下划线/删除线工具后再次选择同一区域即可创建文字标注。跨页选择暂不支持。
4. **图形**：直线、矩形、椭圆、铅笔、荧光笔直接拖动创建；铅笔/荧光笔/直线按住 `Shift` 会吸附到 45° 方向。荧光笔使用正片叠底和独立离屏蒙版，避免同一笔内部重复加深。
5. **擦除**：对象橡皮擦点击最上层对象即删除；一次擦除作为一个撤销步骤。
6. **笔记**：笔记图钉工具在页面点击创建定位点，右侧面板输入 Markdown；也可双击已有标注打开其笔记。保存后从左侧“笔记列表”点击条目跳回原文。
7. **图片/截图**：编辑笔记时使用“插入图片”或“区域截图”；资源写入 `.pano/assets/`，Markdown 中使用相对路径。
8. **保存**：`Ctrl+S` 或工具栏保存。首次保存才创建 `.pano/`；关闭/打开其他文件前若有未保存内容会询问。源 PDF 始终只读。
9. **诊断**：菜单“帮助 → 诊断信息”查看实际 Qt 版本、PDF SHA-256、缓存限制和存储清单。

常用快捷键：`V` 选择、`T` 文本选择、`H` 高亮、`U` 下划线、`S` 删除线、`L` 直线、`R` 矩形、`O` 椭圆、`P` 铅笔、`M` 荧光笔、`E` 橡皮擦、`N` 笔记图钉；`Ctrl+Z`/`Ctrl+Shift+Z` 撤销/重做。

## 数据格式

打开 `book.pdf` 并保存后：

```text
book.pdf
book.pdf.pano/
├── document.json       # schema、document_id、PDF 文件名/大小/SHA-256、时间戳
├── annotations.json    # schema、document_id、按创建顺序排列的标注
└── assets/             # 笔记图片、区域截图等相对资源
```

页面坐标使用 PDF point，原点在未旋转页面左上角，X 向右、Y 向下；页面索引从 0 开始，UI 显示从 1 开始。若 `document.json` 缺失、schema 不匹配或 PDF SHA-256 不一致，加载会停止并提示，不会把新 PDF 静默绑定到旧数据，也不会覆盖旧标注。

## 样本与验证

`examples/sample-paper.tex` 是自制 3 页样本源文件，`examples/sample-paper.pdf` 是已生成样本，覆盖正文选择、公式、表格、引用和滚动阅读。重新生成：

```bash
tools/build-sample-pdf.sh
```

仓库带有少量开发探针，便于回归核心路径（不替代人工验收）：

```bash
# 无显示环境下验证：新增/撤销/重做、笔记编辑撤销、保存后重开、PDF 哈希不变，并生成界面截图
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-flow-probe \
  examples/sample-paper.pdf build/probe/flow.png

# 只生成主界面截图
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-ui-probe \
  examples/sample-paper.pdf build/probe/ui.png

# Markdown + 公式预览渲染检查
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-preview-probe \
  build/probe/preview.png
```

阶段结论、逐项 V01–V14 状态和已知局限见 [docs/development/phase0-results.md](docs/development/phase0-results.md)。

## 项目结构

```text
src/app/         主窗口、笔记面板、区域截图遮罩
src/pdf/         PDF 文档封装、异步页面渲染与 LRU 缓存
src/reader/      连续页面布局、PdfView 交互与选区/绘制
src/annotation/  标注数据、模型、命令、绘制与命中
src/note/        Markdown 解析/预览、离线公式渲染
src/storage/     .pano 数据目录、清单校验、原子保存
examples/        自制 PDF 样本及 LaTeX 源
tools/           样本生成与开发探针
docs/            Phase 0 开发文档与验证记录
```

## 已知边界

- 仅使用 Qt PDF 后端，未接入 OCR；扫描 PDF 只能做几何标注，不能选择文字。
- 文字选择限制在单页内；跨页选择未实现。
- 公式渲染器是本地最小 TeX 子集，不依赖 WebEngine 或网络；矩阵、颜色、复杂对齐等命令未覆盖。
- 不导出 PDF，不建立书架/多文档管理，不做全文搜索。
- 区域截图当前截取视图中的页面像素（按当前缩放）；高 DPI 下的清晰度与坐标漂移仍需真机复验。
- 本阶段以功能闭环和契约验证为主，未做性能基准、安装包和 UI 美化。

## 许可证

MIT，见 [LICENSE](LICENSE)。
