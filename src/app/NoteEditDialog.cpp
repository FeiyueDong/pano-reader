#include "app/NoteEditDialog.h"

#include "common/TimeUtils.h"

#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QImage>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

#include "annotation/Annotation.h"
#include "document/DocumentSession.h"
#include "note/MarkdownPreview.h"
#include "note/MarkdownRenderer.h"
#include "reader/PdfView.h"

namespace pano {

namespace {

// QPlainTextEdit normally discards the rich-text flavour of a paste. Keep the
// Markdown source contract by converting pasted HTML through Qt's Markdown
// export instead of silently inserting flattened text.
class MarkdownPlainTextEdit : public QPlainTextEdit
{
public:
    explicit MarkdownPlainTextEdit(QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
    }

    std::function<void(const QImage &)> onImagePasted;

protected:
    void insertFromMimeData(const QMimeData *source) override
    {
        if (source && source->hasImage()) {
            const QImage image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull() && onImagePasted) {
                onImagePasted(image);
                return;
            }
        }
        if (source && source->hasHtml()) {
            QTextDocument document;
            document.setHtml(source->html());
            const QString markdown = document.toMarkdown(QTextDocument::MarkdownDialectGitHub).trimmed();
            if (!markdown.isEmpty()) {
                insertPlainText(markdown);
                return;
            }
        }
        QPlainTextEdit::insertFromMimeData(source);
    }
};

// Lightweight syntax hints only. The Markdown renderer remains the authority for
// structure; the highlighter must never drive persistence or preview.
class MarkdownHighlighter : public QSyntaxHighlighter
{
public:
    explicit MarkdownHighlighter(QTextDocument *document)
        : QSyntaxHighlighter(document)
    {
        QTextCharFormat heading;
        heading.setForeground(QColor(QStringLiteral("#2f6fed")));
        heading.setFontWeight(QFont::DemiBold);
        QTextCharFormat markup;
        markup.setForeground(QColor(QStringLiteral("#8b5e34")));
        QTextCharFormat code;
        code.setForeground(QColor(QStringLiteral("#a33a5a")));
        code.setFontFamilies({QStringLiteral("DejaVu Sans Mono")});
        QTextCharFormat math;
        math.setForeground(QColor(QStringLiteral("#246b55")));
        math.setFontWeight(QFont::DemiBold);

        m_rules.append({QRegularExpression(QStringLiteral("^#{1,6}\\s+.*$")), heading});
        m_rules.append({QRegularExpression(QStringLiteral("\\*\\*[^*]+\\*\\*")), markup});
        m_rules.append({QRegularExpression(QStringLiteral("(^|[^*])\\*[^*]+\\*")), markup});
        m_rules.append({QRegularExpression(QStringLiteral("`[^`]+`")), code});
        m_rules.append({QRegularExpression(QStringLiteral("\\$[^$]+\\$")), math});
        m_rules.append({QRegularExpression(QStringLiteral("^>.*$")), markup});
        m_rules.append({QRegularExpression(QStringLiteral("^[-*+]\\s+")), markup});
        m_rules.append({QRegularExpression(QStringLiteral("^\\d+\\.\\s+")), markup});
    }

protected:
    void highlightBlock(const QString &text) override
    {
        for (const Rule &rule : m_rules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    QList<Rule> m_rules;
};

} // namespace

NoteEditDialog::NoteEditDialog(DocumentSession *session, QWidget *parent)
    : QDialog(parent)
    , m_session(session)
{
    setWindowTitle(tr("编辑笔记[*]"));
    setModal(false);
    setMinimumSize(760, 520);
    resize(1040, 760);
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("noteEditor/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);

    auto *layout = new QVBoxLayout(this);
    m_headerLabel = new QLabel(tr("新建笔记"), this);
    QFont headerFont = m_headerLabel->font();
    headerFont.setBold(true);
    m_headerLabel->setFont(headerFont);
    layout->addWidget(m_headerLabel);

    auto *linkRow = new QHBoxLayout;
    m_linkLabel = new QLabel(this);
    m_linkLabel->setWordWrap(true);
    m_linkLabel->setEnabled(false);
    m_locateButton = new QPushButton(tr("定位原文"), this);
    m_locateButton->setObjectName(QStringLiteral("noteLocateButton"));
    m_locateButton->setFocusPolicy(Qt::NoFocus);
    linkRow->addWidget(m_linkLabel, 1);
    linkRow->addWidget(m_locateButton);
    layout->addLayout(linkRow);
    connect(m_locateButton, &QPushButton::clicked, this, [this] {
        if (!m_note.annotationId.isEmpty() && m_session)
            m_session->view()->revealAnnotation(m_note.annotationId);
    });

    auto *form = new QFormLayout;
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(tr("笔记标题"));
    form->addRow(tr("标题"), m_titleEdit);
    layout->addLayout(form);

    auto *formatRow = new QHBoxLayout;
    auto addFormatButton = [&](const QString &text, const QString &prefix, const QString &suffix) {
        auto *button = new QPushButton(text, this);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, prefix, suffix] {
            QTextCursor cursor = m_editor->textCursor();
            const QString selected = cursor.selectedText();
            cursor.insertText(prefix + (selected.isEmpty() ? QStringLiteral("文本") : selected) + suffix);
            m_editor->setTextCursor(cursor);
            m_editor->setFocus();
        });
        formatRow->addWidget(button);
    };
    addFormatButton(tr("标题"), QStringLiteral("## "), QString());
    addFormatButton(tr("加粗"), QStringLiteral("**"), QStringLiteral("**"));
    addFormatButton(tr("斜体"), QStringLiteral("*"), QStringLiteral("*"));
    addFormatButton(tr("列表"), QStringLiteral("- "), QString());
    addFormatButton(tr("代码"), QStringLiteral("`"), QStringLiteral("`"));
    addFormatButton(tr("公式"), QStringLiteral("$"), QStringLiteral("$"));
    auto *moreButton = new QToolButton(this);
    moreButton->setText(tr("更多"));
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(moreButton);
    auto addMore = [&](const QString &text, const QString &prefix, const QString &suffix,
                       const QString &placeholder) {
        QAction *action = moreMenu->addAction(text);
        connect(action, &QAction::triggered, this, [this, prefix, suffix, placeholder] {
            QTextCursor cursor = m_editor->textCursor();
            const QString selected = cursor.selectedText();
            cursor.insertText(prefix + (selected.isEmpty() ? placeholder : selected) + suffix);
            m_editor->setTextCursor(cursor);
            m_editor->setFocus();
        });
    };
    addMore(tr("删除线"), QStringLiteral("~~"), QStringLiteral("~~"), tr("文本"));
    addMore(tr("有序列表"), QStringLiteral("1. "), QString(), tr("条目"));
    addMore(tr("引用"), QStringLiteral("> "), QString(), tr("引用内容"));
    addMore(tr("分隔线"), QStringLiteral("\n---\n"), QString(), QString());
    addMore(tr("链接"), QStringLiteral("["), QStringLiteral("](https://)"), tr("链接文字"));
    addMore(tr("表格"), QStringLiteral("\n| 列1 | 列2 |\n| --- | --- |\n| 内容 | 内容 |\n"),
            QString(), QString());
    addMore(tr("代码块"), QStringLiteral("```\n"), QStringLiteral("\n```"), tr("代码"));
    moreButton->setMenu(moreMenu);
    formatRow->addWidget(moreButton);
    auto *smallerButton = new QPushButton(QStringLiteral("A-"), this);
    auto *largerButton = new QPushButton(QStringLiteral("A+"), this);
    smallerButton->setFocusPolicy(Qt::NoFocus);
    largerButton->setFocusPolicy(Qt::NoFocus);
    formatRow->addWidget(smallerButton);
    formatRow->addWidget(largerButton);
    formatRow->addStretch(1);
    layout->addLayout(formatRow);

