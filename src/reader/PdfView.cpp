#include "reader/PdfView.h"

#include "annotation/AnnotationCommands.h"
#include "annotation/AnnotationGeometry.h"
#include "annotation/AnnotationRenderer.h"
#include "annotation/TextSelectionGeometry.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>

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
    const qreal step = std::numbers::pi / 4.0;
    const qreal angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
    return start + QPointF(std::cos(angle), std::sin(angle)) * length;
}

bool shiftHeld()
{
    return QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
}

// Keep a constrained point inside the page by shortening the vector from the
// start point, rather than clipping x and y independently (which would change
// a 45-degree or axis-snapped direction).
QPointF constrainPointToPage(const QPointF &start, const QPointF &end, const QSizeF &pageSize)
{
    const QPointF delta = end - start;
    if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
        return start;
    qreal t = 1.0;
    if (delta.x() > 0.0)
        t = qMin(t, (pageSize.width() - start.x()) / delta.x());
    else if (delta.x() < 0.0)
        t = qMin(t, (0.0 - start.x()) / delta.x());
    if (delta.y() > 0.0)
        t = qMin(t, (pageSize.height() - start.y()) / delta.y());
    else if (delta.y() < 0.0)
        t = qMin(t, (0.0 - start.y()) / delta.y());
    return start + delta * qBound(0.0, t, 1.0);
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
    m_defaultStyle.opacity = 1.0;
    m_defaultStyle.blend = BlendMode::Normal;

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

PdfView::~PdfView()
{
    // Members such as QUndoStack can emit during their own destruction.  The
    // view may already be a child of a destructing workspace, so make sure no
    // signal reaches half-destroyed receivers.
    blockSignals(true);
    m_undoStack.blockSignals(true);
    m_annotations.blockSignals(true);
    m_renderer.blockSignals(true);
}

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
    m_layout.setCurrentPage(0);
    m_fitPending = true;
    m_zoomMode = ZoomMode::FitPage;
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
    if (m_dragMode == DragMode::Erase)
        cancelErase();
    clearTemporarySelections();
    m_tool = tool;
    m_dragMode = DragMode::None;
    m_draft = Annotation();
    m_editId.clear();
    m_dragPage = -1;
    m_textSelecting = false;
    m_eraseIds.clear();
    m_erasePreviewIds.clear();
    m_eraseRemoved.clear();
    m_wheelAngleAccum = 0;
    m_wheelPixelAccum = 0.0;
    m_wheelLastDirection = 0;
    m_wheelCooldown.invalidate();
    m_textHitValid = false;
    applyToolPreset(tool);

    Qt::CursorShape shape = Qt::ArrowCursor;
    if (tool == Tool::RegionSelect)
        shape = Qt::CrossCursor;
    else if (tool == Tool::TextSelect)
        shape = Qt::IBeamCursor;
    else if (tool == Tool::Hand)
        shape = Qt::OpenHandCursor;
    else if (tool == Tool::Note)
        shape = Qt::PointingHandCursor;
    else if (tool == Tool::Eraser)
        updateEraserCursor();
    else if (tool != Tool::Select)
        shape = Qt::CrossCursor;
    if (tool != Tool::Eraser)
        viewport()->setCursor(shape);
    viewport()->update();
    emit toolChanged(tool);
}

QString PdfView::toolDisplayName(Tool tool)
{
    switch (tool) {
    case Tool::Select:
        return tr("选择/移动");
    case Tool::Hand:
        return tr("抓手");
    case Tool::RegionSelect:
        return tr("框选");
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
    m_styleUserModified = true;
}

void PdfView::refreshToolPreset()
{
    m_styleUserModified = false;
    applyToolPreset(m_tool);
}

void PdfView::applyToolPreset(Tool tool)
{
    if (m_styleUserModified)
        return;

    AnnotationStyle style = m_defaultStyle;
    style.strokeColor = QColor(QStringLiteral("#f2b705"));
    switch (tool) {
    case Tool::Highlight:
        style.strokeWidthPt = 1.0;
        style.opacity = 0.45;
        style.blend = BlendMode::Multiply;
        style.fillColor = QColor();
        break;
    case Tool::Highlighter:
        style.strokeWidthPt = 12.0;
        style.opacity = 0.4;
        style.blend = BlendMode::Multiply;
        style.fillColor = QColor();
        break;
    case Tool::Underline:
    case Tool::StrikeOut:
        style.strokeWidthPt = 1.5;
        style.opacity = 1.0;
        style.blend = BlendMode::Normal;
        style.fillColor = QColor();
        break;
    case Tool::Line:
    case Tool::Rectangle:
    case Tool::Ellipse:
    case Tool::Pencil:
        style.strokeWidthPt = 2.0;
        style.opacity = 1.0;
        style.blend = BlendMode::Normal;
        style.fillColor = QColor();
        break;
    case Tool::Select:
    case Tool::Hand:
    case Tool::TextSelect:
    case Tool::Eraser:
    case Tool::Note:
        return;
    }
    m_defaultStyle = style;
}

qreal PdfView::zoom() const
{
    return m_layout.zoom();
}

void PdfView::setZoom(qreal zoom)
{
    m_fitPending = false;
    const bool modeChanged = m_zoomMode != ZoomMode::Fixed;
    m_zoomMode = ZoomMode::Fixed;
    if (modeChanged)
        emit zoomModeChanged(m_zoomMode);
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
    if (viewport()->width() < 120 || viewport()->height() < 120) {
        m_fitPending = true;
        m_zoomMode = ZoomMode::FitWidth;
        emit zoomModeChanged(m_zoomMode);
        return;
    }
    m_fitPending = false;
    m_zoomMode = ZoomMode::FitWidth;
    applyFitZoom();
    emit zoomModeChanged(m_zoomMode);
}

void PdfView::fitPage()
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    // A document is often opened before the tab has received its final size.
    // Defer the fit until the viewport is large enough to produce a useful zoom.
    if (viewport()->width() < 120 || viewport()->height() < 120) {
        m_fitPending = true;
        m_zoomMode = ZoomMode::FitPage;
        emit zoomModeChanged(m_zoomMode);
        return;
    }
    m_fitPending = false;
    m_zoomMode = ZoomMode::FitPage;
    applyFitZoom();
    emit zoomModeChanged(m_zoomMode);
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
    if (m_zoomMode == ZoomMode::FitWidth || m_zoomMode == ZoomMode::FitPage)
        applyFitZoom();
    emit rotationChanged(m_layout.rotation());
}

void PdfView::rotateBy(int degrees)
{
    setRotation(m_layout.rotation() + degrees);
}

PageLayoutMode PdfView::layoutMode() const
{
    return m_layout.layoutMode();
}

void PdfView::setLayoutMode(PageLayoutMode mode)
{
    if (!hasDocument() || m_layout.layoutMode() == mode)
        return;
    const QPointF viewAnchor(viewport()->width() / 2.0, viewport()->height() / 2.0);
    int anchorPage = m_currentPage;
    QPointF anchorPoint = m_layout.pageToContent(anchorPage).inverted().map(viewToContent(viewAnchor));
    m_layout.setLayoutMode(mode);
    m_layout.setCurrentPage(anchorPage);
    m_wheelAngleAccum = 0;
    m_wheelPixelAccum = 0.0;
    m_wheelLastDirection = 0;
    m_wheelCooldown.invalidate();
    if (m_zoomMode == ZoomMode::FitWidth || m_zoomMode == ZoomMode::FitPage)
        applyFitZoom();
    else {
        updateScrollBars();
        m_currentPage = anchorPage;
        restoreViewportAnchor(anchorPage, anchorPoint);
    }
    emit layoutModeChanged(mode);
    emit pageChanged(m_currentPage);
    viewport()->update();
}

bool PdfView::firstPageAlone() const
{
    return m_layout.firstPageAlone();
}

void PdfView::setFirstPageAlone(bool firstAlone)
{
    if (m_layout.firstPageAlone() == firstAlone)
        return;
    const QPointF viewAnchor(viewport()->width() / 2.0, viewport()->height() / 2.0);
    int anchorPage = m_currentPage;
    QPointF anchorPoint = m_layout.pageToContent(anchorPage).inverted().map(viewToContent(viewAnchor));
    m_layout.setFirstPageAlone(firstAlone);
    m_layout.setCurrentPage(anchorPage);
    if (m_zoomMode == ZoomMode::FitWidth || m_zoomMode == ZoomMode::FitPage)
        applyFitZoom();
    else
        restoreViewportAnchor(anchorPage, anchorPoint);
    emit firstPageAloneChanged(firstAlone);
    viewport()->update();
}

int PdfView::pageGap() const
{
    return m_layout.gap();
}

