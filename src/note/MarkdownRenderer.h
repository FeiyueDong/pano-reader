#pragma once

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
};

// Converts the note Markdown into Qt rich text. Math is rendered locally by
// MathRenderer and injected as QTextDocument image resources; local images are
// injected the same way, so previews never need network access.
class MarkdownRenderer
{
public:
    static MarkdownRenderResult render(const QString &markdown, const QString &baseDirectory);
};

} // namespace pano
