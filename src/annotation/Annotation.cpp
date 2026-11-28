#include "Annotation.h"
#include "AnnotationGeometry.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <cmath>

namespace pano {
namespace {

bool readFinite(const QJsonValue &value, qreal *out)
{
    if (!value.isDouble())
        return false;
    const qreal number = value.toDouble();
    if (!std::isfinite(number))
        return false;
    *out = number;
    return true;
}

bool readPoint(const QJsonValue &value, QPointF *out)
{
    if (!value.isArray())
        return false;
    const QJsonArray array = value.toArray();
    if (array.size() != 2)
        return false;
    qreal x = 0.0;
    qreal y = 0.0;
    if (!readFinite(array.at(0), &x) || !readFinite(array.at(1), &y))
        return false;
    *out = QPointF(x, y);
    return true;
}

QJsonArray toPointArray(const QPointF &point)
{
    return QJsonArray{point.x(), point.y()};
}

QJsonArray toRectArray(const QRectF &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QString colorToString(const QColor &color)
{
    if (!color.isValid())
        return QString();
    return color.name(QColor::HexArgb);
}

bool colorFromString(const QString &text, QColor *out)
{
    if (text.isEmpty())
        return false;
    const QColor color(text);
    if (!color.isValid())
        return false;
    *out = color;
    return true;
}

} // namespace

bool Annotation::isTextType() const
{
    return type == AnnotationType::Highlight || type == AnnotationType::Underline
        || type == AnnotationType::StrikeOut;
}

bool Annotation::isPathType() const
{
    return type == AnnotationType::Pencil || type == AnnotationType::Highlighter;
}

bool Annotation::isShapeType() const
{
    return type == AnnotationType::Line || type == AnnotationType::Rectangle
        || type == AnnotationType::Ellipse;
}

bool Annotation::isMovable() const
{
    return !isTextType();
}

bool Annotation::isResizable() const
{
    return isShapeType() || isPathType();
}

QRectF Annotation::bounds() const
{
    switch (type) {
    case AnnotationType::Line:
        return QRectF(p1, p2).normalized();
    case AnnotationType::Pencil:
    case AnnotationType::Highlighter:
        return pathBounds(path);
    case AnnotationType::NotePoint:
        return QRectF(rect.topLeft(), QSizeF(0.0, 0.0));
    default:
        return rect.normalized();
    }
}

void Annotation::translate(const QPointF &delta)
{
    rect.translate(delta);
    p1 += delta;
    p2 += delta;
    for (QPointF &point : path)
        point += delta;
    for (QPolygonF &polygon : polygons)
        polygon.translate(delta);
}

void Annotation::setBounds(const QRectF &newBounds)
{
    if (!isResizable())
        return;

    const QRectF oldBounds = bounds();
    if (oldBounds.isNull())
        return;

    if (type == AnnotationType::Line) {
        const QVector<QPointF> mapped = mapPointsToBounds({p1, p2}, oldBounds, newBounds);
        if (mapped.size() == 2) {
            p1 = mapped.at(0);
            p2 = mapped.at(1);
            rect = QRectF(p1, p2).normalized();
        }
    } else if (isPathType()) {
        path = mapPointsToBounds(path, oldBounds, newBounds);
        rect = pathBounds(path);
    } else {
        rect = newBounds.normalized();
    }
}

QJsonObject Annotation::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("page_index"), pageIndex);
    root.insert(QStringLiteral("type"), annotationTypeName(type));