void PdfView::setPageGap(int gap)
{
    if (m_layout.gap() == gap)
        return;
    m_layout.setMargins(m_layout.margin(), gap);
    rebuildLayoutAndKeepAnchor(m_layout.zoom(), m_layout.rotation(),
                               viewport()->rect().center());
}

bool PdfView::wheelTurnsPage() const
{
    return m_wheelTurnsPage;
}

void PdfView::setWheelTurnsPage(bool enabled)
{
    m_wheelTurnsPage = enabled;
    m_wheelAngleAccum = 0;
    m_wheelPixelAccum = 0.0;
    m_wheelLastDirection = 0;
    m_wheelCooldown.invalidate();
}

void PdfView::setZoomMode(ZoomMode mode)
{
    if (m_zoomMode == mode && mode != ZoomMode::Fixed) {
        applyFitZoom();
        return;
    }
    m_zoomMode = mode;
    if (mode == ZoomMode::FitWidth || mode == ZoomMode::FitPage) {
        if (!hasDocument())
            return;
        applyFitZoom();
    }
    emit zoomModeChanged(mode);
}
int PdfView::pageCount() const
{
    return m_pageSizes.size();
}

void PdfView::goToPage(int pageIndex)
{
    navigateTo(pageIndex, QPointF(), NavigationAlignment::PageTop,
               NavigationReason::PageInput);
}

QPointF PdfView::viewportAnchor(int *pageIndex) const
{
    if (pageIndex)
        *pageIndex = m_currentPage;
    if (!hasDocument() || m_pageSizes.isEmpty())
        return QPointF();

    const QPointF center(viewport()->width() / 2.0, viewport()->height() / 2.0);
    int target = m_currentPage;
    for (int page = 0; page < m_pageSizes.size(); ++page) {
        if (pageRectInView(page).contains(center)) {
            target = page;
            break;
        }
        if (pageRectInView(page).top() <= center.y() && pageRectInView(page).bottom() >= center.y()) {
            target = page;
            break;
        }
    }
    if (pageIndex)
        *pageIndex = target;
    const QPointF pagePoint = pageToViewTransform(target).inverted().map(center);
    const QSizeF pageSize = m_pageSizes.value(target);
    return QPointF(qBound<qreal>(0.0, pagePoint.x(), pageSize.width()),
                   qBound<qreal>(0.0, pagePoint.y(), pageSize.height()));
}

void PdfView::restoreViewportAnchor(int pageIndex, const QPointF &pagePoint)
{
    if (!hasDocument() || pageIndex < 0 || pageIndex >= pageCount())
        return;
    if (!pagePoint.isNull()) {
        navigateTo(pageIndex, pagePoint, NavigationAlignment::Center,
                   NavigationReason::Restore);
    }
}

void PdfView::goToPosition(int pageIndex, const QPointF &pagePoint)
{
    navigateTo(pageIndex, pagePoint, NavigationAlignment::Center,
               NavigationReason::Programmatic);
}

void PdfView::navigateTo(int pageIndex, const QPointF &pagePoint,
                         NavigationAlignment alignment, NavigationReason reason)
{
    if (!hasDocument() || pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return;

    const bool pageChangedBefore = pageIndex != m_currentPage;
    int fromPage = m_currentPage;
    const QPointF fromPoint = viewportAnchor(&fromPage);
    emit aboutToNavigate(fromPage, fromPoint, pageIndex, pagePoint, static_cast<int>(reason));

    m_currentPage = pageIndex;
    m_layout.setCurrentPage(pageIndex);
    if (!m_layout.isContinuous() && m_zoomMode != ZoomMode::Fixed)
        applyFitZoom();
    if (m_layout.pageRect(pageIndex).isNull())
        return;
    updateScrollBars();

    const QPointF point = clampToPage(pagePoint, pageIndex);
    const QPointF contentPoint = m_layout.pageToContent(pageIndex).map(point);
    const QRectF targetRect = m_layout.isFacing() ? m_layout.spreadRect(pageIndex)
                                                  : m_layout.pageRect(pageIndex);

    if (alignment == NavigationAlignment::Center && !pagePoint.isNull()) {
        horizontalScrollBar()->setValue(qRound(contentPoint.x() - viewport()->width() / 2.0));
        verticalScrollBar()->setValue(qRound(contentPoint.y() - viewport()->height() / 2.0));
    } else if (alignment == NavigationAlignment::PageBottom) {
        horizontalScrollBar()->setValue(qRound(targetRect.center().x() - viewport()->width() / 2.0));
        verticalScrollBar()->setValue(qRound(targetRect.bottom() + m_layout.margin()
                                             - viewport()->height()));
    } else {
        const QPointF center(targetRect.center().x(), targetRect.top() + m_layout.margin());
        horizontalScrollBar()->setValue(qRound(center.x() - viewport()->width() / 2.0));
        verticalScrollBar()->setValue(qRound(targetRect.top() - m_layout.margin()));
    }

    if (pageChangedBefore)
        emit pageChanged(m_currentPage);
    emit navigated(pageIndex, point, static_cast<int>(reason));
    viewport()->update();
}

void PdfView::navigatePageStep(int direction, bool history)
{
    if (!hasDocument() || direction == 0)
        return;
    const NavigationReason reason = history ? NavigationReason::PageInput
                                             : NavigationReason::Internal;
    const int current = qBound(0, m_currentPage, m_pageSizes.size() - 1);
    const QVector<int> spread = m_layout.spreadPages(current);
    if (direction > 0) {
        const int next = (spread.isEmpty() ? current : spread.last()) + 1;
        if (next < m_pageSizes.size())
            navigateTo(next, QPointF(), NavigationAlignment::PageTop, reason);
    } else {
        const int previous = (spread.isEmpty() ? current : spread.first()) - 1;
        if (previous >= 0)
            navigateTo(previous, QPointF(), NavigationAlignment::PageBottom, reason);
    }
}

void PdfView::scrollPageViewport(int direction)
{
    if (!hasDocument() || direction == 0)
        return;
    const qreal step = viewport()->height() * 0.9;
    verticalScrollBar()->setValue(qRound(verticalScrollBar()->value()
                                         + (direction > 0 ? step : -step)));
}

void PdfView::revealPageRegion(int pageIndex, const QRectF &pageBounds)
{
    if (!hasDocument() || pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return;

    int fromPage = m_currentPage;
    const QPointF fromPoint = viewportAnchor(&fromPage);
    emit aboutToNavigate(fromPage, fromPoint, pageIndex, pageBounds.center(),
                         static_cast<int>(NavigationReason::Annotation));
    m_layout.setCurrentPage(pageIndex);
    if (m_layout.pageRect(pageIndex).isNull())
        return;
    updateScrollBars();
    const QTransform pageToView = pageToViewTransform(pageIndex);
    const QRectF bounds = pageBounds.normalized();
    if (bounds.isEmpty()) {
        goToPosition(pageIndex, bounds.topLeft());
        return;
    }

    const QRectF regionInView = pageToView.mapRect(bounds);
    const QRectF viewportRect(0.0, 0.0, viewport()->width(), viewport()->height());
    const qreal margin = 40.0;
    const bool alreadyVisible = viewportRect.adjusted(margin, margin, -margin, -margin)
                                    .contains(regionInView);
    if (alreadyVisible) {
        if (pageIndex != m_currentPage) {
            m_currentPage = pageIndex;
            emit pageChanged(m_currentPage);
        }
        viewport()->update();
        return;
    }

    QPointF targetContent;
    if (regionInView.width() > viewportRect.width() - 2.0 * margin
        || regionInView.height() > viewportRect.height() - 2.0 * margin) {
        // Large target: show its starting edge and keep some surrounding context.
        targetContent = m_layout.pageToContent(pageIndex).map(bounds.topLeft());
        horizontalScrollBar()->setValue(qRound(targetContent.x() - margin));
        verticalScrollBar()->setValue(qRound(targetContent.y() - margin));
    } else {
        targetContent = m_layout.pageToContent(pageIndex).map(bounds.center());
        horizontalScrollBar()->setValue(
            qRound(targetContent.x() - viewport()->width() / 2.0));
        verticalScrollBar()->setValue(
            qRound(targetContent.y() - viewport()->height() / 2.0));
    }

    if (pageIndex != m_currentPage) {
        m_currentPage = pageIndex;
        emit pageChanged(m_currentPage);
    }
    emit navigated(pageIndex, bounds.center(), static_cast<int>(NavigationReason::Annotation));
    viewport()->update();
}

void PdfView::revealAnnotation(const QString &annotationId)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation)
        return;
    selectAnnotation(annotationId);
    revealPageRegion(annotation->pageIndex, annotationPageRegion(*annotation));
}

