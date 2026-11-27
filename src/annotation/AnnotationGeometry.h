#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

namespace pano {

// Geometric helpers for page-space annotation data.  A null QRectF means
// "no geometry"; a valid rectangle with zero width and/or height is a
// meaningful horizontal/vertical/point-like path and must not be conflated
// with missing data.
QRectF pathBounds(const QVector<QPointF> &points);
QRectF unionBounds(const QVector<QRectF> &rects);
bool hasFiniteBounds(const QRectF &rect);
bool isDegenerateAxis(qreal size, qreal epsilon = 1e-6);

// Map points from one axis-aligned bounds to another.  Degenerate source axes
// are mapped to the centre of the target range instead of dividing by zero.
QVector<QPointF> mapPointsToBounds(const QVector<QPointF> &points,
                                   const QRectF &source,
                                   const QRectF &target);

} // namespace pano