    // Phase 1 stores exactly the geometry needed by each type. The rectangle
    // is derived on load; writing it for lines, paths and text would duplicate
    // coordinates and invite drift.
    QJsonObject geometry;
    if (isTextType()) {
        QJsonArray polygonArray;
        for (const QPolygonF &polygon : polygons) {
            QJsonArray points;
            for (const QPointF &point : polygon)
                points.append(toPointArray(point));
            polygonArray.append(points);
        }
        geometry.insert(QStringLiteral("polygons"), polygonArray);
        geometry.insert(QStringLiteral("text_start"), textStart);
        geometry.insert(QStringLiteral("text_end"), textEnd);
        geometry.insert(QStringLiteral("text"), text);
    } else if (type == AnnotationType::Line) {
        geometry.insert(QStringLiteral("start"), toPointArray(p1));
        geometry.insert(QStringLiteral("end"), toPointArray(p2));
    } else if (isPathType()) {
        QJsonArray points;
        for (const QPointF &point : path)
            points.append(toPointArray(point));
        geometry.insert(QStringLiteral("points"), points);
    } else if (type == AnnotationType::NotePoint) {
        geometry.insert(QStringLiteral("point"), toPointArray(rect.topLeft()));
    } else {
        geometry.insert(QStringLiteral("x"), rect.x());
        geometry.insert(QStringLiteral("y"), rect.y());
        geometry.insert(QStringLiteral("width"), rect.width());
        geometry.insert(QStringLiteral("height"), rect.height());
    }
    root.insert(QStringLiteral("geometry"), geometry);

    QJsonObject styleObject;
    styleObject.insert(QStringLiteral("stroke_color"), colorToString(style.strokeColor));
    styleObject.insert(QStringLiteral("stroke_width_pt"), style.strokeWidthPt);
    styleObject.insert(QStringLiteral("fill_color"),
                       style.hasFill() ? QJsonValue(colorToString(style.fillColor)) : QJsonValue(QJsonValue::Null));
    styleObject.insert(QStringLiteral("opacity"), style.opacity);
    styleObject.insert(QStringLiteral("blend_mode"), blendModeName(style.blend));
    styleObject.insert(QStringLiteral("line_style"), lineStyleName(style.lineStyle));
    if (!style.strokeEnabled)
        styleObject.insert(QStringLiteral("stroke_enabled"), false);
    root.insert(QStringLiteral("style"), styleObject);

    root.insert(QStringLiteral("created_at"), createdAt);
    root.insert(QStringLiteral("modified_at"), modifiedAt);

    return root;
}

