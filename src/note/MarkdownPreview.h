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
    void setSearchQuery(const QString &query);
    QString searchQuery() const { return m_searchQuery; }
    QString baseDirectory() const { return m_baseDirectory; }

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyDocumentStyle();
    void applySearchHighlight();

    QString m_baseDirectory;
    QString m_lastMarkdown;
    QString m_searchQuery;
};

} // namespace pano
