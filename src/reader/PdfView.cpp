#include "reader/PdfView.h"

#include "annotation/AnnotationCommands.h"
#include "annotation/AnnotationRenderer.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace pano {

namespace {

// Shift-drag constrains freehand strokes and lines to 45 degree steps so that
// horizontal, vertical and diagonal gestures are easy to produce.
QPointF snapToAxis(const QPointF &start, const QPointF &point)
{
    const QPointF delta = point - start;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length < 0.5)
        return point;
    const qreal step = M_PI / 4.0;
    const qreal angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    return start + QPointF(std::cos(angle), std::sin(angle)) * length;
}

bool shiftHeld()
{
    return QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
}

bool pointBefore(const QPointF &a, const QPointF &b)
{
    if (a.y() < b.y() - 0.5)
        return true;
    if (b.y() < a.y() - 0.5)
        return false;
    return a.x() < b.x();
}

QColor withAlpha(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return color;
}

} // namespace

PdfView::PdfView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    horizontalScrollBar()->setSingleStep(48);
    verticalScrollBar()->setSingleStep(48);

    m_defaultStyle.strokeColor = QColor(QStringLiteral("#f2b705"));
    m_defaultStyle.strokeWidthPt = 2.0;
    m_defaultStyle.opacity = 0.55;
    m_defaultStyle.blend = BlendMode::Multiply;

    m_layout.setViewportSize(viewport()->size());
    updateScrollBars();

    connect(&m_renderer, &PageRenderer::pageReady, this,
            [this](int pageIndex, const QSize &, const QImage &, quint64) {
                if (isPageVisible(pageIndex))
                    viewport()->update();
            });
    connect(&m_annotations, &AnnotationModel::structureChanged, this, [this] {
        if (!m_selectedId.isEmpty() && !m_annotations.find(m_selectedId)) {
            m_selectedId.clear();
            emit annotationSelectionChanged(QString());
        }
        viewport()->update();
        emit annotationsChanged();
    });
    connect(&m_annotations, &AnnotationModel::annotationUpdated, this,
            [this](const QString &) {
                viewport()->update();
                emit annotationsChanged();
            });
    connect(&m_annotations, &AnnotationModel::annotationsReset, this, [this] {
        m_selectedId.clear();
        emit annotationSelectionChanged(QString());
        viewport()->update();
        emit annotationsChanged();
    });
    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this](int) {
        viewport()->update();
        emit annotationsChanged();
    });

    viewport()->setCursor(Qt::ArrowCursor);
}

PdfView::~PdfView() = default;

bool PdfView::openFile(const QString &filePath, QString *errorMessage)
{
    closeDocument();

    QString error;
    if (!m_document.load(filePath, &error)) {
        if (errorMessage)
            *errorMessage = error;
        emit statusMessage(error);
        return false;
    }

    m_pageSizes.clear();
    m_pageSizes.reserve(m_document.pageCount());
    for (int i = 0; i < m_document.pageCount(); ++i) {
        QSizeF size = m_document.pagePointSize(i);
        if (size.width() <= 0.0 || size.height() <= 0.0)
            size = QSizeF(595.0, 842.0);
        m_pageSizes.append(size);
    }

    m_renderer.setDocument(&m_document);
    m_annotations.clear();
    m_undoStack.clear();
    m_selectedId.clear();
    m_textSelection.reset();
    m_textSelectionPage = -1;

    m_layout.setViewportSize(viewport()->size());
    m_layout.setRotation(0);
    m_layout.setZoom(1.0);
    m_layout.setPages(m_pageSizes);
    updateScrollBars();
    m_currentPage = 0;
    fitPage();

    emit annotationSelectionChanged(QString());
    emit documentChanged();
    emit pageChanged(m_currentPage);
    emit zoomChanged(m_layout.zoom());
    viewport()->update();
    return true;
}

void PdfView::closeDocument()
{
    const bool hadDocument = m_document.isLoaded();
    m_renderer.setDocument(nullptr);
    m_annotations.clear();
    m_undoStack.clear();
    m_selectedId.clear();
    m_textSelection.reset();
    m_textSelectionPage = -1;
    m_dragMode = DragMode::None;
    m_document.close();
    m_pageSizes.clear();
    m_layout.setPages({});
    m_currentPage = 0;
    updateScrollBars();
    viewport()->update();
    if (hadDocument) {
        emit annotationSelectionChanged(QString());
        emit documentChanged();
        emit pageChanged(0);
    }
}

bool PdfView::hasDocument() const
{
    return m_document.isLoaded();
}

void PdfView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    m_tool = tool;
    m_dragMode = DragMode::None;
    m_draft = Annotation();
    m_textSelecting = false;
    if (tool != Tool::TextSelect) {
        m_textSelection.reset();
        m_textSelectionPage = -1;
    }

    Qt::CursorShape shape = Qt::ArrowCursor;
    if (tool == Tool::TextSelect)
        shape = Qt::IBeamCursor;
    else if (tool == Tool::Note || tool == Tool::Eraser)
        shape = Qt::PointingHandCursor;
    else if (tool != Tool::Select)
        shape = Qt::CrossCursor;
    viewport()->setCursor(shape);
    viewport()->update();
    emit toolChanged(tool);
}

QString PdfView::toolDisplayName(Tool tool)
{
    switch (tool) {
    case Tool::Select:
        return tr("选择/移动");
    case Tool::TextSelect:
        return tr("文本选择");
    case Tool::Highlight:
        return tr("文本高亮");
    case Tool::Underline:
        return tr("下划线");
    case Tool::StrikeOut:
        return tr("删除线");
    case Tool::Line:
        return tr("直线");
    case Tool::Rectangle:
        return tr("矩形");
    case Tool::Ellipse:
        return tr("椭圆");
    case Tool::Pencil:
        return tr("铅笔");
    case Tool::Highlighter:
        return tr("荧光笔");
    case Tool::Eraser:
        return tr("对象橡皮");
    case Tool::Note:
        return tr("附着笔记");
    }
    return QString();
}

void PdfView::setDefaultStyle(const AnnotationStyle &style)
{
    m_defaultStyle = style;
}

