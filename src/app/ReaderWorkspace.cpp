#include "app/ReaderWorkspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include "annotation/AnnotationRenderer.h"
#include "app/IconManager.h"
#include "app/NotePanel.h"
#include "app/RegionCaptureOverlay.h"
#include "document/DocumentSession.h"
#include "pdf/PageRenderer.h"
#include "reader/PdfView.h"
#include "settings/SettingsManager.h"
#include "settings/ToolPresets.h"

namespace pano {

namespace {

QString annotationExcerpt(const Annotation &annotation, int maxLength = 42)
{
    QString text = annotation.text.simplified();
    if (text.isEmpty() && !annotation.note.title.isEmpty())
        text = annotation.note.title;
    if (text.size() > maxLength)
        text = text.left(maxLength - 1) + QChar(0x2026);
    return text;
}

QString geometryText(const Annotation &annotation)
{
    const QRectF box = annotation.bounds();
    switch (annotation.type) {
    case AnnotationType::Line:
        return QStringLiteral("(%1, %2) → (%3, %4)")
            .arg(annotation.p1.x(), 0, 'f', 1).arg(annotation.p1.y(), 0, 'f', 1)
            .arg(annotation.p2.x(), 0, 'f', 1).arg(annotation.p2.y(), 0, 'f', 1);
    case AnnotationType::Pencil:
    case AnnotationType::Highlighter:
        return QStringLiteral("%1 个点 · 包围盒 %2 × %3 pt")
            .arg(annotation.path.size()).arg(box.width(), 0, 'f', 1).arg(box.height(), 0, 'f', 1);
    case AnnotationType::Highlight:
    case AnnotationType::Underline:
    case AnnotationType::StrikeOut:
        return QStringLiteral("%1 个文本片段 · 包围盒 %2 × %3 pt")
            .arg(annotation.polygons.size()).arg(box.width(), 0, 'f', 1).arg(box.height(), 0, 'f', 1);
    default:
        return QStringLiteral("x %1 · y %2 · %3 × %4 pt")
            .arg(box.x(), 0, 'f', 1).arg(box.y(), 0, 'f', 1)
            .arg(box.width(), 0, 'f', 1).arg(box.height(), 0, 'f', 1);
    }
}

QColor safeColor(const QColor &color, const QColor &fallback)
{
    return color.isValid() ? color : fallback;
}

} // namespace

ReaderWorkspace::ReaderWorkspace(DocumentSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    m_view = new PdfView(this);
    m_notePanel = new NotePanel(this);
    m_thumbRenderer = new PageRenderer(this);
    m_captureOverlay = new RegionCaptureOverlay(m_view->viewport());
    m_captureOverlay->hide();

    m_styleTimer = new QTimer(this);
    m_styleTimer->setSingleShot(true);
    m_styleTimer->setInterval(140);
    connect(m_styleTimer, &QTimer::timeout, this, &ReaderWorkspace::applyStyleFromControls);

    m_presetTimer = new QTimer(this);
    m_presetTimer->setSingleShot(true);
    m_presetTimer->setInterval(350);
    connect(m_presetTimer, &QTimer::timeout, this, [this] {
        if (ToolPresets *presets = m_session ? m_session->toolPresets() : nullptr) {
            QString error;
            if (!presets->save(&error) && !error.isEmpty())
                showStatus(error);
        }
    });

    m_noteTimer = new QTimer(this);
    m_noteTimer->setSingleShot(true);
    m_noteTimer->setInterval(2000);
    connect(m_noteTimer, &QTimer::timeout, this, [this] {
        commitPendingNote();
        m_noteDirty = false;
        m_noteMaxTimer->stop();
        m_noteMaxStarted = false;
    });

    m_noteMaxTimer = new QTimer(this);
    m_noteMaxTimer->setSingleShot(true);
    m_noteMaxTimer->setInterval(30000);
    connect(m_noteMaxTimer, &QTimer::timeout, this, [this] {
        commitPendingNote();
        m_noteDirty = false;
        m_noteMaxStarted = false;
    });

    m_panelSaveTimer = new QTimer(this);
    m_panelSaveTimer->setSingleShot(true);
    m_panelSaveTimer->setInterval(450);
    connect(m_panelSaveTimer, &QTimer::timeout, this, &ReaderWorkspace::persistPanelState);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    m_statusTimer->setInterval(4000);
    connect(m_statusTimer, &QTimer::timeout, this, [this] {
        if (m_statusLabel)
            m_statusLabel->clear();
    });

    buildUi();

    connect(m_view, &PdfView::pageChanged, this, &ReaderWorkspace::onPageChanged);
    connect(m_view, &PdfView::zoomChanged, this, &ReaderWorkspace::onZoomChanged);
    connect(m_view, &PdfView::rotationChanged, this, &ReaderWorkspace::onRotationChanged);
    connect(m_view, &PdfView::toolChanged, this, &ReaderWorkspace::onToolChanged);
    connect(m_view, &PdfView::annotationSelectionChanged, this,
            &ReaderWorkspace::onSelectionChanged);
    connect(m_view, &PdfView::annotationsChanged, this, &ReaderWorkspace::onAnnotationsChanged);
    connect(m_view, &PdfView::documentChanged, this, [this] {
        refreshAll();
    });
    connect(m_view, &PdfView::noteRequested, this, &ReaderWorkspace::onNoteRequested);
    connect(m_view, &PdfView::noteDeleteRequested, this,
            &ReaderWorkspace::onNoteDeleteRequested);
    connect(m_view, &PdfView::statusMessage, this, &ReaderWorkspace::showStatus);

    connect(m_notePanel, &NotePanel::editRequested, this, &ReaderWorkspace::beginNoteEdit);
    connect(m_notePanel, &NotePanel::commitRequested, this, &ReaderWorkspace::commitNoteEdit);
    connect(m_notePanel, &NotePanel::cancelRequested, this, &ReaderWorkspace::cancelNoteEdit);
    connect(m_notePanel, &NotePanel::imageRequested, this,
            &ReaderWorkspace::insertImageIntoNote);
    connect(m_notePanel, &NotePanel::captureRequested, this,
            &ReaderWorkspace::startRegionCapture);
    connect(m_notePanel, &NotePanel::contentChanged, this, &ReaderWorkspace::markNoteDirty);

    connect(m_captureOverlay, &RegionCaptureOverlay::regionSelected, this,
            &ReaderWorkspace::finishCapture);
    connect(m_captureOverlay, &RegionCaptureOverlay::cancelled, this, [this] {
        m_view->setSelectionOverlayVisible(true);
    });

    connect(m_thumbRenderer, &PageRenderer::pageReady, this,
            &ReaderWorkspace::onThumbnailRendered);
    connect(m_thumbRenderer, &PageRenderer::cacheChanged, this, [this] {
        if (m_thumbRefreshTimer)
            m_thumbRefreshTimer->start();
    });

    m_thumbRefreshTimer = new QTimer(this);
    m_thumbRefreshTimer->setSingleShot(true);
    m_thumbRefreshTimer->setInterval(250);
    connect(m_thumbRefreshTimer, &QTimer::timeout, this, [this] { requestNearbyThumbnails(); });

    if (m_session) {
        connect(m_session, &DocumentSession::dirtyChanged, this, [this](bool dirty) {
            if (m_saveStateLabel)
                m_saveStateLabel->setText(dirty ? tr("未保存") : tr("已保存"));
        });
        connect(m_session, &DocumentSession::saveFailed, this,
                [this](const QString &message) {
            if (m_saveStateLabel)
                m_saveStateLabel->setText(tr("保存失败"));
            showStatus(message);
        });
        connect(m_session, &DocumentSession::statusMessage, this, &ReaderWorkspace::showStatus);
    }

    applyToolPreset(PdfView::Tool::Select);
    updatePanelVisibility();
    refreshAll();
    if (m_saveStateLabel)
        m_saveStateLabel->setText(m_session && m_session->isDirty() ? tr("未保存")
                                                                   : tr("已保存"));
}

ReaderWorkspace::~ReaderWorkspace()
{
    stopNoteTimers();
    blockSignals(true);
    if (m_view) {
        m_view->blockSignals(true);
        disconnect(m_view, nullptr, this, nullptr);
    }
    if (m_notePanel)
        disconnect(m_notePanel, nullptr, this, nullptr);
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
}

void ReaderWorkspace::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_primaryToolBar = buildPrimaryToolBar();
    m_contextToolBar = buildContextToolBar();
    root->addWidget(m_primaryToolBar);
    root->addWidget(m_contextToolBar);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_leftDock = buildLeftDock();
    m_rightDock = buildRightDock();
    m_splitter->addWidget(m_leftDock);
    m_splitter->addWidget(m_view);
    m_splitter->addWidget(m_rightDock);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setChildrenCollapsible(true);
    if (m_session && m_session->settings()) {
        const AppSettings &settings = m_session->settings()->settings();
        m_splitter->setSizes({settings.readerLeftWidth, 800, settings.readerRightWidth});
        if (m_leftDock)
            m_leftDock->setVisible(settings.readerLeftVisible);
        if (m_rightDock)
            m_rightDock->setVisible(settings.readerRightVisible);
    } else {
        m_splitter->setSizes({280, 800, 340});
    }
    root->addWidget(m_splitter, 1);

