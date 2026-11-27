#include "annotation/AnnotationGeometry.h"

#include <algorithm>
#include <cmath>

namespace pano {

namespace {

bool finitePoint(const QPointF &point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

} // namespace

QRectF pathBounds(const QVector<QPointF> &points)
{
    if (points.isEmpty())
        return QRectF();

    qreal xmin = points.first().x();
    qreal xmax = xmin;
    qreal ymin = points.first().y();
    qreal ymax = ymin;
    for (const QPointF &point : points) {
        if (!finitePoint(point))
            continue;
        xmin = std::min(xmin, point.x());
        xmax = std::max(xmax, point.x());
        ymin = std::min(ymin, point.y());
        ymax = std::max(ymax, point.y());
    }
    if (!std::isfinite(xmin) || !std::isfinite(xmax) || !std::isfinite(ymin)
        || !std::isfinite(ymax))
        return QRectF();
    return QRectF(QPointF(xmin, ymin), QPointF(xmax, ymax));
}

QRectF unionBounds(const QVector<QRectF> &rects)
{
    QRectF result;
    bool first = true;
    for (const QRectF &rect : rects) {
        if (!hasFiniteBounds(rect))
            continue;
        result = first ? rect : result.united(rect);
        first = false;
    }
    return result;
}

bool hasFiniteBounds(const QRectF &rect)
{
    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y())
        || !std::isfinite(rect.width()) || !std::isfinite(rect.height())
        || rect.width() < 0.0 || rect.height() < 0.0)
        return false;
    // A default-constructed QRectF is used as "no geometry"; a zero-size
    // rectangle at a real location is a valid point-set boundary.
    return !(rect.width() == 0.0 && rect.height() == 0.0
             && rect.x() == 0.0 && rect.y() == 0.0);
}

bool isDegenerateAxis(qreal size, qreal epsilon)
{
    return std::abs(size) <= epsilon;
}

QVector<QPointF> mapPointsToBounds(const QVector<QPointF> &points,
                                   const QRectF &source,
                                   const QRectF &target)
{
    QVector<QPointF> mapped;
    mapped.reserve(points.size());

    const bool sourceX = !isDegenerateAxis(source.width());
    const bool sourceY = !isDegenerateAxis(source.height());
    const qreal targetCenterX = target.x() + target.width() / 2.0;
    const qreal targetCenterY = target.y() + target.height() / 2.0;

    for (const QPointF &point : points) {
        const qreal x = sourceX
                            ? target.x() + (point.x() - source.x()) * target.width() / source.width()
                            : targetCenterX;
        const qreal y = sourceY
                            ? target.y() + (point.y() - source.y()) * target.height() / source.height()
                            : targetCenterY;
        mapped.append(QPointF(x, y));
    }
    return mapped;
}

} // namespace pano
