#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <Qt>

namespace pano {

// Annotation types used by the Phase 0 demo. Text annotations carry PDF text
// indices and page-space polygons; geometric annotations carry page-space
// geometry only (points, not device pixels).
enum class AnnotationType {
    Highlight,
    Underline,
    StrikeOut,
    Line,
    Rectangle,
    Ellipse,
    Pencil,
    Highlighter,
    NotePoint
};

enum class BlendMode {
    Normal,
    Multiply
};

enum class LineStyle {
    Solid,
    Dash,
    Dot,
    DashDot,
    DashDotDot
};

struct AnnotationStyle {
    QColor strokeColor = QColor(0xD9, 0x4F, 0x4F);
    qreal strokeWidthPt = 1.5;
    QColor fillColor; // invalid QColor == no fill
    qreal opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    LineStyle lineStyle = LineStyle::Solid;
    bool strokeEnabled = true;

    bool hasFill() const { return fillColor.isValid(); }
};

struct Annotation {
    QString id;
    int pageIndex = 0;
    AnnotationType type = AnnotationType::Rectangle;
    AnnotationStyle style;

    // Page-space geometry in PDF points, origin at the unrotated page's
    // top-left corner, X right, Y down.
    QRectF rect;              // shapes, text selection bounds, note anchor
    QPointF p1;               // line start / note anchor
    QPointF p2;               // line end
    QVector<QPointF> path;    // pencil / highlighter
    QList<QPolygonF> polygons; // text highlight polygons
    int textStart = -1;
    int textEnd = -1;
    QString text;

    QString createdAt;
    QString modifiedAt;

    bool isTextType() const;
    bool isPathType() const;
    bool isShapeType() const;
    bool isMovable() const;
    bool isResizable() const;

    QRectF bounds() const;
    void translate(const QPointF &delta);
    void setBounds(const QRectF &newBounds);

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &object, Annotation *out, QString *error);
};

QString annotationTypeName(AnnotationType type);
QString annotationTypeDisplayName(AnnotationType type);
bool annotationTypeFromString(const QString &name, AnnotationType *type);

QString blendModeName(BlendMode mode);
bool blendModeFromString(const QString &name, BlendMode *mode);

QString lineStyleName(LineStyle style);
QString lineStyleDisplayName(LineStyle style);
bool lineStyleFromString(const QString &name, LineStyle *style);
Qt::PenStyle toPenStyle(LineStyle style);

QString nowIsoUtc();
QString newAnnotationId();

} // namespace pano