    root->addWidget(buildBottomBar());

    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        m_panelSaveTimer->start();
    });
}

QToolBar *ReaderWorkspace::buildPrimaryToolBar()
{
    auto *bar = new QToolBar(this);
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_saveAction = bar->addAction(IconManager::instance()->icon(QStringLiteral("save")),
                                  tr("保存"));
    m_saveAction->setToolTip(tr("保存 (Ctrl+S)"));
    connect(m_saveAction, &QAction::triggered, this, &ReaderWorkspace::saveCurrent);

    m_undoAction = m_view->undoStack()->createUndoAction(this, tr("撤销"));
    m_undoAction->setIcon(IconManager::instance()->icon(QStringLiteral("undo-2")));
    m_undoAction->setToolTip(tr("撤销 (Ctrl+Z)"));
    m_redoAction = m_view->undoStack()->createRedoAction(this, tr("重做"));
    m_redoAction->setIcon(IconManager::instance()->icon(QStringLiteral("redo-2")));
    m_redoAction->setToolTip(tr("重做 (Ctrl+Shift+Z)"));
    bar->addAction(m_undoAction);
    bar->addAction(m_redoAction);
    m_deleteAction = bar->addAction(IconManager::instance()->icon(QStringLiteral("trash-2")),
                                    tr("删除"));
    m_deleteAction->setToolTip(tr("删除选中对象 (Delete)"));
    connect(m_deleteAction, &QAction::triggered, this, &ReaderWorkspace::deleteSelected);
    bar->addSeparator();

    auto *group = new QActionGroup(this);
    group->setExclusive(true);

    auto addTool = [&](PdfView::Tool tool, const QString &iconName, const QString &text) {
        QAction *action = bar->addAction(IconManager::instance()->icon(iconName), text);
        action->setCheckable(true);
        action->setToolTip(text);
        group->addAction(action);
        m_toolActions.insert(action, tool);
        connect(action, &QAction::triggered, this, [this, tool] {
            if (m_syncingTool)
                return;
            m_view->setTool(tool);
        });
        return action;
    };

    addTool(PdfView::Tool::Select, QStringLiteral("mouse-pointer-2"), tr("选择/移动"));
    addTool(PdfView::Tool::Hand, QStringLiteral("hand"), tr("抓手"));
    addTool(PdfView::Tool::TextSelect, QStringLiteral("text-cursor"), tr("文本选择"));
    bar->addSeparator();
    addTool(PdfView::Tool::Highlight, QStringLiteral("highlighter"), tr("文本高亮"));
    addTool(PdfView::Tool::Underline, QStringLiteral("underline"), tr("下划线"));
    addTool(PdfView::Tool::StrikeOut, QStringLiteral("strikethrough"), tr("删除线"));
    bar->addSeparator();
    addTool(PdfView::Tool::Line, QStringLiteral("minus"), tr("直线"));
    addTool(PdfView::Tool::Rectangle, QStringLiteral("square"), tr("矩形"));
    addTool(PdfView::Tool::Ellipse, QStringLiteral("circle"), tr("椭圆"));
    bar->addSeparator();
    addTool(PdfView::Tool::Pencil, QStringLiteral("pencil"), tr("铅笔"));
    addTool(PdfView::Tool::Highlighter, QStringLiteral("highlighter"), tr("荧光笔"));
    addTool(PdfView::Tool::Eraser, QStringLiteral("eraser"), tr("对象橡皮"));
    bar->addSeparator();
    addTool(PdfView::Tool::Note, QStringLiteral("sticky-note"), tr("附着笔记"));

    m_captureAction = bar->addAction(IconManager::instance()->icon(QStringLiteral("scan")),
                                     tr("区域摘录"));
    m_captureAction->setToolTip(tr("把当前页区域截图插入笔记"));
    connect(m_captureAction, &QAction::triggered, this, &ReaderWorkspace::startRegionCapture);

    updateToolActions();
    return bar;
}

