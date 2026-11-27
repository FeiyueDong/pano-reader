# PANO Phase 0.1 阅读器与标注修复记录

- 日期：2026-11-27
- 基线：`3d23ec2`（Phase 0 Demo）
- 范围：按 `docs/development/2.PANO_Phase0_1_Demo_Stabilization.md` 修复坐标、旋转、精准定位、预览、荧光笔合成、样式、删除保护与缓存问题。
- 结论：R01–R10 已完成代码修改，构建成功，并通过非视觉坐标/功能探针；**真实桌面交互、视觉贴合、高 DPI、扫描件及大型文档仍待用户人工验收，本记录不把未观察项写成已通过。**

## 1. 构建与运行

```bash
xmake f -c -m debug --qt=/home/yue/Qt/6.12.0/gcc_64
xmake -j4
```

本轮 `xmake -j4` 成功；主程序仍为 `build/linux/x86_64/debug/pano-reader`。Qt 使用 SDK `/home/yue/Qt/6.12.0/gcc_64` 内的 Qt PDF，可执行文件带 SDK rpath。

启动：

```bash
./build/linux/x86_64/debug/pano-reader examples/sample-paper.pdf
```

## 2. R01–R10 状态

| ID | 状态 | 实际完成情况 |
| --- | --- | --- |
| R01 | 已修改，坐标探针通过 | `PdfView` 统一使用 `pageToContent * Translate(-scroll)`；图像、标注、选区、控制点、命中、导航共用同一 helper。 |
| R02 | 已修改，待视觉验收 | 标注偏移与裁剪路径改用统一变换；`AnnotationRenderer` 高亮蒙版变换顺序同步修正。 |
| R03 | 已修改，待视觉验收 | 页面像素始终按未旋转页面 pt 申请；`paintPage()` 在裁剪后设置页面变换并绘制原始页矩形。 |
| R04 | 已修改，功能探针通过 | 新增 `goToPosition()` / `revealPageRegion()`；`revealAnnotation()` 使用文字多边形或对象几何；列表入口统一走精准定位。 |
| R05 | 已修改，待交互验收 | `DrawShape` / `DrawPath` 以及拖动/缩放编辑均绘制 `m_draft`；释放时使用最终鼠标位置。 |
| R06 | 已修改，待视觉验收 | 荧光笔改为笔画影响范围内的局部 ARGB 蒙版，单笔一次 opacity + Multiply/SourceOver 合成。 |
| R07 | 已修改 | 工具预设只在用户未修改样式时生效；文字高亮/荧光笔默认 Multiply，普通图形/铅笔默认 Normal；创建对象不再硬编码覆盖样式。 |
| R08 | 部分完成 | 删除带笔记标注会先发 `noteDeleteRequested`，主窗口确认后删除；Delete/列表/橡皮共用；撤销恢复对象和笔记。橡皮仍为点击式，未实现连续擦除。 |
| R09 | 已修改，未压力测试 | 绘制路径的缓存命中会更新 LRU；替换缓存项时同步修正字节数；文档切换清空进行中请求。未做快速缩放/大文档压力测试。 |
| R10 | 已完成 | 新增非视觉坐标探针；扩展 flow probe 覆盖精准导航与删除恢复；本记录列明未验证项。 |

## 3. 关键实现

### 3.1 页面到视图坐标

统一入口：

```cpp
QTransform PdfView::pageToViewTransform(int pageIndex) const
{
    return m_layout.pageToContent(pageIndex)
           * QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                       -verticalScrollBar()->value());
}
```

Qt 的 `A * B` 对点先应用 A、再应用 B，因此上式为先页面布局/缩放/旋转，再减滚动偏移。反向映射使用同一矩阵求逆；非法页码或不可逆矩阵直接拒绝。页面裁剪矩形、选区、控制点、笔记图钉和鼠标坐标都通过该入口，避免各处重复拼接矩阵。

`tools/coordinate_probe.cpp` 使用独立预期公式检查：

- 4 种视图旋转：0°、90°、180°、270°；
- 3 种缩放：0.5、1.0、1.75；
- 每页四角加一个页内点；
- 非零滚动 `(137, 281)`；
- 同时检查正向映射、逆映射和旋转后的页面视图尺寸。