qreal PdfView::zoom() const
{
    return m_layout.zoom();
}

void PdfView::setZoom(qreal zoom)
{
    const qreal clamped = qBound(0.05, zoom, 12.0);
    if (qFuzzyCompare(clamped, m_layout.zoom()))
        return;
    rebuildLayoutAndKeepAnchor(clamped, m_layout.rotation(),
                               QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    emit zoomChanged(m_layout.zoom());
}

void PdfView::zoomIn()
{
    setZoom(m_layout.zoom() * 1.2);
}

void PdfView::zoomOut()
{
    setZoom(m_layout.zoom() / 1.2);
}

void PdfView::fitWidth()
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    const int page = qBound(0, m_currentPage, m_pageSizes.size() - 1);
    QSizeF size = m_pageSizes.value(page);
    if (m_layout.rotation() % 180 != 0)
        size.transpose();
    const qreal available = qMax(40.0, viewport()->width() - 2.0 * m_layout.margin() - 8.0);
    if (size.width() > 0.0)
        setZoom(available / size.width());
    goToPage(page);
}

void PdfView::fitPage()
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    const int page = qBound(0, m_currentPage, m_pageSizes.size() - 1);
    QSizeF size = m_pageSizes.value(page);
    if (m_layout.rotation() % 180 != 0)
        size.transpose();
    const qreal availableW = qMax(40.0, viewport()->width() - 2.0 * m_layout.margin() - 8.0);
    const qreal availableH = qMax(40.0, viewport()->height() - 2.0 * m_layout.margin() - 8.0);
    if (size.width() <= 0.0 || size.height() <= 0.0)
        return;
    const qreal zoom = qMin(availableW / size.width(), availableH / size.height());
    setZoom(zoom);
    centerPage(page);
}

void PdfView::resetZoom()
{
    setZoom(1.0);
    goToPage(m_currentPage);
}

int PdfView::rotation() const
{
    return m_layout.rotation();
}

void PdfView::setRotation(int degrees)
{
    int normalized = qRound(degrees / 90.0) * 90;
    normalized = ((normalized % 360) + 360) % 360;
    if (normalized == m_layout.rotation())
        return;
    rebuildLayoutAndKeepAnchor(m_layout.zoom(), normalized,
                               QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    emit rotationChanged(m_layout.rotation());
}

void PdfView::rotateBy(int degrees)
{
    setRotation(m_layout.rotation() + degrees);
}

int PdfView::pageCount() const
{
    return m_pageSizes.size();
}

void PdfView::goToPage(int pageIndex)
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    pageIndex = qBound(0, pageIndex, m_pageSizes.size() - 1);
    const QRectF rect = m_layout.pageRect(pageIndex);
    horizontalScrollBar()->setValue(qRound(rect.center().x() - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(rect.top() - m_layout.margin()));
    m_currentPage = pageIndex;
    updateCurrentPage();
    emit pageChanged(m_currentPage);
    viewport()->update();
}

void PdfView::revealAnnotation(const QString &annotationId)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation)
        return;
    selectAnnotation(annotationId);
    goToPage(annotation->pageIndex);
}

void PdfView::selectAnnotation(const QString &annotationId)
{
    if (m_selectedId == annotationId)
        return;
    m_selectedId = m_annotations.find(annotationId) ? annotationId : QString();
    emit annotationSelectionChanged(m_selectedId);
    viewport()->update();
}

void PdfView::deleteSelected()
{
    if (m_selectedId.isEmpty())
        return;
    const Annotation *annotation = m_annotations.find(m_selectedId);
    if (!annotation)
        return;
    const Annotation copy = *annotation;
    m_undoStack.push(new RemoveAnnotationsCommand(&m_annotations, {copy}));
    m_selectedId.clear();
    emit annotationSelectionChanged(QString());
    emit statusMessage(tr("已删除批注"));
}

void PdfView::applyStyleToSelected(const AnnotationStyle &style)
{
    if (m_selectedId.isEmpty())
        return;
    const Annotation *annotation = m_annotations.find(m_selectedId);
    if (!annotation)
        return;
    Annotation after = *annotation;
    after.style = style;
    after.style.opacity = qBound(0.05, after.style.opacity, 1.0);
    after.style.strokeWidthPt = qBound(0.25, after.style.strokeWidthPt, 200.0);
    if (after.style.strokeColor.alpha() == 0)
        after.style.strokeColor.setAlpha(255);
    after.modifiedAt = nowIsoUtc();
    if (after.style.strokeColor == annotation->style.strokeColor
        && qFuzzyCompare(after.style.strokeWidthPt, annotation->style.strokeWidthPt)
        && after.style.fillColor == annotation->style.fillColor
        && qFuzzyCompare(after.style.opacity, annotation->style.opacity)
        && after.style.blend == annotation->style.blend) {
        return;
    }
    m_undoStack.push(new ModifyAnnotationCommand(&m_annotations, *annotation, after));
    emit statusMessage(tr("已修改批注样式"));
}

void PdfView::setAnnotationNote(const QString &annotationId, const AnnotationNote &note)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation)
        return;
    if (annotation->note.title == note.title && annotation->note.markdown == note.markdown)
        return;
    Annotation after = *annotation;
    after.note = note;
    after.modifiedAt = nowIsoUtc();
    m_undoStack.push(new ModifyAnnotationCommand(&m_annotations, *annotation, after));
    emit statusMessage(tr("已更新笔记"));
}

void PdfView::setSelectionOverlayVisible(bool visible)
{
    if (m_selectionOverlayVisible == visible)
        return;
    m_selectionOverlayVisible = visible;
    viewport()->update();
}

void PdfView::copySelectedText()
{
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    emit statusMessage(tr("已复制 %1 个字符").arg(text.size()));
}

QString PdfView::selectedText() const
{
    if (m_textSelection && m_textSelection->isValid() && !m_textSelection->text().isEmpty())
        return m_textSelection->text();
    if (!m_selectedId.isEmpty()) {
        const Annotation *annotation = m_annotations.find(m_selectedId);
        if (annotation && !annotation->text.isEmpty())
            return annotation->text;
    }
    return QString();
}