QToolBar *ReaderWorkspace::buildContextToolBar()
{
    auto *bar = new QToolBar(this);
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(16, 16));

    m_contextWidget = new QWidget(bar);
    auto *layout = new QHBoxLayout(m_contextWidget);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(8);

    m_contextHint = new QLabel(tr("选择工具或对象后调整样式"), m_contextWidget);
    m_contextHint->setObjectName(QStringLiteral("readerContextHint"));
    layout->addWidget(m_contextHint);

    m_colorWidget = new QWidget(m_contextWidget);
    auto *colorLayout = new QHBoxLayout(m_colorWidget);
    colorLayout->setContentsMargins(0, 0, 0, 0);
    colorLayout->setSpacing(4);
    colorLayout->addWidget(new QLabel(tr("颜色"), m_colorWidget));
    m_colorButton = new QPushButton(m_colorWidget);
    m_colorButton->setFixedSize(34, 22);
    m_colorButton->setFlat(true);
    colorLayout->addWidget(m_colorButton);
    connect(m_colorButton, &QPushButton::clicked, this, &ReaderWorkspace::chooseStrokeColor);
    layout->addWidget(m_colorWidget);

    m_fillWidget = new QWidget(m_contextWidget);
    auto *fillLayout = new QHBoxLayout(m_fillWidget);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(4);
    m_fillCheck = new QCheckBox(tr("填充"), m_fillWidget);
    m_fillButton = new QPushButton(m_fillWidget);
    m_fillButton->setFixedSize(34, 22);
    m_fillButton->setFlat(true);
    fillLayout->addWidget(m_fillCheck);
    fillLayout->addWidget(m_fillButton);
    connect(m_fillButton, &QPushButton::clicked, this, &ReaderWorkspace::chooseFillColor);
    connect(m_fillCheck, &QCheckBox::toggled, this, [this] { scheduleStyleApply(); });
    layout->addWidget(m_fillWidget);

    m_widthWidget = new QWidget(m_contextWidget);
    auto *widthLayout = new QHBoxLayout(m_widthWidget);
    widthLayout->setContentsMargins(0, 0, 0, 0);
    widthLayout->setSpacing(4);
    widthLayout->addWidget(new QLabel(tr("线宽"), m_widthWidget));
    m_widthSpin = new QDoubleSpinBox(m_widthWidget);
    m_widthSpin->setRange(0.25, 20.0);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setSuffix(tr(" pt"));
    m_widthSpin->setKeyboardTracking(false);
    widthLayout->addWidget(m_widthSpin);
    connect(m_widthSpin, &QDoubleSpinBox::editingFinished, this,
            &ReaderWorkspace::scheduleStyleApply);
    layout->addWidget(m_widthWidget);

    m_opacityWidget = new QWidget(m_contextWidget);
    auto *opacityLayout = new QHBoxLayout(m_opacityWidget);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(4);
    opacityLayout->addWidget(new QLabel(tr("不透明度"), m_opacityWidget));
    m_opacitySlider = new QSlider(Qt::Horizontal, m_opacityWidget);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setFixedWidth(95);
    m_opacityValue = new QLabel(QStringLiteral("100%"), m_opacityWidget);
    m_opacityValue->setMinimumWidth(36);
    opacityLayout->addWidget(m_opacitySlider);
    opacityLayout->addWidget(m_opacityValue);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1%").arg(value));
        scheduleStyleApply();
    });
    connect(m_opacitySlider, &QSlider::sliderReleased, this,
            &ReaderWorkspace::applyStyleFromControls);
    layout->addWidget(m_opacityWidget);

    m_blendWidget = new QWidget(m_contextWidget);
    auto *blendLayout = new QHBoxLayout(m_blendWidget);
    blendLayout->setContentsMargins(0, 0, 0, 0);
    blendLayout->setSpacing(4);
    blendLayout->addWidget(new QLabel(tr("混合"), m_blendWidget));
    m_blendCombo = new QComboBox(m_blendWidget);
    m_blendCombo->addItem(tr("正常"), QStringLiteral("normal"));
    m_blendCombo->addItem(tr("正片叠底"), QStringLiteral("multiply"));
    blendLayout->addWidget(m_blendCombo);
    connect(m_blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { scheduleStyleApply(); });
    layout->addWidget(m_blendWidget);
    layout->addStretch(1);

    bar->addWidget(m_contextWidget);
    return bar;
}

QWidget *ReaderWorkspace::buildBottomBar()
{
    auto *bar = new QWidget(this);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("页"), bar));
    m_pageSpin = new QSpinBox(bar);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setMaximum(1);
    m_pageSpin->setFixedWidth(72);
    m_pageSpin->setKeyboardTracking(false);
    layout->addWidget(m_pageSpin);
    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &ReaderWorkspace::goToPageFromSpin);

    layout->addSpacing(6);
    m_zoomCombo = new QComboBox(bar);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setFixedWidth(92);
    for (int percent : {50, 75, 100, 125, 150, 200, 300})
        m_zoomCombo->addItem(QStringLiteral("%1%").arg(percent), percent);
    layout->addWidget(m_zoomCombo);
    connect(m_zoomCombo, &QComboBox::activated, this, &ReaderWorkspace::zoomEdited);
    if (m_zoomCombo->lineEdit())
        connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this,
                &ReaderWorkspace::zoomEdited);

    auto addButton = [&](const QString &icon, const QString &tip, auto slot) {
        auto *button = new QToolButton(bar);
        button->setIcon(IconManager::instance()->icon(icon));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        connect(button, &QToolButton::clicked, this, slot);
        layout->addWidget(button);
        return button;
    };
    addButton(QStringLiteral("zoom-out"), tr("缩小"), [this] { m_view->zoomOut(); });
    addButton(QStringLiteral("zoom-in"), tr("放大"), [this] { m_view->zoomIn(); });
    addButton(QStringLiteral("minus"), tr("适合宽度"), [this] { m_view->fitWidth(); });
    addButton(QStringLiteral("layout-grid"), tr("适合页面"), [this] { m_view->fitPage(); });
    addButton(QStringLiteral("rotate-cw"), tr("顺时针旋转 90°"), [this] { m_view->rotateBy(90); });

    layout->addStretch(1);
    m_saveStateLabel = new QLabel(bar);
    m_saveStateLabel->setObjectName(QStringLiteral("readerSaveState"));
    layout->addWidget(m_saveStateLabel);
    m_leftToggle = new QToolButton(bar);
    m_leftToggle->setIcon(IconManager::instance()->icon(QStringLiteral("panel-left-close")));
    m_leftToggle->setCheckable(true);
    m_leftToggle->setChecked(true);
    m_leftToggle->setAutoRaise(true);
    m_leftToggle->setToolTip(tr("显示/隐藏左侧栏"));
    connect(m_leftToggle, &QToolButton::toggled, this, &ReaderWorkspace::updatePanelVisibility);
    layout->addWidget(m_leftToggle);
    m_rightToggle = new QToolButton(bar);
    m_rightToggle->setIcon(IconManager::instance()->icon(QStringLiteral("panel-right-close")));
    m_rightToggle->setCheckable(true);
    m_rightToggle->setChecked(true);
    m_rightToggle->setAutoRaise(true);
    m_rightToggle->setToolTip(tr("显示/隐藏右侧栏"));
    connect(m_rightToggle, &QToolButton::toggled, this, &ReaderWorkspace::updatePanelVisibility);
    layout->addWidget(m_rightToggle);

    return bar;
}

