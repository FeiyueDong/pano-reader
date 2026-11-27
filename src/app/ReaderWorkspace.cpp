#include "app/ReaderWorkspace.h"

#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "app/NotePanel.h"
#include "app/RegionCaptureOverlay.h"
#include "document/DocumentSession.h"
#include "reader/PdfView.h"

namespace pano {

namespace {

QString firstMarkdownLine(const QString &markdown)
{
    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        while (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('>'))
               || line.startsWith(QLatin1Char('-')) || line.startsWith(QLatin1Char('*')))
            line.remove(0, 1);
        line = line.trimmed();
        line.remove(QStringLiteral("**"));
        line.remove(QStringLiteral("__"));
        line.remove(QLatin1Char('`'));
        if (!line.isEmpty())
            return line;
    }
    return QString();
}

QString ellipsized(const QString &text, int maxCharacters)
{
    return text.size() <= maxCharacters ? text : text.left(maxCharacters - 1) + QStringLiteral("…");
}

} // namespace

ReaderWorkspace::ReaderWorkspace(DocumentSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    buildUi();
    connect(m_view, &PdfView::annotationsChanged, this, &ReaderWorkspace::onAnnotationsChanged);
    connect(m_view, &PdfView::annotationSelectionChanged, this, &ReaderWorkspace::onSelectionChanged);
    connect(m_view, &PdfView::noteRequested, this, &ReaderWorkspace::onNoteRequested);
    connect(m_view, &PdfView::noteDeleteRequested, this, &ReaderWorkspace::onNoteDeleteRequested);
    connect(m_view, &PdfView::statusMessage, this, &ReaderWorkspace::statusMessage);
    connect(m_view, &PdfView::toolChanged, this, [this](PdfView::Tool) { updatePropertyPanel(); });
    connect(m_view, &PdfView::annotationsChanged, this, &ReaderWorkspace::updatePropertyPanel);
    connect(m_view, &PdfView::annotationSelectionChanged, this,
            [this](const QString &) { updatePropertyPanel(); });

    connect(m_notePanel, &NotePanel::editRequested, this, &ReaderWorkspace::beginNoteEdit);
    connect(m_notePanel, &NotePanel::commitRequested, this, &ReaderWorkspace::commitNoteEdit);
    connect(m_notePanel, &NotePanel::cancelRequested, this, &ReaderWorkspace::cancelNoteEdit);
    connect(m_notePanel, &NotePanel::imageRequested, this, &ReaderWorkspace::insertImageIntoNote);
    connect(m_notePanel, &NotePanel::captureRequested, this, &ReaderWorkspace::startRegionCapture);

    m_notePanel->setBaseDirectory(m_session->bookDirectory());
    m_notePanel->setEditable(true);
    refreshAll();
}

ReaderWorkspace::~ReaderWorkspace()
{
    if (m_view) {
        disconnect(m_view, nullptr, this, nullptr);
        m_view->blockSignals(true);
    }
    if (m_notePanel)
        disconnect(m_notePanel, nullptr, this, nullptr);
}