    auto *toolRow = new QHBoxLayout;
    auto *imageButton = new QPushButton(tr("插入图片"), this);
    auto *captureButton = new QPushButton(tr("区域截图"), this);
    auto *regenerateButton = new QPushButton(tr("重新生成正文"), this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("在预览中查找"));
    m_searchEdit->setClearButtonEnabled(true);
    toolRow->addWidget(imageButton);
    toolRow->addWidget(captureButton);
    toolRow->addWidget(regenerateButton);
    toolRow->addWidget(m_searchEdit, 1);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems({tr("仅编辑"), tr("分栏"), tr("仅预览")});
    m_modeCombo->setObjectName(QStringLiteral("noteModeCombo"));
    toolRow->addWidget(m_modeCombo);
    layout->addLayout(toolRow);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    auto *editor = new MarkdownPlainTextEdit(m_splitter);
    m_editor = editor;
    m_editor->setPlaceholderText(tr("Markdown 内容；可以留空。"));
    m_preview = new MarkdownPreview(m_splitter);
    m_preview->setMinimumWidth(260);
    m_splitter->addWidget(m_editor);
    m_splitter->addWidget(m_preview);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({560, 420});
    layout->addWidget(m_splitter, 1);
    m_highlighter = new MarkdownHighlighter(m_editor->document());
    editor->onImagePasted = [this](const QImage &image) { insertImageData(image); };