QWidget *ReaderWorkspace::buildLeftDock()
{
    m_leftTabs = new QTabWidget(this);
    m_leftTabs->setDocumentMode(true);

    auto *outlinePage = new QWidget(m_leftTabs);
    auto *outlineLayout = new QVBoxLayout(outlinePage);
    outlineLayout->setContentsMargins(2, 2, 2, 2);
    m_outlineView = new QTreeView(outlinePage);
    m_outlineView->setHeaderHidden(true);
    m_outlineView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_outlineModel = new QPdfBookmarkModel(this);
    m_outlineView->setModel(m_outlineModel);
    m_outlineEmpty = new QLabel(tr("此 PDF 没有目录"), outlinePage);
    m_outlineEmpty->setAlignment(Qt::AlignCenter);
    m_outlineEmpty->setWordWrap(true);
    outlineLayout->addWidget(m_outlineView, 1);
    outlineLayout->addWidget(m_outlineEmpty);
    connect(m_outlineView, &QTreeView::activated, this,
            &ReaderWorkspace::onOutlineActivated);
    connect(m_outlineView, &QTreeView::clicked, this, &ReaderWorkspace::onOutlineActivated);
    m_leftTabs->addTab(outlinePage,
                       IconManager::instance()->icon(QStringLiteral("list-tree")), tr("目录"));

    m_thumbnailList = new QListWidget(m_leftTabs);
    m_thumbnailList->setViewMode(QListView::IconMode);
    m_thumbnailList->setIconSize(QSize(120, 158));
    m_thumbnailList->setGridSize(QSize(138, 188));
    m_thumbnailList->setResizeMode(QListView::Adjust);
    m_thumbnailList->setMovement(QListView::Static);
    m_thumbnailList->setSpacing(6);
    m_thumbnailList->setUniformItemSizes(true);
    connect(m_thumbnailList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::onThumbnailActivated);
    m_leftTabs->addTab(m_thumbnailList,
                       IconManager::instance()->icon(QStringLiteral("images")), tr("页面"));

    auto *annotationsPage = new QWidget(m_leftTabs);
    auto *annotationLayout = new QVBoxLayout(annotationsPage);
    annotationLayout->setContentsMargins(4, 4, 4, 4);
    annotationLayout->setSpacing(4);
    auto *filterRow = new QHBoxLayout;
    m_annotationFilter = new QComboBox(annotationsPage);
    m_annotationFilter->addItem(tr("全部类型"), QString());
    m_annotationFilter->addItem(tr("文本高亮"), QStringLiteral("text_highlight"));
    m_annotationFilter->addItem(tr("下划线"), QStringLiteral("underline"));
    m_annotationFilter->addItem(tr("删除线"), QStringLiteral("strikeout"));
    m_annotationFilter->addItem(tr("直线"), QStringLiteral("line"));
    m_annotationFilter->addItem(tr("矩形"), QStringLiteral("rectangle"));
    m_annotationFilter->addItem(tr("椭圆"), QStringLiteral("ellipse"));
    m_annotationFilter->addItem(tr("铅笔"), QStringLiteral("pencil"));
    m_annotationFilter->addItem(tr("荧光笔"), QStringLiteral("highlighter"));
    m_annotationFilter->addItem(tr("附着笔记"), QStringLiteral("note_point"));
    filterRow->addWidget(m_annotationFilter, 1);
    m_noteOnlyCheck = new QCheckBox(tr("仅有笔记"), annotationsPage);
    filterRow->addWidget(m_noteOnlyCheck);
    annotationLayout->addLayout(filterRow);
    m_annotationList = new QListWidget(annotationsPage);
    m_annotationList->setWordWrap(true);
    annotationLayout->addWidget(m_annotationList, 1);
    connect(m_annotationList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::onNoteListItemActivated);
    connect(m_annotationList, &QListWidget::itemActivated, this,
            &ReaderWorkspace::onNoteListItemActivated);
    connect(m_annotationFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ReaderWorkspace::refreshAnnotationList);
    connect(m_noteOnlyCheck, &QCheckBox::toggled, this,
            &ReaderWorkspace::refreshAnnotationList);
    m_leftTabs->addTab(annotationsPage,
                       IconManager::instance()->icon(QStringLiteral("sticky-note")), tr("标注"));

    m_leftTabs->setMinimumWidth(180);
    return m_leftTabs;
}

QWidget *ReaderWorkspace::buildRightDock()
{
    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setDocumentMode(true);
    m_rightTabs->addTab(m_notePanel,
                        IconManager::instance()->icon(QStringLiteral("sticky-note")), tr("笔记"));

    auto *detailScroll = new QScrollArea(m_rightTabs);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    detailScroll->setWidget(buildDetailPane());
    m_rightTabs->addTab(detailScroll,
                        IconManager::instance()->icon(QStringLiteral("info")), tr("详情"));
    m_rightTabs->setMinimumWidth(260);
    return m_rightTabs;
}

QWidget *ReaderWorkspace::buildDetailPane()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    m_detailTitle = new QLabel(tr("未选择对象"), page);
    m_detailTitle->setWordWrap(true);
    QFont titleFont = m_detailTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.0);
    m_detailTitle->setFont(titleFont);
    layout->addWidget(m_detailTitle);

    m_detailMeta = new QLabel(page);
    m_detailMeta->setWordWrap(true);
    layout->addWidget(m_detailMeta);

    m_detailFields = new QWidget(page);
    auto *form = new QFormLayout(m_detailFields);
    form->setContentsMargins(0, 0, 0, 0);
    m_detailGeometry = new QLabel(m_detailFields);
    m_detailGeometry->setWordWrap(true);
    form->addRow(tr("几何"), m_detailGeometry);
    layout->addWidget(m_detailFields);

    m_detailPlaceholder = new QLabel(tr("在页面或左侧标注列表中选择一个对象，"
                                       "这里显示类型、页码、时间与几何信息；"
                                       "样式请使用上方属性栏修改。"),
                                     page);
    m_detailPlaceholder->setWordWrap(true);
    m_detailPlaceholder->setEnabled(false);
    layout->addWidget(m_detailPlaceholder);
    layout->addStretch(1);
    return page;
}

void ReaderWorkspace::reloadToolPreset()
{
    m_view->refreshToolPreset();
    updateStylePanel();
}

void ReaderWorkspace::refreshAll()
{
    if (m_session) {
        m_notePanel->setBaseDirectory(m_session->bookDirectory());
        m_thumbRenderer->setDocument(m_view->document());
    }
    refreshOutline();
    refreshThumbnails();
    refreshAnnotationList();
    updateBottomStatus();
    updateDetailPanel();
    updateNotePanel();
    updateContextToolBar();
    updatePanelVisibility();
}

void ReaderWorkspace::refreshOutline()
{
    if (!m_outlineModel || !m_outlineView)
        return;
    QPdfDocument *document = m_view->hasDocument() ? m_view->document()->pdf() : nullptr;
    m_outlineModel->setDocument(document);
    m_outlineView->expandAll();
    const bool empty = m_outlineModel->rowCount() == 0;
    m_outlineView->setVisible(!empty);
    m_outlineEmpty->setVisible(empty);
}

