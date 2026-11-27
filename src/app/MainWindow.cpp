#include "app/MainWindow.h"

#include "app/NotePanel.h"
#include "app/RegionCaptureOverlay.h"
#include "pdf/PdfDocument.h"
#include "storage/AnnotationStore.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QImageReader>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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
    if (text.size() <= maxCharacters)
        return text;
    return text.left(maxCharacters - 1) + QStringLiteral("…");
}

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024LL)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

qreal zoomFromText(const QString &text, bool *ok)
{
    QString value = text;
    value.remove(QLatin1Char('%'));
    value = value.trimmed();
    bool parsed = false;
    const double percent = value.toDouble(&parsed);
    if (ok)
        *ok = parsed;
    if (!parsed)
        return 1.0;
    return std::clamp(percent / 100.0, 0.05, 12.0);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    setMinimumSize(1024, 700);
    resize(1440, 900);
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks);

    m_view = new PdfView(this);
    setCentralWidget(m_view);

    buildActions();
    buildToolBars();
    buildDocks();
    buildStatusBar();
    buildMenus();
    connectSignals();

    updateActions();
    updatePropertyPanel();
    resetDocumentUi();
    m_view->setFocus();

    statusBar()->showMessage(tr("打开一份 PDF 开始阅读、标注和记录笔记。"), 8000);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildActions()
{
    m_openAction = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("打开 PDF…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);

    m_saveAction = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton), tr("保存标注"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveDocument);

    m_closeAction = new QAction(tr("关闭文档"), this);
    m_closeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    connect(m_closeAction, &QAction::triggered, this, &MainWindow::closeDocument);

    m_quitAction = new QAction(tr("退出"), this);
    m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = m_view->undoStack()->createUndoAction(this, tr("撤销"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_undoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_view->addAction(m_undoAction);

    m_redoAction = m_view->undoStack()->createRedoAction(this, tr("重做"));
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_redoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_view->addAction(m_redoAction);

    m_deleteAction = new QAction(tr("删除选中标注"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_view->addAction(m_deleteAction);
    connect(m_deleteAction, &QAction::triggered, this, [this] {
        if (!m_view->selectedAnnotationId().isEmpty())
            m_view->deleteSelected();
    });

    m_editNoteAction = new QAction(tr("编辑选中笔记"), this);
    m_editNoteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    connect(m_editNoteAction, &QAction::triggered, this, &MainWindow::beginNoteEdit);

    m_zoomInAction = new QAction(tr("放大"), this);
    m_zoomInAction->setShortcuts({QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
    connect(m_zoomInAction, &QAction::triggered, m_view, &PdfView::zoomIn);

    m_zoomOutAction = new QAction(tr("缩小"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, m_view, &PdfView::zoomOut);

    m_fitWidthAction = new QAction(tr("适合宽度"), this);
    m_fitWidthAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(m_fitWidthAction, &QAction::triggered, m_view, &PdfView::fitWidth);

    m_fitPageAction = new QAction(tr("适合整页"), this);
    m_fitPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));
    connect(m_fitPageAction, &QAction::triggered, m_view, &PdfView::fitPage);

    m_actualSizeAction = new QAction(tr("实际大小"), this);
    m_actualSizeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(m_actualSizeAction, &QAction::triggered, m_view, &PdfView::resetZoom);

    m_rotateCwAction = new QAction(tr("顺时针旋转 90°"), this);
    m_rotateCwAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(m_rotateCwAction, &QAction::triggered, m_view, [this] { m_view->rotateBy(90); });

    m_rotateCcwAction = new QAction(tr("逆时针旋转 90°"), this);
    m_rotateCcwAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(m_rotateCcwAction, &QAction::triggered, m_view, [this] { m_view->rotateBy(-90); });

    m_captureAction = new QAction(tr("区域截图到笔记"), this);
    m_captureAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
    connect(m_captureAction, &QAction::triggered, this, &MainWindow::startRegionCapture);

    m_diagnosticsAction = new QAction(tr("诊断信息…"), this);
    connect(m_diagnosticsAction, &QAction::triggered, this, &MainWindow::showDiagnostics);

    m_shortcutsAction = new QAction(tr("快捷键…"), this);
    connect(m_shortcutsAction, &QAction::triggered, this, &MainWindow::showShortcuts);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);

    struct ToolDefinition {
        PdfView::Tool tool;
        const char *text;
        const char *key;
    };
    const ToolDefinition definitions[] = {
        {PdfView::Tool::Select, QT_TR_NOOP("选择"), "V"},
        {PdfView::Tool::TextSelect, QT_TR_NOOP("文本选择"), "T"},
        {PdfView::Tool::Highlight, QT_TR_NOOP("高亮"), "H"},
        {PdfView::Tool::Underline, QT_TR_NOOP("下划线"), "U"},
        {PdfView::Tool::StrikeOut, QT_TR_NOOP("删除线"), "S"},
        {PdfView::Tool::Line, QT_TR_NOOP("直线"), "L"},
        {PdfView::Tool::Rectangle, QT_TR_NOOP("矩形"), "R"},
        {PdfView::Tool::Ellipse, QT_TR_NOOP("椭圆"), "O"},
        {PdfView::Tool::Pencil, QT_TR_NOOP("画笔"), "P"},
        {PdfView::Tool::Highlighter, QT_TR_NOOP("荧光笔"), "M"},
        {PdfView::Tool::Eraser, QT_TR_NOOP("橡皮擦"), "E"},
        {PdfView::Tool::Note, QT_TR_NOOP("笔记图钉"), "N"},
    };

    for (const ToolDefinition &definition : definitions) {
        auto *action = new QAction(tr(definition.text), this);
        action->setCheckable(true);
        action->setData(static_cast<int>(definition.tool));
        action->setShortcut(QKeySequence(QString::fromLatin1(definition.key)));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setToolTip(tr("%1 (%2)").arg(tr(definition.text), QString::fromLatin1(definition.key)));
        m_view->addAction(action);
        m_toolGroup->addAction(action);
        m_toolActions.append(action);
    }

    connect(m_toolGroup, &QActionGroup::triggered, this, [this](QAction *action) {
        m_view->setTool(static_cast<PdfView::Tool>(action->data().toInt()));
    });

    if (!m_toolActions.isEmpty())
        m_toolActions.front()->setChecked(true);
}

void MainWindow::buildToolBars()
{
    m_mainToolBar = addToolBar(tr("主工具栏"));
    m_mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_mainToolBar->setMovable(false);
    m_mainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_mainToolBar->addAction(m_openAction);
    m_mainToolBar->addAction(m_saveAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_undoAction);
    m_mainToolBar->addAction(m_redoAction);
    m_mainToolBar->addAction(m_deleteAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_editNoteAction);
    m_mainToolBar->addAction(m_captureAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_fitWidthAction);
    m_mainToolBar->addAction(m_fitPageAction);
    m_mainToolBar->addAction(m_rotateCwAction);

    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mainToolBar->addWidget(spacer);

    m_mainToolBar->addWidget(new QLabel(tr("页码 "), this));
    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setKeyboardTracking(false);
    m_pageSpin->setAlignment(Qt::AlignRight);
    m_pageSpin->setFixedWidth(64);
    m_mainToolBar->addWidget(m_pageSpin);
    m_pageCountLabel = new QLabel(QStringLiteral("/ 0"), this);
    m_mainToolBar->addWidget(m_pageCountLabel);

    m_mainToolBar->addWidget(new QLabel(tr("  缩放 "), this));
    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->setFixedWidth(86);
    m_zoomCombo->addItems({QStringLiteral("50%"), QStringLiteral("75%"), QStringLiteral("100%"),
                           QStringLiteral("125%"), QStringLiteral("150%"), QStringLiteral("200%"),
                           QStringLiteral("300%")});
    m_mainToolBar->addWidget(m_zoomCombo);

    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (m_syncingUi || !m_view->hasDocument())
            return;
        if (value >= 1 && value <= m_view->pageCount())
            m_view->goToPage(value - 1);
    });
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int) {
        bool ok = false;
        const qreal zoom = zoomFromText(m_zoomCombo->currentText(), &ok);
        if (ok && m_view->hasDocument())
            m_view->setZoom(zoom);
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        bool ok = false;
        const qreal zoom = zoomFromText(m_zoomCombo->currentText(), &ok);
        if (ok && m_view->hasDocument())
            m_view->setZoom(zoom);
    });

    m_toolBar = new QToolBar(tr("标注工具"), this);
    m_toolBar->setObjectName(QStringLiteral("annotationToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    addToolBar(Qt::LeftToolBarArea, m_toolBar);
    for (QAction *action : m_toolActions)
        m_toolBar->addAction(action);
}

void MainWindow::buildDocks()
{
    auto *listContainer = new QWidget(this);
    auto *listLayout = new QVBoxLayout(listContainer);
    listLayout->setContentsMargins(6, 6, 6, 6);
    listLayout->setSpacing(4);
    m_noteList = new QListWidget(listContainer);
    m_noteList->setAlternatingRowColors(true);
    m_noteList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_noteList->setWordWrap(true);
    m_noteList->setMouseTracking(true);
    listLayout->addWidget(m_noteList, 1);
    auto *listHint = new QLabel(tr("单击笔记可跳回原文位置。"), listContainer);
    listHint->setWordWrap(true);
    listHint->setStyleSheet(QStringLiteral("color:#6b7280;"));
    listLayout->addWidget(listHint);

    m_listDock = new QDockWidget(tr("笔记列表"), this);
    m_listDock->setObjectName(QStringLiteral("noteListDock"));
    m_listDock->setWidget(listContainer);
    m_listDock->setMinimumWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, m_listDock);

    m_notePanel = new NotePanel(this);
    m_noteDock = new QDockWidget(tr("笔记"), this);
    m_noteDock->setObjectName(QStringLiteral("noteDock"));
    m_noteDock->setWidget(m_notePanel);
    m_noteDock->setMinimumWidth(330);
    addDockWidget(Qt::RightDockWidgetArea, m_noteDock);

    auto *propertyContainer = new QWidget(this);
    auto *propertyLayout = new QVBoxLayout(propertyContainer);
    propertyLayout->setContentsMargins(8, 8, 8, 8);
    propertyLayout->setSpacing(8);

    m_selectionLabel = new QLabel(tr("未选择标注"), propertyContainer);
    m_selectionLabel->setWordWrap(true);
    QFont selectionFont = m_selectionLabel->font();
    selectionFont.setBold(true);
    m_selectionLabel->setFont(selectionFont);
    propertyLayout->addWidget(m_selectionLabel);

    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);

    m_strokeButton = new QPushButton(propertyContainer);
    m_strokeButton->setToolTip(tr("选择描边颜色，可设置透明度"));
    form->addRow(tr("描边颜色"), m_strokeButton);

    m_widthSpin = new QDoubleSpinBox(propertyContainer);
    m_widthSpin->setRange(0.25, 20.0);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setSuffix(tr(" pt"));
    form->addRow(tr("线宽"), m_widthSpin);

    auto *opacityWidget = new QWidget(propertyContainer);
    auto *opacityLayout = new QHBoxLayout(opacityWidget);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->setSpacing(6);
    m_opacitySlider = new QSlider(Qt::Horizontal, opacityWidget);
    m_opacitySlider->setRange(5, 100);
    m_opacitySlider->setValue(100);
    m_opacityValue = new QLabel(QStringLiteral("100%"), opacityWidget);
    m_opacityValue->setMinimumWidth(38);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);
    form->addRow(tr("不透明度"), opacityWidget);

    m_blendCombo = new QComboBox(propertyContainer);
    m_blendCombo->addItem(tr("正常"), QStringLiteral("normal"));
    m_blendCombo->addItem(tr("正片叠底"), QStringLiteral("multiply"));
    form->addRow(tr("混合模式"), m_blendCombo);

    auto *fillWidget = new QWidget(propertyContainer);
    auto *fillLayout = new QHBoxLayout(fillWidget);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    fillLayout->setSpacing(6);
    m_fillCheck = new QCheckBox(tr("启用"), fillWidget);
    m_fillButton = new QPushButton(fillWidget);
    m_fillButton->setToolTip(tr("选择填充颜色"));
    fillLayout->addWidget(m_fillCheck);
    fillLayout->addWidget(m_fillButton, 1);
    form->addRow(tr("填充"), fillWidget);

    propertyLayout->addLayout(form);

    m_applyStyleButton = new QPushButton(tr("应用到选中标注"), propertyContainer);
    propertyLayout->addWidget(m_applyStyleButton);

    auto *propertyHint = new QLabel(
        tr("未选择标注时，这里的样式会成为新建标注的默认样式。"), propertyContainer);
    propertyHint->setWordWrap(true);
    propertyHint->setStyleSheet(QStringLiteral("color:#6b7280;"));
    propertyLayout->addWidget(propertyHint);
    propertyLayout->addStretch(1);

    m_propertyDock = new QDockWidget(tr("标注属性"), this);
    m_propertyDock->setObjectName(QStringLiteral("propertyDock"));
    m_propertyDock->setWidget(propertyContainer);
    m_propertyDock->setMinimumWidth(250);
    addDockWidget(Qt::LeftDockWidgetArea, m_propertyDock);
    splitDockWidget(m_listDock, m_propertyDock, Qt::Vertical);

    resizeDocks({m_listDock, m_propertyDock}, {390, 430}, Qt::Vertical);
    resizeDocks({m_noteDock}, {380}, Qt::Horizontal);

    m_fillButton->setEnabled(false);
    updateColorButton(m_strokeButton, m_strokeColor);
    updateColorButton(m_fillButton, m_fillColor);

    connect(m_strokeButton, &QPushButton::clicked, this, &MainWindow::chooseStrokeColor);
    connect(m_fillButton, &QPushButton::clicked, this, &MainWindow::chooseFillColor);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { styleControlsChanged(); });
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1%").arg(value));
        styleControlsChanged();
    });
    connect(m_blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { styleControlsChanged(); });
    connect(m_fillCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_fillButton->setEnabled(checked);
        if (checked && !m_fillColor.isValid()) {
            m_fillColor = m_strokeColor;
            m_fillColor.setAlpha(56);
            updateColorButton(m_fillButton, m_fillColor);
        }
        styleControlsChanged();
    });
    connect(m_applyStyleButton, &QPushButton::clicked, this, &MainWindow::applyStyleToSelected);
    connect(m_noteList, &QListWidget::itemClicked, this, &MainWindow::onNoteListItemActivated);
    connect(m_noteList, &QListWidget::itemActivated, this, &MainWindow::onNoteListItemActivated);
}

