#include "note/MarkdownPreview.h"

#include "note/MarkdownRenderer.h"

#include <QEvent>
#include <QPalette>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>

namespace pano {

MarkdownPreview::MarkdownPreview(QWidget *parent)
    : QTextBrowser(parent)
{
    setReadOnly(true);
    setFrameShape(QFrame::NoFrame);
    setOpenExternalLinks(false);
    setOpenLinks(false);
    setContextMenuPolicy(Qt::DefaultContextMenu);

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

    const qreal base = font().pointSizeF() > 0.0 ? font().pointSizeF() : 11.0;
    QFont baseFont = font();
    baseFont.setPointSizeF(base);
    document()->setDefaultFont(baseFont);
    document()->setDefaultStyleSheet(QStringLiteral(
        "h1 { font-size: %1pt; font-weight: 600; margin-top: 12px; margin-bottom: 6px; }"
        "h2 { font-size: %2pt; font-weight: 600; margin-top: 12px; margin-bottom: 4px; }"
        "h3 { font-size: %3pt; font-weight: 600; margin-top: 10px; margin-bottom: 4px; }"
        "h4, h5, h6 { font-size: %4pt; font-weight: 600; margin-top: 8px; margin-bottom: 2px; }"
        "p { margin-top: 3px; margin-bottom: 5px; }"
        "code { font-family: \"DejaVu Sans Mono\", monospace; background-color: %5; color: %6; }"
        "pre { background-color: %5; color: %6; padding: 8px; margin-top: 6px; margin-bottom: 6px; }"
        "blockquote { border-left: 3px solid %7; margin-left: 0px; padding-left: 10px; color: %8; }"
        "table { border-collapse: collapse; }"
        "th, td { border: 1px solid %7; padding: 3px 6px; }"
        "th { background-color: %9; }"
        "hr { border: none; border-top: 1px solid %7; }"
        "a { color: %10; }"
        ".math-error, .asset-missing { color: #c0392b; }")
                                     .arg(QString::number(base * 1.8, 'f', 1))
                                     .arg(QString::number(base * 1.55, 'f', 1))
                                     .arg(QString::number(base * 1.35, 'f', 1))
                                     .arg(QString::number(base * 1.15, 'f', 1))
                                     .arg(codeBackground)
                                     .arg(text)
                                     .arg(border)
                                     .arg(muted)
                                     .arg(headingBackground)
                                     .arg(pal.color(QPalette::Link).name()));
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
    const qreal base = font().pointSizeF() > 0.0 ? font().pointSizeF() : 11.0;
    setRenderedMarkdown(MarkdownRenderer::render(markdown, baseDirectory, base,
                                                  palette().color(QPalette::Text)),
                        markdown, baseDirectory);
}

void MarkdownPreview::setRenderedMarkdown(const MarkdownRenderResult &rendered,
                                          const QString &markdown, const QString &baseDirectory)
{
    m_baseDirectory = baseDirectory;
    m_lastMarkdown = markdown;
    document()->clear();
    for (auto it = rendered.resources.cbegin(); it != rendered.resources.cend(); ++it)
        document()->addResource(QTextDocument::ImageResource, it.key(), it.value());
    document()->setBaseUrl(QUrl::fromLocalFile(baseDirectory + QLatin1Char('/')));

    if (rendered.html.trimmed().isEmpty())
        setHtml(QStringLiteral("<p style=\"color:#7a808a;\">暂无笔记内容</p>"));
    else
        setHtml(rendered.html);

    verticalScrollBar()->setValue(0);
    applySearchHighlight();
}

void MarkdownPreview::setSearchQuery(const QString &query)
{
    m_searchQuery = query;
    applySearchHighlight();
}

void MarkdownPreview::applySearchHighlight()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!m_searchQuery.isEmpty()) {
        const QColor background = palette().color(QPalette::Highlight);
        const QColor foreground = palette().color(QPalette::HighlightedText);
        QTextCursor cursor(document());
        while (true) {
            cursor = document()->find(m_searchQuery, cursor);
            if (cursor.isNull())
                break;
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format.setBackground(background);
            selection.format.setForeground(foreground);
            selections.append(selection);
        }
    }
    setExtraSelections(selections);
}

} // namespace pano