void ReaderWorkspace::refreshThumbnails()
{
    if (!m_thumbnailList)
        return;
    const QSignalBlocker blocker(m_thumbnailList);
    m_thumbnailList->clear();
    m_thumbRequests.clear();
    if (!m_view->hasDocument()) {
        m_thumbRenderer->setDocument(nullptr);
        return;
    }
    m_thumbRenderer->setDocument(m_view->document());
    const int count = m_view->pageCount();
    const QPalette palette = this->palette();
    for (int page = 0; page < count; ++page) {
        auto *item = new QListWidgetItem(QStringLiteral("%1").arg(page + 1), m_thumbnailList);
        item->setData(Qt::UserRole, page);
        item->setTextAlignment(Qt::AlignHCenter);
        QPixmap placeholder(120, 158);
        placeholder.fill(palette.color(QPalette::Base));
        item->setIcon(QIcon(placeholder));
    }
    if (m_view->currentPage() >= 0 && m_view->currentPage() < m_thumbnailList->count())
        m_thumbnailList->setCurrentRow(m_view->currentPage());
    requestNearbyThumbnails();
}

void ReaderWorkspace::requestNearbyThumbnails()
{
    if (!m_view->hasDocument() || !m_thumbnailList)
        return;
    const int current = m_view->currentPage();
    const int first = qMax(0, current - 2);
    const int last = qMin(m_view->pageCount() - 1, current + 3);
    for (int page = first; page <= last; ++page) {
        if (m_thumbnailList->item(page) && !m_thumbnailList->item(page)->icon().isNull()
            && m_thumbRequests.contains(page))
            continue;
        const quint64 request = m_thumbRenderer->requestPage(page, QSize(160, 220));
        if (request != 0)
            m_thumbRequests.insert(page, request);
    }
}

void ReaderWorkspace::onThumbnailRendered(int page, const QSize &size, const QImage &image,
                                          quint64 requestId)
{
    Q_UNUSED(size);
    if (!m_thumbnailList || page < 0 || page >= m_thumbnailList->count())
        return;
    if (m_thumbRequests.contains(page) && m_thumbRequests.value(page) != requestId)
        return;
    m_thumbRequests.remove(page);
    QImage oriented = image;
    if (m_view->rotation() != 0) {
        QTransform transform;
        transform.rotate(m_view->rotation());
        oriented = image.transformed(transform, Qt::SmoothTransformation);
    }
    const QPixmap pixmap = QPixmap::fromImage(oriented).scaled(
        QSize(116, 154), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (QListWidgetItem *item = m_thumbnailList->item(page)) {
        item->setIcon(QIcon(pixmap));
        item->setSizeHint(QSize(132, 176));
    }
}

void ReaderWorkspace::onThumbnailActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    m_view->goToPage(item->data(Qt::UserRole).toInt());
}

void ReaderWorkspace::onOutlineActivated(const QModelIndex &index)
{
    if (!index.isValid() || !m_outlineModel)
        return;
    const int page = index.data(static_cast<int>(QPdfBookmarkModel::Role::Page)).toInt();
    const QPointF location =
        index.data(static_cast<int>(QPdfBookmarkModel::Role::Location)).toPointF();
    if (location.isNull())
        m_view->goToPage(page);
    else
        m_view->goToPosition(page, location);
    m_view->setFocus();
}

void ReaderWorkspace::refreshAnnotationList()
{
    if (!m_annotationList)
        return;
    const QString id = m_view->selectedAnnotationId();
    const QSignalBlocker blocker(m_annotationList);
    m_annotationList->clear();
    const QString filter = m_annotationFilter ? m_annotationFilter->currentData().toString()
                                              : QString();
    const bool noteOnly = m_noteOnlyCheck && m_noteOnlyCheck->isChecked();
    const auto &annotations = m_view->annotations()->annotations();
    for (const Annotation &annotation : annotations) {
        if (!filter.isEmpty() && annotationTypeName(annotation.type) != filter)
            continue;
        if (noteOnly && annotation.note.isEmpty())
            continue;
        QListWidgetItem *item = makeAnnotationItem(annotation);
        m_annotationList->addItem(item);
        if (annotation.id == id)
            m_annotationList->setCurrentItem(item);
    }
}

QListWidgetItem *ReaderWorkspace::makeAnnotationItem(const Annotation &annotation) const
{
    QString title = annotation.note.title;
    if (title.isEmpty())
        title = annotationTypeDisplayName(annotation.type);
    const QString excerpt = annotationExcerpt(annotation);
    QString text = QStringLiteral("P%1 · %2").arg(annotation.pageIndex + 1).arg(title);
    if (!excerpt.isEmpty())
        text += QStringLiteral("\n%1").arg(excerpt);
    if (!annotation.note.isEmpty())
        text += QStringLiteral("  ✎");

    auto *item = new QListWidgetItem(text);
    item->setData(Qt::UserRole, annotation.id);
    QPixmap swatch(14, 14);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = safeColor(annotation.style.strokeColor, QColor(0x47, 0x7E, 0xB8));
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(1, 1, 12, 12);
    item->setIcon(QIcon(swatch));
    item->setToolTip(annotation.text.isEmpty() ? annotation.note.markdown : annotation.text);
    return item;
}

void ReaderWorkspace::onNoteListItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;
    m_applyingSelection = true;
    m_view->selectAnnotation(id);
    m_applyingSelection = false;
    m_view->revealAnnotation(id);
    onSelectionChanged(id);
}

const Annotation *ReaderWorkspace::currentAnnotation() const
{
    if (!m_view)
        return nullptr;
    const QString id = m_view->selectedAnnotationId();
    return id.isEmpty() ? nullptr : m_view->annotations()->find(id);
}

void ReaderWorkspace::onAnnotationsChanged()
{
    refreshAnnotationList();
    updateDetailPanel();
    if (!isEditingNote())
        updateNotePanel();
    updateContextToolBar();
    updateBottomStatus();
}

void ReaderWorkspace::onSelectionChanged(const QString &annotationId)
{
    if (m_applyingSelection)
        return;
    if (isEditingNote() && m_editingId != annotationId)
        commitNoteEdit();

    refreshAnnotationList();
    updateDetailPanel();
    updateNotePanel();
    updateContextToolBar();
    if (!annotationId.isEmpty())
        m_view->revealAnnotation(annotationId);
}

void ReaderWorkspace::onNoteRequested(const QString &annotationId)
{
    if (annotationId.isEmpty())
        return;
    if (m_view->selectedAnnotationId() != annotationId) {
        m_applyingSelection = true;
        m_view->selectAnnotation(annotationId);
        m_applyingSelection = false;
    }
    onSelectionChanged(annotationId);
    beginNoteEdit();
}