void MainWindow::buildStatusBar()
{
    m_statusPage = new QLabel(tr("第 — / — 页"), this);
    m_statusZoom = new QLabel(tr("缩放 —"), this);
    m_statusCache = new QLabel(tr("缓存 —"), this);
    statusBar()->addPermanentWidget(m_statusPage);
    statusBar()->addPermanentWidget(m_statusZoom);
    statusBar()->addPermanentWidget(m_statusCache);
    statusBar()->setSizeGripEnabled(true);
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_closeAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    auto *editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_deleteAction);
    editMenu->addAction(m_editNoteAction);

    auto *viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addAction(m_fitPageAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_rotateCwAction);
    viewMenu->addAction(m_rotateCcwAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_mainToolBar->toggleViewAction());
    viewMenu->addAction(m_toolBar->toggleViewAction());
    viewMenu->addAction(m_listDock->toggleViewAction());
    viewMenu->addAction(m_propertyDock->toggleViewAction());
    viewMenu->addAction(m_noteDock->toggleViewAction());

    auto *annotationMenu = menuBar()->addMenu(tr("标注"));
    for (QAction *action : m_toolActions)
        annotationMenu->addAction(action);
    annotationMenu->addSeparator();
    annotationMenu->addAction(m_editNoteAction);
    annotationMenu->addAction(m_captureAction);
    annotationMenu->addAction(m_deleteAction);

    auto *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(m_shortcutsAction);
    helpMenu->addAction(m_diagnosticsAction);
    helpMenu->addSeparator();
    auto *aboutAction = helpMenu->addAction(tr("关于 PANO"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("关于 PANO"),
                           tr("<p><b>PANO</b> Phase 0 Demo</p>"
                              "<p>本地优先的 PDF 阅读与标注工具：原文只读，标注与 Markdown 笔记保存在 PDF 同级的 .pano 目录。</p>"
                              "<p>使用 Qt %1，核心功能完全离线。</p>"
                              "<p>MIT License</p>")
                               .arg(QString::fromLatin1(qVersion())));
    });
}