    m_renderStatus = new QLabel(this);
    m_renderStatus->setWordWrap(true);
    m_renderStatus->setEnabled(false);
    layout->addWidget(m_renderStatus);

    auto *buttons = new QHBoxLayout;
    m_saveButton = new QPushButton(tr("保存并继续"), this);
    m_okButton = new QPushButton(tr("确定"), this);
    auto *cancel = new QPushButton(tr("取消"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_saveButton);
    buttons->addWidget(m_okButton);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(250);
    connect(m_previewTimer, &QTimer::timeout, this, &NoteEditDialog::refreshPreview);

    auto adjustFont = [this](int delta) {
        QFont font = m_editor->font();
        const int size = qBound(8, font.pointSize() + delta, 32);
        font.setPointSize(size);
        m_editor->setFont(font);
        if (m_preview)
            m_preview->setFont(font);
        QSettings settings;
        settings.setValue(QStringLiteral("noteEditor/fontSize"), size);
    };
    connect(smallerButton, &QPushButton::clicked, this, [adjustFont] { adjustFont(-1); });
    connect(largerButton, &QPushButton::clicked, this, [adjustFont] { adjustFont(1); });
    connect(imageButton, &QPushButton::clicked, this, &NoteEditDialog::imageRequested);
    connect(captureButton, &QPushButton::clicked, this, &NoteEditDialog::captureRequested);
    connect(regenerateButton, &QPushButton::clicked, this, &NoteEditDialog::regenerateMarkdown);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_preview)
            m_preview->setSearchQuery(text);
    });
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

    {
        QSettings settings;
        const int savedFont = settings.value(QStringLiteral("noteEditor/fontSize"), 0).toInt();
        if (savedFont >= 8 && savedFont <= 32) {
            QFont font = m_editor->font();
            font.setPointSize(savedFont);
            m_editor->setFont(font);
            m_preview->setFont(font);
        }
    }
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &NoteEditDialog::applyMode);
    {
        QSettings settings;
        const int mode = settings.value(QStringLiteral("noteEditor/mode"), 1).toInt();
        m_modeCombo->setCurrentIndex(qBound(0, mode, 2));
    }
    connect(m_titleEdit, &QLineEdit::textChanged, this, [this] { m_dirty = true; });
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        m_dirty = true;
        schedulePreview();
    });
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
    m_editor->clear();
    if (m_session && !annotationId.isEmpty()) {
        if (const Annotation *annotation = m_session->view()->annotations()->find(annotationId)) {
            const QString excerpt = annotation->text.trimmed();
            if (!excerpt.isEmpty()) {
                QString quoted;
                const QStringList lines = excerpt.split(QLatin1Char('\n'));
                for (const QString &line : lines)
                    quoted += QStringLiteral("> ") + line + QLatin1Char('\n');
                m_editor->setPlainText(quoted + QLatin1Char('\n'));
            }
        }
    }
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
        m_locateButton->setEnabled(true);
    } else if (!m_note.annotationId.isEmpty()) {
        m_linkLabel->setText(tr("关联的标注已不存在；保存时将解除关联。"));
        m_locateButton->setEnabled(false);
    } else {
        m_linkLabel->setText(tr("独立笔记（未关联标注）"));
        m_locateButton->setEnabled(false);
    }
    setWindowModified(m_dirty);
    refreshPreview();
}