void ReaderWorkspace::buildUi()
{
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    auto *leftTabs = new QTabWidget(splitter);
    leftTabs->setMinimumWidth(230);
    leftTabs->setMaximumWidth(340);
    m_annotationList = new QListWidget(leftTabs);
    m_annotationList->setAlternatingRowColors(true);
    connect(m_annotationList, &QListWidget::itemActivated, this,
            &ReaderWorkspace::onNoteListItemActivated);
    connect(m_annotationList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::onNoteListItemActivated);
    leftTabs->addTab(m_annotationList, tr("标注与笔记"));
    leftTabs->addTab(buildPropertyPanel(), tr("属性"));

    m_view = new PdfView(splitter);
    m_notePanel = new NotePanel(splitter);
    m_notePanel->setMinimumWidth(300);

    splitter->addWidget(leftTabs);
    splitter->addWidget(m_view);
    splitter->addWidget(m_notePanel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({260, 760, 360});
}

QWidget *ReaderWorkspace::buildPropertyPanel()
{
    auto *widget = new QWidget;
    auto *outer = new QVBoxLayout(widget);
    outer->setContentsMargins(10, 10, 10, 10);

    auto *styleBox = new QGroupBox(tr("标注样式"), widget);
    auto *form = new QFormLayout(styleBox);
    m_strokeButton = new QPushButton(tr("选择颜色"), styleBox);
    m_strokeButton->setToolTip(tr("描边颜色"));
    connect(m_strokeButton, &QPushButton::clicked, this, &ReaderWorkspace::chooseStrokeColor);
    form->addRow(tr("描边"), m_strokeButton);

    m_fillCheck = new QCheckBox(tr("使用填充"), styleBox);
    connect(m_fillCheck, &QCheckBox::toggled, this, [this](bool) { updatePropertyPanel(); });
    form->addRow(QString(), m_fillCheck);
    m_fillButton = new QPushButton(tr("选择填充色"), styleBox);
    connect(m_fillButton, &QPushButton::clicked, this, &ReaderWorkspace::chooseFillColor);
    form->addRow(tr("填充"), m_fillButton);

    m_widthSpin = new QDoubleSpinBox(styleBox);
    m_widthSpin->setRange(0.25, 20.0);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setSuffix(tr(" pt"));
    m_widthSpin->setValue(2.0);
    form->addRow(tr("线宽"), m_widthSpin);

    m_opacitySlider = new QSlider(Qt::Horizontal, styleBox);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setValue(100);
    m_opacityValue = new QLabel(QStringLiteral("100%"), styleBox);
    auto *opacityRow = new QWidget(styleBox);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);
    form->addRow(tr("不透明度"), opacityRow);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1%").arg(value));
    });

    m_blendCombo = new QComboBox(styleBox);
    m_blendCombo->addItem(tr("正常"), static_cast<int>(BlendMode::Normal));
    m_blendCombo->addItem(tr("正片叠底"), static_cast<int>(BlendMode::Multiply));
    form->addRow(tr("混合模式"), m_blendCombo);

    m_applyStyleButton = new QPushButton(tr("应用样式"), styleBox);
    connect(m_applyStyleButton, &QPushButton::clicked, this, &ReaderWorkspace::applyStyle);
    form->addRow(QString(), m_applyStyleButton);

    m_selectionLabel = new QLabel(tr("当前未选中标注。点击画布或左侧列表选择。"), styleBox);
    m_selectionLabel->setWordWrap(true);
    form->addRow(m_selectionLabel);

    outer->addWidget(styleBox);

    auto *hint = new QLabel(tr("提示：属性面板在没有选中标注时修改的是后续新建标注的默认样式。"), widget);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("mutedHint"));
    outer->addWidget(hint);
    outer->addStretch(1);
    return widget;
}

void ReaderWorkspace::refreshAll()
{
    refreshAnnotationList();
    onSelectionChanged(m_view->selectedAnnotationId());
    updatePropertyPanel();
}

void ReaderWorkspace::onAnnotationsChanged()
{
    refreshAnnotationList();
    updateNotePanel();
    refreshSelectionLabel();
    if (m_session)
        m_session->noteActivity();
}

void ReaderWorkspace::onSelectionChanged(const QString &annotationId)
{
    refreshSelectionLabel();
    updateNotePanel();
    updatePropertyPanel();
    if (annotationId.isEmpty())
        return;
    if (!m_notePanel->isEditing()) {
        const Annotation *annotation = currentAnnotation();
        if (annotation && !annotation->note.isEmpty())
            m_notePanel->setHeading(QStringLiteral("第 %1 页 · %2")
                                        .arg(annotation->pageIndex + 1)
                                        .arg(annotationTypeDisplayName(annotation->type)));
    }
    QSignalBlocker blocker(m_annotationList);
    for (int row = 0; row < m_annotationList->count(); ++row) {
        QListWidgetItem *item = m_annotationList->item(row);
        if (item->data(Qt::UserRole).toString() == annotationId) {
            m_annotationList->setCurrentItem(item);
            break;
        }
    }
}