void MainWindow::connectSignals()
{
    connect(m_view, &PdfView::documentChanged, this, &MainWindow::onDocumentChanged);
    connect(m_view, &PdfView::annotationsChanged, this, &MainWindow::onAnnotationsChanged);
    connect(m_view, &PdfView::annotationSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_view, &PdfView::noteRequested, this, &MainWindow::onNoteRequested);
    connect(m_view, &PdfView::pageChanged, this, &MainWindow::onPageChanged);
    connect(m_view, &PdfView::zoomChanged, this, &MainWindow::onZoomChanged);
    connect(m_view, &PdfView::rotationChanged, this, &MainWindow::onRotationChanged);
    connect(m_view, &PdfView::toolChanged, this, &MainWindow::onToolChanged);
    connect(m_view, &PdfView::statusMessage, this, &MainWindow::onStatusMessage);
    connect(m_view, &PdfView::openFileRequested, this, &MainWindow::requestOpenFile);
    connect(m_view->renderer(), &PageRenderer::cacheChanged, this, &MainWindow::updateCacheLabel);
    connect(m_view->undoStack(), &QUndoStack::indexChanged, this, [this](int) {
        updateActions();
        updateWindowTitle();
    });

    connect(m_notePanel, &NotePanel::editRequested, this, &MainWindow::beginNoteEdit);
    connect(m_notePanel, &NotePanel::commitRequested, this, &MainWindow::commitNoteEdit);
    connect(m_notePanel, &NotePanel::cancelRequested, this, &MainWindow::cancelNoteEdit);
    connect(m_notePanel, &NotePanel::imageRequested, this, &MainWindow::insertImageIntoNote);
    connect(m_notePanel, &NotePanel::captureRequested, this, &MainWindow::startRegionCapture);
}