void ReaderWorkspace::onNoteDeleteRequested(const QString &annotationId)
{
    const Annotation *annotation = m_view->annotations()->find(annotationId);
    if (!annotation)
        return;
    const QString message = tr("“%1”带有笔记。删除批注会同时删除笔记。\n\n"
                               "确定删除这个对象吗？")
                                .arg(annotation->note.title.isEmpty()
                                         ? annotationTypeDisplayName(annotation->type)
                                         : annotation->note.title);
    if (QMessageBox::question(this, tr("删除附带笔记的批注"), message,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    if (m_editingId == annotationId) {
        stopNoteTimers();
        m_notePanel->endEdit();
        m_editingId.clear();
    }
    m_view->deleteAnnotation(annotationId, true);
}

void ReaderWorkspace::updateDetailPanel()
{
    const Annotation *annotation = currentAnnotation();
    const bool has = annotation != nullptr;
    if (m_detailPlaceholder)
        m_detailPlaceholder->setVisible(!has);
    if (m_detailFields)
        m_detailFields->setVisible(has);
    if (!m_detailTitle)
        return;
    if (!has) {
        m_detailTitle->setText(tr("未选择对象"));
        m_detailMeta->clear();
        return;
    }
    m_detailTitle->setText(annotation->note.title.isEmpty()
                               ? annotationTypeDisplayName(annotation->type)
                               : annotation->note.title);
    m_detailMeta->setText(tr("%1 · 第 %2 页\n创建：%3\n修改：%4")
                              .arg(annotationTypeDisplayName(annotation->type))
                              .arg(annotation->pageIndex + 1)
                              .arg(annotation->createdAt)
                              .arg(annotation->modifiedAt));
    m_detailGeometry->setText(geometryText(*annotation));
}

void ReaderWorkspace::updateNotePanel()
{
    const QString id = m_view->selectedAnnotationId();
    if (!id.isEmpty() && m_editingId == id) {
        m_notePanel->setEditable(true);
        return;
    }
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        m_notePanel->clearNote(tr("选择一个批注后可以查看和编辑它的笔记。"));
        m_notePanel->setEditable(false);
        return;
    }
    m_notePanel->setBaseDirectory(m_session ? m_session->bookDirectory() : QString());
    m_notePanel->setEditable(true);
    m_notePanel->setHeading(annotation->note.title.isEmpty()
                                ? annotationTypeDisplayName(annotation->type)
                                : annotation->note.title);
    m_notePanel->setNote(annotation->note.markdown);
}

bool ReaderWorkspace::isEditingNote() const
{
    return !m_editingId.isEmpty();
}

void ReaderWorkspace::beginNoteEdit()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation)
        return;
    if (isEditingNote() && m_editingId != annotation->id)
        commitNoteEdit();
    m_editingId = annotation->id;
    m_notePanel->setEditable(true);
    if (!m_notePanel->isEditing())
        m_notePanel->beginEdit();
    if (m_rightTabs)
        m_rightTabs->setCurrentIndex(0);
    showStatus(tr("正在编辑笔记：%1").arg(annotation->note.title.isEmpty()
                                            ? annotationTypeDisplayName(annotation->type)
                                            : annotation->note.title));
}

void ReaderWorkspace::commitNoteEdit()
{
    commitPendingNote();
    stopNoteTimers();
    if (m_notePanel && m_notePanel->isEditing())
        m_notePanel->endEdit();
    m_editingId.clear();
    m_noteDirty = false;
    updateNotePanel();
    updateDetailPanel();
}

void ReaderWorkspace::commitPendingNote()
{
    if (m_editingId.isEmpty() || !m_notePanel)
        return;
    const Annotation *annotation = m_view->annotations()->find(m_editingId);
    if (!annotation) {
        m_editingId.clear();
        return;
    }
    const QString markdown = m_notePanel->markdown();
    QString title = annotation->note.title;
    if (title.isEmpty()) {
        title = noteTitleFor(*annotation);
    }
    AnnotationNote note;
    note.title = title;
    note.markdown = markdown;
    if (annotation->note.markdown == note.markdown && annotation->note.title == note.title)
        return;

    m_applyingSelection = true;
    m_view->setAnnotationNote(m_editingId, note);
    m_applyingSelection = false;
    m_noteDirty = false;
}

void ReaderWorkspace::cancelNoteEdit()
{
    stopNoteTimers();
    m_noteDirty = false;
    if (m_notePanel && m_notePanel->isEditing())
        m_notePanel->endEdit();
    m_editingId.clear();
    updateNotePanel();
}

QString ReaderWorkspace::noteTitleFor(const Annotation &annotation) const
{
    QString text = m_notePanel ? m_notePanel->markdown().trimmed() : QString();
    if (text.isEmpty())
        return annotationTypeDisplayName(annotation.type);
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0).trimmed();
    QString title = firstLine;
    while (title.startsWith(QLatin1Char('#')) || title.startsWith(QLatin1Char('>'))
           || title.startsWith(QLatin1Char('-')) || title.startsWith(QLatin1Char('*')))
        title.remove(0, 1);
    title = title.trimmed();
    if (title.size() > 40)
        title = title.left(39) + QChar(0x2026);
    return title.isEmpty() ? annotationTypeDisplayName(annotation.type) : title;
}

void ReaderWorkspace::markNoteDirty()
{
    if (m_editingId.isEmpty())
        return;
    m_noteDirty = true;
    if (m_session)
        m_session->noteActivity();
    if (m_noteTimer)
        m_noteTimer->start();
    if (m_noteMaxTimer && !m_noteMaxStarted) {
        m_noteMaxStarted = true;
        m_noteMaxTimer->start();
    }
}

void ReaderWorkspace::stopNoteTimers()
{
    if (m_noteTimer)
        m_noteTimer->stop();
    if (m_noteMaxTimer)
        m_noteMaxTimer->stop();
    m_noteMaxStarted = false;
}

void ReaderWorkspace::onToolChanged(PdfView::Tool tool)
{
    updateToolActions();
    if (tool == PdfView::Tool::Select) {
        updateContextToolBar();
        return;
    }
    applyToolPreset(tool);
    updateContextToolBar();
    showStatus(PdfView::toolDisplayName(tool));
}

void ReaderWorkspace::updateToolActions()
{
    if (!m_view)
        return;
    m_syncingTool = true;
    const PdfView::Tool current = m_view->tool();
    for (auto it = m_toolActions.constBegin(); it != m_toolActions.constEnd(); ++it)
        it.key()->setChecked(it.value() == current);
    m_syncingTool = false;
}

QString ReaderWorkspace::presetIdFor(PdfView::Tool tool) const
{
    switch (tool) {
    case PdfView::Tool::Select:
        return QStringLiteral("select");
    case PdfView::Tool::TextSelect:
        return QStringLiteral("text");
    case PdfView::Tool::Highlight:
        return QStringLiteral("text_highlight");
    case PdfView::Tool::Underline:
        return QStringLiteral("underline");
    case PdfView::Tool::StrikeOut:
        return QStringLiteral("strikeout");
    case PdfView::Tool::Line:
        return QStringLiteral("line");
    case PdfView::Tool::Rectangle:
        return QStringLiteral("rectangle");
    case PdfView::Tool::Ellipse:
        return QStringLiteral("ellipse");
    case PdfView::Tool::Pencil:
        return QStringLiteral("pencil");
    case PdfView::Tool::Highlighter:
        return QStringLiteral("highlighter");
    case PdfView::Tool::Eraser:
        return QStringLiteral("eraser");
    case PdfView::Tool::Note:
        return QStringLiteral("note_point");
    case PdfView::Tool::Hand:
        return QString();
    }
    return QString();
}