void PdfView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    m_layout.setViewportSize(viewport()->size());
    updateScrollBars();
    updateCurrentPage();
    viewport()->update();
}

void PdfView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(viewport()->rect(), QColor(QStringLiteral("#eceef2")));

    if (!hasDocument() || m_pageSizes.isEmpty()) {
        paintEmptyState(painter);
        return;
    }

    const int scrollX = horizontalScrollBar()->value();
    const int scrollY = verticalScrollBar()->value();
    const QRectF viewportRect(scrollX, scrollY, viewport()->width(), viewport()->height());

    for (int page = 0; page < m_pageSizes.size(); ++page) {
        const QRectF contentRect = m_layout.pageRect(page);
        if (!contentRect.intersects(viewportRect))
            continue;
        const QRectF pageRectInView = contentRect.translated(-scrollX, -scrollY);
        const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                      * m_layout.pageToContent(page);
        paintPage(painter, page, pageRectInView, pageToView);
    }

    paintSelectionOverlay(painter);

    for (const Annotation &annotation : m_annotations.annotations()) {
        if (annotation.pageIndex < 0 || annotation.pageIndex >= m_pageSizes.size())
            continue;
        if (!isPageVisible(annotation.pageIndex))
            continue;
        if (annotation.id == m_editId
            && (m_dragMode == DragMode::DragAnnotation || m_dragMode == DragMode::ResizeAnnotation)) {
            continue;
        }
        const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                      * m_layout.pageToContent(annotation.pageIndex);
        const QRectF pageRectInView = m_layout.pageRect(annotation.pageIndex)
                                          .translated(-scrollX, -scrollY);
        AnnotationRenderer::paint(painter, annotation, pageToView, pageRectInView,
                                  devicePixelRatioF());
    }

    if (m_draft.id.isEmpty() && !m_draft.polygons.isEmpty() && m_dragPage >= 0) {
        const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                      * m_layout.pageToContent(m_dragPage);
        const QRectF pageRectInView = m_layout.pageRect(m_dragPage).translated(-scrollX, -scrollY);
        AnnotationRenderer::paint(painter, m_draft, pageToView, pageRectInView,
                                  devicePixelRatioF());
    } else if (m_dragMode == DragMode::DragAnnotation || m_dragMode == DragMode::ResizeAnnotation) {
        if (m_dragPage >= 0 && m_draft.pageIndex >= 0) {
            const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                          * m_layout.pageToContent(m_draft.pageIndex);
            const QRectF pageRectInView = m_layout.pageRect(m_draft.pageIndex)
                                              .translated(-scrollX, -scrollY);
            AnnotationRenderer::paint(painter, m_draft, pageToView, pageRectInView,
                                      devicePixelRatioF());
        }
    }

    paintSelectedAnnotationOutline(painter);
}

void PdfView::paintPage(QPainter &painter, int pageIndex, const QRectF &pageRectInView,
                        const QTransform &pageToView)
{
    Q_UNUSED(pageToView);
    painter.save();
    painter.setClipRect(pageRectInView.adjusted(-2.0, -2.0, 2.0, 2.0));

    painter.fillRect(pageRectInView.translated(0.0, 3.0), QColor(0, 0, 0, 28));
    painter.fillRect(pageRectInView, Qt::white);

    const QSize pixelSize = pagePixelSize(pageIndex);
    const QImage image = m_renderer.cachedImage(pageIndex, pixelSize);
    if (!image.isNull()) {
        painter.drawImage(pageRectInView, image);
    } else {
        ensurePageImage(pageIndex);
        painter.setPen(QColor(QStringLiteral("#9aa1ab")));
        QFont font = painter.font();
        font.setPointSizeF(qMax(9.0, font.pointSizeF()));
        painter.setFont(font);
        painter.drawText(pageRectInView, Qt::AlignCenter, tr("正在渲染页面…"));
    }

    painter.setPen(QPen(QColor(QStringLiteral("#d3d7de")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(pageRectInView.adjusted(0.5, 0.5, -0.5, -0.5));
    painter.restore();
}

void PdfView::paintSelectionOverlay(QPainter &painter)
{
    if (!m_selectionOverlayVisible)
        return;
    if (!m_textSelection || m_textSelectionPage < 0 || m_textSelectionPage >= m_pageSizes.size())
        return;
    if (!m_textSelection->isValid() || m_textSelection->bounds().isEmpty())
        return;
    if (!isPageVisible(m_textSelectionPage))
        return;

    const int scrollX = horizontalScrollBar()->value();
    const int scrollY = verticalScrollBar()->value();
    const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                  * m_layout.pageToContent(m_textSelectionPage);
    const QRectF pageRectInView = m_layout.pageRect(m_textSelectionPage)
                                      .translated(-scrollX, -scrollY);

    QColor color;
    if (m_tool == Tool::TextSelect)
        color = QColor(QStringLiteral("#2f6fed"));
    else if (m_tool == Tool::Highlight || m_tool == Tool::Highlighter)
        color = m_defaultStyle.strokeColor;
    else
        color = m_defaultStyle.strokeColor;
    color.setAlpha(m_tool == Tool::TextSelect ? 70 : 85);

    painter.save();
    painter.setClipRect(pageRectInView);
    painter.setTransform(pageToView);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPolygonF &polygon : m_textSelection->bounds()) {
        painter.drawPolygon(polygon);
    }
    painter.restore();
}

void PdfView::paintSelectedAnnotationOutline(QPainter &painter)
{
    if (!m_selectionOverlayVisible)
        return;
    if (hasDocument() == false)
        return;

    const Annotation *selection = nullptr;
    if (!m_editId.isEmpty()
        && (m_dragMode == DragMode::DragAnnotation || m_dragMode == DragMode::ResizeAnnotation)) {
        selection = &m_draft;
    } else if (!m_selectedId.isEmpty()) {
        selection = m_annotations.find(m_selectedId);
    }
    if (!selection || selection->pageIndex < 0 || selection->pageIndex >= m_pageSizes.size())
        return;

    const int scrollX = horizontalScrollBar()->value();
    const int scrollY = verticalScrollBar()->value();
    const QTransform pageToView = QTransform::fromTranslate(-scrollX, -scrollY)
                                  * m_layout.pageToContent(selection->pageIndex);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (selection->type == AnnotationType::NotePoint) {
        const QPointF center = pageToView.map(selection->p1);
        painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 1.5, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, 9.0, 9.0);
    } else {
        const QRectF bounds = selection->bounds();
        QPolygonF polygon;
        polygon << pageToView.map(bounds.topLeft()) << pageToView.map(bounds.topRight())
                << pageToView.map(bounds.bottomRight()) << pageToView.map(bounds.bottomLeft());
        painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 1.25, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(polygon);

        if (selection->isResizable()
            && m_dragMode != DragMode::DragAnnotation) {
            const QVector<QPointF> handles = handlePointsInPage(selection->bounds());
            painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 1.0));
            painter.setBrush(Qt::white);
            for (const QPointF &handle : handles) {
                const QPointF mapped = pageToView.map(handle);
                painter.drawRect(QRectF(mapped.x() - 3.5, mapped.y() - 3.5, 7.0, 7.0));
            }
        }
    }
    painter.restore();
}