void MainWindow::openFile()
{
    const QString startDirectory = m_view->hasDocument() ? baseDirectory() : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开 PDF"), startDirectory, tr("PDF 文件 (*.pdf);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    openDocument(path);
}

bool MainWindow::openDocument(const QString &filePath)
{
    if (!confirmSaveIfNeeded())
        return false;

    if (m_view->hasDocument())
        unloadDocument();

    const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
    QString error;
    if (!m_view->openFile(absolutePath, &error)) {
        QMessageBox::warning(this, tr("无法打开 PDF"), error);
        resetDocumentUi();
        return false;
    }

    auto store = std::make_unique<AnnotationStore>();
    if (!store->attachToDocument(*m_view->document(), &error)) {
        QMessageBox::critical(
            this, tr("停止加载标注"),
            tr("%1\n\n为避免误写，PANO 不会打开或覆盖这个数据目录。").arg(error));
        unloadDocument();
        return false;
    }

    QVector<Annotation> annotations;
    if (!store->loadAnnotations(m_view->pageCount(), &annotations, &error)) {
        QMessageBox::critical(this, tr("无法加载标注"), error);
        unloadDocument();
        return false;
    }

    m_store = std::move(store);
    m_storeReady = true;

    m_loading = true;
    m_view->annotations()->setAnnotations(annotations);
    m_view->undoStack()->clear();
    m_loading = false;

    m_notePanel->setBaseDirectory(m_store->directory());
    m_dirty = false;

    onDocumentChanged();
    setDirty(false);
    refreshNoteList();
    updateNotePanel();
    updatePropertyPanel();
    updateCacheLabel();
    m_view->setFocus();

    if (m_store->isNewStore()) {
        statusBar()->showMessage(
            tr("已打开“%1”。首次保存标注时会创建 %2")
                .arg(m_view->document()->fileName(), QDir(m_store->directory()).dirName()),
            9000);
    } else {
        statusBar()->showMessage(
            tr("已打开“%1”，加载 %2 条标注。")
                .arg(m_view->document()->fileName())
                .arg(annotations.size()),
            6000);
    }
    return true;
}

void MainWindow::requestOpenFile(const QString &filePath)
{
    if (!filePath.isEmpty())
        openDocument(filePath);
}

