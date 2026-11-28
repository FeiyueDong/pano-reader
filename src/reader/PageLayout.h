#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QVector>

namespace pano {

enum class PageLayoutMode {
    SinglePage,
    ContinuousSingle,
    FacingPages,
    ContinuousFacing
};

QString pageLayoutModeToString(PageLayoutMode mode);
bool pageLayoutModeFromString(const QString &value, PageLayoutMode *mode);
QString pageLayoutModeDisplayName(PageLayoutMode mode);

// Page geometry is kept in PDF points; zoom converts points to viewport logical
// pixels. View rotation is a pure display transform and never changes the
// document or stored geometry. Facing layouts group pages into spreads.
class PageLayout
{
public:
    void setPages(const QVector<QSizeF> &pageSizesPt);
    void setViewportSize(const QSizeF &viewportSize);
    void setZoom(qreal zoom);
    void setRotation(int degrees);
    void setMargins(qreal margin, qreal gap);
    void setLayoutMode(PageLayoutMode mode);
    void setFirstPageAlone(bool firstAlone);
    void setCurrentPage(int pageIndex);

    int pageCount() const { return m_pageSizesPt.size(); }
    QSizeF pageSizePt(int pageIndex) const;
    QSizeF pageSizeView(int pageIndex) const;
    QRectF pageRect(int pageIndex) const;
    QSizeF contentSize() const { return m_contentSize; }
    qreal zoom() const { return m_zoom; }
    int rotation() const { return m_rotation; }
    qreal margin() const { return m_margin; }
    qreal gap() const { return m_gap; }
    QSizeF viewportSize() const { return m_viewportSize; }

    PageLayoutMode layoutMode() const { return m_layoutMode; }
    bool firstPageAlone() const { return m_firstPageAlone; }
    int currentPage() const { return m_currentPage; }
    bool isContinuous() const;
    bool isFacing() const;
    QVector<int> spreadPages(int pageIndex) const;
    QRectF spreadRect(int pageIndex) const;
    QVector<int> visiblePages() const;

    QTransform pageToContent(int pageIndex) const;
    int pageAtPoint(const QPointF &contentPoint) const;
    int pageAtY(qreal contentY) const;

private:
    void rebuild();
    QSizeF rotatedSizePt(int pageIndex) const;
    int clampedCurrentPage() const;

    QVector<QSizeF> m_pageSizesPt;
    QVector<QRectF> m_pageRects;
    QSizeF m_viewportSize;
    QSizeF m_contentSize;
    qreal m_zoom = 1.0;
    int m_rotation = 0;
    qreal m_margin = 24.0;
    qreal m_gap = 16.0;
    PageLayoutMode m_layoutMode = PageLayoutMode::ContinuousSingle;
    bool m_firstPageAlone = true;
    int m_currentPage = 0;
};

} // namespace pano
