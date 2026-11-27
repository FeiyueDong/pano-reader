#include "note/MarkdownPreview.h"

#include "note/MarkdownRenderer.h"

#include <QEvent>
#include <QPalette>
#include <QScrollBar>
#include <QTextDocument>

namespace pano {

MarkdownPreview::MarkdownPreview(QWidget *parent)
    : QTextBrowser(parent)
{
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setOpenExternalLinks(true);
    setOpenLinks(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);

    QFont baseFont = font();
    baseFont.setPointSizeF(10.5);
    document()->setDefaultFont(baseFont);
    document()->setDocumentMargin(4);
    applyDocumentStyle();
}

void MarkdownPreview::applyDocumentStyle()
{
    const QPalette pal = palette();
    const QString text = pal.color(QPalette::Text).name();
    const QString muted = pal.color(QPalette::Disabled, QPalette::Text).name();
    const QString codeBackground = pal.color(QPalette::AlternateBase).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString headingBackground = pal.color(QPalette::AlternateBase).name();

    document()->setDefaultStyleSheet(QStringLiteral(
        "h1 { font-size: 20px; font-weight: 600; margin-top: 12px; margin-bottom: 6px; }"
        "h2 { font-size: 17px; font-weight: 600; margin-top: 12px; margin-bottom: 4px; }"
        "h3 { font-size: 15px; font-weight: 600; margin-top: 10px; margin-bottom: 4px; }"
        "h4, h5, h6 { font-size: 13px; font-weight: 600; margin-top: 8px; margin-bottom: 2px; }"
        "p { margin-top: 3px; margin-bottom: 5px; }"
        "code { font-family: \"DejaVu Sans Mono\", monospace; background-color: %1; color: %2; }"
        "pre { background-color: %1; color: %2; padding: 8px; margin-top: 6px; margin-bottom: 6px; }"
        "blockquote { border-left: 3px solid %3; margin-left: 0px; padding-left: 10px; color: %4; }"
        "table { border-collapse: collapse; }"
        "th, td { border: 1px solid %3; padding: 3px 6px; }"
        "th { background-color: %5; }"
        "hr { border: none; border-top: 1px solid %3; }"
        "a { color: #2f6fed; }")
                                     .arg(codeBackground, text, border, muted, headingBackground));
}

void MarkdownPreview::changeEvent(QEvent *event)
{
    QTextBrowser::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        applyDocumentStyle();
        if (!m_lastMarkdown.isEmpty())
            setNoteMarkdown(m_lastMarkdown, m_baseDirectory);
    }
}

void MarkdownPreview::setNoteMarkdown(const QString &markdown, const QString &baseDirectory)
{
    m_baseDirectory = baseDirectory;
    m_lastMarkdown = markdown;
    const MarkdownRenderResult rendered = MarkdownRenderer::render(markdown, baseDirectory);

    document()->clear();
    for (auto it = rendered.resources.cbegin(); it != rendered.resources.cend(); ++it)
        document()->addResource(QTextDocument::ImageResource, it.key(), it.value());
    document()->setBaseUrl(QUrl::fromLocalFile(baseDirectory + QLatin1Char('/')));

    if (rendered.html.trimmed().isEmpty())
        setHtml(QStringLiteral("<p style=\"color:#7a808a;\">暂无笔记内容</p>"));
    else
        setHtml(rendered.html);

    verticalScrollBar()->setValue(0);
}

} // namespace pano