void ReaderWorkspace::applyToolPreset(PdfView::Tool tool)
{
    const QString id = presetIdFor(tool);
    if (id.isEmpty() || !m_session || !m_session->toolPresets())
        return;
    if (tool == PdfView::Tool::Select || tool == PdfView::Tool::TextSelect
        || tool == PdfView::Tool::Eraser)
        return;
    m_view->setDefaultStyle(m_session->toolPresets()->style(id));
}

AnnotationStyle ReaderWorkspace::styleFromControls() const
{
    const Annotation *annotation = currentAnnotation();
    AnnotationStyle style;
    if (annotation && m_view->tool() == PdfView::Tool::Select)
        style = annotation->style;
    else
        style = m_view->defaultStyle();
    if (m_colorButton)
        style.strokeColor = m_colorButton->property("panoColor").value<QColor>();
    if (m_fillCheck && m_fillButton) {
        style.fillColor = m_fillCheck->isChecked()
                              ? m_fillButton->property("panoColor").value<QColor>()
                              : QColor();
    }
    if (m_widthSpin)
        style.strokeWidthPt = m_widthSpin->value();
    if (m_opacitySlider)
        style.opacity = m_opacitySlider->value() / 100.0;
    if (m_blendCombo)
        style.blend = m_blendCombo->currentData().toString() == QStringLiteral("multiply")
                          ? BlendMode::Multiply
                          : BlendMode::Normal;
    return style;
}

void ReaderWorkspace::setControlsFromStyle(const AnnotationStyle &style)
{
    m_syncingControls = true;
    if (m_colorButton)
        updateColorButton(m_colorButton, safeColor(style.strokeColor, QColor(0x47, 0x7E, 0xB8)));
    if (m_fillCheck)
        m_fillCheck->setChecked(style.hasFill());
    if (m_fillButton)
        updateColorButton(m_fillButton,
                          safeColor(style.fillColor, QColor(0xFF, 0xE0, 0x66)));
    if (m_widthSpin)
        m_widthSpin->setValue(qBound(0.25, style.strokeWidthPt, 20.0));
    if (m_opacitySlider) {
        m_opacitySlider->setValue(qBound(5, qRound(style.opacity * 100.0), 100));
        if (m_opacityValue)
            m_opacityValue->setText(QStringLiteral("%1%").arg(m_opacitySlider->value()));
    }
    if (m_blendCombo)
        m_blendCombo->setCurrentIndex(style.blend == BlendMode::Multiply ? 1 : 0);
    m_syncingControls = false;
}

void ReaderWorkspace::updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button)
        return;
    button->setProperty("panoColor", color);
    button->setStyleSheet(QStringLiteral(
        "QPushButton { background-color: %1; border: 1px solid palette(mid); "
        "border-radius: 3px; }")
                              .arg(color.name(QColor::HexArgb)));
}

