#pragma once

#include <QPolygonF>
#include <QVector>

namespace pano {

// Converts the glyph/word polygons returned by Qt PDF into the geometry used
// for painting and hit-testing text annotations.  Input and output are in
// unrotated page points (top-left origin).  Word gaps on the same text line are
// merged, while column gaps, paragraph gaps and slanted/vertical text are kept
// separate.
class TextSelectionGeometry
{
public:
    static QVector<QPolygonF> normalize(const QVector<QPolygonF> &rawPolygons);
    static QVector<QPolygonF> mergeLineBands(const QVector<QPolygonF> &rawPolygons);
    static bool isAxisAligned(const QPolygonF &polygon, qreal tolerance = 0.35);
};

} // namespace pano