预期公式直接使用文档 5.3.3 的旋转约定：0° `(x,y)`、90° `(H-y,x)`、180° `(W-x,H-y)`、270° `(y,W-x)`，不是只做矩阵往返。

### 3.2 页面图像与旋转

- `pagePixelSize()` 使用 `pageSizePt * zoom * DPR`，不按旋转后的宽高申请图像；最大像素数和单边上限使用同一比例缩小，避免宽高比变化。
- `paintPage()` 先在视图坐标设置页面裁剪，再 `setWorldTransform(pageToView)`，在 `QRectF(0,0,pageSizePt)` 内绘制页面图像；标注使用完全相同的变换。
- 缓存仍保存未施加用户视图旋转的原始页面图；同一页/同一像素尺寸可跨旋转复用。旋转导致适应宽度或适合整页的缩放变化时，才允许产生新分辨率。
- 页面阴影、白底、边框仍在视图矩形绘制，便于处理旋转后的占位区域。

### 3.3 精准导航

- `goToPosition(page, pagePoint)`：目标点已在视口 40px 安全边距内时不滚动，否则把页内点映射到内容坐标并居中；保持当前缩放和旋转。
- `revealPageRegion(page, pageBounds)`：小区域居中；区域大于视口时优先显示起始边缘并保留上下文；目标已可见时不改变阅读位置。
- `revealAnnotation(id)`：文字标注取所有 `polygons` 的联合包围框；定位图钉取规范位置 `rect.topLeft()`；其他对象取几何范围；选中后调用精准定位。
- 笔记列表与标注列表入口统一调用 `revealAnnotation()`，避免先选择再跳转造成列表刷新或重复滚动。

### 3.4 实时预览与编辑状态

`paintEvent()` 在 `DrawShape` / `DrawPath` 以及 `DragAnnotation` / `ResizeAnnotation` 状态下绘制 `m_draft`；拖动已有对象时跳过旧对象，避免出现两个位置。鼠标释放事件在提交前用释放位置更新临时几何；Esc 清理编辑/拖动/缩放手柄状态，不生成对象。

### 3.5 荧光笔局部合成

`AnnotationRenderer::paintHighlighter()`：

1. 由路径包围盒加线宽/圆端 padding 得到笔画影响范围；
2. 用 `pageToView.mapRect()` 映射到视图，并与页面视图矩形相交；
3. 按 DPR 创建局部 `ARGB32_Premultiplied` 蒙版；
4. 蒙版变换为 `pageToView * Translate(-maskOrigin)`；
5. 整笔以一次 opacity 和 Multiply/SourceOver 合成到页面目标，不对同一笔画内部重叠采样重复叠加。

### 3.6 工具预设、删除保护与缓存

- `applyToolPreset(Tool)` 只在 `m_styleUserModified == false` 时设置工具默认样式；用户修改颜色/线宽/透明度/混合模式后，新对象直接使用用户样式。
- `deleteAnnotation(id, confirmed)` 对带笔记对象先发出 `noteDeleteRequested`；主窗口显示确认对话，取消时保留全部数据；`deleteSelected()` 与橡皮擦入口共用该保护。删除命令仍保存完整 `Annotation`，撤销可恢复笔记。
- `PageRenderer::cachedImage()` 在缓存命中时更新 LRU；`onPageRendered()` 替换已有缓存项时同步调整缓存字节数；`setDocument()` 继续隔离文档切换后的旧请求。
- 橡皮擦保持点击式对象删除；未实现连续擦除分割，记录为 Demo 限制。

## 4. 已执行的检查

### 4.1 构建

```bash
xmake -j4
```

结果：成功，无编译错误。

### 4.2 非视觉坐标探针

```bash
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-coordinate-probe
```

实际输出：

```text
coordinate probe passed: 4 rotations x 3 zooms x 5 points, with scroll (137,281)
```

该检查覆盖页角与内部点、非单位缩放、非零滚动及四种旋转，具有独立预期值。

