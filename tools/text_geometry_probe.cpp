#include "annotation/AnnotationGeometry.h"
#include "annotation/TextSelectionGeometry.h"

#include <QGuiApplication>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <cmath>
#include <cstdio>

using namespace pano;

static QPolygonF box(qreal x, qreal y, qreal w, qreal h)
{
    QPolygonF polygon;
    polygon << QPointF(x, y) << QPointF(x + w, y) << QPointF(x + w, y + h)
            << QPointF(x, y + h);
    return polygon;
}

static int failures = 0;

static void expect(int actual, int expected, const char *label)
{
    if (actual == expected) {
        std::printf("PASS: %s (%d)\n", label, actual);
    } else {
        std::printf("FAIL: %s expected %d, got %d\n", label, expected, actual);
        ++failures;
    }
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // A normal word gap must collapse to one band.
    QVector<QPolygonF> words;
    words << box(0, 0, 10, 10) << box(12, 0, 10, 10) << box(24, 0, 10, 10);
    expect(TextSelectionGeometry::normalize(words).size(), 1, "normal word gaps merge");

    // Separate lines stay separate even when their x ranges overlap.
    QVector<QPolygonF> lines;
    lines << box(0, 0, 10, 10) << box(12, 0, 10, 10) << box(0, 30, 10, 10);
    expect(TextSelectionGeometry::normalize(lines).size(), 2, "lines stay separate");

    // A column gutter is much wider than a word gap.
    QVector<QPolygonF> columns;
    columns << box(0, 0, 10, 10) << box(110, 0, 10, 10);
    expect(TextSelectionGeometry::normalize(columns).size(), 2, "column gutter stays separate");

    // Superscripts belong to the line they sit on.
    QVector<QPolygonF> script;
    script << box(0, 0, 10, 10) << box(11, -1, 5, 5) << box(17, 0, 10, 10);
    expect(TextSelectionGeometry::normalize(script).size(), 1, "superscript stays on line");

    // A line with a large leading indent is still one line, but a gap far
    // larger than local glyph height remains separate.
    QVector<QPolygonF> indent;
    indent << box(60, 0, 10, 10) << box(72, 0, 10, 10);
    expect(TextSelectionGeometry::normalize(indent).size(), 1, "indented words merge");

    // Degenerate point-set bounds must stay finite and usable.
    QVector<QPointF> horizontal{QPointF(5, 7), QPointF(45, 7)};
    const QRectF hb = pathBounds(horizontal);
    if (!hb.isNull() && std::abs(hb.width() - 40.0) < 0.001
        && std::abs(hb.height()) < 0.001)
        std::printf("PASS: horizontal path bounds are non-null\n");
    else {
        std::printf("FAIL: horizontal path bounds %g x %g\n", hb.width(), hb.height());
        ++failures;
    }

    QVector<QPointF> single{QPointF(12, 34)};
    const QRectF sb = pathBounds(single);
    if (hasFiniteBounds(sb) && std::abs(sb.width()) < 0.001 && std::abs(sb.height()) < 0.001)
        std::printf("PASS: single-point path bounds are non-null\n");
    else {
        std::printf("FAIL: single-point path bounds\n");
        ++failures;
    }

    const QVector<QPointF> mapped = mapPointsToBounds(
        {QPointF(0, 0), QPointF(0, 10)}, QRectF(0, 0, 0, 10), QRectF(0, 0, 20, 10));
    if (mapped.size() == 2 && std::abs(mapped[0].x() - 10.0) < 0.001
        && std::abs(mapped[1].x() - 10.0) < 0.001)
        std::printf("PASS: degenerate axis maps without division by zero\n");
    else {
        std::printf("FAIL: degenerate axis mapping\n");
        ++failures;
    }

    if (failures == 0)
        std::printf("text geometry probe passed\n");
    else
        std::printf("text geometry probe failed: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