void MainWindow::saveDocument()
{
    if (!m_view->hasDocument())
        return;

    if (m_notePanel->isEditing())
        commitNoteEdit();

    if (!m_storeReady) {
        QMessageBox::warning(this, tr("无法保存"), tr("当前文档没有可写的数据目录。"));
        return;
    }

    QString error;
    if (!saveToStore(&error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        statusBar()->showMessage(tr("保存失败：%1").arg(error), 8000);
        return;
    }

    setDirty(false);
    statusBar()->showMessage(
        tr("已保存 %1 条标注到 %2")
            .arg(m_view->annotations()->count())
            .arg(QDir(m_store->directory()).dirName()),
        5000);
}

bool MainWindow::saveToStore(QString *error)
{
    if (!m_storeReady || !m_store) {
        if (error)
            *error = tr("当前文档没有绑定数据目录。");
        return false;
    }
    return m_store->save(m_view->annotations()->annotations(), error);
}

bool MainWindow::confirmSaveIfNeeded()
{
    if (m_notePanel->isEditing())
        commitNoteEdit();

    if (!m_dirty)
        return true;

    const QMessageBox::StandardButton answer = QMessageBox::warning(
        this, tr("有未保存的标注"),
        tr("“%1”的标注或笔记有修改，尚未保存。").arg(m_view->document()->fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);

    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;

    QString error;
    if (!saveToStore(&error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return false;
    }
    setDirty(false);
    return true;
}

void MainWindow::closeDocument()
{
    if (!m_view->hasDocument())
        return;
    if (!confirmSaveIfNeeded())
        return;
    unloadDocument();
    statusBar()->showMessage(tr("文档已关闭。"), 4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmSaveIfNeeded()) {
        event->ignore();
        return;
    }
    event->accept();
}

void MainWindow::unloadDocument()
{
    if (m_notePanel->isEditing())
        m_notePanel->endEdit();

    m_storeReady = false;
    m_loading = true;
    m_view->closeDocument();
    m_loading = false;
    m_store.reset();
    m_dirty = false;
    m_noteId.clear();
    resetDocumentUi();
    updateActions();
}

void MainWindow::resetDocumentUi()
{
    if (m_notePanel->isEditing())
        m_notePanel->endEdit();

    m_syncingUi = true;
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setValue(1);
    m_pageCountLabel->setText(QStringLiteral("/ 0"));
    m_zoomCombo->setCurrentText(QStringLiteral("—"));
    m_syncingUi = false;

    m_noteId.clear();
    m_notePanel->setHeading(tr("笔记"));
    m_notePanel->clearNote(tr("打开 PDF 并选择一条标注后，可在这里记录 Markdown 笔记。"));
    m_notePanel->setEditable(false);
    m_noteList->clear();

    m_statusPage->setText(tr("第 — / — 页"));
    m_statusZoom->setText(tr("缩放 —"));
    m_statusCache->setText(tr("缓存 —"));

    updateWindowTitle();
    updateActions();
    updatePropertyPanel();
}

void MainWindow::onDocumentChanged()
{
    if (!m_view->hasDocument()) {
        resetDocumentUi();
        return;
    }

    m_syncingUi = true;
    const int pageCount = m_view->pageCount();
    m_pageSpin->setRange(1, std::max(1, pageCount));
    m_pageSpin->setValue(std::clamp(m_view->currentPage() + 1, 1, std::max(1, pageCount)));
    m_pageCountLabel->setText(QStringLiteral("/ %1").arg(pageCount));
    m_syncingUi = false;

    m_notePanel->setBaseDirectory(baseDirectory());
    updateWindowTitle();
    updateActions();
    updateCacheLabel();
    m_statusPage->setText(tr("第 %1 / %2 页")
                             .arg(m_view->currentPage() + 1)
                             .arg(pageCount));
}

void MainWindow::onAnnotationsChanged()
{
    if (m_loading)
        return;

    if (m_storeReady)
        setDirty(true);

    refreshNoteList();
    updateActions();

    if (!m_noteId.isEmpty() && !m_notePanel->isEditing()) {
        const Annotation *annotation = m_view->annotations()->find(m_noteId);
        if (!annotation) {
            m_noteId.clear();
            if (m_view->selectedAnnotationId().isEmpty()) {
                m_notePanel->setHeading(tr("笔记"));
                m_notePanel->clearNote(tr("该标注已删除。"));
                m_notePanel->setEditable(false);
            }
        } else if (annotation->note.markdown != m_notePanel->noteMarkdown()) {
            updateNotePanel();
        }
    }
}

void MainWindow::onSelectionChanged(const QString &annotationId)
{
    if (m_notePanel->isEditing() && annotationId != m_noteId)
        commitNoteEdit();

    m_noteId = annotationId;
    updateNotePanel();
    updatePropertyPanel();
    updateActions();

    if (annotationId.isEmpty())
        return;

    for (int row = 0; row < m_noteList->count(); ++row) {
        QListWidgetItem *item = m_noteList->item(row);
        if (item && item->data(Qt::UserRole).toString() == annotationId) {
            const QSignalBlocker blocker(m_noteList);
            m_noteList->setCurrentItem(item);
            break;
        }
    }
}

void MainWindow::onNoteRequested(const QString &annotationId)
{
    if (annotationId.isEmpty())
        return;
    m_view->selectAnnotation(annotationId);
    updateNotePanel();
    beginNoteEdit();
}

void MainWindow::onNoteListItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString annotationId = item->data(Qt::UserRole).toString();
    if (annotationId.isEmpty())
        return;
    m_view->selectAnnotation(annotationId);
    m_view->revealAnnotation(annotationId);
}

void MainWindow::beginNoteEdit()
{
    if (!m_view->hasDocument())
        return;
    if (!m_storeReady) {
        statusBar()->showMessage(tr("当前文档没有可写的数据目录，无法编辑笔记。"), 5000);
        return;
    }
    if (m_noteId.isEmpty() || !m_view->annotations()->find(m_noteId)) {
        statusBar()->showMessage(tr("请先选择一条标注，或使用笔记图钉工具在页面上创建。"), 5000);
        return;
    }
    updateNotePanel();
    m_notePanel->beginEdit();
}

void MainWindow::commitNoteEdit()
{
    if (!m_notePanel->isEditing())
        return;

    const Annotation *annotation = m_view->annotations()->find(m_noteId);
    if (!annotation) {
        m_notePanel->endEdit();
        return;
    }

    AnnotationNote note = annotation->note;
    const QString markdown = m_notePanel->markdown();
    note.markdown = markdown;
    if (markdown.trimmed().isEmpty()) {
        note.title.clear();
    } else if (note.title.trimmed().isEmpty()) {
        note.title = ellipsized(firstMarkdownLine(markdown), 40);
    }

    m_view->setAnnotationNote(m_noteId, note);
    m_notePanel->endEdit();

    const Annotation *updated = m_view->annotations()->find(m_noteId);
    if (updated)
        m_notePanel->setNote(updated->note.markdown);
    refreshNoteList();
    updateActions();
}

void MainWindow::cancelNoteEdit()
{
    if (!m_notePanel->isEditing())
        return;
    m_notePanel->endEdit();
    updateNotePanel();
}

void MainWindow::insertImageIntoNote()
{
    if (!m_storeReady) {
        statusBar()->showMessage(tr("当前文档没有可写的数据目录，无法插入图片。"), 5000);
        return;
    }
    if (m_noteId.isEmpty()) {
        statusBar()->showMessage(tr("请先选择一条笔记。"), 5000);
        return;
    }
    if (!m_notePanel->isEditing())
        beginNoteEdit();
    if (!m_notePanel->isEditing())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("插入图片到笔记"), baseDirectory(),
        tr("图片 (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    insertImageFile(path);
}

void MainWindow::insertImageFile(const QString &path)
{
    QImageReader reader(path);
    if (!reader.canRead()) {
        QMessageBox::warning(this, tr("无法插入图片"),
                             tr("无法读取图片“%1”。").arg(QFileInfo(path).fileName()));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("无法插入图片"), file.errorString());
        return;
    }
    if (file.size() > 20LL * 1024LL * 1024LL) {
        QMessageBox::warning(this, tr("图片过大"), tr("请选择小于 20 MiB 的图片。"));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QString extension = QFileInfo(path).suffix().toLower();
    if (extension.isEmpty())
        extension = QString::fromLatin1(reader.format()).toLower();
    if (extension.isEmpty())
        extension = QStringLiteral("png");

    QString relativePath;
    QString error;
    if (!m_store->writeAsset(QStringLiteral("image"), extension, bytes, &relativePath, &error)) {
        QMessageBox::warning(this, tr("无法保存图片"), error);
        return;
    }

    QString alt = QFileInfo(path).completeBaseName();
    alt.replace(QLatin1Char(']'), QLatin1Char(')'));
    m_notePanel->insertMarkdown(
        QStringLiteral("\n\n![%1](%2)\n\n").arg(alt, relativePath));
    statusBar()->showMessage(tr("图片已复制到数据目录：%1").arg(relativePath), 5000);
}

void MainWindow::startRegionCapture()
{
    if (!m_storeReady || !m_view->hasDocument()) {
        statusBar()->showMessage(tr("当前文档没有可写的数据目录。"), 5000);
        return;
    }
    if (m_noteId.isEmpty()) {
        statusBar()->showMessage(tr("请先选择一条标注，再框选区域。"), 5000);
        return;
    }
    if (!m_notePanel->isEditing())
        beginNoteEdit();
    if (!m_notePanel->isEditing())
        return;

    m_captureAction->setEnabled(false);
    auto *overlay = new RegionCaptureOverlay(m_view->viewport());
    overlay->setGeometry(m_view->viewport()->rect());
    overlay->raise();
    overlay->show();
    overlay->setFocus();
    m_view->setSelectionOverlayVisible(false);

    connect(overlay, &RegionCaptureOverlay::regionSelected, this,
            [this, overlay](const QRect &rect) {
                overlay->hide();
                finishCapture(rect);
                overlay->deleteLater();
                m_view->setSelectionOverlayVisible(true);
                updateActions();
            });
    connect(overlay, &RegionCaptureOverlay::cancelled, this, [this, overlay] {
        overlay->deleteLater();
        m_view->setSelectionOverlayVisible(true);
        updateActions();
        statusBar()->showMessage(tr("已取消区域截图。"), 3000);
    });
}

void MainWindow::finishCapture(const QRect &rect)
{
    if (rect.width() < 4 || rect.height() < 4)
        return;

    const QPixmap pixmap = m_view->viewport()->grab(rect);
    if (pixmap.isNull()) {
        QMessageBox::warning(this, tr("截图失败"), tr("无法读取所选区域。"));
        return;
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.toImage().save(&buffer, "PNG")) {
        QMessageBox::warning(this, tr("截图失败"), tr("无法把截图编码为 PNG。"));
        return;
    }
    buffer.close();

    QString relativePath;
    QString error;
    if (!m_store->writeAsset(QStringLiteral("capture"), QStringLiteral("png"), bytes,
                             &relativePath, &error)) {
        QMessageBox::warning(this, tr("无法保存截图"), error);
        return;
    }

    m_notePanel->insertMarkdown(
        QStringLiteral("\n\n![区域截图](%1)\n\n").arg(relativePath));
    statusBar()->showMessage(tr("截图已插入笔记：%1").arg(relativePath), 5000);
}

void MainWindow::updateNotePanel()
{
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        m_noteId.clear();
        m_notePanel->setHeading(tr("笔记"));
        m_notePanel->clearNote(tr("选择页面上的标注，或使用笔记图钉工具新建一条笔记。"));
        m_notePanel->setEditable(false);
        return;
    }

    m_noteId = annotation->id;
    m_notePanel->setHeading(
        tr("笔记 · %1 · 第 %2 页")
            .arg(annotationTypeDisplayName(annotation->type))
            .arg(annotation->pageIndex + 1));
    m_notePanel->setBaseDirectory(baseDirectory());
    if (annotation->note.isEmpty())
        m_notePanel->clearNote(tr("该标注还没有笔记。点击“编辑”开始记录。"));
    else
        m_notePanel->setNote(annotation->note.markdown);
    m_notePanel->setEditable(m_storeReady);
}

void MainWindow::updatePropertyPanel()
{
    if (!m_view)
        return;

    const Annotation *annotation = currentAnnotation();
    if (annotation) {
        m_selectionLabel->setText(tr("已选择：%1 · 第 %2 页")
                                      .arg(annotationTypeDisplayName(annotation->type))
                                      .arg(annotation->pageIndex + 1));
        setControlsFromStyle(annotation->style);
    } else {
        m_selectionLabel->setText(tr("未选择标注 — 下方样式用于新标注"));
        setControlsFromStyle(m_view->defaultStyle());
    }
    m_applyStyleButton->setEnabled(annotation != nullptr);
}

void MainWindow::updateActions()
{
    const bool hasDocument = m_view->hasDocument();
    const bool hasSelection = !m_view->selectedAnnotationId().isEmpty();

    m_saveAction->setEnabled(hasDocument && m_storeReady);
    m_closeAction->setEnabled(hasDocument);
    m_deleteAction->setEnabled(hasDocument && hasSelection);
    m_editNoteAction->setEnabled(hasDocument && m_storeReady && hasSelection);
    m_captureAction->setEnabled(hasDocument && m_storeReady && hasSelection);
    m_zoomInAction->setEnabled(hasDocument);
    m_zoomOutAction->setEnabled(hasDocument);
    m_fitWidthAction->setEnabled(hasDocument);
    m_fitPageAction->setEnabled(hasDocument);
    m_actualSizeAction->setEnabled(hasDocument);
    m_rotateCwAction->setEnabled(hasDocument);
    m_rotateCcwAction->setEnabled(hasDocument);
    m_pageSpin->setEnabled(hasDocument);
    m_zoomCombo->setEnabled(hasDocument);
    m_toolBar->setEnabled(hasDocument);
    m_propertyDock->setEnabled(hasDocument);
    if (m_applyStyleButton)
        m_applyStyleButton->setEnabled(hasDocument && hasSelection);
}

void MainWindow::updateWindowTitle()
{
    if (!m_view->hasDocument()) {
        setWindowTitle(tr("PANO"));
        setWindowModified(false);
        return;
    }
    setWindowTitle(tr("%1[*] — PANO").arg(m_view->document()->fileName()));
    setWindowModified(m_dirty);
}

void MainWindow::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        updateWindowTitle();
        return;
    }
    m_dirty = dirty;
    setWindowModified(dirty);
    updateWindowTitle();
    updateActions();
}

