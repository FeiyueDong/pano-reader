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

    if (failures == 0)
        std::printf("text geometry probe passed\n");
    else
        std::printf("text geometry probe failed: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
