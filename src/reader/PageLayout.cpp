#include "PageLayout.h"

#include <QtMath>

#include <limits>

namespace pano {

QString pageLayoutModeToString(PageLayoutMode mode)
{
    switch (mode) {
    case PageLayoutMode::SinglePage:
        return QStringLiteral("single");
    case PageLayoutMode::ContinuousSingle:
        return QStringLiteral("continuous");
    case PageLayoutMode::FacingPages:
        return QStringLiteral("facing");
    case PageLayoutMode::ContinuousFacing:
        return QStringLiteral("continuous_facing");
    }
    return QStringLiteral("continuous");
}

bool pageLayoutModeFromString(const QString &value, PageLayoutMode *mode)
{
    const QString key = value.trimmed().toLower();
    if (key == QLatin1String("single")) {
        if (mode) *mode = PageLayoutMode::SinglePage;
        return true;
    }
    if (key == QLatin1String("continuous") || key == QLatin1String("single_continuous")) {
        if (mode) *mode = PageLayoutMode::ContinuousSingle;
        return true;
    }
    if (key == QLatin1String("facing")) {
        if (mode) *mode = PageLayoutMode::FacingPages;
        return true;
    }
    if (key == QLatin1String("continuous_facing") || key == QLatin1String("facing_continuous")) {
        if (mode) *mode = PageLayoutMode::ContinuousFacing;
        return true;
    }
    return false;
}

QString pageLayoutModeDisplayName(PageLayoutMode mode)
{
    switch (mode) {
    case PageLayoutMode::SinglePage:
        return QStringLiteral("单页");
    case PageLayoutMode::ContinuousSingle:
        return QStringLiteral("连续单页");
    case PageLayoutMode::FacingPages:
        return QStringLiteral("双页");
    case PageLayoutMode::ContinuousFacing:
        return QStringLiteral("连续双页");
    }
    return QStringLiteral("连续单页");
}

void PageLayout::setPages(const QVector<QSizeF> &pageSizesPt)
{
    m_pageSizesPt = pageSizesPt;
    m_currentPage = qBound(0, m_currentPage, qMax(0, m_pageSizesPt.size() - 1));
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

void PageLayout::setLayoutMode(PageLayoutMode mode)
{
    if (m_layoutMode == mode)
        return;
    m_layoutMode = mode;
    rebuild();
}

void PageLayout::setFirstPageAlone(bool firstAlone)
{
    if (m_firstPageAlone == firstAlone)
        return;
    m_firstPageAlone = firstAlone;
    rebuild();
}

void PageLayout::setCurrentPage(int pageIndex)
{
    if (m_pageSizesPt.isEmpty())
        return;
    const int bounded = qBound(0, pageIndex, m_pageSizesPt.size() - 1);
    if (bounded == m_currentPage)
        return;
    const bool needsRebuild = !isContinuous();
    m_currentPage = bounded;
    if (needsRebuild)
        rebuild();
}

bool PageLayout::isContinuous() const
{
    return m_layoutMode == PageLayoutMode::ContinuousSingle
        || m_layoutMode == PageLayoutMode::ContinuousFacing;
}

bool PageLayout::isFacing() const
{
    return m_layoutMode == PageLayoutMode::FacingPages
        || m_layoutMode == PageLayoutMode::ContinuousFacing;
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

QVector<int> PageLayout::spreadPages(int pageIndex) const
{
    QVector<int> pages;
    if (m_pageSizesPt.isEmpty())
        return pages;
    pageIndex = qBound(0, pageIndex, m_pageSizesPt.size() - 1);
    if (!isFacing()) {
        pages.append(pageIndex);
        return pages;
    }
    int start = 0;
    if (m_firstPageAlone) {
        if (pageIndex == 0) {
            pages.append(0);
            return pages;
        }
        start = 1 + ((pageIndex - 1) / 2) * 2;
    } else {
        start = (pageIndex / 2) * 2;
    }
    pages.append(start);
    if (start + 1 < m_pageSizesPt.size())
        pages.append(start + 1);
    return pages;
}

QRectF PageLayout::spreadRect(int pageIndex) const
{
    const QVector<int> pages = spreadPages(pageIndex);
    QRectF result;
    for (int page : pages) {
        const QRectF rect = pageRect(page);
        if (rect.isNull())
            continue;
        result = result.isNull() ? rect : result.united(rect);
    }
    return result;
}

QVector<int> PageLayout::visiblePages() const
{
    QVector<int> pages;
    for (int i = 0; i < m_pageRects.size(); ++i) {
        if (!m_pageRects.at(i).isNull())
            pages.append(i);
    }
    return pages;
}

QTransform PageLayout::pageToContent(int pageIndex) const
{
    const QRectF rect = pageRect(pageIndex);
    const QSizeF size = pageSizePt(pageIndex);
    if (rect.isNull() || size.isEmpty())
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
        const QRectF rect = m_pageRects.at(i);
        if (!rect.isNull() && rect.contains(contentPoint))
            return i;
    }
    return -1;
}

int PageLayout::pageAtY(qreal contentY) const
{
    int nearest = -1;
    qreal best = std::numeric_limits<qreal>::max();
    for (int i = 0; i < m_pageRects.size(); ++i) {
        const QRectF rect = m_pageRects.at(i);
        if (rect.isNull())
            continue;
        if (contentY >= rect.top() && contentY <= rect.bottom())
            return i;
        const qreal distance = contentY < rect.top() ? rect.top() - contentY : contentY - rect.bottom();
        if (distance < best) {
            best = distance;
            nearest = i;
        }
    }
    if (nearest >= 0)
        return nearest;
    return m_pageSizesPt.isEmpty() ? -1 : clampedCurrentPage();
}

int PageLayout::clampedCurrentPage() const
{
    if (m_pageSizesPt.isEmpty())
        return -1;
    return qBound(0, m_currentPage, m_pageSizesPt.size() - 1);
}

void PageLayout::rebuild()
{
    m_pageRects.clear();
    m_pageRects.resize(m_pageSizesPt.size());

    if (m_pageSizesPt.isEmpty()) {
        m_contentSize = m_viewportSize;
        return;
    }

    const int current = clampedCurrentPage();
    m_currentPage = current;

    if (m_layoutMode == PageLayoutMode::ContinuousSingle) {
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
        const qreal height = totalHeight + 2.0 * m_margin;
        qreal y = m_margin;
        for (int i = 0; i < m_pageSizesPt.size(); ++i) {
            const QSizeF viewSize = pageSizeView(i);
            const qreal x = (width - viewSize.width()) / 2.0;
            m_pageRects[i] = QRectF(QPointF(x, y), viewSize);
            y += viewSize.height() + m_gap;
        }
        m_contentSize = QSizeF(width, qMax(height, m_viewportSize.height()));
        return;
    }

    if (m_layoutMode == PageLayoutMode::SinglePage) {
        const QSizeF pageSize = pageSizeView(current);
        const qreal width = qMax(m_viewportSize.width(), pageSize.width() + 2.0 * m_margin);
        const qreal height = qMax(m_viewportSize.height(), pageSize.height() + 2.0 * m_margin);
        const qreal x = (width - pageSize.width()) / 2.0;
        const qreal y = m_margin;
        m_pageRects[current] = QRectF(QPointF(x, y), pageSize);
        m_contentSize = QSizeF(width, height);
        return;
    }

    // Facing layouts: build spreads in page order. Continuous mode stacks every
    // group; fixed mode lays out only the current group.
    const bool continuous = m_layoutMode == PageLayoutMode::ContinuousFacing;
    int groupStart = 0;
    if (m_firstPageAlone)
        groupStart = current == 0 ? 0 : 1 + ((current - 1) / 2) * 2;
    else
        groupStart = (current / 2) * 2;

    qreal widestGroup = 0.0;
    qreal totalHeight = 0.0;
    qreal y = m_margin;

    struct Group { QVector<int> pages; qreal width = 0; qreal height = 0; };
    QVector<Group> groups;

    int start = 0;
    while (start < m_pageSizesPt.size()) {
        Group group;
        if (m_firstPageAlone && start == 0) {
            group.pages.append(0);
            start = 1;
        } else {
            group.pages.append(start);
            if (start + 1 < m_pageSizesPt.size())
                group.pages.append(start + 1);
            start += 2;
        }
        for (int page : group.pages) {
            const QSizeF size = pageSizeView(page);
            group.width += size.width();
            if (group.pages.size() > 1 && page != group.pages.first())
                group.width += m_gap;
            group.height = qMax(group.height, size.height());
        }
        groups.append(group);
    }

    for (int gi = 0; gi < groups.size(); ++gi) {
        const Group &group = groups.at(gi);
        const bool isCurrentGroup = group.pages.contains(current);
        if (!continuous && !isCurrentGroup)
            continue;
        widestGroup = qMax(widestGroup, group.width);
        if (continuous)
            totalHeight += group.height + (gi > 0 ? m_gap : 0.0);
    }

    qreal currentGroupHeight = 0.0;
    for (const Group &group : groups) {
        if (group.pages.contains(current)) {
            currentGroupHeight = group.height;
            break;
        }
    }
    const qreal contentWidth = qMax(m_viewportSize.width(), widestGroup + 2.0 * m_margin);
    const qreal contentHeight = continuous
        ? qMax(m_viewportSize.height(), totalHeight + 2.0 * m_margin)
        : qMax(m_viewportSize.height(), currentGroupHeight + 2.0 * m_margin);

    qreal currentY = m_margin;
    for (int gi = 0; gi < groups.size(); ++gi) {
        const Group &group = groups.at(gi);
        const bool isCurrentGroup = group.pages.contains(current);
        if (!continuous && !isCurrentGroup)
            continue;
        const qreal groupX = (contentWidth - group.width) / 2.0;
        qreal x = groupX;
        for (int page : group.pages) {
            const QSizeF size = pageSizeView(page);
            m_pageRects[page] = QRectF(QPointF(x, currentY), size);
            x += size.width() + m_gap;
        }
        currentY += group.height + m_gap;
    }

    m_contentSize = QSizeF(contentWidth, contentHeight);
}

} // namespace pano