void MainWindow::refreshNoteList()
{
    if (!m_noteList)
        return;

    const QString selectedId = m_noteId;
    const QSignalBlocker blocker(m_noteList);
    m_noteList->clear();

    int noteCount = 0;
    for (const Annotation &annotation : m_view->annotations()->annotations()) {
        if (annotation.note.isEmpty())
            continue;
        ++noteCount;

        const QString title = annotation.note.title.trimmed().isEmpty()
                                  ? ellipsized(firstMarkdownLine(annotation.note.markdown), 42)
                                  : ellipsized(annotation.note.title, 42);
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n第 %2 页 · %3")
                .arg(title.isEmpty() ? tr("(无内容笔记)") : title)
                .arg(annotation.pageIndex + 1)
                .arg(annotationTypeDisplayName(annotation.type)),
            m_noteList);
        item->setData(Qt::UserRole, annotation.id);
        item->setToolTip(annotation.note.markdown.left(400));
        item->setSizeHint(QSize(180, 44));
        if (annotation.id == selectedId)
            m_noteList->setCurrentItem(item);
    }

    if (noteCount == 0) {
        auto *item = new QListWidgetItem(tr("暂无笔记。\n在标注上双击或使用笔记图钉创建。"), m_noteList);
        item->setFlags(Qt::NoItemFlags);
        item->setForeground(QColor(QStringLiteral("#7a808a")));
        item->setSizeHint(QSize(180, 48));
    }

    if (m_listDock)
        m_listDock->setWindowTitle(tr("笔记列表 (%1)").arg(noteCount));
}