### 4.3 功能闭环探针

```bash
QT_QPA_PLATFORM=offscreen ./build/linux/x86_64/debug/pano-flow-probe \
  examples/sample-paper.pdf build/probe/flow.png
```

本轮实际结果：

```text
sample text selection valid: true chars: 84 polygons: 1
after add rect: 1 undo: true redo: false
after undo: 0
after redo: 1
note after edit chars: 240 editing: false
note after undo chars: 224
note after redo chars: 240
precise navigation reached page 1: true back to pin page: 0
delete annotation+note: true undo restores note: true
rotation/zoom roundtrip keeps annotation: true rotation: 0
store exists: true annotations exists: true manifest exists: true pdf unchanged: true
mismatched PDF rejected: true
after reopen annotations: 2
note markdown chars after select: 240
saved screenshot: "build/probe/flow.png" QSize(1440, 900)
```

说明：

- 该探针验证文字选择层、新增/撤销/重做、笔记编辑撤销、`goToPosition`/`revealAnnotation`、删除带笔记对象后撤销恢复、旋转/缩放往返、保存重开、哈希绑定拒绝，以及源 PDF 未被修改。
- 探针仍会生成截图文件，但本轮没有查看截图或用它做视觉判定；不将截图生成视为视觉验收通过。
- 未执行自动截图对比、GUI 交互脚本或高 DPI/大文档压力测试。

## 5. 待用户人工验收

建议按 `docs/development/2.PANO_Phase0_1_Demo_Stabilization.md` 的 V01–V11 执行，重点：

1. V01/V02：不同页面位置创建文字与图形标注，50%–200% 缩放、横向滚动和跨页后检查贴合与裁剪。
2. V03/V04：使用带方向标识、圆形和正方形的页面，依次旋转 90°/180°/270°/0°，检查文字方向、圆/方形比例，以及旋转状态下选区、绘制、拖动和恢复。
3. V05：同页顶部/中部/底部及跨页笔记，高倍缩放和旋转下跳转，确认不只到页首且保持缩放/旋转。
4. V06：绘制、拖动、缩放、Esc 取消，确认预览与提交一致。
5. V07：扫描文字上的直线与自由荧光笔，检查同一笔画无重复叠色、Multiply 不明显遮挡黑字。
6. V08：删除带/不带笔记对象，确认提示、取消保留、确认删除和撤销完整恢复。
7. V09：保存、关闭、重开后几何/样式/笔记恢复，源 PDF 哈希不变。
8. V10/V11：大型/扫描 PDF、非 100% 系统 DPI；无环境时记录未验证，不宣称通过。

样本：`examples/sample-paper.pdf`；重新生成使用 `tools/build-sample-pdf.sh`。

## 6. 已知限制与未验证项

- 真实桌面鼠标交互、视觉贴合、旋转后文字选区、扫描件、混合页面尺寸、PDF 固有旋转及高 DPI 均未在本轮验证。
- 橡皮擦仍为点击一次删除一个对象，未实现连续擦除和局部笔画分割；带笔记对象仍受确认保护。
- 异步渲染未实现请求取消或复杂预取；快速缩放时可能短时保留旧尺寸请求，依赖 LRU 淘汰和旧结果隔离，未做 500+ 页压力测试。
- 旧 Phase 0 数据没有统一坐标迁移；定位图钉读取时对缺失 `p1` 回退到 `rect.topLeft()`，不静默改写已有 JSON。若旧数据存在其它不一致，应按文档备份并说明，不应批量平移掩盖。
- 页面图缓存按“页 + 像素尺寸”区分；旋转本身不清空缓存，只有实际分辨率变化才产生新项。
- 本阶段未新增书架、多文档标签、OCR、全文搜索、PDF 导出或标注类型。

## 7. 结论

坐标、旋转绘制、精准导航、预览、荧光笔局部合成、工具预设、删除保护和缓存命中路径已完成代码修复，构建及非视觉探针通过。当前状态应表述为“已修复，待人工验收”，不能据此冻结正式版本的坐标与渲染契约；若人工验收发现残留问题，应优先继续收敛这些基础路径。