void PdfView::paintEmptyState(QPainter &painter)
{
    painter.save();
    painter.setPen(QColor(QStringLiteral("#5f6673")));
    QFont titleFont = painter.font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 12.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    const QRect titleRect(0, viewport()->height() / 2 - 64, viewport()->width(), 40);
    painter.drawText(titleRect, Qt::AlignCenter, QStringLiteral("PANO"));

    QFont bodyFont = painter.font();
    bodyFont.setPointSizeF(qMax(10.0, bodyFont.pointSizeF() - 10.0));
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(QColor(QStringLiteral("#8a919e")));
    const QRect helpRect(0, titleRect.bottom() + 8, viewport()->width(), 60);
    painter.drawText(helpRect, Qt::AlignCenter | Qt::TextWordWrap,
                     tr("打开 PDF 开始阅读\n也可以把 PDF 拖到这里"));
    painter.restore();
}

QSize PdfView::pagePixelSize(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return QSize();
    const QSizeF viewSize = m_layout.pageSizeView(pageIndex);
    qreal width = viewSize.width() * devicePixelRatioF();
    qreal height = viewSize.height() * devicePixelRatioF();
    if (width <= 0.0 || height <= 0.0)
        return QSize();

    constexpr qreal maxPixels = 16.0 * 1000.0 * 1000.0;
    const qreal pixels = width * height;
    if (pixels > maxPixels) {
        const qreal factor = std::sqrt(maxPixels / pixels);
        width *= factor;
        height *= factor;
    }
    width = qBound(1.0, width, 8192.0);
    height = qBound(1.0, height, 8192.0);
    return QSize(qMax(1, qRound(width)), qMax(1, qRound(height)));
}

void PdfView::ensurePageImage(int pageIndex)
{
    const QSize size = pagePixelSize(pageIndex);
    if (size.isEmpty())
        return;
    if (m_renderer.hasCachedImage(pageIndex, size))
        return;
    m_renderer.requestPage(pageIndex, size);
}

bool PdfView::isPageVisible(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return false;
    const QRectF viewportRect(horizontalScrollBar()->value(), verticalScrollBar()->value(),
                              viewport()->width(), viewport()->height());
    return m_layout.pageRect(pageIndex).intersects(viewportRect);
}

void PdfView::updateScrollBars()
{
    const QSizeF content = m_layout.contentSize();
    const QSize view = viewport()->size();
    if (content.width() > 0.0 && content.height() > 0.0) {
        horizontalScrollBar()->setRange(0, qMax(0, qCeil(content.width() - view.width())));
        verticalScrollBar()->setRange(0, qMax(0, qCeil(content.height() - view.height())));
    } else {
        horizontalScrollBar()->setRange(0, 0);
        verticalScrollBar()->setRange(0, 0);
    }
    horizontalScrollBar()->setPageStep(qMax(1, view.width() * 9 / 10));
    verticalScrollBar()->setPageStep(qMax(1, view.height() * 9 / 10));
}

void PdfView::updateCurrentPage()
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    const QPointF center(horizontalScrollBar()->value() + viewport()->width() / 2.0,
                         verticalScrollBar()->value() + viewport()->height() / 2.0);
    int page = m_layout.pageAtPoint(center);
    if (page < 0)
        page = m_layout.pageAtY(center.y());
    if (page >= 0 && page != m_currentPage) {
        m_currentPage = page;
        emit pageChanged(page);
    }
}

void PdfView::rebuildLayoutAndKeepAnchor(qreal newZoom, int newRotation,
                                         const QPointF &viewAnchor)
{
    m_layout.setViewportSize(viewport()->size());

    int anchorPage = -1;
    QPointF anchorPagePoint;
    if (hasDocument() && !m_pageSizes.isEmpty()) {
        const QPointF contentAnchor = viewToContent(viewAnchor);
        anchorPage = m_layout.pageAtPoint(contentAnchor);
        if (anchorPage < 0)
            anchorPage = m_layout.pageAtY(contentAnchor.y());
        if (anchorPage >= 0)
            anchorPagePoint = m_layout.pageToContent(anchorPage).inverted().map(contentAnchor);
    }

    m_layout.setZoom(newZoom);
    m_layout.setRotation(newRotation);
    updateScrollBars();

    if (anchorPage >= 0) {
        const QPointF newContent = m_layout.pageToContent(anchorPage).map(anchorPagePoint);
        horizontalScrollBar()->setValue(qRound(newContent.x() - viewAnchor.x()));
        verticalScrollBar()->setValue(qRound(newContent.y() - viewAnchor.y()));
    }
    updateCurrentPage();
    viewport()->update();
}

void PdfView::centerPage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return;
    const QRectF rect = m_layout.pageRect(pageIndex);
    horizontalScrollBar()->setValue(qRound(rect.center().x() - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(rect.center().y() - viewport()->height() / 2.0));
    updateCurrentPage();
}

QPointF PdfView::viewToContent(const QPointF &viewPos) const
{
    return QPointF(viewPos.x() + horizontalScrollBar()->value(),
                   viewPos.y() + verticalScrollBar()->value());
}