void MainWindow::updateCacheLabel()
{
    if (!m_statusCache)
        return;
    if (!m_view->hasDocument()) {
        m_statusCache->setText(tr("缓存 —"));
        return;
    }
    const PageRenderer *renderer = m_view->renderer();
    m_statusCache->setText(tr("缓存 %1 / %2")
                               .arg(humanSize(renderer->cacheBytes()),
                                    humanSize(renderer->cacheLimit())));
}

void MainWindow::onPageChanged(int pageIndex)
{
    m_syncingUi = true;
    m_pageSpin->setValue(pageIndex + 1);
    m_syncingUi = false;
    m_statusPage->setText(tr("第 %1 / %2 页").arg(pageIndex + 1).arg(m_view->pageCount()));
    updateActions();
}

void MainWindow::onZoomChanged(qreal zoom)
{
    m_syncingUi = true;
    m_zoomCombo->setCurrentText(QStringLiteral("%1%").arg(qRound(zoom * 100.0)));
    m_syncingUi = false;
    m_statusZoom->setText(tr("缩放 %1%").arg(qRound(zoom * 100.0)));
}

void MainWindow::onRotationChanged(int degrees)
{
    Q_UNUSED(degrees);
    statusBar()->showMessage(tr("页面视图已旋转，标注仍使用原始页面坐标。"), 4000);
}

void MainWindow::onToolChanged(PdfView::Tool tool)
{
    for (QAction *action : m_toolActions) {
        if (static_cast<PdfView::Tool>(action->data().toInt()) == tool) {
            action->setChecked(true);
            break;
        }
    }
    statusBar()->showMessage(tr("当前工具：%1").arg(PdfView::toolDisplayName(tool)), 3000);
}

void MainWindow::onStatusMessage(const QString &message)
{
    statusBar()->showMessage(message, 6000);
}

AnnotationStyle MainWindow::styleFromControls() const
{
    AnnotationStyle style;
    style.strokeColor = m_strokeColor;
    style.strokeWidthPt = m_widthSpin ? m_widthSpin->value() : 1.5;
    style.opacity = m_opacitySlider ? std::clamp(m_opacitySlider->value() / 100.0, 0.05, 1.0) : 1.0;
    style.blend = (m_blendCombo && m_blendCombo->currentData().toString() == QLatin1String("multiply"))
                      ? BlendMode::Multiply
                      : BlendMode::Normal;
    if (m_fillCheck && m_fillCheck->isChecked()) {
        QColor fill = m_fillColor;
        if (!fill.isValid()) {
            fill = m_strokeColor;
            fill.setAlpha(56);
        }
        style.fillColor = fill;
    }
    return style;
}

void MainWindow::setControlsFromStyle(const AnnotationStyle &style)
{
    if (!m_widthSpin)
        return;

    m_syncingUi = true;
    m_strokeColor = style.strokeColor.isValid() ? style.strokeColor : QColor(QStringLiteral("#f2b705"));
    m_fillColor = style.fillColor;
    m_widthSpin->setValue(std::clamp(style.strokeWidthPt, 0.25, 20.0));
    m_opacitySlider->setValue(std::clamp(qRound(style.opacity * 100.0), 5, 100));
    m_opacityValue->setText(QStringLiteral("%1%").arg(m_opacitySlider->value()));
    const int blendIndex = m_blendCombo->findData(
        style.blend == BlendMode::Multiply ? QStringLiteral("multiply") : QStringLiteral("normal"));
    m_blendCombo->setCurrentIndex(blendIndex >= 0 ? blendIndex : 0);
    m_fillCheck->setChecked(style.hasFill());
    m_fillButton->setEnabled(style.hasFill());
    updateColorButton(m_strokeButton, m_strokeColor);
    updateColorButton(m_fillButton, m_fillColor);
    m_syncingUi = false;
}

void MainWindow::styleControlsChanged()
{
    if (m_syncingUi || !m_view)
        return;
    m_view->setDefaultStyle(styleFromControls());
}

void MainWindow::applyStyleToSelected()
{
    if (!m_view->hasDocument() || m_view->selectedAnnotationId().isEmpty()) {
        statusBar()->showMessage(tr("请先选择一条标注。"), 4000);
        return;
    }
    const AnnotationStyle style = styleFromControls();
    m_view->setDefaultStyle(style);
    m_view->applyStyleToSelected(style);
    updatePropertyPanel();
    statusBar()->showMessage(tr("样式已应用到选中标注。"), 3000);
}

void MainWindow::chooseStrokeColor()
{
    const QColor color = QColorDialog::getColor(
        m_strokeColor, this, tr("选择描边颜色"), QColorDialog::ShowAlphaChannel);
    if (!color.isValid())
        return;
    m_strokeColor = color;
    updateColorButton(m_strokeButton, m_strokeColor);
    styleControlsChanged();
}