void PdfView::selectAnnotation(const QString &annotationId)
{
    if (m_selectedId == annotationId)
        return;
    m_selectedId = m_annotations.find(annotationId) ? annotationId : QString();
    emit annotationSelectionChanged(m_selectedId);
    viewport()->update();
}

void PdfView::deleteAnnotation(const QString &annotationId, bool confirmed)
{
    const Annotation *annotation = m_annotations.find(annotationId);
    if (!annotation)
        return;

    if (!confirmed && !annotation->note.isEmpty()) {
        emit noteDeleteRequested(annotationId);
        return;
    }

    const Annotation copy = *annotation;
    m_undoStack.push(new RemoveAnnotationsCommand(&m_annotations, {copy}));
    if (m_selectedId == annotationId) {
        m_selectedId.clear();
        emit annotationSelectionChanged(QString());
    }
    emit statusMessage(copy.note.isEmpty()
                           ? tr("已删除%1").arg(annotationTypeDisplayName(copy.type))
                           : tr("已删除%1及其附属笔记，可用 Ctrl+Z 撤销")
                                 .arg(annotationTypeDisplayName(copy.type)));
}

void PdfView::deleteSelected()
{
    if (m_selectedId.isEmpty())
        return;
    deleteAnnotation(m_selectedId);
}

void PdfView::applyStyleToSelected(const AnnotationStyle &style)
{
    applyStyleToAnnotation(m_selectedId, style);
}

void PdfView::applyStyleToAnnotation(const QString &annotationId, const AnnotationStyle &style)
{
    if (annotationId.isEmpty())
        return;
    const Annotation *annotation = m_annotations.find(annotationId);
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
        && after.style.blend == annotation->style.blend
        && after.style.lineStyle == annotation->style.lineStyle
        && after.style.strokeEnabled == annotation->style.strokeEnabled) {
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
    if (m_fitPending)
        fitPage();
    else if (m_zoomMode == ZoomMode::FitWidth || m_zoomMode == ZoomMode::FitPage)
        applyFitZoom();
    updateCurrentPage();
    emit readingAnchorChanged();
    viewport()->update();
}

void PdfView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(viewport()->rect(), palette().color(QPalette::AlternateBase));

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
        const QTransform pageToView = pageToViewTransform(page);
        paintPage(painter, page, pageRectInView, pageToView);
    }

    for (auto it = m_searchHighlights.constBegin(); it != m_searchHighlights.constEnd(); ++it) {
        const int page = it.key();
        if (page < 0 || page >= m_pageSizes.size() || !isPageVisible(page))
            continue;
        const QTransform pageToView = pageToViewTransform(page);
        painter.save();
        painter.setClipRect(pageRectInView(page));
        painter.setTransform(pageToView);
        for (const QRectF &rect : it.value()) {
            const bool current = page == m_searchCurrentPage
                                  && m_searchCurrentRects.contains(rect);
            painter.fillRect(rect, QColor(255, 206, 84, current ? 190 : 110));
            painter.setPen(QPen(QColor(230, 150, 0), current ? 1.5 : 0.8));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect);
        }
        painter.restore();
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
        if (m_dragMode == DragMode::Erase && m_erasePreviewIds.contains(annotation.id))
            continue;
        const QTransform pageToView = pageToViewTransform(annotation.pageIndex);
        const QRectF pageViewRect = pageRectInView(annotation.pageIndex);
        AnnotationRenderer::paint(painter, annotation, pageToView, pageViewRect,
                                  devicePixelRatioF());
    }

    const bool previewingNewStroke = (m_dragMode == DragMode::DrawShape
                                      || m_dragMode == DragMode::DrawPath)
                                     && m_draft.pageIndex >= 0;
    const bool previewingEditedObject = (m_dragMode == DragMode::DragAnnotation
                                         || m_dragMode == DragMode::ResizeAnnotation)
                                        && m_draft.pageIndex >= 0;
    if (previewingNewStroke || previewingEditedObject) {
        AnnotationRenderer::paint(painter, m_draft,
                                  pageToViewTransform(m_draft.pageIndex),
                                  pageRectInView(m_draft.pageIndex), devicePixelRatioF());
    }

    paintSelectedAnnotationOutline(painter);
}