QPointF PdfView::contentToView(const QPointF &contentPos) const
{
    return QPointF(contentPos.x() - horizontalScrollBar()->value(),
                   contentPos.y() - verticalScrollBar()->value());
}

int PdfView::pageAtViewPos(const QPointF &viewPos) const
{
    if (!hasDocument())
        return -1;
    return m_layout.pageAtPoint(viewToContent(viewPos));
}

QPointF PdfView::viewToPage(const QPointF &viewPos, int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return QPointF();
    return m_layout.pageToContent(pageIndex).inverted().map(viewToContent(viewPos));
}

QPointF PdfView::clampToPage(const QPointF &pagePoint, int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return pagePoint;
    const QSizeF size = m_pageSizes.value(pageIndex);
    return QPointF(qBound(0.0, pagePoint.x(), size.width()),
                   qBound(0.0, pagePoint.y(), size.height()));
}

void PdfView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    updateCurrentPage();
    viewport()->update();
}

void PdfView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const qreal factor = std::pow(1.0016, delta);
            const qreal target = qBound(0.05, m_layout.zoom() * factor, 12.0);
            if (!qFuzzyCompare(target, m_layout.zoom())) {
                rebuildLayoutAndKeepAnchor(target, m_layout.rotation(), event->position());
                emit zoomChanged(m_layout.zoom());
            }
            event->accept();
            return;
        }
    }
    if ((event->modifiers() & Qt::ShiftModifier) && event->angleDelta().y() != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                       - event->angleDelta().y());
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PdfView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_tool == Tool::Select) {
            deleteSelected();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        m_dragMode = DragMode::None;
        m_draft = Annotation();
        m_textSelecting = false;
        m_textSelection.reset();
        m_textSelectionPage = -1;
        selectAnnotation(QString());
        viewport()->update();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        if (!selectedText().isEmpty()) {
            copySelectedText();
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PdfView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    if (!hasDocument())
        return;

    viewport()->setFocus(Qt::MouseFocusReason);
    const QPointF viewPos = event->position();
    const QPointF contentPos = viewToContent(viewPos);
    const int pageIndex = m_layout.pageAtPoint(contentPos);
    m_dragStartView = viewPos;
    m_dragStartPage = QPointF();
    m_dragCurrentPage = QPointF();
    m_dragMoved = false;

    if (pageIndex < 0) {
        if (m_tool == Tool::Select)
            selectAnnotation(QString());
        return;
    }

    const QPointF pagePoint = viewToPage(viewPos, pageIndex);

    if (m_tool == Tool::Note) {
        Annotation annotation = makeBaseAnnotation(AnnotationType::NotePoint, pageIndex);
        annotation.p1 = pagePoint;
        annotation.rect = QRectF(pagePoint, QSizeF(0.0, 0.0));
        annotation.style.strokeColor = m_defaultStyle.strokeColor;
        addAnnotation(annotation, true);
        emit noteRequested(annotation.id);
        return;
    }

    if (m_tool == Tool::Eraser) {
        eraseAt(viewPos);
        return;
    }

    if (m_tool == Tool::Select) {
        if (!m_selectedId.isEmpty()) {
            const Annotation *selected = m_annotations.find(m_selectedId);
            if (selected && selected->isResizable()) {
                const QVector<QPointF> handles = handlePointsInPage(selected->bounds());
                const QTransform pageToView = QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                                                        -verticalScrollBar()->value())
                                              * m_layout.pageToContent(selected->pageIndex);
                for (int i = 0; i < handles.size(); ++i) {
                    if (QLineF(pageToView.map(handles.at(i)), viewPos).length() <= 7.0) {
                        startResize(m_selectedId, i, viewPos);
                        return;
                    }
                }
            }
        }

        const QString hit = annotationAt(viewPos);
        selectAnnotation(hit);
        if (!hit.isEmpty()) {
            const Annotation *annotation = m_annotations.find(hit);
            if (annotation && annotation->isMovable())
                beginAnnotationDrag(hit, viewPos);
        }
        return;
    }

    if (m_tool == Tool::TextSelect || m_tool == Tool::Highlight || m_tool == Tool::Underline
        || m_tool == Tool::StrikeOut) {
        m_dragPage = pageIndex;
        m_dragStartPage = clampToPage(pagePoint, pageIndex);
        m_dragCurrentPage = m_dragStartPage;
        m_textSelecting = true;
        m_textSelectionPage = pageIndex;
        m_textSelection.reset();
        m_dragMode = DragMode::TextSelection;
        return;
    }

    beginDraw(pageIndex, pagePoint);
}

void PdfView::mouseMoveEvent(QMouseEvent *event)
{
    if (!hasDocument()) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const QPointF viewPos = event->position();

    switch (m_dragMode) {
    case DragMode::TextSelection:
        updateTextSelection(viewPos);
        break;
    case DragMode::DrawShape:
    case DragMode::DrawPath:
        updateDraw(m_dragPage, viewToPage(viewPos, m_dragPage));
        break;
    case DragMode::DragAnnotation:
    case DragMode::ResizeAnnotation:
        updateAnnotationDrag(viewPos);
        break;
    case DragMode::None:
        break;
    }

    if (m_dragMode == DragMode::None && m_tool == Tool::Select) {
        Qt::CursorShape shape = Qt::ArrowCursor;
        const QString hit = annotationAt(viewPos);
        if (!hit.isEmpty()) {
            const Annotation *annotation = m_annotations.find(hit);
            if (annotation && annotation->isMovable())
                shape = Qt::SizeAllCursor;
        }
        if (!m_selectedId.isEmpty()) {
            const Annotation *selected = m_annotations.find(m_selectedId);
            if (selected && selected->isResizable()) {
                const QVector<QPointF> handles = handlePointsInPage(selected->bounds());
                const QTransform pageToView = QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                                                        -verticalScrollBar()->value())
                                              * m_layout.pageToContent(selected->pageIndex);
                for (int i = 0; i < handles.size(); ++i) {
                    const QPointF handle = pageToView.map(handles.at(i));
                    if (QLineF(handle, viewPos).length() <= 7.0) {
                        if (i == 0 || i == 2)
                            shape = Qt::SizeFDiagCursor;
                        else if (i == 1 || i == 3)
                            shape = Qt::SizeBDiagCursor;
                        else if (i == 4 || i == 6)
                            shape = Qt::SizeVerCursor;
                        else
                            shape = Qt::SizeHorCursor;
                        break;
                    }
                }
            }
        }
        viewport()->setCursor(shape);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void PdfView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    switch (m_dragMode) {
    case DragMode::TextSelection:
        finishTextSelection();
        break;
    case DragMode::DrawShape:
    case DragMode::DrawPath:
        finishDraw();
        break;
    case DragMode::DragAnnotation:
    case DragMode::ResizeAnnotation:
        finishAnnotationDrag();
        break;
    case DragMode::None:
        break;
    }
    m_dragMode = DragMode::None;
    m_textSelecting = false;
    viewport()->update();
}