bool Annotation::fromJson(const QJsonObject &object, Annotation *out, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    Annotation result;
    result.id = object.value(QStringLiteral("id")).toString();
    if (result.id.trimmed().isEmpty())
        return fail(QStringLiteral("标注缺少有效 id"));
    if (!object.value(QStringLiteral("page_index")).isDouble())
        return fail(QStringLiteral("标注 %1 缺少 page_index").arg(result.id));
    const qreal pageValue = object.value(QStringLiteral("page_index")).toDouble();
    if (!std::isfinite(pageValue) || pageValue < 0.0 || pageValue != std::floor(pageValue))
        return fail(QStringLiteral("标注 %1 的 page_index 无效").arg(result.id));
    result.pageIndex = static_cast<int>(pageValue);

    const QString typeName = object.value(QStringLiteral("type")).toString();
    if (!annotationTypeFromString(typeName, &result.type))
        return fail(QStringLiteral("标注 %1 的类型无效：%2").arg(result.id, typeName));

    const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
    const auto readRect = [&](QRectF *rect) {
        qreal x = 0.0;
        qreal y = 0.0;
        qreal width = 0.0;
        qreal height = 0.0;
        if (!readFinite(geometry.value(QStringLiteral("x")), &x)
            || !readFinite(geometry.value(QStringLiteral("y")), &y)
            || !readFinite(geometry.value(QStringLiteral("width")), &width)
            || !readFinite(geometry.value(QStringLiteral("height")), &height)) {
            return false;
        }
        if (width < 0.0 || height < 0.0)
            return false;
        *rect = QRectF(x, y, width, height);
        return true;
    };

    if (result.isTextType()) {
        if (!geometry.value(QStringLiteral("polygons")).isArray())
            return fail(QStringLiteral("标注 %1 缺少文字多边形").arg(result.id));
        const QJsonArray polygons = geometry.value(QStringLiteral("polygons")).toArray();
        if (polygons.isEmpty())
            return fail(QStringLiteral("标注 %1 的文字多边形为空").arg(result.id));
        QRectF bounds;
        bool hasBounds = false;
        for (const QJsonValue &polygonValue : polygons) {
            if (!polygonValue.isArray())
                return fail(QStringLiteral("标注 %1 的文字多边形无效").arg(result.id));
            QPolygonF polygon;
            for (const QJsonValue &pointValue : polygonValue.toArray()) {
                QPointF point;
                if (!readPoint(pointValue, &point))
                    return fail(QStringLiteral("标注 %1 的文字多边形顶点无效").arg(result.id));
                polygon.append(point);
            }
            if (polygon.size() < 3)
                return fail(QStringLiteral("标注 %1 的文字多边形顶点过少").arg(result.id));
            const QRectF polygonBounds = polygon.boundingRect();
            bounds = hasBounds ? bounds.united(polygonBounds) : polygonBounds;
            hasBounds = true;
            result.polygons.append(polygon);
        }
        result.rect = bounds;
        result.textStart = geometry.value(QStringLiteral("text_start")).toInt(-1);
        result.textEnd = geometry.value(QStringLiteral("text_end")).toInt(-1);
        result.text = geometry.value(QStringLiteral("text"))
                          .toString(geometry.value(QStringLiteral("excerpt")).toString());
    } else if (result.type == AnnotationType::Line) {
        const QJsonValue startValue = geometry.contains(QStringLiteral("start"))
                                          ? geometry.value(QStringLiteral("start"))
                                          : geometry.value(QStringLiteral("p1"));
        const QJsonValue endValue = geometry.contains(QStringLiteral("end"))
                                        ? geometry.value(QStringLiteral("end"))
                                        : geometry.value(QStringLiteral("p2"));
        if (!readPoint(startValue, &result.p1) || !readPoint(endValue, &result.p2)) {
            return fail(QStringLiteral("标注 %1 的直线端点无效").arg(result.id));
        }
        result.rect = QRectF(result.p1, result.p2).normalized();
    } else if (result.type == AnnotationType::NotePoint) {
        const QJsonValue pointValue = geometry.contains(QStringLiteral("point"))
                                          ? geometry.value(QStringLiteral("point"))
                                          : geometry.value(QStringLiteral("p1"));
        if (pointValue.isArray()) {
            if (!readPoint(pointValue, &result.p1))
                return fail(QStringLiteral("标注 %1 的笔记定位点无效").arg(result.id));
        } else if (!readRect(&result.rect)) {
            return fail(QStringLiteral("标注 %1 的笔记定位点无效").arg(result.id));
        } else {
            result.p1 = result.rect.topLeft();
        }
        result.rect = QRectF(result.p1, QSizeF(0.0, 0.0));
    } else if (result.isPathType()) {
        if (!geometry.value(QStringLiteral("points")).isArray())
            return fail(QStringLiteral("标注 %1 缺少路径点").arg(result.id));
        const QJsonArray points = geometry.value(QStringLiteral("points")).toArray();
        if (points.size() < 1)
            return fail(QStringLiteral("标注 %1 的路径为空").arg(result.id));
        for (const QJsonValue &pointValue : points) {
            QPointF point;
            if (!readPoint(pointValue, &point))
                return fail(QStringLiteral("标注 %1 的路径点无效").arg(result.id));
            result.path.append(point);
        }
        result.rect = pathBounds(result.path);
    } else if (!readRect(&result.rect)) {
        return fail(QStringLiteral("标注 %1 的几何矩形无效").arg(result.id));
    }

    const QJsonObject styleObject = object.value(QStringLiteral("style")).toObject();
    QColor stroke = QColor(0xD9, 0x4F, 0x4F);
    if (styleObject.contains(QStringLiteral("stroke_color"))) {
        if (!colorFromString(styleObject.value(QStringLiteral("stroke_color")).toString(), &stroke))
            return fail(QStringLiteral("标注 %1 的描边颜色无效").arg(result.id));
    }
    result.style.strokeColor = stroke;

    qreal strokeWidth = 1.5;
    if (styleObject.contains(QStringLiteral("stroke_width_pt"))
        && !readFinite(styleObject.value(QStringLiteral("stroke_width_pt")), &strokeWidth)) {
        return fail(QStringLiteral("标注 %1 的线宽无效").arg(result.id));
    }
    if (strokeWidth <= 0.0 || strokeWidth > 200.0)
        return fail(QStringLiteral("标注 %1 的线宽超出范围").arg(result.id));
    result.style.strokeWidthPt = strokeWidth;

    const QJsonValue fillValue = styleObject.value(QStringLiteral("fill_color"));
    if (fillValue.isString()) {
        QColor fill;
        if (!colorFromString(fillValue.toString(), &fill))
            return fail(QStringLiteral("标注 %1 的填充颜色无效").arg(result.id));
        result.style.fillColor = fill;
    }

    qreal opacity = 1.0;
    if (styleObject.contains(QStringLiteral("opacity"))
        && !readFinite(styleObject.value(QStringLiteral("opacity")), &opacity)) {
        return fail(QStringLiteral("标注 %1 的不透明度无效").arg(result.id));
    }
    result.style.opacity = qBound(0.0, opacity, 1.0);

    const QString blend = styleObject.value(QStringLiteral("blend_mode")).toString();
    if (!blend.isEmpty() && !blendModeFromString(blend, &result.style.blend))
        return fail(QStringLiteral("标注 %1 的混合模式无效：%2").arg(result.id, blend));

    const QString lineStyleValue = styleObject.value(QStringLiteral("line_style")).toString();
    if (!lineStyleValue.isEmpty()
        && !lineStyleFromString(lineStyleValue, &result.style.lineStyle)) {
        return fail(QStringLiteral("标注 %1 的线型无效：%2").arg(result.id, lineStyleValue));
    }
    result.style.strokeEnabled =
        styleObject.value(QStringLiteral("stroke_enabled")).toBool(true);

    result.createdAt = object.value(QStringLiteral("created_at")).toString();
    result.modifiedAt = object.value(QStringLiteral("modified_at")).toString();
    if (result.createdAt.isEmpty())
        result.createdAt = nowIsoUtc();
    if (result.modifiedAt.isEmpty())
        result.modifiedAt = result.createdAt;


    *out = result;
    return true;
}

