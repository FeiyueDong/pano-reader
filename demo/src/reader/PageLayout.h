#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <QVector>

namespace pano {

// Continuous single-page-column layout. All page geometry is kept in PDF
// points; zoom converts points to viewport logical pixels. View rotation is a
// pure display transform and never changes the document or stored geometry.
class PageLayout
{
public:
    void setPages(const QVector<QSizeF> &pageSizesPt);
    void setViewportSize(const QSizeF &viewportSize);
    void setZoom(qreal zoom);
    void setRotation(int degrees);
    void setMargins(qreal margin, qreal gap);

    int pageCount() const { return m_pageRects.size(); }
    QSizeF pageSizePt(int pageIndex) const;
    QSizeF pageSizeView(int pageIndex) const;
    QRectF pageRect(int pageIndex) const;
    QSizeF contentSize() const { return m_contentSize; }
    qreal zoom() const { return m_zoom; }
    int rotation() const { return m_rotation; }
    qreal margin() const { return m_margin; }
    qreal gap() const { return m_gap; }
    QSizeF viewportSize() const { return m_viewportSize; }

    QTransform pageToContent(int pageIndex) const;
    int pageAtPoint(const QPointF &contentPoint) const;
    int pageAtY(qreal contentY) const;

private:
    void rebuild();
    QSizeF rotatedSizePt(int pageIndex) const;

    QVector<QSizeF> m_pageSizesPt;
    QVector<QRectF> m_pageRects;
    QSizeF m_viewportSize;
    QSizeF m_contentSize;
    qreal m_zoom = 1.0;
    int m_rotation = 0;
    qreal m_margin = 24.0;
    qreal m_gap = 16.0;
};

} // namespace pano