void PdfView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && hasDocument() && m_tool == Tool::Select) {
        const QString hit = annotationAt(event->position());
        if (!hit.isEmpty()) {
            selectAnnotation(hit);
            emit noteRequested(hit);
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

QString PdfView::annotationAt(const QPointF &viewPos, int *handleIndex) const
{
    if (handleIndex)
        *handleIndex = -1;
    if (!hasDocument())
        return QString();
    const int pageIndex = pageAtViewPos(viewPos);
    if (pageIndex < 0)
        return QString();
    const QPointF pagePoint = viewToPage(viewPos, pageIndex);
    const qreal tolerance = qMax(2.0, 6.0 / m_layout.zoom());

    const QVector<Annotation> &items = m_annotations.annotations();
    for (int i = items.size() - 1; i >= 0; --i) {
        const Annotation &annotation = items.at(i);
        if (annotation.pageIndex != pageIndex)
            continue;
        if (annotation.type == AnnotationType::NotePoint) {
            if (noteHitAtViewPos(annotation, viewPos))
                return annotation.id;
            continue;
        }
        if (AnnotationRenderer::hitTest(annotation, pagePoint, tolerance))
            return annotation.id;
    }
    return QString();
}

bool PdfView::noteHitAtViewPos(const Annotation &annotation, const QPointF &viewPos) const
{
    if (annotation.pageIndex < 0 || annotation.pageIndex >= m_pageSizes.size())
        return false;
    const QTransform pageToView = QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                                            -verticalScrollBar()->value())
                                  * m_layout.pageToContent(annotation.pageIndex);
    return QLineF(pageToView.map(annotation.p1), viewPos).length() <= 9.0;
}

QPolygonF PdfView::annotationViewPolygon(const Annotation &annotation, int pageIndex) const
{
    const QRectF bounds = annotation.bounds();
    const QTransform pageToView = QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                                            -verticalScrollBar()->value())
                                  * m_layout.pageToContent(pageIndex);
    QPolygonF polygon;
    polygon << pageToView.map(bounds.topLeft()) << pageToView.map(bounds.topRight())
            << pageToView.map(bounds.bottomRight()) << pageToView.map(bounds.bottomLeft());
    return polygon;
}

QVector<QPointF> PdfView::handlePointsInPage(const QRectF &pageBounds) const
{
    const QRectF bounds = pageBounds.normalized();
    const QPointF center = bounds.center();
    QVector<QPointF> handles;
    handles << bounds.topLeft() << bounds.topRight() << bounds.bottomRight()
            << bounds.bottomLeft() << QPointF(center.x(), bounds.top())
            << QPointF(bounds.right(), center.y()) << QPointF(center.x(), bounds.bottom())
            << QPointF(bounds.left(), center.y());
    return handles;
}

QRectF PdfView::resizedBounds(const QRectF &original, int handle, const QPointF &pagePoint) const
{
    QRectF bounds = original.normalized();
    if (m_dragPage < 0 || m_dragPage >= m_pageSizes.size())
        return bounds;
    const QSizeF pageSize = m_pageSizes.value(m_dragPage);
    const qreal minSize = 4.0;

    qreal left = bounds.left();
    qreal top = bounds.top();
    qreal right = bounds.right();
    qreal bottom = bounds.bottom();

    switch (handle) {
    case 0:
        left = pagePoint.x();
        top = pagePoint.y();
        break;
    case 1:
        right = pagePoint.x();
        top = pagePoint.y();
        break;
    case 2:
        right = pagePoint.x();
        bottom = pagePoint.y();
        break;
    case 3:
        left = pagePoint.x();
        bottom = pagePoint.y();
        break;
    case 4:
        top = pagePoint.y();
        break;
    case 5:
        right = pagePoint.x();
        break;
    case 6:
        bottom = pagePoint.y();
        break;
    case 7:
        left = pagePoint.x();
        break;
    default:
        break;
    }

    left = qBound(0.0, left, pageSize.width() - minSize);
    top = qBound(0.0, top, pageSize.height() - minSize);
    right = qBound(left + minSize, right, pageSize.width());
    bottom = qBound(top + minSize, bottom, pageSize.height());
    return QRectF(QPointF(left, top), QPointF(right, bottom)).normalized();
}

void PdfView::beginTextSelection(const QPointF &viewPos)
{
    const int pageIndex = pageAtViewPos(viewPos);
    if (pageIndex < 0)
        return;
    m_dragPage = pageIndex;
    m_dragStartPage = clampToPage(viewToPage(viewPos, pageIndex), pageIndex);
    m_dragCurrentPage = m_dragStartPage;
    m_textSelecting = true;
    m_textSelectionPage = pageIndex;
    m_textSelection.reset();
    m_dragMode = DragMode::TextSelection;
}

