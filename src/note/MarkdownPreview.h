#pragma once

#include <QTextBrowser>

namespace pano {

// Read-only rich-text preview of a Markdown note. Math is rendered locally and
// local images are embedded as document resources; the preview never edits the
// Markdown source.
class MarkdownPreview : public QTextBrowser
{
    Q_OBJECT

public:
    explicit MarkdownPreview(QWidget *parent = nullptr);

    void setNoteMarkdown(const QString &markdown, const QString &baseDirectory);
    QString baseDirectory() const { return m_baseDirectory; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyDocumentStyle();

    QString m_baseDirectory;
    QString m_lastMarkdown;
};

} // namespace pano
