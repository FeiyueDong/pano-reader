#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

namespace pano {

class MarkdownPreview;

// Right-hand note dock: read-only Markdown preview plus an inline editor.
// Editing stays in the main window so a PDF region can still be captured.
class NotePanel : public QWidget
{
    Q_OBJECT

public:
    explicit NotePanel(QWidget *parent = nullptr);

    void setBaseDirectory(const QString &directory);
    void setHeading(const QString &heading);
    void setNote(const QString &markdown);
    void clearNote(const QString &placeholder = QString());
    void setEditable(bool editable);

    void beginEdit();
    void endEdit();
    bool isEditing() const { return m_editing; }
    QString markdown() const;
    QString noteMarkdown() const { return m_noteMarkdown; }
    void insertMarkdown(const QString &text);
    void refreshPreview();

signals:
    void editRequested();
    void commitRequested();
    void cancelRequested();
    void imageRequested();
    void captureRequested();
    void contentChanged();

private:
    QLabel *m_heading = nullptr;
    QLabel *m_hint = nullptr;
    QTabWidget *m_tabs = nullptr;
    MarkdownPreview *m_preview = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_commitButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_imageButton = nullptr;
    QPushButton *m_captureButton = nullptr;
    QTimer *m_previewTimer = nullptr;
    QString m_baseDirectory;
    QString m_noteMarkdown;
    bool m_editing = false;
    bool m_editable = false;
};

} // namespace pano