void PdfView::updateTextSelection(const QPointF &viewPos)
{
    if (!m_textSelecting || m_dragPage < 0)
        return;
    const QPointF pagePoint = clampToPage(viewToPage(viewPos, m_dragPage), m_dragPage);
    m_dragCurrentPage = pagePoint;

    if (QLineF(m_dragStartPage, pagePoint).length() < 1.5) {
        m_textSelection.reset();
        viewport()->update();
        return;
    }

    QPointF start = m_dragStartPage;
    QPointF end = pagePoint;
    if (pointBefore(end, start))
        std::swap(start, end);

    QPdfSelection selection = m_document.pdf()->getSelection(m_dragPage, start, end);
    if (!selection.isValid() && start != end) {
        QPdfSelection swapped = m_document.pdf()->getSelection(m_dragPage, end, start);
        if (swapped.isValid())
            selection = swapped;
    }
    m_textSelection = selection;
    if (selection.isValid() && !selection.text().isEmpty())
        m_dragMoved = true;
    viewport()->update();
}

void PdfView::finishTextSelection()
{
    if (!m_textSelecting) {
        return;
    }
    const QPdfSelection *selection = m_textSelection ? &*m_textSelection : nullptr;
    const bool hasSelection = selection && selection->isValid()
                              && !selection->text().trimmed().isEmpty()
                              && !selection->bounds().isEmpty();

    if (m_tool == Tool::TextSelect) {
        if (hasSelection) {
            m_textSelectionPage = m_dragPage;
            emit statusMessage(tr("已选择 %1 个字符，Ctrl+C 复制").arg(selection->text().size()));
        } else {
            m_textSelection.reset();
            m_textSelectionPage = -1;
        }
        m_textSelecting = false;
        m_dragMode = DragMode::None;
        viewport()->update();
        return;
    }

    if (hasSelection) {
        AnnotationType type = AnnotationType::Highlight;
        if (m_tool == Tool::Underline)
            type = AnnotationType::Underline;
        else if (m_tool == Tool::StrikeOut)
            type = AnnotationType::StrikeOut;
        createTextAnnotation(*selection, m_dragPage);
        Q_UNUSED(type);
    }
    m_textSelection.reset();
    m_textSelectionPage = -1;
    m_textSelecting = false;
}

void PdfView::beginDraw(int pageIndex, const QPointF &pagePoint)
{
    AnnotationType type = AnnotationType::Pencil;
    switch (m_tool) {
    case Tool::Line:
        type = AnnotationType::Line;
        break;
    case Tool::Rectangle:
        type = AnnotationType::Rectangle;
        break;
    case Tool::Ellipse:
        type = AnnotationType::Ellipse;
        break;
    case Tool::Pencil:
        type = AnnotationType::Pencil;
        break;
    case Tool::Highlighter:
        type = AnnotationType::Highlighter;
        break;
    default:
        return;
    }

    m_dragPage = pageIndex;
    m_dragStartPage = clampToPage(pagePoint, pageIndex);
    m_dragCurrentPage = m_dragStartPage;
    m_dragMoved = false;
    m_draft = makeBaseAnnotation(type, pageIndex);
    m_draft.path.clear();
    m_draft.polygons.clear();
    if (type == AnnotationType::Highlighter) {
        m_draft.style.strokeWidthPt = qMax(12.0, m_defaultStyle.strokeWidthPt * 3.0);
        m_draft.style.blend = BlendMode::Multiply;
        m_draft.style.opacity = qMin(0.5, m_defaultStyle.opacity);
        m_draft.style.strokeColor = m_defaultStyle.strokeColor;
    } else {
        m_draft.style = m_defaultStyle;
    }
    m_dragMode = (type == AnnotationType::Pencil || type == AnnotationType::Highlighter)
                     ? DragMode::DrawPath
                     : DragMode::DrawShape;
    updateDraw(pageIndex, m_dragStartPage);
}

void PdfView::updateDraw(int pageIndex, const QPointF &pagePoint)
{
    if (m_draft.pageIndex != pageIndex)
        return;
    const QPointF point = clampToPage(pagePoint, pageIndex);
    m_dragCurrentPage = point;

    switch (m_draft.type) {
    case AnnotationType::Line:
        m_draft.p1 = m_dragStartPage;
        m_draft.p2 = shiftHeld() ? snapToAxis(m_dragStartPage, point) : point;
        m_draft.rect = QRectF(m_draft.p1, m_draft.p2).normalized();
        break;
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse:
        m_draft.rect = QRectF(m_dragStartPage, point).normalized();
        break;
    case AnnotationType::Pencil:
    case AnnotationType::Highlighter: {
        if (m_draft.path.isEmpty())
            m_draft.path.append(m_dragStartPage);
        if (shiftHeld()) {
            m_draft.path = QVector<QPointF>{m_dragStartPage, snapToAxis(m_dragStartPage, point)};
        } else if (m_draft.path.last() != point) {
            if (QLineF(m_draft.path.last(), point).length() >= 0.4)
                m_draft.path.append(point);
        }
        QRectF bounds(m_dragStartPage, QSizeF(0.0, 0.0));
        for (const QPointF &p : m_draft.path)
            bounds |= QRectF(p, QSizeF(0.0, 0.0));
        m_draft.rect = bounds.adjusted(-m_draft.style.strokeWidthPt / 2.0,
                                       -m_draft.style.strokeWidthPt / 2.0,
                                       m_draft.style.strokeWidthPt / 2.0,
                                       m_draft.style.strokeWidthPt / 2.0);
        break;
    }
    default:
        break;
    }
    viewport()->update();
}

void PdfView::finishDraw()
{
    if (m_draft.pageIndex < 0)
        return;
    bool valid = false;
    if (m_draft.type == AnnotationType::Pencil || m_draft.type == AnnotationType::Highlighter) {
        valid = m_draft.path.size() >= 2
                || (m_draft.path.size() == 1
                    && QLineF(m_dragStartPage, m_dragCurrentPage).length() > 0.5);
        if (m_draft.path.size() == 1)
            m_draft.path.append(m_dragCurrentPage);
    } else if (m_draft.type == AnnotationType::Line) {
        valid = QLineF(m_draft.p1, m_draft.p2).length() >= 1.5;
    } else {
        valid = m_draft.rect.width() >= 2.0 && m_draft.rect.height() >= 2.0;
    }

    if (valid) {
        m_draft.id = newAnnotationId();
        m_draft.createdAt = nowIsoUtc();
        m_draft.modifiedAt = m_draft.createdAt;
        addAnnotation(m_draft, true);
    }
    m_draft = Annotation();
    m_dragPage = -1;
    m_dragMode = DragMode::None;
    viewport()->update();
}

