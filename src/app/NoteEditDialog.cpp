#include "app/NoteEditDialog.h"

#include "common/TimeUtils.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "annotation/Annotation.h"
#include "document/DocumentSession.h"
#include "reader/PdfView.h"

namespace pano {

NoteEditDialog::NoteEditDialog(DocumentSession *session, QWidget *parent)
    : QDialog(parent)
    , m_session(session)
{
    setWindowTitle(tr("编辑笔记[*]"));
    setModal(false);
    resize(640, 520);
    setMinimumSize(420, 320);

    auto *layout = new QVBoxLayout(this);
    m_headerLabel = new QLabel(tr("新建笔记"), this);
    QFont headerFont = m_headerLabel->font();
    headerFont.setBold(true);
    m_headerLabel->setFont(headerFont);
    layout->addWidget(m_headerLabel);

    m_linkLabel = new QLabel(this);
    m_linkLabel->setWordWrap(true);
    m_linkLabel->setEnabled(false);
    layout->addWidget(m_linkLabel);

    auto *form = new QFormLayout;
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(tr("笔记标题"));
    form->addRow(tr("标题"), m_titleEdit);
    layout->addLayout(form);

    m_editor = new QPlainTextEdit(this);
    m_editor->setPlaceholderText(tr("Markdown 内容；可以留空。"));
    layout->addWidget(m_editor, 1);

    auto *buttons = new QHBoxLayout;
    m_saveButton = new QPushButton(tr("保存并继续"), this);
    m_okButton = new QPushButton(tr("确定"), this);
    auto *cancel = new QPushButton(tr("取消"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_saveButton);
    buttons->addWidget(m_okButton);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (saveInternal(false, &error))
            setWindowTitle(tr("编辑笔记[*]"));
        else if (!error.isEmpty())
            QMessageBox::warning(this, tr("保存失败"), error);
    });
    connect(m_okButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (saveInternal(true, &error))
            return;
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("保存失败"), error);
    });
    connect(cancel, &QPushButton::clicked, this, &NoteEditDialog::requestClose);
    auto *shortcut = new QShortcut(QKeySequence::Save, this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, [this] {
        QString error;
        if (!saveInternal(false, &error) && !error.isEmpty())
            QMessageBox::warning(this, tr("保存失败"), error);
    });

    connect(m_titleEdit, &QLineEdit::textChanged, this, [this] { m_dirty = true; });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { m_dirty = true; });
}

void NoteEditDialog::openNewNote(const QString &parentGroupId, const QString &annotationId)
{
    m_note = Note();
    m_note.id = newNoteId();
    m_note.title = QStringLiteral("未命名笔记");
    m_note.annotationId = annotationId;
    m_note.createdAt = nowIsoUtc();
    m_note.modifiedAt = m_note.createdAt;
    m_parentGroupId = parentGroupId;
    m_isNew = true;
    m_dirty = true;
    updateHeader();
    show();
    raise();
    activateWindow();
    m_titleEdit->selectAll();
    m_titleEdit->setFocus();
}

void NoteEditDialog::openExistingNote(const QString &noteId)
{
    const Note *value = m_session ? m_session->note(noteId) : nullptr;
    if (!value) {
        QMessageBox::warning(this, tr("编辑笔记"), tr("笔记已不存在。"));
        return;
    }
    m_note = *value;
    m_parentGroupId.clear();
    m_isNew = false;
    m_dirty = false;
    updateHeader();
    show();
    raise();
    activateWindow();
    m_editor->setFocus();
}

void NoteEditDialog::updateHeader()
{
    m_headerLabel->setText(m_isNew ? tr("新建笔记") : tr("编辑笔记 · %1").arg(m_note.displayTitle()));
    m_titleEdit->blockSignals(true);
    m_titleEdit->setText(m_note.title);
    m_titleEdit->blockSignals(false);
    m_editor->blockSignals(true);
    m_editor->setPlainText(m_note.markdown);
    m_editor->blockSignals(false);
    const Annotation *annotation = m_session && !m_note.annotationId.isEmpty()
                                       ? m_session->view()->annotations()->find(m_note.annotationId)
                                       : nullptr;
    if (annotation) {
        m_linkLabel->setText(tr("关联：%1 · 第 %2 页")
                                 .arg(annotationTypeDisplayName(annotation->type))
                                 .arg(annotation->pageIndex + 1));
    } else if (!m_note.annotationId.isEmpty()) {
        m_linkLabel->setText(tr("关联的标注已不存在；保存时将解除关联。"));
    } else {
        m_linkLabel->setText(tr("独立笔记（未关联标注）"));
    }
    setWindowModified(m_dirty);
}

bool NoteEditDialog::saveInternal(bool closeAfter, QString *errorMessage)
{
    if (!m_session) {
        if (errorMessage)
            *errorMessage = QStringLiteral("会话不可用。");
        return false;
    }
    m_note.title = m_titleEdit->text().trimmed();
    if (m_note.title.isEmpty())
        m_note.title = QStringLiteral("未命名笔记");
    m_note.markdown = m_editor->toPlainText();
    if (!m_note.annotationId.isEmpty() && !m_session->view()->annotations()->find(m_note.annotationId)) {
        m_note.annotationId.clear();
    }
    QString localError;
    bool ok = false;
    if (m_isNew) {
        QString id;
        ok = m_session->createNote(m_note, m_parentGroupId, &id, &localError);
        if (ok) {
            m_note.id = id;
            m_isNew = false;
        }
    } else {
        ok = m_session->updateNote(m_note, &localError);
    }
    if (!ok) {
        if (errorMessage)
            *errorMessage = localError.isEmpty() ? QStringLiteral("保存笔记失败。") : localError;
        return false;
    }
    m_dirty = false;
    setWindowModified(false);
    updateHeader();
    emit saved(m_note.id);
    if (closeAfter) {
        m_closing = true;
        QDialog::accept();
        m_closing = false;
    }
    return true;
}

void NoteEditDialog::insertMarkdown(const QString &text)
{
    if (text.isEmpty())
        return;
    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!m_editor->toPlainText().isEmpty() && !m_editor->toPlainText().endsWith(QLatin1Char('\n')))
        cursor.insertText(QStringLiteral("\n"));
    cursor.insertText(text);
    m_editor->setTextCursor(cursor);
    m_dirty = true;
    show();
    raise();
    activateWindow();
}

bool NoteEditDialog::commitIfDirty(QString *errorMessage)
{
    if (!m_dirty)
        return true;
    return saveInternal(false, errorMessage);
}

void NoteEditDialog::requestClose()
{
    if (!m_dirty) {
        reject();
        return;
    }
    const QMessageBox::StandardButton result = QMessageBox::question(
        this, tr("未保存的笔记"), tr("笔记有未保存的修改。"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (result == QMessageBox::Cancel)
        return;
    if (result == QMessageBox::Save) {
        QString error;
        if (!saveInternal(false, &error)) {
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("保存失败"), error);
            return;
        }
    } else {
        m_dirty = false;
    }
    reject();
}

void NoteEditDialog::closeEvent(QCloseEvent *event)
{
    if (m_closing || !m_dirty) {
        QDialog::closeEvent(event);
        emit closed();
        return;
    }
    event->ignore();
    requestClose();
}

} // namespace pano
