#include "annotation/TextSelectionGeometry.h"

#include <QLineF>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace pano {

namespace {

struct GlyphBox {
    QPolygonF polygon;
    QRectF box;
    qreal height = 0.0;
    qreal centerY = 0.0;
    qreal centerX = 0.0;
};

qreal median(QVector<qreal> values)
{
    if (values.isEmpty())
        return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

qreal angleDegrees(const QPolygonF &polygon)
{
    if (polygon.size() < 2)
        return 0.0;
    const QLineF edge(polygon.at(0), polygon.at(1));
    qreal angle = std::atan2(edge.dy(), edge.dx()) * 180.0 / M_PI;
    while (angle <= -90.0)
        angle += 180.0;
    while (angle > 90.0)
        angle -= 180.0;
    return angle;
}

bool sameLine(const GlyphBox &a, const GlyphBox &b, qreal referenceHeight)
{
    const qreal overlap = std::min(a.box.bottom(), b.box.bottom())
        - std::max(a.box.top(), b.box.top());
    if (overlap <= 0.0)
        return false;
    const qreal minHeight = std::min(a.height, b.height);
    if (overlap < minHeight * 0.42)
        return false;
    if (referenceHeight <= 0.0)
        return false;
    const qreal centerDelta = std::abs(a.centerY - b.centerY);
    return centerDelta <= referenceHeight * 0.55;
}

QRectF unionRect(const QRectF &a, const QRectF &b)
{
    return a.isNull() ? b : a.united(b);
}

QPolygonF rectPolygon(const QRectF &rect)
{
    QPolygonF polygon;
    polygon << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
    return polygon;
}

} // namespace

bool TextSelectionGeometry::isAxisAligned(const QPolygonF &polygon, qreal tolerance)
{
    if (polygon.size() < 3)
        return false;
    for (int i = 0; i < polygon.size(); ++i) {
        const QLineF edge(polygon.at(i), polygon.at((i + 1) % polygon.size()));
        const qreal angle = std::abs(std::atan2(edge.dy(), edge.dx()) * 180.0 / M_PI);
        const qreal normalized = std::min(angle, std::abs(angle - 180.0));
        if (normalized > tolerance && std::abs(normalized - 90.0) > tolerance)
            return false;
    }
    return true;
}

QVector<QPolygonF> TextSelectionGeometry::normalize(const QVector<QPolygonF> &rawPolygons)
{
    QVector<QPolygonF> cleaned;
    cleaned.reserve(rawPolygons.size());
    for (const QPolygonF &polygon : rawPolygons) {
        const QRectF box = polygon.boundingRect();
        if (polygon.size() < 3 || box.width() < 0.05 || box.height() < 0.05)
            continue;
        if (!std::isfinite(box.x()) || !std::isfinite(box.y()))
            continue;
        if (!isAxisAligned(polygon))
            return rawPolygons; // Slanted / vertical prose: keep the original geometry.
        cleaned.append(polygon);
    }
    if (cleaned.isEmpty())
        return rawPolygons;
    return mergeLineBands(cleaned);
}

QVector<QPolygonF> TextSelectionGeometry::mergeLineBands(const QVector<QPolygonF> &rawPolygons)
{
    QVector<GlyphBox> boxes;
    boxes.reserve(rawPolygons.size());
    QVector<qreal> heights;
    for (const QPolygonF &polygon : rawPolygons) {
        const QRectF box = polygon.boundingRect();
        if (box.width() < 0.05 || box.height() < 0.05)
            continue;
        GlyphBox glyph;
        glyph.polygon = polygon;
        glyph.box = box;
        glyph.height = box.height();
        glyph.centerY = box.center().y();
        glyph.centerX = box.center().x();
        heights.append(glyph.height);
        boxes.append(glyph);
    }
    if (boxes.isEmpty())
        return rawPolygons;

    const qreal referenceHeight = median(heights);
    if (referenceHeight <= 0.0)
        return rawPolygons;

    // Greedily cluster glyphs into text lines.  Using overlap instead of equal
    // y values tolerates mixed font sizes and superscripts on one line.
    QVector<QVector<GlyphBox>> lines;
    for (const GlyphBox &glyph : boxes) {
        int bestLine = -1;
        qreal bestOverlap = 0.0;
        for (int i = 0; i < lines.size(); ++i) {
            const GlyphBox &anchor = lines.at(i).first();
            const qreal overlap = std::min(anchor.box.bottom(), glyph.box.bottom())
                - std::max(anchor.box.top(), glyph.box.top());
            if (overlap > bestOverlap && sameLine(anchor, glyph, referenceHeight)) {
                bestOverlap = overlap;
                bestLine = i;
            }
        }
        if (bestLine < 0)
            lines.append({glyph});
        else
            lines[bestLine].append(glyph);
    }

    QVector<QPolygonF> result;
    for (QVector<GlyphBox> &line : lines) {
        std::sort(line.begin(), line.end(), [](const GlyphBox &a, const GlyphBox &b) {
            return a.box.left() < b.box.left();
        });
        QRectF band;
        qreal previousRight = 0.0;
        bool first = true;
        for (const GlyphBox &glyph : line) {
            const qreal gap = first ? 0.0 : glyph.box.left() - previousRight;
            const qreal localHeight = std::max<qreal>(1.0, std::min(glyph.height, referenceHeight));
            // Normal inter-word spaces are around 0.25–0.4 em; column gutters
            // are far wider.  Keep a large gap as a separate band.
            const qreal mergeLimit = std::max<qreal>(1.2, localHeight * 0.55);
            if (first || gap > mergeLimit) {
                if (!band.isNull())
                    result.append(rectPolygon(band));
                band = glyph.box;
            } else {
                band = unionRect(band, glyph.box);
            }
            previousRight = glyph.box.right();
            first = false;
        }
        if (!band.isNull())
            result.append(rectPolygon(band));
    }
    return result;
}

} // namespace pano
