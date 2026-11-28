#pragma once

#include <QDialog>

#include <QSet>

#include "annotation/Annotation.h"
#include "note/Note.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QSyntaxHighlighter;
class QTimer;

namespace pano {

class DocumentSession;
class MarkdownPreview;

// Non-modal editor for one note. The dialog is bound to a book/note target,
// not to the reader selection, so changing the current PDF selection cannot
// redirect a save.
class NoteEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NoteEditDialog(DocumentSession *session, QWidget *parent = nullptr);
    ~NoteEditDialog() override;

    void openNewNote(const QString &parentGroupId, const QString &annotationId = QString());
    void openNewNoteAtPoint(const QString &parentGroupId, int pageIndex, const QPointF &pagePoint,
                            const AnnotationStyle &style);
    void openExistingNote(const QString &noteId);
    QString currentNoteId() const { return m_note.id; }
    bool hasUnsavedChanges() const { return m_dirty; }
    bool commitIfDirty(QString *errorMessage);
    bool prepareToClose(QString *errorMessage);
    void insertMarkdown(const QString &text);
    void insertAssetMarkdown(const QString &relativePath, const QString &altText);
    DocumentSession *session() const { return m_session; }

signals:
    void saved(const QString &noteId);
    void closed();
    void imageRequested();
    void captureRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void done(int result) override;

private:
    void updateHeader();
    void loadEditorContent();
    void cleanupDraftAssets();
    bool saveInternal(bool closeAfter, QString *errorMessage = nullptr);
    bool confirmSwitchTarget(const QString &targetNoteId);
    void requestClose();
    void schedulePreview();
    void refreshPreview();
    void applyMode(int mode);
    void insertImageData(const QImage &image);

    DocumentSession *m_session = nullptr;
    Note m_note;
    QString m_parentGroupId;
    int m_pendingPageIndex = -1;
    QPointF m_pendingPagePoint;
    AnnotationStyle m_pendingPointStyle;
    bool m_createNotePoint = false;
    bool m_isNew = false;
    bool m_dirty = false;
    bool m_closing = false;
    bool m_closedEmitted = false;
    QSet<QString> m_draftAssets;
    QSet<QString> m_committedAssets;

    QLabel *m_headerLabel = nullptr;
    QLabel *m_linkLabel = nullptr;
    QLineEdit *m_titleEdit = nullptr;
    QPlainTextEdit *m_editor = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_okButton = nullptr;
    QLabel *m_renderStatus = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    MarkdownPreview *m_preview = nullptr;
    QSplitter *m_splitter = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QPushButton *m_locateButton = nullptr;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QTimer *m_previewTimer = nullptr;
};

} // namespace pano
