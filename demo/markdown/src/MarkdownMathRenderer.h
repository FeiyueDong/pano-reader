#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace pano::markdown {

struct MathRenderResult {
    QImage image;
    QSizeF logicalSize;
    QString error;
    bool ok() const { return !image.isNull() && error.isEmpty(); }
};

struct MarkdownRenderResult {
    QString html;
    QHash<QUrl, QImage> resources;
    int formulaCount = 0;
    int formulaFailures = 0;
    int missingImages = 0;
    QStringList warnings;
    qint64 parseMs = 0;
    qint64 mathMs = 0;
};

class MarkdownMathRenderer {
public:
    static MathRenderResult renderMath(const QString& latex, bool display, qreal fontPt,
                                       const QColor& color, qreal dpr = 2.0);
    static MarkdownRenderResult render(const QString& markdown, const QString& baseDirectory,
                                       qreal fontPt, const QColor& textColor, qreal dpr = 2.0,
                                       qreal availableWidth = 900.0);
    static QString defaultStyleSheet(bool dark, qreal fontPt);
};

} // namespace pano::markdown