void NoteEditDialog::schedulePreview()
{
    if (m_previewTimer)
        m_previewTimer->start();
}

void NoteEditDialog::refreshPreview()
{
    if (!m_preview || !m_session)
        return;
    const QString markdown = m_editor->toPlainText();
    const qreal base = m_preview ? m_preview->font().pointSizeF() : 11.0;
    const MarkdownRenderResult result = MarkdownRenderer::render(
        markdown, m_session->bookDirectory(),
        base > 0.0 ? base : 11.0, palette().color(QPalette::Text));
    QScrollBar *scroll = m_preview->verticalScrollBar();
    const int oldValue = scroll->value();
    const int oldMaximum = scroll->maximum();
    const qreal ratio = oldMaximum > 0 ? qreal(oldValue) / qreal(oldMaximum) : 0.0;
    m_preview->setNoteMarkdown(markdown, m_session->bookDirectory());
    QTimer::singleShot(0, m_preview, [scroll, ratio] {
        scroll->setValue(qRound(ratio * scroll->maximum()));
    });
    m_preview->setSearchQuery(m_searchEdit ? m_searchEdit->text() : QString());

    QStringList status;
    if (result.mathFailures > 0)
        status.append(tr("公式失败 %1（已保留源文本）").arg(result.mathFailures));
    if (!result.warnings.isEmpty())
        status.append(result.warnings.first());
    m_renderStatus->setText(status.join(QStringLiteral(" · ")));
    m_renderStatus->setToolTip(result.warnings.join(QLatin1Char('\n')));
}

void NoteEditDialog::regenerateMarkdown()
{
    if (!m_preview)
        return;
    const QString markdown = m_preview->document()->toMarkdown(QTextDocument::MarkdownDialectGitHub).trimmed();
    if (markdown.isEmpty()) {
        QMessageBox::information(this, tr("重新生成正文"), tr("预览中没有可转换为 Markdown 的内容。"));
        return;
    }
    if (QMessageBox::question(this, tr("重新生成正文"),
                              tr("将用当前预览内容替换 Markdown 正文，未保存的正文修改会丢失。是否继续？"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        return;
    }
    m_editor->setPlainText(markdown);
    m_dirty = true;
    schedulePreview();
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
    schedulePreview();
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
    QSettings settings;
    settings.setValue(QStringLiteral("noteEditor/geometry"), saveGeometry());
    if (m_closing || !m_dirty) {
        QDialog::closeEvent(event);
        emit closed();
        return;
    }
    event->ignore();
    requestClose();
}

void NoteEditDialog::applyMode(int mode)
{
    const bool showEditor = mode != 2;
    const bool showPreview = mode != 0;
    if (m_editor)
        m_editor->setVisible(showEditor);
    if (m_preview)
        m_preview->setVisible(showPreview);
    if (m_splitter && showEditor && showPreview && m_splitter->sizes().value(0) <= 0)
        m_splitter->setSizes({560, 420});
    QSettings settings;
    settings.setValue(QStringLiteral("noteEditor/mode"), mode);
    if (showPreview)
        refreshPreview();
    if (showEditor)
        m_editor->setFocus();
}

void NoteEditDialog::insertImageData(const QImage &image)
{
    if (image.isNull() || !m_session)
        return;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return;
    QString relative;
    QString error;
    if (!m_session->writeAsset(QStringLiteral("image"), QStringLiteral("png"), bytes, &relative,
                               &error)) {
        QMessageBox::warning(this, tr("插入图片失败"), error);
        return;
    }
    insertMarkdown(QStringLiteral("![剪贴板图片](%1)\n").arg(relative));
}

} // namespace pano