void PdfView::paintPage(QPainter &painter, int pageIndex, const QRectF &pageViewRect,
                        const QTransform &pageToView)
{
    painter.save();
    painter.setClipRect(pageViewRect.adjusted(-2.0, -2.0, 2.0, 2.0));

    painter.fillRect(pageViewRect.translated(0.0, 3.0), QColor(0, 0, 0, 28));
    painter.fillRect(pageViewRect, Qt::white);

    const QSize pixelSize = pagePixelSize(pageIndex);
    const QImage image = m_renderer.cachedImage(pageIndex, pixelSize);
    if (!image.isNull()) {
        painter.save();
        painter.setClipRect(pageViewRect, Qt::IntersectClip);
        painter.setWorldTransform(pageToView);
        const QRectF sourcePageRect(QPointF(0.0, 0.0), m_layout.pageSizePt(pageIndex));
        painter.drawImage(sourcePageRect, image);
        painter.restore();
    } else {
        ensurePageImage(pageIndex);
        painter.setPen(QColor(QStringLiteral("#9aa1ab")));
        QFont font = painter.font();
        font.setPointSizeF(qMax(9.0, font.pointSizeF()));
        painter.setFont(font);
        painter.drawText(pageViewRect, Qt::AlignCenter, tr("正在渲染页面…"));
    }

    painter.setPen(QPen(QColor(QStringLiteral("#d3d7de")), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(pageViewRect.adjusted(0.5, 0.5, -0.5, -0.5));
    painter.restore();
}

void PdfView::paintSelectionOverlay(QPainter &painter)
{
    if (!m_selectionOverlayVisible)
        return;
    if (hasRegionSelection()) {
        const QTransform pageToView = pageToViewTransform(m_regionSelectionPage);
        const QRectF pageViewRect = pageRectInView(m_regionSelectionPage);
        const QRectF rect = pageToView.mapRect(m_regionSelection);
        painter.save();
        painter.setClipRect(pageViewRect);
        QColor fill(QStringLiteral("#2f6fed"));
        fill.setAlpha(40);
        painter.fillRect(rect, fill);
        painter.setPen(QPen(QColor(QStringLiteral("#2f6fed")), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect);
        painter.restore();
        return;
    }
    if (!m_textSelection || m_textSelectionPage < 0 || m_textSelectionPage >= m_pageSizes.size())
        return;
    if (!m_textSelection->isValid() || m_textSelection->bounds().isEmpty())
        return;
    if (!isPageVisible(m_textSelectionPage))
        return;

    const QTransform pageToView = pageToViewTransform(m_textSelectionPage);
    const QRectF pageViewRect = pageRectInView(m_textSelectionPage);

    QColor color;
    if (m_tool == Tool::TextSelect)
        color = QColor(QStringLiteral("#2f6fed"));
    else if (m_tool == Tool::Highlight || m_tool == Tool::Highlighter)
        color = m_defaultStyle.strokeColor;
    else
        color = m_defaultStyle.strokeColor;
    color.setAlpha(m_tool == Tool::TextSelect ? 70 : 85);

    painter.save();
    painter.setClipRect(pageViewRect);
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

    const QTransform pageToView = pageToViewTransform(selection->pageIndex);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (selection->type == AnnotationType::NotePoint) {
        // rect.topLeft() is the canonical pin position used by painting and hit
        // testing; p1 is kept in JSON for backward compatibility only.
        const QPointF center = pageToView.map(selection->rect.topLeft());
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
            const QVector<QPointF> handles = handlePointsInPage(*selection);
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

    // The raster image is always the original, unrotated page. User view
    // rotation is applied by pageToViewTransform() at paint time, so the
    // pixel aspect ratio must follow the unrotated page too.
    const QSizeF pageSize = m_layout.pageSizePt(pageIndex);
    const qreal scale = m_layout.zoom() * devicePixelRatioF();
    qreal width = pageSize.width() * scale;
    qreal height = pageSize.height() * scale;
    if (width <= 0.0 || height <= 0.0)
        return QSize();

    constexpr qreal maxPixels = 16.0 * 1000.0 * 1000.0;
    constexpr qreal maxDimension = 8192.0;
    const qreal pixels = width * height;
    if (pixels > maxPixels) {
        const qreal factor = std::sqrt(maxPixels / pixels);
        width *= factor;
        height *= factor;
    }
    if (width > maxDimension || height > maxDimension) {
        const qreal factor = qMin(maxDimension / width, maxDimension / height);
        width *= factor;
        height *= factor;
    }
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
    const QRectF viewportRect(horizontalScrollBar()->value(), verticalScrollBar()->value(),
                              viewport()->width(), viewport()->height());
    int bestPage = -1;
    qreal bestArea = 0.0;
    qreal currentArea = 0.0;
    for (int page = 0; page < m_pageSizes.size(); ++page) {
        const QRectF rect = m_layout.pageRect(page);
        if (rect.isNull())
            continue;
        const QRectF visible = rect.intersected(viewportRect);
        const qreal area = visible.width() * visible.height();
        if (area <= 0.0)
            continue;
        if (page == m_currentPage)
            currentArea = area;
        if (area > bestArea) {
            bestArea = area;
            bestPage = page;
        }
    }
    if (bestPage < 0) {
        // Tracks mouse position for the eraser cursor even when the pointer is
        // outside a page.
        return;
    }
    if (bestPage != m_currentPage) {
        const bool currentVisible = currentArea > 0.0;
        if (!currentVisible || bestArea > currentArea * 1.15) {
            m_currentPage = bestPage;
            m_layout.setCurrentPage(bestPage);
            emit pageChanged(bestPage);
        }
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
    m_layout.setCurrentPage(pageIndex);
    const QRectF rect = m_layout.spreadRect(pageIndex);
    if (rect.isNull())
        return;
    horizontalScrollBar()->setValue(qRound(rect.center().x() - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(rect.center().y() - viewport()->height() / 2.0));
    updateCurrentPage();
}

QRectF PdfView::currentSpreadRect() const
{
    if (!hasDocument() || m_currentPage < 0 || m_currentPage >= m_pageSizes.size())
        return QRectF();
    return m_layout.spreadRect(m_currentPage);
}

void PdfView::applyFitZoom()
{
    if (!hasDocument() || m_pageSizes.isEmpty())
        return;
    m_layout.setCurrentPage(m_currentPage);
    QRectF rect = m_layout.spreadRect(m_currentPage);
    if (rect.isNull())
        rect = m_layout.pageRect(m_currentPage);
    if (rect.isNull() || rect.width() <= 0.0 || rect.height() <= 0.0)
        return;

    const qreal availableW = qMax<qreal>(40.0, viewport()->width() - 2.0 * m_layout.margin() - 8.0);
    const qreal availableH = qMax<qreal>(40.0, viewport()->height() - 2.0 * m_layout.margin() - 8.0);
    const qreal zoomW = m_layout.zoom() * availableW / rect.width();
    const qreal zoomH = m_layout.zoom() * availableH / rect.height();
    qreal target = m_zoomMode == ZoomMode::FitWidth ? zoomW : qMin(zoomW, zoomH);
    target = qBound<qreal>(0.05, target, 12.0);
    if (qFuzzyCompare(target, m_layout.zoom())) {
        updateScrollBars();
        return;
    }
    rebuildLayoutAndKeepAnchor(target, m_layout.rotation(),
                               QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    emit zoomChanged(m_layout.zoom());
}

QTransform PdfView::pageToViewTransform(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return QTransform();
    // For QTransform, (A * B).map(p) applies A first, then B. Apply the page
    // transform first, then subtract the scroll offset in view coordinates.
    return m_layout.pageToContent(pageIndex)
           * QTransform::fromTranslate(-horizontalScrollBar()->value(),
                                       -verticalScrollBar()->value());
}

QRectF PdfView::pageRectInView(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizes.size())
        return QRectF();
    return m_layout.pageRect(pageIndex)
        .translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

QRectF PdfView::annotationPageRegion(const Annotation &annotation) const
{
    if (annotation.isTextType() && !annotation.polygons.isEmpty()) {
        const QVector<QPolygonF> polygons = TextSelectionGeometry::normalize(annotation.polygons);
        QVector<QRectF> rects;
        rects.reserve(polygons.size());
        for (const QPolygonF &polygon : polygons)
            rects.append(polygon.boundingRect());
        const QRectF region = unionBounds(rects);
        if (!region.isNull())
            return region;
    }
    if (annotation.type == AnnotationType::NotePoint)
        return QRectF(annotation.rect.topLeft(), QSizeF(0.0, 0.0));

    QRectF bounds = annotation.bounds();
    if (!hasFiniteBounds(bounds))
        return QRectF();
    if (annotation.isPathType() || annotation.type == AnnotationType::Line) {
        const qreal pad = std::max<qreal>(0.5, annotation.style.strokeWidthPt / 2.0);
        bounds = bounds.adjusted(-pad, -pad, pad, pad);
    }
    return bounds;
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
    bool invertible = false;
    const QTransform inverse = pageToViewTransform(pageIndex).inverted(&invertible);
    if (!invertible)
        return QPointF();
    return inverse.map(viewPos);
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
    emit readingAnchorChanged();
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
                if (m_zoomMode != ZoomMode::Fixed) {
                    m_zoomMode = ZoomMode::Fixed;
                    emit zoomModeChanged(m_zoomMode);
                }
                rebuildLayoutAndKeepAnchor(target, m_layout.rotation(), event->position());
                emit zoomChanged(m_layout.zoom());
            }
            m_wheelAngleAccum = 0;
            m_wheelPixelAccum = 0.0;
            event->accept();
            return;
        }
    }
    if ((event->modifiers() & Qt::ShiftModifier) && event->angleDelta().y() != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()
                                       - event->angleDelta().y());
        m_wheelAngleAccum = 0;
        m_wheelPixelAccum = 0.0;
        event->accept();
        return;
    }

    const int angleDelta = event->angleDelta().y();
    const int pixelDelta = event->pixelDelta().y();
    const bool usePixels = angleDelta == 0 && pixelDelta != 0;
    const int rawDelta = usePixels ? pixelDelta : angleDelta;
    const int direction = rawDelta < 0 ? 1 : (rawDelta > 0 ? -1 : 0);

    if (event->phase() == Qt::ScrollEnd) {
        m_wheelAngleAccum = 0;
        m_wheelPixelAccum = 0.0;
        m_wheelLastDirection = 0;
    }

    if (!m_wheelTurnsPage || m_layout.isContinuous() || direction == 0) {
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    QScrollBar *bar = verticalScrollBar();
    const bool down = direction > 0;
    const bool atBoundary = down ? bar->value() >= bar->maximum()
                                 : bar->value() <= bar->minimum();
    const bool hasScrollRoom = bar->maximum() > bar->minimum();

    if (!atBoundary) {
        m_wheelAngleAccum = 0;
        m_wheelPixelAccum = 0.0;
        m_wheelLastDirection = direction;
        QAbstractScrollArea::wheelEvent(event);
        return;
    }

    // A direction change is a deliberate reverse gesture: do not let the
    // previous page turn's cooldown swallow it.
    if (m_wheelLastDirection != 0 && m_wheelLastDirection != direction) {
        m_wheelAngleAccum = 0;
        m_wheelPixelAccum = 0.0;
        m_wheelCooldown.invalidate();
    }
    m_wheelLastDirection = direction;

    const bool cooldownReady = !m_wheelCooldown.isValid()
                               || m_wheelCooldown.elapsed() >= 250;
    if (!cooldownReady) {
        event->accept();
        return;
    }

    if (usePixels)
        m_wheelPixelAccum += qAbs(pixelDelta);
    else
        m_wheelAngleAccum += std::abs(angleDelta);

    const qreal pixelThreshold = 80.0;
    const int angleThreshold = 120;
    const bool thresholdReached = usePixels ? m_wheelPixelAccum >= pixelThreshold
                                            : m_wheelAngleAccum >= angleThreshold;
    if (!hasScrollRoom || thresholdReached) {
        const int before = m_currentPage;
        navigatePageStep(down ? 1 : -1, false);
        m_wheelAngleAccum = 0;
        m_wheelPixelAccum = 0.0;
        if (m_currentPage != before) {
            m_wheelCooldown.start();
            event->accept();
            return;
        }
    }
    event->accept();
}

void PdfView::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat() && event->key() == Qt::Key_Shift)
        return;
    if (event->key() == Qt::Key_Shift && m_draft.pageIndex >= 0
        && (m_dragMode == DragMode::DrawShape || m_dragMode == DragMode::DrawPath)) {
        m_drawShift = true;
        updateDraw(m_draft.pageIndex, m_dragCurrentPage);
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp)
        && event->modifiers() == Qt::NoModifier) {
        if (m_layout.isContinuous())
            scrollPageViewport(event->key() == Qt::Key_PageDown ? 1 : -1);
        else
            navigatePageStep(event->key() == Qt::Key_PageDown ? 1 : -1, false);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        if (m_tool == Tool::Select) {
            deleteSelected();
            event->accept();
            return;
        }
    }
    if (event->key() == Qt::Key_Escape) {
        if (handleEscape()) {
            event->accept();
            return;
        }
    }
    if (event->matches(QKeySequence::Copy)) {
        if (copySelectionOrAnnotation()) {
            event->accept();
            return;
        }
    }
    if (event->matches(QKeySequence::Paste)) {
        if (pasteAnnotation()) {
            event->accept();
            return;
        }
    }
    if (event->matches(QKeySequence::New) && (event->modifiers() & Qt::ControlModifier)) {
        // Ctrl+D is handled here rather than using QKeySequence::New semantics.
    }
    if (event->key() == Qt::Key_D && (event->modifiers() & Qt::ControlModifier)) {
        if (duplicateSelected()) {
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void PdfView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat() && event->key() == Qt::Key_Shift)
        return;
    if (event->key() == Qt::Key_Shift && m_draft.pageIndex >= 0
        && (m_dragMode == DragMode::DrawShape || m_dragMode == DragMode::DrawPath)) {
        m_drawShift = false;
        updateDraw(m_draft.pageIndex, m_dragCurrentPage);
        event->accept();
        return;
    }
    QAbstractScrollArea::keyReleaseEvent(event);
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
        if (m_tool == Tool::Select) {
            selectAnnotation(QString());
            clearTemporarySelections();
        }
        return;
    }

    const QPointF pagePoint = viewToPage(viewPos, pageIndex);

    if (m_tool == Tool::Hand) {
        m_dragMode = DragMode::None;
        m_handLastView = viewPos;
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }

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
        m_dragMode = DragMode::Erase;
        m_dragStartView = viewPos;
        m_lastMouseViewPos = viewPos.toPoint();
        m_eraseLastView = viewPos;
        m_eraseIds.clear();
        m_eraseRemoved.clear();
        eraseSegment(viewPos, viewPos);
        viewport()->update();
        return;
    }

    if (m_tool == Tool::RegionSelect) {
        beginRegionSelection(viewPos);
        return;
    }

    if (m_tool == Tool::Select) {
        const bool forceRegion = event->modifiers().testFlag(Qt::ControlModifier);
        const bool forceText = event->modifiers().testFlag(Qt::AltModifier);
        if (forceRegion) {
            beginRegionSelection(viewPos);
            return;
        }
        if (forceText) {
            selectAnnotation(QString());
            beginTextSelection(viewPos);
            return;
        }
        if (!m_selectedId.isEmpty()) {
            const Annotation *selected = m_annotations.find(m_selectedId);
            if (selected && selected->isResizable()) {
                const QVector<QPointF> handles = handlePointsInPage(*selected);
                const QTransform pageToView = pageToViewTransform(selected->pageIndex);
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
            if (annotation && annotation->isTextType()) {
                beginTextSelection(viewPos);
                return;
            }
            if (annotation && annotation->isMovable())
                beginAnnotationDrag(hit, viewPos);
            return;
        }
        if (textAtPagePoint(clampToPage(pagePoint, pageIndex), pageIndex))
            beginTextSelection(viewPos);
        else
            beginRegionSelection(viewPos);
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

    m_drawShift = event->modifiers().testFlag(Qt::ShiftModifier);
    beginDraw(pageIndex, pagePoint);
}

void PdfView::mouseMoveEvent(QMouseEvent *event)
{
    if (!hasDocument()) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    const QPointF viewPos = event->position();
    m_lastMouseViewPos = viewPos.toPoint();
    if (m_tool == Tool::Hand) {
        if (event->buttons() & Qt::LeftButton) {
            const QPointF delta = viewPos - m_handLastView;
            m_handLastView = viewPos;
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
            viewport()->update();
        }
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    if (m_dragMode == DragMode::DrawShape || m_dragMode == DragMode::DrawPath)
        m_drawShift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (m_dragMode) {
    case DragMode::Erase:
        eraseSegment(m_eraseLastView.isNull() ? viewPos : m_eraseLastView, viewPos);
        m_eraseLastView = viewPos;
        break;
    case DragMode::TextSelection:
        updateTextSelection(viewPos);
        break;
    case DragMode::RegionSelection:
        updateRegionSelection(viewPos);
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
                const QVector<QPointF> handles = handlePointsInPage(*selected);
                const QTransform pageToView = pageToViewTransform(selected->pageIndex);
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
        if (shape == Qt::ArrowCursor && hit.isEmpty()) {
            const int page = pageAtViewPos(viewPos);
            if (page >= 0 && textAtPagePoint(viewToPage(viewPos, page), page))
                shape = Qt::IBeamCursor;
        }
        viewport()->setCursor(shape);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void PdfView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_tool == Tool::Hand) {
        viewport()->setCursor(Qt::OpenHandCursor);
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    m_drawShift = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (m_dragMode) {
    case DragMode::Erase: {
        const QPointF releasePos = event->position();
        eraseSegment(m_eraseLastView.isNull() ? releasePos : m_eraseLastView, releasePos);
        commitErase();
        break;
    }
    case DragMode::TextSelection:
        updateTextSelection(event->position());
        finishTextSelection();
        break;
    case DragMode::RegionSelection:
        updateRegionSelection(event->position());
        finishRegionSelection();
        break;
    case DragMode::DrawShape:
    case DragMode::DrawPath:
        updateDraw(m_dragPage, viewToPage(event->position(), m_dragPage));
        finishDraw();
        break;
    case DragMode::DragAnnotation:
    case DragMode::ResizeAnnotation:
        updateAnnotationDrag(event->position());
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
    const QTransform pageToView = pageToViewTransform(annotation.pageIndex);
    // rect.topLeft() is the canonical pin position; p1 is only kept for old
    // JSON payloads and may be absent on data written before the Phase 0.1 fix.
    return QLineF(pageToView.map(annotation.rect.topLeft()), viewPos).length() <= 9.0;
}

QPolygonF PdfView::annotationViewPolygon(const Annotation &annotation, int pageIndex) const
{
    const QRectF bounds = annotation.bounds();
    const QTransform pageToView = pageToViewTransform(pageIndex);
    QPolygonF polygon;
    polygon << pageToView.map(bounds.topLeft()) << pageToView.map(bounds.topRight())
            << pageToView.map(bounds.bottomRight()) << pageToView.map(bounds.bottomLeft());
    return polygon;
}

QVector<QPointF> PdfView::handlePointsInPage(const Annotation &annotation) const
{
    if (annotation.type == AnnotationType::Line)
        return {annotation.p1, annotation.p2};
    if ((annotation.type == AnnotationType::Pencil
         || annotation.type == AnnotationType::Highlighter)
        && annotation.path.size() == 2)
        return {annotation.path.first(), annotation.path.last()};
    const QRectF bounds = annotation.bounds().normalized();
    const QPointF center = bounds.center();
    QVector<QPointF> handles;
    handles << bounds.topLeft() << bounds.topRight() << bounds.bottomRight()
            << bounds.bottomLeft() << QPointF(center.x(), bounds.top())
            << QPointF(bounds.right(), center.y()) << QPointF(center.x(), bounds.bottom())
            << QPointF(bounds.left(), center.y());
    return handles;
}

QRectF PdfView::resizedBounds(const QRectF &original, int handle, const QPointF &pagePoint,
                              bool keepAspect) const
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
    const qreal aspect = bounds.height() > 0.0 ? bounds.width() / bounds.height() : 1.0;
    QPointF opposite;
    if (handle == 0)
        opposite = bounds.bottomRight();
    else if (handle == 1)
        opposite = bounds.bottomLeft();
    else if (handle == 2)
        opposite = bounds.topLeft();
    else if (handle == 3)
        opposite = bounds.topRight();
    if (keepAspect && handle <= 3 && bounds.height() > 0.0) {
        const qreal sx = pagePoint.x() >= opposite.x() ? 1.0 : -1.0;
        const qreal sy = pagePoint.y() >= opposite.y() ? 1.0 : -1.0;
        qreal width = qMax(minSize, qAbs(pagePoint.x() - opposite.x()));
        qreal height = width / aspect;
        if (height < minSize) {
            height = minSize;
            width = height * aspect;
        }
        QPointF corner(opposite.x() + sx * width, opposite.y() + sy * height);
        switch (handle) {
        case 0: left = corner.x(); top = corner.y(); right = opposite.x(); bottom = opposite.y(); break;
        case 1: right = corner.x(); top = corner.y(); left = opposite.x(); bottom = opposite.y(); break;
        case 2: right = corner.x(); bottom = corner.y(); left = opposite.x(); top = opposite.y(); break;
        case 3: left = corner.x(); bottom = corner.y(); right = opposite.x(); top = opposite.y(); break;
        }
    } else {
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

    if (m_tool == Tool::TextSelect || m_tool == Tool::Select) {
        if (hasSelection) {
            m_textSelectionPage = m_dragPage;
            emit statusMessage(tr("已选择 %1 个字符").arg(selection->text().size()));
            emit textSelectionChanged();
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

QString PdfView::addTextAnnotation(const QPdfSelection &selection, int pageIndex,
                                    AnnotationType type, const AnnotationStyle &style)
{
    if (!selection.isValid() || selection.bounds().isEmpty())
        return QString();
    Annotation annotation = makeBaseAnnotation(type, pageIndex);
    annotation.style = style;
    annotation.polygons.clear();
    for (const QPolygonF &polygon : selection.bounds()) {
        const QRectF box = polygon.boundingRect();
        if (polygon.size() >= 3 && box.width() >= 0.05 && box.height() >= 0.05
            && std::isfinite(box.x()) && std::isfinite(box.y()))
            annotation.polygons.append(polygon);
    }
    if (annotation.polygons.isEmpty())
        return QString();
    QRectF rawRect;
    for (const QPolygonF &polygon : annotation.polygons)
        rawRect = rawRect.isNull() ? polygon.boundingRect()
                                   : rawRect.united(polygon.boundingRect());
    annotation.rect = rawRect.normalized();
    annotation.textStart = selection.startIndex();
    annotation.textEnd = selection.endIndex();
    annotation.text = selection.text();
    addAnnotation(annotation, true);
    return annotation.id;
}

QString PdfView::createTextAnnotationFromSelection(AnnotationType type,
                                                  const AnnotationStyle &style)
{
    if (!m_textSelection || m_textSelectionPage < 0)
        return QString();
    const QString id = addTextAnnotation(*m_textSelection, m_textSelectionPage, type, style);
    clearTemporarySelections();
    return id;
}

QString PdfView::createRegionAnnotation(bool highlighter, const AnnotationStyle &style)
{
    if (!hasRegionSelection())
        return QString();
    Annotation annotation = makeBaseAnnotation(AnnotationType::Rectangle, m_regionSelectionPage);
    annotation.style = style;
    annotation.rect = m_regionSelection.normalized();
    if (highlighter) {
        annotation.style.strokeEnabled = false;
        annotation.style.fillColor = style.fillColor.isValid() ? style.fillColor
                                                               : QColor(QStringLiteral("#FFE066"));
        annotation.style.opacity = style.opacity;
        annotation.style.blend = BlendMode::Multiply;
    }
    addAnnotation(annotation, true);
    clearTemporarySelections();
    return annotation.id;
}

QImage PdfView::regionImage() const
{
    if (!hasRegionSelection())
        return QImage();
    const QSizeF pageSize = m_layout.pageSizePt(m_regionSelectionPage);
    if (pageSize.isEmpty())
        return QImage();
    qreal scale = 2.0;
    const QRectF region = m_regionSelection.normalized();
    if (region.width() > 0.0 && region.height() > 0.0) {
        scale = qMin<qreal>(3.0, qMin<qreal>(1800.0 / region.width(),
                                             1800.0 / region.height()));
        scale = qMax<qreal>(1.0, scale);
    }
    const QSize pagePixels(qCeil(pageSize.width() * scale), qCeil(pageSize.height() * scale));
    QImage pageImage = m_document.pdf()->render(m_regionSelectionPage, pagePixels,
                                                QPdfDocumentRenderOptions(),
                                                QImage::Format_ARGB32_Premultiplied,
                                                QColor(Qt::white));
    if (pageImage.isNull())
        return QImage();
    QPainter painter(&pageImage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QTransform pageToImage = QTransform::fromScale(scale, scale);
    for (const Annotation &annotation : m_annotations.annotations()) {
        if (annotation.pageIndex != m_regionSelectionPage)
            continue;
        AnnotationRenderer::paint(painter, annotation, pageToImage,
                                  QRectF(QPointF(0, 0), pagePixels), 1.0);
    }
    painter.end();
    const QRect crop = QRectF(region.left() * scale, region.top() * scale,
                              region.width() * scale, region.height() * scale)
                           .toAlignedRect()
                           .intersected(pageImage.rect());
    return crop.isEmpty() ? QImage() : pageImage.copy(crop);
}

QRectF PdfView::textSelectionViewRect() const
{
    if (!m_textSelection || m_textSelectionPage < 0
        || m_textSelectionPage >= m_pageSizes.size() || m_textSelection->bounds().isEmpty())
        return QRectF();
    const QTransform pageToView = pageToViewTransform(m_textSelectionPage);
    QRectF rect;
    for (const QPolygonF &polygon : m_textSelection->bounds()) {
        const QRectF mapped = pageToView.mapRect(polygon.boundingRect());
        rect = rect.isNull() ? mapped : rect.united(mapped);
    }
    return rect;
}

QRectF PdfView::regionViewRect() const
{
    if (!hasRegionSelection())
        return QRectF();
    return pageToViewTransform(m_regionSelectionPage).mapRect(m_regionSelection);
}

bool PdfView::hasTextSelection() const
{
    return m_textSelection && m_textSelection->isValid()
           && !m_textSelection->text().trimmed().isEmpty()
           && !m_textSelection->bounds().isEmpty();
}

bool PdfView::hasRegionSelection() const
{
    return m_regionSelectionPage >= 0 && m_regionSelectionPage < m_pageSizes.size()
           && m_regionSelection.width() >= 4.0 && m_regionSelection.height() >= 4.0;
}

void PdfView::clearTemporarySelections()
{
    const bool hadText = hasTextSelection();
    const bool hadRegion = hasRegionSelection();
    m_textSelection.reset();
    m_textSelectionPage = -1;
    m_regionSelection = QRectF();
    m_regionSelectionPage = -1;
    m_textSelecting = false;
    viewport()->update();
    if (hadText)
        emit textSelectionChanged();
    if (hadRegion)
        emit regionSelectionChanged();
}

bool PdfView::handleEscape()
{
    if (m_dragMode == DragMode::Erase) {
        cancelErase();
        return true;
    }
    if (m_dragMode != DragMode::None) {
        m_dragMode = DragMode::None;
        m_draft = Annotation();
        m_editId.clear();
        m_resizeHandle = -1;
        m_dragPage = -1;
        m_textSelecting = false;
        viewport()->update();
        return true;
    }
    if (hasTextSelection() || hasRegionSelection()) {
        clearTemporarySelections();
        return true;
    }
    if (m_tool != Tool::Select) {
        setTool(Tool::Select);
        return true;
    }
    return false;
}

QString PdfView::annotationClipboardJson() const
{
    const Annotation *annotation = m_annotations.find(m_selectedId);
    if (!annotation || annotation->isTextType() || annotation->type == AnnotationType::NotePoint)
        return QString();
    QJsonObject payload;
    payload.insert(QStringLiteral("schema_version"), 1);
    payload.insert(QStringLiteral("annotation"), annotation->toJson());
    return QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

bool PdfView::copySelectionOrAnnotation()
{
    if (hasTextSelection() && !selectedText().isEmpty()) {
        copySelectedText();
        return true;
    }
    const QString json = annotationClipboardJson();
    if (json.isEmpty()) {
        emit statusMessage(tr("当前没有可复制的文本或图形标注"));
        return false;
    }
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-pano-annotation+json"), json.toUtf8());
    QApplication::clipboard()->setMimeData(mime);
    emit statusMessage(tr("已复制标注"));
    return true;
}

bool PdfView::pasteAnnotation(const QPointF &viewPos)
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QStringLiteral("application/x-pano-annotation+json"))) {
        emit statusMessage(tr("剪贴板中没有可粘贴的标注"));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        mime->data(QStringLiteral("application/x-pano-annotation+json")), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit statusMessage(tr("剪贴板中的标注数据无效"));
        return false;
    }
    const QJsonObject payload = doc.object();
    const QJsonObject object = payload.value(QStringLiteral("annotation")).toObject();
    Annotation annotation;
    QString error;
    if (!Annotation::fromJson(object, &annotation, &error)) {
        emit statusMessage(tr("无法粘贴标注：%1").arg(error));
        return false;
    }
    if (annotation.isTextType() || annotation.type == AnnotationType::NotePoint) {
        emit statusMessage(tr("该标注类型不支持粘贴"));
        return false;
    }

    int page = pageAtViewPos(viewPos);
    QPointF point;
    if (page >= 0) {
        point = clampToPage(viewToPage(viewPos, page), page);
    } else {
        page = qBound(0, m_currentPage, pageCount() - 1);
        if (page < 0)
            return false;
        const QSizeF size = m_pageSizes.value(page);
        point = QPointF(size.width() / 2.0, size.height() / 2.0);
    }

    const QRectF bounds = annotation.bounds();
    annotation.pageIndex = page;
    annotation.translate(point - bounds.center());
    annotation.id = newAnnotationId();
    annotation.createdAt = nowIsoUtc();
    annotation.modifiedAt = annotation.createdAt;
    annotation.note = AnnotationNote();
    m_undoStack.push(new AddAnnotationCommand(&m_annotations, annotation));
    selectAnnotation(annotation.id);
    emit statusMessage(tr("已粘贴%1").arg(annotationTypeDisplayName(annotation.type)));
    return true;
}

bool PdfView::duplicateSelected()
{
    const Annotation *annotation = m_annotations.find(m_selectedId);
    if (!annotation || annotation->isTextType() || annotation->type == AnnotationType::NotePoint) {
        emit statusMessage(tr("当前没有可复制的图形标注"));
        return false;
    }
    Annotation copy = *annotation;
    copy.id = newAnnotationId();
    copy.createdAt = nowIsoUtc();
    copy.modifiedAt = copy.createdAt;
    copy.note = AnnotationNote();
    const QSizeF size = m_pageSizes.value(copy.pageIndex);
    const qreal offset = 12.0;
    const QRectF bounds = copy.bounds();
    const qreal dx = qMin(offset, qMax<qreal>(0.0, size.width() - bounds.right()));
    const qreal dy = qMin(offset, qMax<qreal>(0.0, size.height() - bounds.bottom()));
    copy.translate(QPointF(dx, dy));
    m_undoStack.push(new AddAnnotationCommand(&m_annotations, copy));
    selectAnnotation(copy.id);
    emit statusMessage(tr("已复制标注"));
    return true;
}

void PdfView::setEraserRadius(int radius)
{
    m_eraserRadius = qBound(4, radius, 25);
    updateEraserCursor();
    emit statusMessage(tr("橡皮半径：%1 px").arg(m_eraserRadius));
    viewport()->update();
}

void PdfView::beginRegionSelection(const QPointF &viewPos)
{
    const int pageIndex = pageAtViewPos(viewPos);
    if (pageIndex < 0) {
        clearTemporarySelections();
        return;
    }
    m_regionSelectionPage = pageIndex;
    m_dragPage = pageIndex;
    m_dragStartPage = clampToPage(viewToPage(viewPos, pageIndex), pageIndex);
    m_dragCurrentPage = m_dragStartPage;
    m_regionSelection = QRectF(m_dragStartPage, QSizeF(0, 0));
    m_dragMode = DragMode::RegionSelection;
    m_textSelection.reset();
    m_textSelectionPage = -1;
    emit textSelectionChanged();
}

void PdfView::updateRegionSelection(const QPointF &viewPos)
{
    if (m_dragMode != DragMode::RegionSelection || m_dragPage < 0)
        return;
    const QPointF point = clampToPage(viewToPage(viewPos, m_dragPage), m_dragPage);
    m_dragCurrentPage = point;
    m_regionSelection = QRectF(m_dragStartPage, point).normalized();
    viewport()->update();
}

void PdfView::finishRegionSelection()
{
    if (m_dragMode != DragMode::RegionSelection)
        return;
    m_dragMode = DragMode::None;
    if (!hasRegionSelection()) {
        m_regionSelection = QRectF();
        m_regionSelectionPage = -1;
    }
    viewport()->update();
    if (hasRegionSelection())
        emit statusMessage(tr("已框选区域，可复制、保存或创建标注"));
    emit regionSelectionChanged();
}

bool PdfView::textAtPagePoint(const QPointF &pagePoint, int pageIndex)
{
    if (!hasDocument() || pageIndex < 0)
        return false;
    if (m_textHitValid && m_textHitPage == pageIndex
        && QLineF(m_textHitPoint, pagePoint).length() <= 0.75)
        return m_textHitResult;

    const qreal tolerance = 5.0 / qMax<qreal>(0.05, m_layout.zoom());
    QPdfSelection selection = m_document.pdf()->getSelection(
        pageIndex, pagePoint - QPointF(tolerance, tolerance),
        pagePoint + QPointF(tolerance, tolerance));
    if (!selection.isValid())
        selection = m_document.pdf()->getSelection(pageIndex, pagePoint,
                                                   pagePoint + QPointF(tolerance, 0.0));
    m_textHitValid = true;
    m_textHitPage = pageIndex;
    m_textHitPoint = pagePoint;
    m_textHitResult = selection.isValid() && !selection.text().trimmed().isEmpty();
    return m_textHitResult;
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
    m_drawRawPath.clear();
    m_drawRawPath.append(m_dragStartPage);
    m_draft.polygons.clear();
    m_draft.style = m_defaultStyle;
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
    case AnnotationType::Line: {
        m_draft.p1 = m_dragStartPage;
        QPointF end = m_drawShift ? snapToAxis(m_dragStartPage, point) : point;
        end = constrainPointToPage(m_dragStartPage, end, m_pageSizes.value(pageIndex));
        m_draft.p2 = end;
        m_draft.rect = QRectF(m_draft.p1, m_draft.p2).normalized();
        break;
    }
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse: {
        QPointF end = point;
        if (m_drawShift) {
            const QSizeF pageSize = m_pageSizes.value(pageIndex);
            qreal dx = point.x() - m_dragStartPage.x();
            qreal dy = point.y() - m_dragStartPage.y();
            const qreal side = qMax(std::abs(dx), std::abs(dy));
            const qreal sx = dx < 0.0 ? -1.0 : 1.0;
            const qreal sy = dy < 0.0 ? -1.0 : 1.0;
            const qreal maxX = sx > 0.0 ? pageSize.width() - m_dragStartPage.x()
                                        : m_dragStartPage.x();
            const qreal maxY = sy > 0.0 ? pageSize.height() - m_dragStartPage.y()
                                        : m_dragStartPage.y();
            const qreal clamped = qMin(side, qMin(maxX, maxY));
            end = m_dragStartPage + QPointF(sx * clamped, sy * clamped);
        }
        m_draft.rect = QRectF(m_dragStartPage, end).normalized();
        break;
    }
    case AnnotationType::Pencil:
    case AnnotationType::Highlighter: {
        // Keep every raw freehand point even while Shift is held. Shift only
        // changes the preview to a snapped segment; releasing it returns to the
        // full freehand path instead of discarding collected input.
        if (m_drawRawPath.isEmpty())
            m_drawRawPath.append(m_dragStartPage);
        if (m_drawRawPath.last() != point
            && QLineF(m_drawRawPath.last(), point).length() >= 0.4)
            m_drawRawPath.append(point);
        m_draft.path = m_drawRawPath;
        if (m_drawShift && !m_drawRawPath.isEmpty()) {
            QPointF snapped = snapToAxis(m_drawRawPath.first(), point);
            snapped = constrainPointToPage(m_drawRawPath.first(), snapped,
                                           m_pageSizes.value(pageIndex));
            m_draft.path = QVector<QPointF>{m_drawRawPath.first(), snapped};
        }
        const QRectF bounds = pathBounds(m_draft.path);
        const qreal pad = m_draft.style.strokeWidthPt / 2.0;
        m_draft.rect = bounds.adjusted(-pad, -pad, pad, pad);
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
        if (!m_drawRawPath.isEmpty()) {
            if (m_drawRawPath.last() != m_dragCurrentPage
                && QLineF(m_drawRawPath.last(), m_dragCurrentPage).length() >= 0.4)
                m_drawRawPath.append(m_dragCurrentPage);
            if (m_drawShift) {
                QPointF snapped = snapToAxis(m_drawRawPath.first(), m_dragCurrentPage);
                snapped = constrainPointToPage(m_drawRawPath.first(), snapped,
                                               m_pageSizes.value(m_draft.pageIndex));
                m_draft.path = QVector<QPointF>{m_drawRawPath.first(), snapped};
            } else {
                m_draft.path = m_drawRawPath;
            }
            const QRectF bounds = pathBounds(m_draft.path);
            const qreal pad = m_draft.style.strokeWidthPt / 2.0;
            m_draft.rect = bounds.adjusted(-pad, -pad, pad, pad);
        }
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
        if (m_editOriginal.type == AnnotationType::Line) {
            QPointF fixed = m_resizeHandle == 0 ? m_editOriginal.p2 : m_editOriginal.p1;
            QPointF moving = shiftHeld() ? snapToAxis(fixed, point) : point;
            m_draft = m_editOriginal;
            if (m_resizeHandle == 0)
                m_draft.p1 = moving;
            else
                m_draft.p2 = moving;
            m_draft.rect = QRectF(m_draft.p1, m_draft.p2).normalized();
            m_draft.modifiedAt = nowIsoUtc();
            m_dragMoved = QLineF(m_draft.p1, m_draft.p2).length() >= 0.5;
            viewport()->update();
            return;
        }
        if ((m_editOriginal.type == AnnotationType::Pencil
             || m_editOriginal.type == AnnotationType::Highlighter)
            && m_editOriginal.path.size() == 2 && m_resizeHandle < 2) {
            QPointF fixed = m_resizeHandle == 0 ? m_editOriginal.path.last()
                                                : m_editOriginal.path.first();
            QPointF moving = shiftHeld() ? snapToAxis(fixed, point) : point;
            m_draft = m_editOriginal;
            if (m_resizeHandle == 0)
                m_draft.path[0] = moving;
            else
                m_draft.path[1] = moving;
            m_draft.rect = pathBounds(m_draft.path);
            m_draft.modifiedAt = nowIsoUtc();
            m_dragMoved = true;
            viewport()->update();
            return;
        }
        const QRectF newBounds = resizedBounds(m_editOriginal.bounds(), m_resizeHandle,
                                               point, shiftHeld());
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
    AnnotationType type = AnnotationType::Highlight;
    if (m_tool == Tool::Underline)
        type = AnnotationType::Underline;
    else if (m_tool == Tool::StrikeOut)
        type = AnnotationType::StrikeOut;
    addTextAnnotation(selection, pageIndex, type, m_defaultStyle);
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

void PdfView::setSearchHighlights(const QHash<int, QVector<QRectF>> &highlights,
                                  int currentPage, const QVector<QRectF> &currentRects)
{
    m_searchHighlights = highlights;
    m_searchCurrentPage = currentPage;
    m_searchCurrentRects = currentRects;
    viewport()->update();
}

void PdfView::clearSearchHighlights()
{
    if (m_searchHighlights.isEmpty() && m_searchCurrentPage < 0)
        return;
    m_searchHighlights.clear();
    m_searchCurrentPage = -1;
    m_searchCurrentRects.clear();
    viewport()->update();
}

QImage PdfView::selectedRegionImage() const
{
    if (!hasRegionSelection() || m_regionSelectionPage < 0)
        return QImage();
    return regionImage();
}

void PdfView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!hasDocument()) {
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }
    const QPointF viewPos = event->pos();
    const int page = pageAtViewPos(viewPos);
    const QPointF pagePoint = page >= 0 ? viewToPage(viewPos, page) : QPointF();
    const QString hit = annotationAt(viewPos);
    QMenu menu(this);

    if (hasRegionSelection() && regionViewRect().contains(viewPos)) {
        QAction *copyImage = menu.addAction(tr("复制区域图片"));
        QAction *saveImage = menu.addAction(tr("保存区域图片…"));
        menu.addSeparator();
        QAction *note = menu.addAction(tr("为区域添加笔记"));
        QAction *chosen = menu.exec(event->globalPos());
        if (!chosen)
            return;
        if (chosen == copyImage) {
            const QImage image = regionImage();
            if (!image.isNull())
                QApplication::clipboard()->setImage(image);
        } else if (chosen == saveImage) {
            const QString path = QFileDialog::getSaveFileName(this, tr("保存区域图片"),
                                                              QStringLiteral("region.png"),
                                                              tr("PNG 图片 (*.png)"));
            if (!path.isEmpty()) {
                const QImage image = regionImage();
                if (!image.isNull())
                    image.save(path, "PNG");
            }
        } else if (chosen == note) {
            emit noteRequested(QString());
        }
        return;
    }

    if (!hit.isEmpty()) {
        selectAnnotation(hit);
        const Annotation *annotation = m_annotations.find(hit);
        if (annotation) {
            menu.addAction(tr("复制"), this, &PdfView::copySelectionOrAnnotation);
            menu.addAction(tr("创建副本"), this, &PdfView::duplicateSelected);
            menu.addAction(tr("编辑笔记…"), this, [this, id = hit]() { emit noteRequested(id); });
            menu.addSeparator();
            menu.addAction(tr("删除"), this, [this, id = hit]() { deleteAnnotation(id); });
        }
    } else if (!selectedText().isEmpty()) {
        menu.addAction(tr("复制文本"), this, &PdfView::copySelectedText);
        menu.addAction(tr("高亮选中文本"), this, [this]() {
            if (m_textSelection)
                createTextAnnotation(*m_textSelection, m_textSelectionPage);
        });
    } else {
        menu.addAction(tr("粘贴标注"), this, [this, viewPos]() { pasteAnnotation(viewPos); });
        menu.addAction(tr("全选当前页文本"), this, [this, page]() {
            if (page < 0)
                return;
            const QSizeF size = m_pageSizes.value(page);
            QPdfSelection selection = m_document.pdf()->getSelection(
                page, QPointF(0, 0), QPointF(size.width(), size.height()));
            if (selection.isValid()) {
                m_textSelection = selection;
                m_textSelectionPage = page;
                emit textSelectionChanged();
                viewport()->update();
            }
        });
        if (page >= 0)
            menu.addAction(tr("在此处添加书签"), this,
                           [this, page, pagePoint]() { emit bookmarkRequested(page, pagePoint); });
    }

    if (!menu.isEmpty())
        menu.exec(event->globalPos());
}
void PdfView::updateEraserCursor()
{
    if (m_tool != Tool::Eraser || !viewport())
        return;
    const qreal dpr = devicePixelRatioF();
    const int diameter = qMax(8, m_eraserRadius * 2);
    const int margin = 3;
    const int extent = diameter + margin * 2;
    QPixmap pixmap(qCeil(extent * dpr), qCeil(extent * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 255, 255, 210), 3.0));
    painter.setBrush(QColor(47, 111, 237, 38));
    painter.drawEllipse(QRectF(margin, margin, diameter, diameter));
    painter.setPen(QPen(QColor(47, 111, 237, 235), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QRectF(margin, margin, diameter, diameter));
    painter.end();
    viewport()->setCursor(QCursor(pixmap, extent / 2, extent / 2));
}

void PdfView::cancelErase()
{
    if (m_dragMode != DragMode::Erase)
        return;
    m_dragMode = DragMode::None;
    m_eraseIds.clear();
    m_erasePreviewIds.clear();
    m_eraseRemoved.clear();
    m_eraseProtectedHinted = false;
    m_eraseLastView = QPointF();
    viewport()->update();
}

void PdfView::eraseAt(const QPointF &viewPos)
{
    m_eraseIds.clear();
    m_eraseRemoved.clear();
    eraseSegment(viewPos, viewPos);
    commitErase();
}

void PdfView::eraseSegment(const QPointF &from, const QPointF &to)
{
    // Hit-test the capsule swept by the circular eraser between two mouse
    // samples. This respects the visible circle edge and removes every object
    // under it, not just the topmost one.
    QPainterPath capsule;
    if (QLineF(from, to).length() < 0.5) {
        capsule.addEllipse(from, m_eraserRadius, m_eraserRadius);
    } else {
        QPainterPath line;
        line.moveTo(from);
        line.lineTo(to);
        QPainterPathStroker stroker;
        stroker.setWidth(m_eraserRadius * 2.0);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        capsule = stroker.createStroke(line);
    }
    const QRectF capsuleBounds = capsule.boundingRect();

    for (const Annotation &annotation : m_annotations.annotations()) {
        if (annotation.pageIndex < 0 || annotation.pageIndex >= m_pageSizes.size())
            continue;
        if (m_eraseIds.contains(annotation.id))
            continue;
        const QRectF pageViewRect = pageRectInView(annotation.pageIndex);
        if (!pageViewRect.intersects(capsuleBounds))
            continue;
        QPainterPath hit = AnnotationRenderer::hitPath(annotation, 0.0);
        if (hit.isEmpty())
            continue;
        const QPainterPath mapped = pageToViewTransform(annotation.pageIndex).map(hit);
        if (!mapped.intersects(capsule))
            continue;
        if (!annotation.note.isEmpty()) {
            if (!m_eraseProtectedHinted) {
                m_eraseProtectedHinted = true;
                emit statusMessage(tr("带笔记的标注已跳过；请用选择工具删除并确认"));
            }
            continue;
        }
        m_eraseIds.insert(annotation.id);
        m_erasePreviewIds.insert(annotation.id);
        m_eraseRemoved.append(annotation);
    }
}

void PdfView::commitErase()
{
    if (m_eraseRemoved.isEmpty()) {
        emit statusMessage(tr("此处没有可删除的标注"));
        m_erasePreviewIds.clear();
        m_eraseProtectedHinted = false;
        m_eraseLastView = QPointF();
        m_dragMode = DragMode::None;
        viewport()->update();
        return;
    }
    m_undoStack.push(new RemoveAnnotationsCommand(&m_annotations, m_eraseRemoved));
    selectAnnotation(QString());
    emit statusMessage(tr("已擦除 %1 个标注").arg(m_eraseRemoved.size()));
    m_eraseRemoved.clear();
    m_eraseIds.clear();
    m_erasePreviewIds.clear();
    m_eraseProtectedHinted = false;
    m_eraseLastView = QPointF();
    m_dragMode = DragMode::None;
    viewport()->update();
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
