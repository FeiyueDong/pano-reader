#include "AnnotationRenderer.h"

#include <QImage>
#include <QLineF>
#include <QPainterPathStroker>
#include <QtMath>

namespace pano {
namespace AnnotationRenderer {
namespace {

QPainterPath pathFromPoints(const QVector<QPointF> &points)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;
    path.moveTo(points.first());
    for (int i = 1; i < points.size(); ++i)
        path.lineTo(points.at(i));
    return path;
}

QPainterPath polygonPath(const Annotation &annotation)
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    for (const QPolygonF &polygon : annotation.polygons)
        path.addPolygon(polygon);
    return path;
}

QPainterPath strokedPath(const QPainterPath &path, qreal width)
{
    QPainterPathStroker stroker;
    stroker.setWidth(qMax<qreal>(0.5, width));
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}

QPainter::CompositionMode compositionMode(BlendMode mode)
{
    return mode == BlendMode::Multiply ? QPainter::CompositionMode_Multiply
                                       : QPainter::CompositionMode_SourceOver;
}

void paintHighlighter(QPainter &painter, const Annotation &annotation,
                      const QTransform &pageToView, const QRectF &pageRectInView,
                      qreal devicePixelRatio)
{
    QPainterPath path = pathFromPoints(annotation.path);
    if (path.elementCount() == 0)
        return;

    const qreal penWidth = qMax(1.0, annotation.style.strokeWidthPt);
    const qreal padding = penWidth / 2.0 + 2.0;
    QRectF strokeBounds = path.boundingRect().adjusted(-padding, -padding, padding, padding);
    if (annotation.path.size() == 1) {
        strokeBounds = QRectF(annotation.path.first(), QSizeF(0.0, 0.0))
                           .adjusted(-padding, -padding, padding, padding);
    }

    // Only rasterize the stroke's actual influence area. The mask is still
    // composited once for the whole stroke, so self-overlap does not build up.
    QRectF maskRect = pageToView.mapRect(strokeBounds).intersected(pageRectInView);
    if (maskRect.isEmpty())
        return;
    maskRect = maskRect.adjusted(-1.0, -1.0, 1.0, 1.0).intersected(pageRectInView);
    if (maskRect.isEmpty())
        return;

    const QSize maskSize(qMax(1, qCeil(maskRect.width() * devicePixelRatio)),
                         qMax(1, qCeil(maskRect.height() * devicePixelRatio)));
    QImage mask(maskSize, QImage::Format_ARGB32_Premultiplied);
    mask.setDevicePixelRatio(devicePixelRatio);
    mask.fill(Qt::transparent);

    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setWorldTransform(pageToView
                                      * QTransform::fromTranslate(-maskRect.x(), -maskRect.y()));
        QPen pen(annotation.style.strokeColor, penWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        maskPainter.setPen(pen);
        maskPainter.setBrush(Qt::NoBrush);
        if (annotation.path.size() == 1) {
            const qreal radius = pen.widthF() / 2.0;
            maskPainter.setBrush(annotation.style.strokeColor);
            maskPainter.setPen(Qt::NoPen);
            maskPainter.drawEllipse(annotation.path.first(), radius, radius);
        } else {
            maskPainter.drawPath(path);
        }
    }

    painter.save();
    painter.setClipRect(pageRectInView);
    painter.setOpacity(qBound<qreal>(0.0, annotation.style.opacity, 1.0));
    painter.setCompositionMode(compositionMode(annotation.style.blend));
    painter.drawImage(maskRect, mask);
    painter.restore();
}

} // namespace

QPainterPath geometryPath(const Annotation &annotation)
{
    switch (annotation.type) {
    case AnnotationType::Line:
        return pathFromPoints({annotation.p1, annotation.p2});
    case AnnotationType::Pencil:
    case AnnotationType::Highlighter:
        return pathFromPoints(annotation.path);
    case AnnotationType::Rectangle: {
        QPainterPath path;
        path.addRect(annotation.rect.normalized());
        return path;
    }
    case AnnotationType::Ellipse: {
        QPainterPath path;
        path.addEllipse(annotation.rect.normalized());
        return path;
    }
    case AnnotationType::Highlight:
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut:
        return polygonPath(annotation);
    case AnnotationType::NotePoint:
        return QPainterPath();
    }
    return QPainterPath();
}