QString annotationTypeName(AnnotationType type)
{
    switch (type) {
    case AnnotationType::Highlight:
        return QStringLiteral("text_highlight");
    case AnnotationType::Underline:
        return QStringLiteral("underline");
    case AnnotationType::StrikeOut:
        return QStringLiteral("strikeout");
    case AnnotationType::Line:
        return QStringLiteral("line");
    case AnnotationType::Rectangle:
        return QStringLiteral("rectangle");
    case AnnotationType::Ellipse:
        return QStringLiteral("ellipse");
    case AnnotationType::Pencil:
        return QStringLiteral("pencil");
    case AnnotationType::Highlighter:
        return QStringLiteral("highlighter");
    case AnnotationType::NotePoint:
        return QStringLiteral("note_point");
    }
    return QStringLiteral("rectangle");
}

QString annotationTypeDisplayName(AnnotationType type)
{
    switch (type) {
    case AnnotationType::Highlight:
        return QStringLiteral("高亮");
    case AnnotationType::Underline:
        return QStringLiteral("下划线");
    case AnnotationType::StrikeOut:
        return QStringLiteral("删除线");
    case AnnotationType::Line:
        return QStringLiteral("直线");
    case AnnotationType::Rectangle:
        return QStringLiteral("矩形");
    case AnnotationType::Ellipse:
        return QStringLiteral("椭圆");
    case AnnotationType::Pencil:
        return QStringLiteral("铅笔");
    case AnnotationType::Highlighter:
        return QStringLiteral("荧光笔");
    case AnnotationType::NotePoint:
        return QStringLiteral("定位笔记");
    }
    return QStringLiteral("标注");
}