void PdfView::beginAnnotationDrag(const QString &annotationId, const QPointF &viewPos)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation)
        return;
    m_editId = annotationId;
    m_editOriginal = *annotation;
    m_draft = *annotation;
    m_dragPage = annotation->pageIndex;
    m_dragStartPage = clampToPage(viewToPage(viewPos, m_dragPage), m_dragPage);
    m_dragLastPage = m_dragStartPage;
    m_dragMoved = false;
    m_dragMode = DragMode::DragAnnotation;
}

void PdfView::startResize(const QString &annotationId, int handle, const QPointF &viewPos)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation || !annotation->isResizable())
        return;
    m_editId = annotationId;
    m_editOriginal = *annotation;
    m_draft = *annotation;
    m_dragPage = annotation->pageIndex;
    m_dragStartPage = clampToPage(viewToPage(viewPos, m_dragPage), m_dragPage);
    m_resizeHandle = handle;
    m_dragMoved = false;
    m_dragMode = DragMode::ResizeAnnotation;
}

void PdfView::updateAnnotationDrag(const QPointF &viewPos)
{
    if (m_editId.isEmpty() || m_dragPage < 0)
        return;
    const QPointF point = clampToPage(viewToPage(viewPos, m_dragPage), m_dragPage);

    if (m_dragMode == DragMode::DragAnnotation) {
        const QRectF bounds = m_editOriginal.bounds();
        const QSizeF pageSize = m_pageSizes.value(m_dragPage);
        qreal dx = point.x() - m_dragStartPage.x();
        qreal dy = point.y() - m_dragStartPage.y();
        dx = qBound(-bounds.left(), dx, pageSize.width() - bounds.right());
        dy = qBound(-bounds.top(), dy, pageSize.height() - bounds.bottom());
        if (std::abs(dx) + std::abs(dy) > 0.5 / m_layout.zoom())
            m_dragMoved = true;
        m_draft = m_editOriginal;
        m_draft.translate(QPointF(dx, dy));
        m_draft.modifiedAt = nowIsoUtc();
    } else if (m_dragMode == DragMode::ResizeAnnotation) {
        const QRectF newBounds = resizedBounds(m_editOriginal.bounds(), m_resizeHandle, point);
        m_draft = m_editOriginal;
        m_draft.setBounds(newBounds);
        if (newBounds != m_editOriginal.bounds().normalized())
            m_dragMoved = true;
        m_draft.modifiedAt = nowIsoUtc();
    }
    viewport()->update();
}

void PdfView::finishAnnotationDrag()
{
    const bool changed = m_dragMoved && !m_editId.isEmpty();
    if (changed) {
        m_draft.modifiedAt = nowIsoUtc();
        m_undoStack.push(new ModifyAnnotationCommand(&m_annotations, m_editOriginal, m_draft));
        emit statusMessage(tr("已修改批注位置或大小"));
    }
    m_editId.clear();
    m_resizeHandle = -1;
    m_draft = Annotation();
    m_dragMode = DragMode::None;
    viewport()->update();
}

Annotation PdfView::makeBaseAnnotation(AnnotationType type, int pageIndex) const
{
    Annotation annotation;
    annotation.id = newAnnotationId();
    annotation.pageIndex = pageIndex;
    annotation.type = type;
    annotation.style = m_defaultStyle;
    annotation.createdAt = nowIsoUtc();
    annotation.modifiedAt = annotation.createdAt;
    return annotation;
}

void PdfView::createTextAnnotation(const QPdfSelection &selection, int pageIndex)
{
    if (!selection.isValid() || selection.bounds().isEmpty())
        return;

    AnnotationType type = AnnotationType::Highlight;
    if (m_tool == Tool::Underline)
        type = AnnotationType::Underline;
    else if (m_tool == Tool::StrikeOut)
        type = AnnotationType::StrikeOut;

    Annotation annotation = makeBaseAnnotation(type, pageIndex);
    annotation.rect = selection.boundingRectangle().normalized();
    annotation.polygons = selection.bounds();
    annotation.textStart = selection.startIndex();
    annotation.textEnd = selection.endIndex();
    annotation.text = selection.text();
    annotation.style = m_defaultStyle;
    if (type == AnnotationType::Highlight) {
        annotation.style.blend = BlendMode::Multiply;
        annotation.style.opacity = qMin(0.5, m_defaultStyle.opacity);
        annotation.style.strokeWidthPt = 1.0;
    } else {
        annotation.style.blend = BlendMode::Normal;
        annotation.style.opacity = 1.0;
        annotation.style.strokeWidthPt = qMax(1.0, m_defaultStyle.strokeWidthPt);
    }
    addAnnotation(annotation, true);
}

void PdfView::addAnnotation(const Annotation &annotation, bool selectAfter)
{
    Annotation copy = annotation;
    if (copy.id.isEmpty())
        copy.id = newAnnotationId();
    if (copy.createdAt.isEmpty())
        copy.createdAt = nowIsoUtc();
    copy.modifiedAt = nowIsoUtc();
    m_undoStack.push(new AddAnnotationCommand(&m_annotations, copy));
    if (selectAfter)
        selectAnnotation(copy.id);
    emit statusMessage(tr("已添加%1").arg(annotationTypeDisplayName(copy.type)));
}

void PdfView::eraseAt(const QPointF &viewPos)
{
    const QString hit = annotationAt(viewPos);
    if (hit.isEmpty()) {
        emit statusMessage(tr("此处没有可删除的批注"));
        return;
    }
    const Annotation *annotation = m_annotations.find(hit);
    if (!annotation)
        return;
    const Annotation copy = *annotation;
    m_undoStack.push(new RemoveAnnotationsCommand(&m_annotations, {copy}));
    if (m_selectedId == hit)
        selectAnnotation(QString());
    emit statusMessage(tr("已删除%1").arg(annotationTypeDisplayName(copy.type)));
}

void PdfView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()
            && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
}

void PdfView::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            emit openFileRequested(path);
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
}

} // namespace pano
