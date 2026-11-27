#include "PageLayout.h"

#include <QtMath>

#include <limits>

namespace pano {

void PageLayout::setPages(const QVector<QSizeF> &pageSizesPt)
{
    m_pageSizesPt = pageSizesPt;
    rebuild();
}

void PageLayout::setViewportSize(const QSizeF &viewportSize)
{
    m_viewportSize = viewportSize;
    rebuild();
}

void PageLayout::setZoom(qreal zoom)
{
    const qreal clamped = qBound<qreal>(0.05, zoom, 12.0);
    if (qFuzzyCompare(clamped, m_zoom))
        return;
    m_zoom = clamped;
    rebuild();
}

void PageLayout::setRotation(int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    normalized = (normalized / 90) * 90;
    if (normalized == m_rotation)
        return;
    m_rotation = normalized;
    rebuild();
}

void PageLayout::setMargins(qreal margin, qreal gap)
{
    m_margin = qMax<qreal>(0.0, margin);
    m_gap = qMax<qreal>(0.0, gap);
    rebuild();
}

QSizeF PageLayout::pageSizePt(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageSizesPt.size())
        return QSizeF();
    return m_pageSizesPt.at(pageIndex);
}

QSizeF PageLayout::rotatedSizePt(int pageIndex) const
{
    const QSizeF size = pageSizePt(pageIndex);
    if (m_rotation == 90 || m_rotation == 270)
        return QSizeF(size.height(), size.width());
    return size;
}

QSizeF PageLayout::pageSizeView(int pageIndex) const
{
    const QSizeF size = rotatedSizePt(pageIndex);
    return QSizeF(size.width() * m_zoom, size.height() * m_zoom);
}

QRectF PageLayout::pageRect(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= m_pageRects.size())
        return QRectF();
    return m_pageRects.at(pageIndex);
}

QTransform PageLayout::pageToContent(int pageIndex) const
{
    const QRectF rect = pageRect(pageIndex);
    const QSizeF size = pageSizePt(pageIndex);
    if (rect.isEmpty() || size.isEmpty())
        return QTransform();

    QTransform transform;
    transform.translate(rect.x(), rect.y());
    transform.scale(m_zoom, m_zoom);
    switch (m_rotation) {
    case 90:
        transform.translate(size.height(), 0.0);
        transform.rotate(90.0);
        break;
    case 180:
        transform.translate(size.width(), size.height());
        transform.rotate(180.0);
        break;
    case 270:
        transform.translate(0.0, size.width());
        transform.rotate(270.0);
        break;
    default:
        break;
    }
    return transform;
}

int PageLayout::pageAtPoint(const QPointF &contentPoint) const
{
    for (int i = 0; i < m_pageRects.size(); ++i) {
        if (m_pageRects.at(i).contains(contentPoint))
            return i;
    }
    return -1;
}

int PageLayout::pageAtY(qreal contentY) const
{
    if (m_pageRects.isEmpty())
        return -1;
    for (int i = 0; i < m_pageRects.size(); ++i) {
        const QRectF rect = m_pageRects.at(i);
        if (contentY >= rect.top() && contentY <= rect.bottom())
            return i;
    }

    // Inside a gap or outside the document: choose the nearest page.
    int nearest = 0;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_pageRects.size(); ++i) {
        const QRectF rect = m_pageRects.at(i);
        const qreal distance = contentY < rect.top() ? rect.top() - contentY : contentY - rect.bottom();
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    return nearest;
}

void PageLayout::rebuild()
{
    m_pageRects.clear();
    m_pageRects.reserve(m_pageSizesPt.size());

    qreal widest = 0.0;
    qreal totalHeight = 0.0;
    for (int i = 0; i < m_pageSizesPt.size(); ++i) {
        const QSizeF viewSize = pageSizeView(i);
        widest = qMax(widest, viewSize.width());
        totalHeight += viewSize.height();
        if (i > 0)
            totalHeight += m_gap;
    }

    const qreal width = qMax(m_viewportSize.width(), widest + 2.0 * m_margin);
    const qreal height = m_pageSizesPt.isEmpty()
        ? m_viewportSize.height()
        : totalHeight + 2.0 * m_margin;

    qreal y = m_margin;
    for (int i = 0; i < m_pageSizesPt.size(); ++i) {
        const QSizeF viewSize = pageSizeView(i);
        const qreal x = (width - viewSize.width()) / 2.0;
        m_pageRects.append(QRectF(QPointF(x, y), viewSize));
        y += viewSize.height() + m_gap;
    }
    m_contentSize = QSizeF(width, height);
}

} // namespace pano
