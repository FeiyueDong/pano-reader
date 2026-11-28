#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace pano {

struct MarkdownRenderResult
{
    QString html;
    QHash<QUrl, QImage> resources;
    QStringList warnings;
    int formulaCount = 0;
    int mathFailures = 0;
    int missingImages = 0;
};

// Converts note Markdown using MD4C (CommonMark + GFM tables/strikethrough and
// LaTeX math spans). Formulas are rendered locally with JKQTMathText and
// injected as QTextDocument image resources; only local images are loaded.
class MarkdownRenderer
{
public:
    static MarkdownRenderResult render(const QString &markdown, const QString &baseDirectory,
                                       qreal fontPt = 11.0,
                                       const QColor &textColor = QColor(QStringLiteral("#20242b")));
};

} // namespace pano