void ReaderWorkspace::onNoteRequested(const QString &annotationId)
{
    if (annotationId.isEmpty())
        return;
    m_view->selectAnnotation(annotationId);
    m_view->revealAnnotation(annotationId);
    beginNoteEdit();
}

void ReaderWorkspace::onNoteDeleteRequested(const QString &annotationId)
{
    const Annotation *annotation = m_view->annotations()->find(annotationId);
    if (!annotation)
        return;
    const QString preview = ellipsized(firstMarkdownLine(annotation->note.markdown), 160);
    const auto answer = QMessageBox::warning(
        this, tr("删除带笔记的标注"),
        tr("该标注附有笔记：\n\n%1\n\n删除后笔记会随标注一起移除；可通过撤销恢复。是否继续？")
            .arg(preview.isEmpty() ? tr("（无正文）") : preview),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes)
        m_view->deleteAnnotation(annotationId, true);
}

void ReaderWorkspace::onNoteListItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    m_view->selectAnnotation(id);
    m_view->revealAnnotation(id);
    if (item->isSelected())
        beginNoteEdit();
}

void ReaderWorkspace::refreshAnnotationList()
{
    if (!m_view)
        return;
    const QString selected = m_view->selectedAnnotationId();
    QSignalBlocker blocker(m_annotationList);
    m_annotationList->clear();
    for (const Annotation &annotation : m_view->annotations()->annotations()) {
        const QString line = firstMarkdownLine(annotation.note.markdown);
        const QString title = annotation.note.title.trimmed().isEmpty()
                                  ? (line.isEmpty() ? annotationTypeDisplayName(annotation.type)
                                                    : ellipsized(line, 30))
                                  : annotation.note.title;
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n第 %2 页 · %3")
                .arg(title)
                .arg(annotation.pageIndex + 1)
                .arg(annotationTypeDisplayName(annotation.type)));
        item->setData(Qt::UserRole, annotation.id);
        if (annotation.id == selected)
            item->setSelected(true);
        m_annotationList->addItem(item);
    }
    if (m_annotationList->count() == 0) {
        auto *empty = new QListWidgetItem(tr("尚无标注。选择工具栏中的工具，在页面上拖动或点击。"));
        empty->setFlags(Qt::NoItemFlags);
        m_annotationList->addItem(empty);
    }
}

void ReaderWorkspace::refreshSelectionLabel()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        if (m_selectionLabel)
            m_selectionLabel->setText(tr("当前未选中标注。修改属性将作为新标注的默认样式。"));
        return;
    }
    m_selectionLabel->setText(
        tr("已选中：%1 · 第 %2 页%3")
            .arg(annotationTypeDisplayName(annotation->type))
            .arg(annotation->pageIndex + 1)
            .arg(annotation->note.isEmpty() ? QString() : tr(" · 含笔记")));
}

void ReaderWorkspace::updatePropertyPanel()
{
    if (!m_view)
        return;
    const Annotation *annotation = currentAnnotation();
    const AnnotationStyle style = annotation ? annotation->style : m_view->defaultStyle();
    setControlsFromStyle(style);
    refreshSelectionLabel();
    m_applyStyleButton->setText(annotation ? tr("应用到选中标注") : tr("设为默认样式"));
}

void ReaderWorkspace::updateNotePanel()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        m_notePanel->clearNote(tr("选中一个标注或点击笔记工具，在页面上放置笔记定位点。"));
        return;
    }
    if (m_notePanel->isEditing())
        return;
    m_notePanel->setHeading(QStringLiteral("第 %1 页 · %2")
                                .arg(annotation->pageIndex + 1)
                                .arg(annotationTypeDisplayName(annotation->type)));
    m_notePanel->setNote(annotation->note.markdown);
}

bool ReaderWorkspace::isEditingNote() const
{
    return m_notePanel && m_notePanel->isEditing();
}

void ReaderWorkspace::beginNoteEdit()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        emit statusMessage(tr("请先选择一个标注，再编辑它的笔记。"));
        return;
    }
    m_notePanel->setBaseDirectory(m_session->bookDirectory());
    m_notePanel->setHeading(QStringLiteral("第 %1 页 · %2")
                                .arg(annotation->pageIndex + 1)
                                .arg(annotationTypeDisplayName(annotation->type)));
    if (annotation->note.markdown.isEmpty())
        m_notePanel->setNote(QStringLiteral("## %1\n\n").arg(noteTitleFor(*annotation)));
    m_notePanel->beginEdit();
}