void ReaderWorkspace::chooseStrokeColor()
{
    const Annotation *annotation = currentAnnotation();
    const QColor current = annotation && m_view->tool() == PdfView::Tool::Select
                               ? annotation->style.strokeColor
                               : m_view->defaultStyle().strokeColor;
    const QColor picked = QColorDialog::getColor(current, this, tr("选择颜色"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!picked.isValid())
        return;
    updateColorButton(m_colorButton, picked);
    applyStyleFromControls();
}

void ReaderWorkspace::chooseFillColor()
{
    const Annotation *annotation = currentAnnotation();
    const QColor current = annotation && m_view->tool() == PdfView::Tool::Select
                               ? annotation->style.fillColor
                               : m_view->defaultStyle().fillColor;
    const QColor picked = QColorDialog::getColor(current.isValid() ? current : QColor(0xFF, 0xE0, 0x66),
                                                 this, tr("选择填充颜色"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!picked.isValid())
        return;
    if (m_fillCheck)
        m_fillCheck->setChecked(true);
    updateColorButton(m_fillButton, picked);
    applyStyleFromControls();
}

void ReaderWorkspace::scheduleStyleApply()
{
    if (!m_syncingControls && m_styleTimer)
        m_styleTimer->start();
}

void ReaderWorkspace::schedulePresetSave()
{
    if (m_presetTimer)
        m_presetTimer->start();
}

void ReaderWorkspace::applyStyleFromControls()
{
    if (m_syncingControls || m_applyingStyle)
        return;
    m_applyingStyle = true;
    const AnnotationStyle style = styleFromControls();
    if (m_view->tool() == PdfView::Tool::Select) {
        if (currentAnnotation()) {
            m_view->applyStyleToSelected(style);
            updateDetailPanel();
            updateContextToolBar();
        }
        m_applyingStyle = false;
        return;
    }

    const QString id = presetIdFor(m_view->tool());
    if (!id.isEmpty() && m_session && m_session->toolPresets()) {
        m_session->toolPresets()->setStyle(id, style);
        m_view->setDefaultStyle(style);
        schedulePresetSave();
    }
    m_applyingStyle = false;
}

void ReaderWorkspace::updateContextToolBar()
{
    if (!m_contextToolBar || !m_contextWidget)
        return;
    const PdfView::Tool tool = m_view->tool();
    const Annotation *annotation = currentAnnotation();
    const bool objectMode = tool == PdfView::Tool::Select && annotation;
    const AnnotationStyle style = objectMode ? annotation->style
                                             : m_view->defaultStyle();
    setControlsFromStyle(style);

    bool color = false;
    bool fill = false;
    bool width = false;
    bool opacity = false;
    bool blend = false;
    QString hint;
    if (tool == PdfView::Tool::TextSelect || tool == PdfView::Tool::Hand) {
        hint = tool == PdfView::Tool::Hand ? tr("拖动画布滚动手感：按住左键移动页面。")
                                           : tr("拖选文字；Ctrl+C 复制。");
    } else if (tool == PdfView::Tool::Eraser) {
        hint = tr("单击对象删除；带笔记的对象会先确认。");
    } else if (tool == PdfView::Tool::Select && !annotation) {
        hint = tr("选择对象后可在此编辑它的颜色、线宽、透明度与填充。");
    } else if (tool == PdfView::Tool::Note || (objectMode && annotation->type == AnnotationType::NotePoint)) {
        hint = objectMode ? tr("附着笔记对象") : tr("单击页面创建笔记图钉。");
        color = true;
        opacity = true;
    } else if (tool == PdfView::Tool::Highlight
               || (objectMode && annotation->type == AnnotationType::Highlight)) {
        hint = tr("文本高亮 · 正片叠底不遮挡文字");
        color = true;
        opacity = true;
        blend = true;
    } else if (tool == PdfView::Tool::Underline || tool == PdfView::Tool::StrikeOut
               || (objectMode && (annotation->type == AnnotationType::Underline
                                  || annotation->type == AnnotationType::StrikeOut))) {
        hint = PdfView::toolDisplayName(tool);
        color = true;
        width = true;
        opacity = true;
    } else if (tool == PdfView::Tool::Highlighter
               || (objectMode && annotation->type == AnnotationType::Highlighter)) {
        hint = tr("荧光笔 · 笔画按整笔合成");
        color = true;
        width = true;
        opacity = true;
        blend = true;
    } else if (tool == PdfView::Tool::Rectangle || tool == PdfView::Tool::Ellipse
               || (objectMode && (annotation->type == AnnotationType::Rectangle
                                  || annotation->type == AnnotationType::Ellipse))) {
        hint = objectMode ? tr("编辑选中形状") : tr("拖动绘制形状");
        color = true;
        fill = true;
        width = true;
        opacity = true;
        blend = true;
    } else {
        hint = PdfView::toolDisplayName(tool);
        color = true;
        width = true;
        opacity = true;
        blend = true;
    }

    m_contextHint->setText(hint);
    m_contextHint->setVisible(!hint.isEmpty() && !color);
    m_colorWidget->setVisible(color);
    m_fillWidget->setVisible(fill);
    m_widthWidget->setVisible(width);
    m_opacityWidget->setVisible(opacity);
    m_blendWidget->setVisible(blend);
    if (!color && hint.isEmpty())
        m_contextHint->setVisible(true);
}

void ReaderWorkspace::updateStylePanel()
{
    updateContextToolBar();
}

void ReaderWorkspace::onPageChanged(int page)
{
    if (m_pageSpin) {
        const QSignalBlocker blocker(m_pageSpin);
        m_pageSpin->setValue(page + 1);
    }
    if (m_thumbnailList && page >= 0 && page < m_thumbnailList->count()) {
        const QSignalBlocker blocker(m_thumbnailList);
        m_thumbnailList->setCurrentRow(page);
        m_thumbnailList->scrollToItem(m_thumbnailList->item(page),
                                      QAbstractItemView::EnsureVisible);
    }
    requestNearbyThumbnails();
    updateBottomStatus();
}

void ReaderWorkspace::onZoomChanged(qreal zoom)
{
    if (m_zoomCombo) {
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setEditText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    }
    updateBottomStatus();
}

void ReaderWorkspace::onRotationChanged(int degrees)
{
    Q_UNUSED(degrees);
    refreshThumbnails();
    updateBottomStatus();
}

void ReaderWorkspace::goToPageFromSpin(int oneBasedPage)
{
    m_view->goToPage(oneBasedPage - 1);
}

void ReaderWorkspace::zoomEdited()
{
    QString text = m_zoomCombo ? m_zoomCombo->currentText().trimmed() : QString();
    text.remove(QLatin1Char('%'));
    bool ok = false;
    const double percent = text.toDouble(&ok);
    if (ok && percent > 0.0)
        m_view->setZoom(qBound(0.05, percent / 100.0, 12.0));
}

void ReaderWorkspace::updateBottomStatus()
{
    if (!m_view)
        return;
    if (m_pageSpin) {
        const QSignalBlocker blocker(m_pageSpin);
        m_pageSpin->setMaximum(qMax(1, m_view->pageCount()));
        m_pageSpin->setValue(m_view->currentPage() + 1);
    }
    if (m_zoomCombo) {
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setEditText(QStringLiteral("%1%").arg(qRound(m_view->zoom() * 100.0)));
    }
    if (m_saveStateLabel)
        m_saveStateLabel->setText(m_session && m_session->isDirty() ? tr("未保存") : tr("已保存"));
}

void ReaderWorkspace::saveCurrent()
{
    if (!m_session)
        return;
    commitPendingNote();
    QString error;
    if (m_session->saveNow(&error)) {
        if (m_saveStateLabel)
            m_saveStateLabel->setText(tr("已保存"));
        showStatus(tr("已保存"));
    } else {
        if (m_saveStateLabel)
            m_saveStateLabel->setText(tr("保存失败"));
        showStatus(error);
    }
}

void ReaderWorkspace::deleteSelected()
{
    m_view->deleteSelected();
}

void ReaderWorkspace::showStatus(const QString &text)
{
    if (text.isEmpty())
        return;
    if (m_statusLabel)
        m_statusLabel->setText(text);
    emit statusMessage(text);
    if (m_statusTimer)
        m_statusTimer->start();
}

void ReaderWorkspace::updatePanelVisibility()
{
    if (m_leftDock)
        m_leftDock->setVisible(m_leftToggle ? m_leftToggle->isChecked() : true);
    if (m_rightDock)
        m_rightDock->setVisible(m_rightToggle ? m_rightToggle->isChecked() : true);
}

void ReaderWorkspace::persistPanelState()
{
    if (!m_session || !m_session->settings() || !m_splitter)
        return;
    const QList<int> sizes = m_splitter->sizes();
    if (sizes.size() == 3) {
        m_session->settings()->setReaderPanelState(
            m_leftDock && m_leftDock->isVisible(), m_rightDock && m_rightDock->isVisible(),
            sizes.at(0), sizes.at(2));
        m_session->settings()->save();
    }
}

void ReaderWorkspace::insertImageIntoNote()
{
    if (!m_session)
        return;
    if (!isEditingNote())
        beginNoteEdit();
    if (!isEditingNote())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("插入图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.gif *.bmp);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        showStatus(tr("无法读取图片：%1").arg(reader.errorString()));
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 20 * 1024 * 1024) {
        showStatus(tr("图片必须小于 20 MiB"));
        return;
    }
    const QByteArray bytes = file.readAll();
    const QString extension = QFileInfo(path).suffix().toLower();
    QString relative;
    QString error;
    if (!m_session->writeAsset(QStringLiteral("image"), extension, bytes, &relative, &error)) {
        showStatus(error);
        return;
    }
    m_notePanel->insertMarkdown(QStringLiteral("![%1](%2)")
                                    .arg(QFileInfo(path).completeBaseName(), relative));
}

void ReaderWorkspace::startRegionCapture()
{
    if (!isEditingNote())
        beginNoteEdit();
    if (!isEditingNote() || !m_session) {
        showStatus(tr("请先选择一个批注并开始编辑笔记"));
        return;
    }
    m_view->setSelectionOverlayVisible(false);
    m_captureOverlay->setParent(m_view->viewport());
    m_captureOverlay->setGeometry(m_view->viewport()->rect());
    m_captureOverlay->show();
    m_captureOverlay->raise();
    m_captureOverlay->setFocus();
    showStatus(tr("拖拽选择要插入笔记的区域，Esc 取消"));
}

void ReaderWorkspace::finishCapture(const QRect &rect)
{
    m_view->setSelectionOverlayVisible(true);
    if (!m_session || rect.width() < 4 || rect.height() < 4)
        return;
    const QPixmap pixmap = m_view->viewport()->grab(rect);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    QString relative;
    QString error;
    if (!m_session->writeAsset(QStringLiteral("capture"), QStringLiteral("png"), bytes,
                               &relative, &error)) {
        showStatus(error);
        return;
    }
    m_notePanel->insertMarkdown(QStringLiteral("![区域截图](%1)").arg(relative));
    showStatus(tr("已插入区域截图"));
}

} // namespace pano
