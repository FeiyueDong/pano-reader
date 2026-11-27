#pragma once

#include "Annotation.h"

#include <QPainter>
#include <QPainterPath>
#include <QTransform>

namespace pano {
namespace AnnotationRenderer {

QPainterPath geometryPath(const Annotation &annotation);
QPainterPath hitPath(const Annotation &annotation, qreal tolerancePt);
bool hitTest(const Annotation &annotation, const QPointF &pagePoint, qreal tolerancePt);

// Paints one annotation. pageToView maps page coordinates (unrotated, pt) to
// viewport logical pixels; pageRectInView is the page's displayed rectangle.
void paint(QPainter &painter, const Annotation &annotation, const QTransform &pageToView,
           const QRectF &pageRectInView, qreal devicePixelRatio);

} // namespace AnnotationRenderer
} // namespace pano