QPainterPath hitPath(const Annotation &annotation, qreal tolerancePt)
{
    const qreal tolerance = qMax<qreal>(0.0, tolerancePt);
    const qreal strokeWidth = qMax<qreal>(1.0, annotation.style.strokeWidthPt);

    switch (annotation.type) {
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse: {
        QPainterPath path = geometryPath(annotation);
        QPainterPath result = strokedPath(path, strokeWidth + tolerance * 2.0);
        if (annotation.style.hasFill())
            result.addPath(path);
        return result;
    }
    case AnnotationType::Line:
        return strokedPath(geometryPath(annotation), strokeWidth + tolerance * 2.0);
    case AnnotationType::Pencil:
        return strokedPath(geometryPath(annotation), strokeWidth + tolerance * 2.0);
    case AnnotationType::Highlighter:
        return strokedPath(geometryPath(annotation),
                           qMax(strokeWidth, 8.0) + tolerance * 2.0);
    case AnnotationType::Highlight:
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut: {
        QPainterPath path = polygonPath(annotation);
        QPainterPath result = path;
        for (const QPolygonF &polygon : annotation.polygons) {
            QPainterPath polygonPath;
            polygonPath.addPolygon(polygon);
            result.addPath(strokedPath(polygonPath, tolerance * 2.0));
        }
        return result;
    }
    case AnnotationType::NotePoint:
        return QPainterPath();
    }
    return QPainterPath();
}

bool hitTest(const Annotation &annotation, const QPointF &pagePoint, qreal tolerancePt)
{
    if (annotation.type == AnnotationType::NotePoint) {
        const qreal radius = qMax<qreal>(tolerancePt, 7.0);
        return QLineF(annotation.rect.topLeft(), pagePoint).length() <= radius;
    }

    const QPainterPath hit = hitPath(annotation, tolerancePt);
    if (hit.isEmpty())
        return false;
    return hit.contains(pagePoint);
}

void paint(QPainter &painter, const Annotation &annotation, const QTransform &pageToView,
           const QRectF &pageRectInView, qreal devicePixelRatio)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(pageRectInView);

    if (annotation.type == AnnotationType::Highlighter) {
        paintHighlighter(painter, annotation, pageToView, pageRectInView, devicePixelRatio);
        painter.restore();
        return;
    }

    if (annotation.type == AnnotationType::NotePoint) {
        const QPointF center = pageToView.map(annotation.rect.topLeft());
        const qreal radius = 6.0;
        painter.setOpacity(qBound<qreal>(0.0, annotation.style.opacity, 1.0));
        painter.setPen(QPen(QColor(255, 255, 255, 230), 1.5));
        painter.setBrush(annotation.style.strokeColor.isValid() ? annotation.style.strokeColor
                                                               : QColor(0xF5, 0x9E, 0x0B));
        painter.drawEllipse(center, radius, radius);
        painter.setPen(QPen(QColor(0x7A, 0x4A, 0x00), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
        painter.restore();
        return;
    }

    painter.setOpacity(qBound<qreal>(0.0, annotation.style.opacity, 1.0));
    painter.setCompositionMode(compositionMode(annotation.style.blend));
    painter.setWorldTransform(pageToView);

    QPen pen(annotation.style.strokeColor);
    pen.setWidthF(qMax<qreal>(0.25, annotation.style.strokeWidthPt));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (annotation.type) {
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse: {
        QPainterPath path = geometryPath(annotation);
        if (annotation.style.hasFill())
            painter.fillPath(path, annotation.style.fillColor);
        painter.drawPath(path);
        break;
    }
    case AnnotationType::Line:
        painter.drawLine(annotation.p1, annotation.p2);
        break;
    case AnnotationType::Pencil: {
        if (annotation.path.size() == 1) {
            painter.setBrush(annotation.style.strokeColor);
            painter.setPen(Qt::NoPen);
            const qreal radius = pen.widthF() / 2.0;
            painter.drawEllipse(annotation.path.first(), radius, radius);
        } else {
            painter.drawPath(geometryPath(annotation));
        }
        break;
    }
    case AnnotationType::Highlight: {
        QPainterPath path = polygonPath(annotation);
        painter.fillPath(path, annotation.style.strokeColor);
        break;
    }
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut: {
        const qreal width = qMax<qreal>(0.25, annotation.style.strokeWidthPt);
        for (const QPolygonF &polygon : annotation.polygons) {
            const QRectF box = polygon.boundingRect();
            qreal y = 0.0;
            if (annotation.type == AnnotationType::Underline)
                y = box.bottom() - width * 0.5 - 0.5;
            else
                y = box.center().y();
            painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
        }
        break;
    }
    case AnnotationType::Highlighter:
    case AnnotationType::NotePoint:
        break;
    }

    painter.restore();
}

} // namespace AnnotationRenderer
} // namespace pano