void MainWindow::chooseFillColor()
{
    const QColor color = QColorDialog::getColor(
        m_fillColor.isValid() ? m_fillColor : m_strokeColor, this, tr("选择填充颜色"),
        QColorDialog::ShowAlphaChannel);
    if (!color.isValid())
        return;
    m_fillColor = color;
    m_fillCheck->setChecked(true);
    updateColorButton(m_fillButton, m_fillColor);
    styleControlsChanged();
}

void MainWindow::updateColorButton(QPushButton *button, const QColor &color)
{
    if (!button)
        return;

    QPixmap swatch(16, 16);
    swatch.fill(Qt::transparent);
    QPainter painter(&swatch);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (color.isValid()) {
        painter.setBrush(color);
        painter.setPen(QColor(QStringLiteral("#8b929c")));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 15.0, 15.0), 2.0, 2.0);
        button->setText(color.alpha() == 255 ? color.name(QColor::HexRgb).toUpper()
                                            : color.name(QColor::HexArgb).toUpper());
    } else {
        painter.setPen(QColor(QStringLiteral("#c3c8cf")));
        painter.setBrush(QColor(QStringLiteral("#f3f4f6")));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 15.0, 15.0), 2.0, 2.0);
        painter.drawLine(QPointF(2, 14), QPointF(14, 2));
        button->setText(tr("无"));
    }
    painter.end();
    button->setIcon(QIcon(swatch));
}

const Annotation *MainWindow::currentAnnotation() const
{
    if (!m_view || m_view->selectedAnnotationId().isEmpty())
        return nullptr;
    return m_view->annotations()->find(m_view->selectedAnnotationId());
}

QString MainWindow::baseDirectory() const
{
    if (m_store && m_store->isValid())
        return m_store->directory();
    if (m_view->hasDocument())
        return QFileInfo(m_view->document()->filePath()).absolutePath();
    return QDir::homePath();
}

QString MainWindow::noteTitleFor(const Annotation &annotation) const
{
    if (!annotation.note.title.trimmed().isEmpty())
        return annotation.note.title;
    return ellipsized(firstMarkdownLine(annotation.note.markdown), 40);
}

void MainWindow::showDiagnostics()
{
    QString cacheText = tr("未打开文档");
    QString documentText = tr("未打开文档");
    QString storeText = tr("未绑定");
    QString annotationText = QStringLiteral("0");
    QString pageText = QStringLiteral("—");

    if (m_view->hasDocument()) {
        const PdfDocument *document = m_view->document();
        documentText = QStringLiteral("%1\n  SHA-256: %2\n  大小: %3")
                           .arg(document->filePath(), document->sha256(),
                                humanSize(document->fileSize()));
        pageText = QStringLiteral("%1 / %2；当前 %3；旋转 %4°")
                       .arg(m_view->currentPage() + 1)
                       .arg(m_view->pageCount())
                       .arg(m_view->zoom() * 100.0, 0, 'f', 0)
                       .arg(m_view->rotation());
        annotationText = QString::number(m_view->annotations()->count());
        cacheText = tr("%1 / %2")
                        .arg(humanSize(m_view->renderer()->cacheBytes()),
                             humanSize(m_view->renderer()->cacheLimit()));
        if (m_storeReady && m_store) {
            storeText = QStringLiteral("%1\n  文档 ID: %2\n  schema: %3\n  状态: %4")
                            .arg(m_store->directory(), m_store->documentId())
                            .arg(m_store->manifest().schemaVersion)
                            .arg(m_store->isNewStore() ? tr("首次保存时写入")
                                                       : tr("已存在并校验通过"));
        }
    }

    const QString text = tr(
                             "PANO Phase 0 诊断信息\n"
                             "================================\n"
                             "应用版本: %1\n"
                             "Qt 版本: %2\n"
                             "平台: %3\n\n"
                             "文档\n%4\n\n"
                             "页面: %5\n"
                             "标注数量: %6\n\n"
                             "数据目录\n%7\n\n"
                             "页图缓存: %8\n"
                             "缓存策略: 只缓存原始页面像素，标注实时合成\n"
                             "渲染: 异步 QPdfPageRenderer，过期请求丢弃\n"
                             "网络: 核心功能不发起网络请求\n")
                             .arg(QCoreApplication::applicationVersion(), QString::fromLatin1(qVersion()),
                                  QGuiApplication::platformName(), documentText, pageText,
                                  annotationText, storeText, cacheText);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("诊断信息"));
    dialog.resize(720, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *textEdit = new QPlainTextEdit(&dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    layout->addWidget(textEdit, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(
        this, tr("快捷键"),
        tr("<p><b>文件</b><br>"
           "Ctrl+O 打开 PDF<br>"
           "Ctrl+S 保存标注<br>"
           "Ctrl+W 关闭文档<br>"
           "Ctrl+Q 退出</p>"
           "<p><b>阅读</b><br>"
           "Ctrl+滚轮 以光标为中心缩放<br>"
           "Shift+滚轮 水平滚动<br>"
           "Ctrl+0 适合整页；Ctrl+1 适合宽度；Ctrl+2 实际大小<br>"
           "Ctrl+R / Ctrl+Shift+R 旋转页面视图</p>"
           "<p><b>标注</b><br>"
           "V 选择；T 文本选择；H 高亮；U 下划线；S 删除线<br>"
           "L 直线；R 矩形；O 椭圆；P 画笔；M 荧光笔；E 橡皮擦；N 笔记图钉<br>"
           "Delete 删除选中标注；Ctrl+Z / Ctrl+Shift+Z 撤销/重做<br>"
           "双击标注可编辑笔记；Ctrl+Shift+A 区域截图到笔记</p>"
           "<p>工具快捷键仅在 PDF 视图获得焦点时生效，不会干扰笔记编辑。</p>"));
}

} // namespace pano