void ReaderWorkspace::commitNoteEdit()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation || !m_notePanel->isEditing())
        return;
    AnnotationNote note;
    note.markdown = m_notePanel->markdown();
    note.title = annotation->note.title;
    if (note.title.trimmed().isEmpty())
        note.title = firstMarkdownLine(note.markdown).left(40);
    m_view->setAnnotationNote(annotation->id, note);
    m_notePanel->endEdit();
    m_session->noteActivity();
    emit statusMessage(tr("笔记已更新"));
    updateNotePanel();
}

void ReaderWorkspace::cancelNoteEdit()
{
    m_notePanel->endEdit();
    updateNotePanel();
}

void ReaderWorkspace::insertImageIntoNote()
{
    if (!m_notePanel->isEditing() && !isEditingNote()) {
        emit statusMessage(tr("请先开始编辑笔记。"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("插入图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    const QFileInfo info(path);
    if (info.size() > 20LL * 1024 * 1024) {
        QMessageBox::warning(this, tr("图片过大"), tr("单张图片不能超过 20 MiB。"));
        return;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(this, tr("无法读取图片"), reader.errorString());
        return;
    }
    QByteArray bytes;
    {
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QString extension = info.suffix().toLower();
        if (extension.isEmpty())
            extension = QStringLiteral("png");
        if (!image.save(&buffer, extension.toUpper().toLatin1())) {
            QMessageBox::warning(this, tr("无法转换图片"), tr("请换一张 PNG/JPEG 图片。"));
            return;
        }
    }
    QString relative;
    QString error;
    if (!m_session->writeAsset(QStringLiteral("image"), info.suffix(), bytes, &relative, &error)) {
        QMessageBox::warning(this, tr("插入图片失败"), error);
        return;
    }
    const QString alt = info.completeBaseName();
    m_notePanel->insertMarkdown(QStringLiteral("![%1](%2)\n").arg(alt, relative));
    emit statusMessage(tr("图片已复制到书籍 assets 目录。"));
}

void ReaderWorkspace::startRegionCapture()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        emit statusMessage(tr("请先选择一个带笔记的标注，然后再截图。"));
        return;
    }
    if (!m_notePanel->isEditing())
        beginNoteEdit();
    if (!m_captureOverlay) {
        m_captureOverlay = new RegionCaptureOverlay(m_view->viewport());
        connect(m_captureOverlay, &RegionCaptureOverlay::regionSelected,
                this, &ReaderWorkspace::finishCapture);
        connect(m_captureOverlay, &RegionCaptureOverlay::cancelled, this, [this]() {
            m_view->setSelectionOverlayVisible(true);
        });
    }
    m_view->setSelectionOverlayVisible(false);
    m_captureOverlay->setGeometry(m_view->viewport()->rect());
    m_captureOverlay->show();
    m_captureOverlay->raise();
    m_captureOverlay->setFocus();
}

void ReaderWorkspace::finishCapture(const QRect &rect)
{
    m_view->setSelectionOverlayVisible(true);
    if (rect.width() < 4 || rect.height() < 4)
        return;
    const QPixmap pixmap = m_view->viewport()->grab(rect);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.save(&buffer, "PNG")) {
        emit statusMessage(tr("截取区域保存失败。"));
        return;
    }
    QString relative;
    QString error;
    if (!m_session->writeAsset(QStringLiteral("capture"), QStringLiteral("png"), bytes, &relative, &error)) {
        emit statusMessage(error);
        return;
    }
    m_notePanel->insertMarkdown(QStringLiteral("![区域截图](%1)\n").arg(relative));
    emit statusMessage(tr("区域截图已插入笔记。"));
}

void ReaderWorkspace::chooseStrokeColor()
{
    const Annotation *annotation = currentAnnotation();
    const QColor initial = annotation ? annotation->style.strokeColor : m_view->defaultStyle().strokeColor;
    const QColor color = QColorDialog::getColor(initial, this, tr("选择描边颜色"),
                                                QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
        updateColorButton(m_strokeButton, color);
        applyStyle();
    }
}

void ReaderWorkspace::chooseFillColor()
{
    const Annotation *annotation = currentAnnotation();
    const QColor initial = annotation && annotation->style.hasFill() ? annotation->style.fillColor
                                                                     : m_view->defaultStyle().fillColor;
    const QColor color = QColorDialog::getColor(initial.isValid() ? initial : QColor(0x60, 0x8A, 0xFF),
                                                this, tr("选择填充颜色"),
                                                QColorDialog::ShowAlphaChannel);
    if (color.isValid()) {
        m_fillCheck->setChecked(true);
        updateColorButton(m_fillButton, color);
        applyStyle();
    }
}

void ReaderWorkspace::applyStyle()
{
    const AnnotationStyle style = styleFromControls();
    if (currentAnnotation())
        m_view->applyStyleToSelected(style);
    m_view->setDefaultStyle(style);
    m_session->noteActivity();
    emit statusMessage(tr("样式已应用"));
    updatePropertyPanel();
}

AnnotationStyle ReaderWorkspace::styleFromControls() const
{
    AnnotationStyle style;
    style.strokeColor = m_strokeButton->property("panoColor").value<QColor>();
    if (!style.strokeColor.isValid())
        style.strokeColor = QColor(0xD9, 0x4F, 0x4F);
    style.strokeWidthPt = m_widthSpin->value();
    style.opacity = m_opacitySlider->value() / 100.0;
    style.blend = static_cast<BlendMode>(m_blendCombo->currentData().toInt());
    if (m_fillCheck->isChecked()) {
        const QColor fill = m_fillButton->property("panoColor").value<QColor>();
        style.fillColor = fill.isValid() ? fill : style.strokeColor;
    }
    return style;
}

void ReaderWorkspace::setControlsFromStyle(const AnnotationStyle &style)
{
    QSignalBlocker widthBlocker(m_widthSpin);
    QSignalBlocker opacityBlocker(m_opacitySlider);
    QSignalBlocker blendBlocker(m_blendCombo);
    QSignalBlocker fillBlocker(m_fillCheck);
    m_widthSpin->setValue(style.strokeWidthPt);
    m_opacitySlider->setValue(qRound(style.opacity * 100.0));
    m_opacityValue->setText(QStringLiteral("%1%").arg(m_opacitySlider->value()));
    m_blendCombo->setCurrentIndex(m_blendCombo->findData(static_cast<int>(style.blend)));
    m_fillCheck->setChecked(style.hasFill());
    updateColorButton(m_strokeButton, style.strokeColor);
    updateColorButton(m_fillButton, style.hasFill() ? style.fillColor : QColor());
    m_fillButton->setEnabled(style.hasFill());
}

void ReaderWorkspace::updateColorButton(QPushButton *button, const QColor &color)
{
    button->setProperty("panoColor", color);
    if (!color.isValid()) {
        button->setText(tr("无"));
        button->setStyleSheet(QString());
        return;
    }
    const QColor opaque(color.red(), color.green(), color.blue());
    button->setText(color.name(QColor::HexArgb));
    const QString textColor = opaque.lightness() > 150 ? QStringLiteral("#20242b") : QStringLiteral("#ffffff");
    button->setStyleSheet(QStringLiteral("background:%1; color:%2;").arg(opaque.name(), textColor));
}

const Annotation *ReaderWorkspace::currentAnnotation() const
{
    if (!m_view)
        return nullptr;
    return m_view->annotations()->find(m_view->selectedAnnotationId());
}

QString ReaderWorkspace::noteTitleFor(const Annotation &annotation) const
{
    if (!annotation.note.title.trimmed().isEmpty())
        return annotation.note.title;
    return QStringLiteral("%1 · 第 %2 页")
        .arg(annotationTypeDisplayName(annotation.type))
        .arg(annotation.pageIndex + 1);
}

} // namespace pano