bool annotationTypeFromString(const QString &name, AnnotationType *type)
{
    if (name == QLatin1String("text_highlight") || name == QLatin1String("highlight"))
        *type = AnnotationType::Highlight;
    else if (name == QLatin1String("text_underline") || name == QLatin1String("underline"))
        *type = AnnotationType::Underline;
    else if (name == QLatin1String("text_strikeout") || name == QLatin1String("strikeout")
             || name == QLatin1String("strike_out"))
        *type = AnnotationType::StrikeOut;
    else if (name == QLatin1String("line"))
        *type = AnnotationType::Line;
    else if (name == QLatin1String("rectangle") || name == QLatin1String("rect"))
        *type = AnnotationType::Rectangle;
    else if (name == QLatin1String("ellipse"))
        *type = AnnotationType::Ellipse;
    else if (name == QLatin1String("pencil"))
        *type = AnnotationType::Pencil;
    else if (name == QLatin1String("highlighter"))
        *type = AnnotationType::Highlighter;
    else if (name == QLatin1String("note_point") || name == QLatin1String("note"))
        *type = AnnotationType::NotePoint;
    else
        return false;
    return true;
}

QString blendModeName(BlendMode mode)
{
    return mode == BlendMode::Multiply ? QStringLiteral("multiply") : QStringLiteral("normal");
}

bool blendModeFromString(const QString &name, BlendMode *mode)
{
    if (name == QLatin1String("normal")) {
        *mode = BlendMode::Normal;
        return true;
    }
    if (name == QLatin1String("multiply")) {
        *mode = BlendMode::Multiply;
        return true;
    }
    return false;
}

QString lineStyleName(LineStyle style)
{
    switch (style) {
    case LineStyle::Dash:
        return QStringLiteral("dash");
    case LineStyle::Dot:
        return QStringLiteral("dot");
    case LineStyle::DashDot:
        return QStringLiteral("dash_dot");
    case LineStyle::DashDotDot:
        return QStringLiteral("dash_dot_dot");
    case LineStyle::Solid:
    default:
        return QStringLiteral("solid");
    }
}

QString lineStyleDisplayName(LineStyle style)
{
    switch (style) {
    case LineStyle::Dash:
        return QStringLiteral("虚线");
    case LineStyle::Dot:
        return QStringLiteral("点线");
    case LineStyle::DashDot:
        return QStringLiteral("点划线");
    case LineStyle::DashDotDot:
        return QStringLiteral("双点划线");
    case LineStyle::Solid:
    default:
        return QStringLiteral("实线");
    }
}

bool lineStyleFromString(const QString &name, LineStyle *style)
{
    LineStyle value = LineStyle::Solid;
    if (name == QLatin1String("solid"))
        value = LineStyle::Solid;
    else if (name == QLatin1String("dash"))
        value = LineStyle::Dash;
    else if (name == QLatin1String("dot"))
        value = LineStyle::Dot;
    else if (name == QLatin1String("dash_dot"))
        value = LineStyle::DashDot;
    else if (name == QLatin1String("dash_dot_dot"))
        value = LineStyle::DashDotDot;
    else
        return false;
    if (style)
        *style = value;
    return true;
}

Qt::PenStyle toPenStyle(LineStyle style)
{
    switch (style) {
    case LineStyle::Dash:
        return Qt::DashLine;
    case LineStyle::Dot:
        return Qt::DotLine;
    case LineStyle::DashDot:
        return Qt::DashDotLine;
    case LineStyle::DashDotDot:
        return Qt::DashDotDotLine;
    case LineStyle::Solid:
    default:
        return Qt::SolidLine;
    }
}

QString nowIsoUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newAnnotationId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace pano
