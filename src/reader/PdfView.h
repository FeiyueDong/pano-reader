#pragma once

#include <QAbstractScrollArea>
#include <QImage>
#include <optional>
#include <QPdfSelection>
#include <QPointer>
#include <QUndoStack>
#include <QVector>

#include "annotation/Annotation.h"
#include "annotation/AnnotationModel.h"
#include "pdf/PdfDocument.h"
#include "pdf/PageRenderer.h"
#include "reader/PageLayout.h"

class QRubberBand;

namespace pano {

class PdfView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        Hand,
        RegionSelect,
        TextSelect,
        Highlight,
        Underline,
        StrikeOut,
        Line,
        Rectangle,
        Ellipse,
        Pencil,
        Highlighter,
        Eraser,
        Note
    };
    Q_ENUM(Tool)

    explicit PdfView(QWidget *parent = nullptr);
    ~PdfView() override;

    bool openFile(const QString &filePath, QString *errorMessage = nullptr);
    void closeDocument();
    bool hasDocument() const;
    PdfDocument *document() const { return const_cast<PdfDocument *>(&m_document); }
    PageRenderer *renderer() { return &m_renderer; }
    AnnotationModel *annotations() { return &m_annotations; }
    QUndoStack *undoStack() { return &m_undoStack; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    static QString toolDisplayName(Tool tool);

    void setDefaultStyle(const AnnotationStyle &style);
    void applyToolPreset(Tool tool);
    void refreshToolPreset();
    AnnotationStyle defaultStyle() const { return m_defaultStyle; }

    qreal zoom() const;
    void setZoom(qreal zoom);
    void zoomIn();
    void zoomOut();
    void fitWidth();
    void fitPage();
    void resetZoom();
    int rotation() const;
    void setRotation(int degrees);
    void rotateBy(int degrees);

    int currentPage() const { return m_currentPage; }
    void goToPage(int pageIndex);
    void goToPosition(int pageIndex, const QPointF &pagePoint);
    QPointF viewportAnchor(int *pageIndex) const;
    void restoreViewportAnchor(int pageIndex, const QPointF &pagePoint);
    void revealPageRegion(int pageIndex, const QRectF &pageBounds);
    void revealAnnotation(const QString &annotationId);
    int pageCount() const;

    QString selectedAnnotationId() const { return m_selectedId; }
    void selectAnnotation(const QString &annotationId);
    void deleteAnnotation(const QString &annotationId, bool confirmed = false);
    void deleteSelected();
    void applyStyleToSelected(const AnnotationStyle &style);
    void applyStyleToAnnotation(const QString &annotationId, const AnnotationStyle &style);
    void setAnnotationNote(const QString &annotationId, const AnnotationNote &note);
    void setSelectionOverlayVisible(bool visible);
    void copySelectedText();

    QString selectedText() const;
    QString createTextAnnotationFromSelection(AnnotationType type, const AnnotationStyle &style);
    QString createRegionAnnotation(bool highlighter, const AnnotationStyle &style);
    QImage regionImage() const;
    QRectF textSelectionViewRect() const;
    QRectF regionViewRect() const;
    bool hasTextSelection() const;
    bool hasRegionSelection() const;
    void clearTemporarySelections();

signals:
    void pageChanged(int pageIndex);
    void readingAnchorChanged();
    void zoomChanged(qreal zoom);
    void rotationChanged(int degrees);
    void toolChanged(PdfView::Tool tool);
    void annotationSelectionChanged(const QString &annotationId);
    void annotationsChanged();
    void documentChanged();
    void textSelectionChanged();
    void regionSelectionChanged();
    void noteRequested(const QString &annotationId);
    void noteDeleteRequested(const QString &annotationId);
    void openFileRequested(const QString &filePath);
    void statusMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class DragMode {
        None,
        TextSelection,
        RegionSelection,
        DragAnnotation,
        ResizeAnnotation,
        DrawShape,
        DrawPath
    };

    QTransform pageToViewTransform(int pageIndex) const;
    QRectF pageRectInView(int pageIndex) const;
    QPointF viewToContent(const QPointF &viewPos) const;
    QPointF contentToView(const QPointF &contentPos) const;
    int pageAtViewPos(const QPointF &viewPos) const;
    QPointF viewToPage(const QPointF &viewPos, int pageIndex) const;
    QPointF clampToPage(const QPointF &pagePoint, int pageIndex) const;
    QRectF annotationPageRegion(const Annotation &annotation) const;
    bool isPageVisible(int pageIndex) const;
    void ensurePageImage(int pageIndex);
    QSize pagePixelSize(int pageIndex) const;
    void updateScrollBars();
    void updatePageMetrics();
    void updateCurrentPage();
    void rebuildLayoutAndKeepAnchor(qreal newZoom, int newRotation, const QPointF &viewAnchor);
    void centerPage(int pageIndex);
    void requestRepaint();

    QString annotationAt(const QPointF &viewPos, int *handleIndex = nullptr) const;
    bool noteHitAtViewPos(const Annotation &annotation, const QPointF &viewPos) const;
    QPolygonF annotationViewPolygon(const Annotation &annotation, int pageIndex) const;
    QVector<QPointF> handlePointsInPage(const Annotation &annotation) const;
    QRectF resizedBounds(const QRectF &original, int handle, const QPointF &pagePoint,
                         bool keepAspect) const;
    void beginTextSelection(const QPointF &viewPos);
    void beginRegionSelection(const QPointF &viewPos);
    void updateRegionSelection(const QPointF &viewPos);
    void finishRegionSelection();
    bool textAtPagePoint(const QPointF &pagePoint, int pageIndex) const;
    void updateTextSelection(const QPointF &viewPos);
    void finishTextSelection();
    void beginDraw(int pageIndex, const QPointF &pagePoint);
    void updateDraw(int pageIndex, const QPointF &pagePoint);
    void finishDraw();
    void beginAnnotationDrag(const QString &annotationId, const QPointF &viewPos);
    void updateAnnotationDrag(const QPointF &viewPos);
    void finishAnnotationDrag();
    void startResize(const QString &annotationId, int handle, const QPointF &viewPos);
    void createTextAnnotation(const QPdfSelection &selection, int pageIndex);
    QString addTextAnnotation(const QPdfSelection &selection, int pageIndex,
                              AnnotationType type, const AnnotationStyle &style);
    Annotation makeBaseAnnotation(AnnotationType type, int pageIndex) const;
    void addAnnotation(const Annotation &annotation, bool selectAfter);
    void eraseAt(const QPointF &viewPos);
    void paintPage(QPainter &painter, int pageIndex, const QRectF &pageRectInView, const QTransform &pageToView);
    void paintSelectionOverlay(QPainter &painter);
    void paintSelectedAnnotationOutline(QPainter &painter);
    void paintEmptyState(QPainter &painter);

    PdfDocument m_document;
    PageRenderer m_renderer;
    AnnotationModel m_annotations;
    QUndoStack m_undoStack;
    PageLayout m_layout;

    QVector<QSizeF> m_pageSizes;
    int m_currentPage = 0;
    Tool m_tool = Tool::Select;
    AnnotationStyle m_defaultStyle;
    bool m_styleUserModified = false;
    bool m_fitPending = false;
    QString m_selectedId;

    DragMode m_dragMode = DragMode::None;
    int m_dragPage = -1;
    QPointF m_dragStartPage;
    QPointF m_dragCurrentPage;
    QPointF m_dragStartView;
    QPointF m_handLastView;
    QPointF m_dragLastPage;
    int m_resizeHandle = -1;
    bool m_dragMoved = false;
    Annotation m_draft;
    Annotation m_editOriginal;
    QString m_editId;

    bool m_selectionOverlayVisible = true;
    bool m_textSelecting = false;
    std::optional<QPdfSelection> m_textSelection;
    int m_textSelectionPage = -1;
    QRectF m_regionSelection;
    int m_regionSelectionPage = -1;

    QPoint m_lastMouseViewPos;
};

} // namespace pano
