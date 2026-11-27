#include "app/NotePanel.h"

#include "note/MarkdownPreview.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace pano {

NotePanel::NotePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 8);
    layout->setSpacing(6);

    m_heading = new QLabel(tr("笔记"), this);
    QFont headingFont = m_heading->font();
    headingFont.setBold(true);
    m_heading->setFont(headingFont);
    layout->addWidget(m_heading);

    m_tabs = new QTabWidget(this);
    m_preview = new MarkdownPreview(m_tabs);
    m_editor = new QPlainTextEdit(m_tabs);
    m_editor->setPlaceholderText(tr("使用 Markdown 记录说明，支持 $公式$、图片和表格。"));
    m_editor->setTabChangesFocus(false);
    QFont editorFont(QStringLiteral("DejaVu Sans Mono"));
    editorFont.setStyleHint(QFont::Monospace);
    editorFont.setPointSizeF(10.0);
    m_editor->setFont(editorFont);
    m_tabs->addTab(m_preview, tr("预览"));
    m_tabs->addTab(m_editor, tr("编辑"));
    m_editor->setReadOnly(true);
    m_tabs->setTabEnabled(1, false);
    m_tabs->setCurrentWidget(m_preview);
    layout->addWidget(m_tabs, 1);

    m_hint = new QLabel(tr("选择页面上的笔记图钉，或使用笔记工具新建一条笔记。"), this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(m_hint);

    auto *buttons = new QHBoxLayout();
    m_editButton = new QPushButton(tr("编辑"), this);
    m_commitButton = new QPushButton(tr("完成"), this);
    m_commitButton->setDefault(true);
    m_cancelButton = new QPushButton(tr("取消"), this);
    m_imageButton = new QPushButton(tr("插入图片"), this);
    m_captureButton = new QPushButton(tr("区域截图"), this);
    m_commitButton->hide();
    m_cancelButton->hide();
    m_imageButton->hide();
    m_captureButton->hide();
    buttons->addWidget(m_editButton);
    buttons->addWidget(m_imageButton);
    buttons->addWidget(m_captureButton);
    buttons->addStretch(1);
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_commitButton);
    layout->addLayout(buttons);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(350);
    connect(m_previewTimer, &QTimer::timeout, this, &NotePanel::refreshPreview);

    connect(m_editButton, &QPushButton::clicked, this, &NotePanel::editRequested);
    connect(m_commitButton, &QPushButton::clicked, this, &NotePanel::commitRequested);
    connect(m_cancelButton, &QPushButton::clicked, this, &NotePanel::cancelRequested);
    connect(m_imageButton, &QPushButton::clicked, this, &NotePanel::imageRequested);
    connect(m_captureButton, &QPushButton::clicked, this, &NotePanel::captureRequested);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_editing) {
            m_previewTimer->start();
            emit contentChanged();
        }
    });

    setEditable(false);
}

void NotePanel::setBaseDirectory(const QString &directory)
{
    m_baseDirectory = directory;
}

void NotePanel::setHeading(const QString &heading)
{
    m_heading->setText(heading);
}

void NotePanel::setNote(const QString &markdown)
{
    m_noteMarkdown = markdown;
    m_editor->setPlainText(markdown);
    m_preview->setNoteMarkdown(markdown, m_baseDirectory);
    m_hint->setVisible(markdown.trimmed().isEmpty());
    m_tabs->setCurrentWidget(m_preview);
}

void NotePanel::clearNote(const QString &placeholder)
{
    m_noteMarkdown.clear();
    m_editor->clear();
    m_preview->setHtml(QStringLiteral("<p style=\"color:#7a808a;\">%1</p>")
                           .arg(placeholder.isEmpty()
                                    ? tr("该标注还没有笔记。点击“编辑”开始记录。")
                                    : placeholder.toHtmlEscaped()));
    m_hint->setVisible(true);
    m_tabs->setCurrentWidget(m_preview);
}

void NotePanel::setEditable(bool editable)
{
    m_editable = editable;
    if (!editable && m_editing)
        endEdit();
    m_editButton->setEnabled(editable);
}

void NotePanel::beginEdit()
{
    if (!m_editable)
        return;
    m_editing = true;
    m_tabs->setTabEnabled(1, true);
    m_editor->setReadOnly(false);
    m_tabs->setCurrentWidget(m_editor);
    m_editButton->hide();
    m_commitButton->show();
    m_cancelButton->show();
    m_imageButton->show();
    m_captureButton->show();
    m_hint->hide();
    m_editor->moveCursor(QTextCursor::End);
    m_editor->setFocus();
}

void NotePanel::endEdit()
{
    m_editing = false;
    m_previewTimer->stop();
    m_editor->setReadOnly(true);
    m_tabs->setTabEnabled(1, false);
    m_tabs->setCurrentWidget(m_preview);
    m_editButton->show();
    m_commitButton->hide();
    m_cancelButton->hide();
    m_imageButton->hide();
    m_captureButton->hide();
    m_hint->setVisible(m_noteMarkdown.trimmed().isEmpty());
}

QString NotePanel::markdown() const
{
    return m_editor->toPlainText();
}

void NotePanel::insertMarkdown(const QString &text)
{
    if (!m_editing)
        return;
    QTextCursor cursor = m_editor->textCursor();
    QString insertion = text;
    const QString current = m_editor->toPlainText();
    if (!current.isEmpty() && cursor.atStart())
        insertion = insertion.trimmed() + QLatin1Char('\n');
    cursor.insertText(insertion);
    m_editor->setTextCursor(cursor);
    m_editor->setFocus();
    m_previewTimer->start();
}

void NotePanel::refreshPreview()
{
    m_preview->setNoteMarkdown(m_editor->toPlainText(), m_baseDirectory);
    m_hint->setVisible(m_editor->toPlainText().trimmed().isEmpty());
}

} // namespace pano
