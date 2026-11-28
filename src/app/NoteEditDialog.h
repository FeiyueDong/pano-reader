#pragma once

#include <QDialog>

#include "note/Note.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace pano {

class DocumentSession;

// Non-modal editor for one note.  The dialog is bound to a book/note target,
// not to the reader selection, so changing the current PDF selection cannot
// redirect a save.
class NoteEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NoteEditDialog(DocumentSession *session, QWidget *parent = nullptr);

    void openNewNote(const QString &parentGroupId, const QString &annotationId = QString());
    void openExistingNote(const QString &noteId);
    QString currentNoteId() const { return m_note.id; }
    bool hasUnsavedChanges() const { return m_dirty; }
    bool commitIfDirty(QString *errorMessage);
    void insertMarkdown(const QString &text);

signals:
    void saved(const QString &noteId);
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void updateHeader();
    bool saveInternal(bool closeAfter, QString *errorMessage = nullptr);
    void requestClose();

    DocumentSession *m_session = nullptr;
    Note m_note;
    QString m_parentGroupId;
    bool m_isNew = false;
    bool m_dirty = false;
    bool m_closing = false;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_linkLabel = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_okButton = nullptr;
};

} // namespace pano
