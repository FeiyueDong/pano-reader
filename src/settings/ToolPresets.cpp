#include "settings/ToolPresets.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "storage/JsonUtils.h"

namespace pano {

namespace {

QColor colorFromName(const QString &name, const QColor &fallback)
{
    const QColor color(name);
    return color.isValid() ? color : fallback;
}

AnnotationStyle styleFor(const QString &id)
{
    AnnotationStyle style;
    style.opacity = 1.0;
    style.blend = BlendMode::Normal;
    style.strokeWidthPt = 1.5;
    if (id == QStringLiteral("text_highlight")) {
        style.strokeColor = QColor(QStringLiteral("#FFE066"));
        style.strokeWidthPt = 1.0;
        style.opacity = 0.70;
        style.blend = BlendMode::Multiply;
    } else if (id == QStringLiteral("underline")) {
        style.strokeColor = QColor(QStringLiteral("#477EB8"));
        style.strokeWidthPt = 1.0;
    } else if (id == QStringLiteral("strikeout")) {
        style.strokeColor = QColor(QStringLiteral("#D94F4F"));
        style.strokeWidthPt = 1.0;
    } else if (id == QStringLiteral("line") || id == QStringLiteral("rectangle")
               || id == QStringLiteral("ellipse")) {
        style.strokeColor = QColor(QStringLiteral("#D94F4F"));
        style.strokeWidthPt = 1.5;
    } else if (id == QStringLiteral("pencil")) {
        style.strokeColor = QColor(QStringLiteral("#344054"));
        style.strokeWidthPt = 1.5;
    } else if (id == QStringLiteral("highlighter")) {
        style.strokeColor = QColor(QStringLiteral("#45B9E6"));
        style.strokeWidthPt = 12.0;
        style.opacity = 0.80;
        style.blend = BlendMode::Multiply;
    } else if (id == QStringLiteral("note_point")) {
        style.strokeColor = QColor(QStringLiteral("#F59E0B"));
        style.strokeWidthPt = 1.5;
    }
    return style;
}

QJsonObject styleToJson(const AnnotationStyle &style)
{
    QJsonObject object;
    object.insert(QStringLiteral("stroke_color"), style.strokeColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("stroke_width_pt"), style.strokeWidthPt);
    object.insert(QStringLiteral("fill_color"),
                  style.hasFill() ? QJsonValue(style.fillColor.name(QColor::HexArgb)) : QJsonValue());
    object.insert(QStringLiteral("opacity"), style.opacity);
    object.insert(QStringLiteral("blend_mode"), blendModeName(style.blend));
    object.insert(QStringLiteral("line_style"), lineStyleName(style.lineStyle));
    object.insert(QStringLiteral("stroke_enabled"), style.strokeEnabled);
    return object;
}

bool styleFromJson(const QJsonObject &object, AnnotationStyle *style)
{
    if (!style)
        return false;
    AnnotationStyle result;
    if (object.contains(QStringLiteral("stroke_color")))
        result.strokeColor = colorFromName(object.value(QStringLiteral("stroke_color")).toString(),
                                           QColor(QStringLiteral("#D94F4F")));
    result.strokeWidthPt = qBound(0.25, object.value(QStringLiteral("stroke_width_pt")).toDouble(1.5), 40.0);
    if (object.value(QStringLiteral("fill_color")).isString())
        result.fillColor = QColor(object.value(QStringLiteral("fill_color")).toString());
    result.opacity = qBound(0.05, object.value(QStringLiteral("opacity")).toDouble(1.0), 1.0);
    BlendMode blend = BlendMode::Normal;
    blendModeFromString(object.value(QStringLiteral("blend_mode")).toString(), &blend);
    result.blend = blend;
    LineStyle lineStyle = LineStyle::Solid;
    if (lineStyleFromString(object.value(QStringLiteral("line_style")).toString(), &lineStyle))
        result.lineStyle = lineStyle;
    result.strokeEnabled = object.value(QStringLiteral("stroke_enabled")).toBool(true);
    *style = result;
    return true;
}

} // namespace

ToolPresets::ToolPresets()
{
    resetAll();
}

ToolPresets::~ToolPresets() = default;

QStringList ToolPresets::toolIds()
{
    return {QStringLiteral("select"),        QStringLiteral("text"),
            QStringLiteral("text_highlight"), QStringLiteral("underline"),
            QStringLiteral("strikeout"),     QStringLiteral("line"),
            QStringLiteral("rectangle"),     QStringLiteral("ellipse"),
            QStringLiteral("pencil"),        QStringLiteral("highlighter"),
            QStringLiteral("eraser"),        QStringLiteral("note_point")};
}

QString ToolPresets::displayName(const QString &toolId)
{
    if (toolId == QStringLiteral("select")) return QStringLiteral("选择");
    if (toolId == QStringLiteral("text")) return QStringLiteral("文本选择");
    if (toolId == QStringLiteral("text_highlight")) return QStringLiteral("文字高亮");
    if (toolId == QStringLiteral("underline")) return QStringLiteral("下划线");
    if (toolId == QStringLiteral("strikeout")) return QStringLiteral("删除线");
    if (toolId == QStringLiteral("line")) return QStringLiteral("直线");
    if (toolId == QStringLiteral("rectangle")) return QStringLiteral("矩形");
    if (toolId == QStringLiteral("ellipse")) return QStringLiteral("椭圆");
    if (toolId == QStringLiteral("pencil")) return QStringLiteral("铅笔");
    if (toolId == QStringLiteral("highlighter")) return QStringLiteral("荧光笔");
    if (toolId == QStringLiteral("eraser")) return QStringLiteral("橡皮");
    if (toolId == QStringLiteral("note_point")) return QStringLiteral("笔记");
    return QStringLiteral("标注");
}

bool ToolPresets::isValidToolId(const QString &toolId)
{
    return toolIds().contains(toolId);
}

AnnotationStyle ToolPresets::defaultStyle(const QString &toolId)
{
    return styleFor(toolId);
}

AnnotationStyle ToolPresets::style(const QString &toolId) const
{
    return m_styles.value(toolId, styleFor(toolId));
}

void ToolPresets::setStyle(const QString &toolId, const AnnotationStyle &style)
{
    if (!isValidToolId(toolId))
        return;
    m_styles.insert(toolId, style);
}

void ToolPresets::reset(const QString &toolId)
{
    if (!isValidToolId(toolId))
        return;
    m_styles.insert(toolId, styleFor(toolId));
}

void ToolPresets::resetAll()
{
    m_styles.clear();
    for (const QString &id : toolIds())
        m_styles.insert(id, styleFor(id));
}

QString ToolPresets::filePath()
{
    return QDir(appConfigDirectory()).filePath(QStringLiteral("tools.json"));
}

bool ToolPresets::load(QString *warningMessage)
{
    resetAll();
    const QFileInfo info(filePath());
    if (!info.exists())
        return true;
    QJsonObject object;
    QString error;
    if (!readJsonObject(filePath(), &object, &error)) {
        if (warningMessage)
            *warningMessage = QStringLiteral("工具预设文件无法读取，已使用内置默认值：%1").arg(error);
        return false;
    }
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("pano-tool-presets")
        || object.value(QStringLiteral("schema_version")).toInt() != 1) {
        if (warningMessage)
            *warningMessage = QStringLiteral("工具预设文件格式或版本不受支持，已保留原文件并使用内置默认值。");
        return false;
    }
    const QJsonObject presets = object.value(QStringLiteral("presets")).toObject();
    for (auto it = presets.begin(); it != presets.end(); ++it) {
        if (!isValidToolId(it.key()) || !it.value().isObject())
            continue;
        AnnotationStyle style;
        if (styleFromJson(it.value().toObject(), &style))
            m_styles.insert(it.key(), style);
    }
    return true;
}

bool ToolPresets::save(QString *errorMessage) const
{
    QJsonObject presets;
    for (auto it = m_styles.begin(); it != m_styles.end(); ++it)
        presets.insert(it.key(), styleToJson(it.value()));
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-tool-presets"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("presets"), presets);
    const QString path = filePath();
    if (!ensureDirectory(QFileInfo(path).absolutePath(), errorMessage))
        return false;
    if (!writeJsonObjectAtomic(path, root, errorMessage))
        return false;
    return true;
}

} // namespace pano
