#include "app/ReaderWorkspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
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
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QInputDialog>
#include <QRegularExpression>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPdfBookmarkModel>
#include <QPdfLink>
#include <QPdfSearchModel>
#include <QPdfDocument>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
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
#include "document/BookmarkStore.h"
#include "document/DocumentSession.h"
#include "pdf/PageRenderer.h"
#include "reader/PageLayout.h"
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

    m_searchModel = new QPdfSearchModel(this);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchTimer, &QTimer::timeout, this, &ReaderWorkspace::runSearch);

    buildUi();
    buildFloatingToolBars();
    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this] {
        if (!closeTransientUi())
            m_view->setTool(PdfView::Tool::Select);
    });
    if (ToolPresets *presets = m_session ? m_session->toolPresets() : nullptr) {
        m_view->setEraserRadius(presets->eraserRadius());
        if (m_eraserRadiusSpin)
            m_eraserRadiusSpin->setValue(presets->eraserRadius());
    }

    connect(m_searchModel, &QPdfSearchModel::countChanged, this,
            &ReaderWorkspace::refreshSearchResults);
    connect(m_searchModel, &QPdfSearchModel::statusChanged, this, [this](QPdfSearchModel::Status status) {
        if (status == QPdfSearchModel::Status::Finished && m_searchCount) {
            const int count = m_searchModel->count();
            m_searchCount->setText(count > 0 ? tr("%1 处").arg(count) : tr("无结果"));
            if (count == 0)
                showStatus(tr("未找到匹配文本"));
        }
    });

    connect(m_view, &PdfView::pageChanged, this, &ReaderWorkspace::onPageChanged);
    connect(m_view, &PdfView::zoomChanged, this, &ReaderWorkspace::onZoomChanged);
    connect(m_view, &PdfView::rotationChanged, this, &ReaderWorkspace::onRotationChanged);
    connect(m_view, &PdfView::navigated, this, &ReaderWorkspace::onNavigated);
    connect(m_view, &PdfView::layoutModeChanged, this,
            &ReaderWorkspace::onLayoutModeChanged);
    connect(m_view, &PdfView::firstPageAloneChanged, this,
            &ReaderWorkspace::onFirstPageAloneChanged);
    connect(m_view, &PdfView::bookmarkRequested, this, &ReaderWorkspace::addBookmarkAt);
    if (m_session && m_session->bookmarks()) {
        connect(m_session->bookmarks(), &BookmarkStore::bookmarksChanged, this,
                &ReaderWorkspace::refreshBookmarks);
    }
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
    connect(m_view, &PdfView::textSelectionChanged, this,
            &ReaderWorkspace::updateFloatingToolBars);
    connect(m_view, &PdfView::regionSelectionChanged, this,
            &ReaderWorkspace::updateFloatingToolBars);
    connect(m_view, &PdfView::readingAnchorChanged, this,
            &ReaderWorkspace::updateFloatingToolBars);
    connect(m_view, &PdfView::zoomChanged, this, &ReaderWorkspace::updateFloatingToolBars);
    connect(m_view, &PdfView::pageChanged, this, &ReaderWorkspace::updateFloatingToolBars);
    connect(m_view, &PdfView::rotationChanged, this, &ReaderWorkspace::updateFloatingToolBars);

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

    m_searchBar = buildSearchBar();
    root->addWidget(m_searchBar);
    m_searchBar->hide();

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
        m_splitter->setSizes({360, 720, 340});
    }
    root->addWidget(m_splitter, 1);

    m_bottomBar = buildBottomBar();
    root->addWidget(m_bottomBar);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        m_panelSaveTimer->start();
    });
}

void ReaderWorkspace::buildFloatingToolBars()
{
    QWidget *viewport = m_view->viewport();
    auto makeButton = [](QWidget *parent, const QString &text, const QString &tip,
                         const std::function<void()> &handler) {
        auto *button = new QToolButton(parent);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setCursor(Qt::PointingHandCursor);
        QObject::connect(button, &QToolButton::clicked, parent, handler);
        if (auto *layout = qobject_cast<QBoxLayout *>(parent->layout()))
            layout->addWidget(button);
        return button;
    };

    m_textFloat = new QWidget(viewport);
    m_textFloat->setObjectName(QStringLiteral("textSelectionToolbar"));
    auto *textLayout = new QHBoxLayout(m_textFloat);
    textLayout->setContentsMargins(6, 4, 6, 4);
    textLayout->setSpacing(2);
    textLayout->setSizeConstraint(QLayout::SetFixedSize);
    makeButton(m_textFloat, tr("高亮"), tr("用当前文字高亮预设创建标注"),
               [this] { applyTextFloatingAction(0); });
    makeButton(m_textFloat, tr("下划线"), tr("用当前下划线预设创建标注"),
               [this] { applyTextFloatingAction(1); });
    makeButton(m_textFloat, tr("删除线"), tr("用当前删除线预设创建标注"),
               [this] { applyTextFloatingAction(2); });
    makeButton(m_textFloat, tr("复制"), tr("复制选中的原文"),
               [this] { applyTextFloatingAction(3); });
    makeButton(m_textFloat, tr("笔记"), tr("基于选中文字创建带笔记的标注"),
               [this] { applyTextFloatingAction(4); });
    m_textFloat->setStyleSheet(QStringLiteral(
        "QWidget#textSelectionToolbar { background: palette(window); border: 1px solid palette(mid); "
        "border-radius: 6px; } QToolButton { padding: 3px 6px; }"));
    m_textFloat->hide();

    m_regionFloat = new QWidget(viewport);
    m_regionFloat->setObjectName(QStringLiteral("regionSelectionToolbar"));
    auto *regionLayout = new QHBoxLayout(m_regionFloat);
    regionLayout->setContentsMargins(6, 4, 6, 4);
    regionLayout->setSpacing(2);
    regionLayout->setSizeConstraint(QLayout::SetFixedSize);
    makeButton(m_regionFloat, tr("复制图片"), tr("把框选区域复制为图片"),
               [this] { applyRegionFloatingAction(0); });
    makeButton(m_regionFloat, tr("保存图片"), tr("把框选区域保存为 PNG"),
               [this] { applyRegionFloatingAction(1); });
    makeButton(m_regionFloat, tr("矩形"), tr("用当前矩形预设创建标注"),
               [this] { applyRegionFloatingAction(2); });
    makeButton(m_regionFloat, tr("区域高亮"), tr("创建无边框的半透明区域高亮"),
               [this] { applyRegionFloatingAction(3); });
    makeButton(m_regionFloat, tr("笔记"), tr("基于框选区域创建笔记"),
               [this] { applyRegionFloatingAction(4); });
    m_regionFloat->setStyleSheet(QStringLiteral(
        "QWidget#regionSelectionToolbar { background: palette(window); border: 1px solid palette(mid); "
        "border-radius: 6px; } QToolButton { padding: 3px 6px; }"));
    m_regionFloat->hide();
}

void ReaderWorkspace::updateFloatingToolBars()
{
    auto place = [this](QWidget *bar, const QRectF &viewRect) {
        if (!bar)
            return;
        QWidget *viewport = m_view->viewport();
        if (viewRect.isEmpty() || !QRectF(viewport->rect()).intersects(viewRect)) {
            bar->hide();
            return;
        }
        bar->adjustSize();
        const QPoint preferred = viewRect.bottomRight().toPoint() + QPoint(8, 8);
        int x = qBound(4, preferred.x(), qMax(4, viewport->width() - bar->width() - 4));
        int y = qBound(4, preferred.y(), qMax(4, viewport->height() - bar->height() - 4));
        bar->move(x, y);
        bar->raise();
        bar->show();
    };
    if (m_view->tool() == PdfView::Tool::Select || m_view->tool() == PdfView::Tool::TextSelect)
        place(m_textFloat, m_view->textSelectionViewRect());
    else
        m_textFloat->hide();
    place(m_regionFloat, m_view->regionViewRect());
}

void ReaderWorkspace::applyTextFloatingAction(int action)
{
    if (!m_view->hasTextSelection())
        return;
    if (action == 3) {
        QApplication::clipboard()->setText(m_view->selectedText());
        showStatus(tr("已复制选中文字"));
        return;
    }
    AnnotationType type = AnnotationType::Highlight;
    QString preset = QStringLiteral("text_highlight");
    if (action == 1) {
        type = AnnotationType::Underline;
        preset = QStringLiteral("underline");
    } else if (action == 2) {
        type = AnnotationType::StrikeOut;
        preset = QStringLiteral("strikeout");
    }
    const AnnotationStyle style = m_session->toolPresets()->style(preset);
    const QString id = m_view->createTextAnnotationFromSelection(type, style);
    if (id.isEmpty())
        return;
    if (action == 4)
        onNoteRequested(id);
    else
        showStatus(tr("已在选中文字上创建标注"));
}

void ReaderWorkspace::applyRegionFloatingAction(int action)
{
    if (!m_view->hasRegionSelection())
        return;
    if (action == 0) {
        QApplication::clipboard()->setImage(m_view->regionImage());
        showStatus(tr("已复制框选区域图片"));
        return;
    }
    if (action == 1) {
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString path = QFileDialog::getSaveFileName(
            this, tr("保存框选区域"), QDir(directory).filePath(QStringLiteral("pano-region.png")),
            tr("PNG 图片 (*.png)"));
        if (path.isEmpty())
            return;
        if (!m_view->regionImage().save(path, "PNG"))
            QMessageBox::warning(this, tr("保存失败"), tr("无法写入图片：%1").arg(path));
        else
            showStatus(tr("已保存框选区域"));
        return;
    }
    QString preset = QStringLiteral("rectangle");
    AnnotationStyle style = m_session->toolPresets()->style(preset);
    bool highlighter = false;
    if (action == 3) {
        highlighter = true;
        style.fillColor = QColor(QStringLiteral("#FFE066"));
        style.opacity = 0.45;
        style.blend = BlendMode::Multiply;
        style.strokeEnabled = false;
    } else if (action == 4) {
        highlighter = true;
        style.fillColor = QColor(QStringLiteral("#FFE066"));
        style.opacity = 0.45;
        style.blend = BlendMode::Multiply;
        style.strokeEnabled = false;
    }
    QImage regionImage;
    QString imageRelative;
    if (action == 4 && m_session) {
        regionImage = m_view->regionImage();
        if (!regionImage.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            regionImage.save(&buffer, "PNG");
            QString error;
            if (!m_session->writeAsset(QStringLiteral("capture"), QStringLiteral("png"), bytes,
                                       &imageRelative, &error))
                showStatus(error);
        }
    }
    const QString id = m_view->createRegionAnnotation(highlighter, style);
    if (id.isEmpty())
        return;
    if (action == 4) {
        onNoteRequested(id);
        if (!imageRelative.isEmpty())
            m_notePanel->insertMarkdown(QStringLiteral("![区域截图](%1)\n").arg(imageRelative));
    } else {
        showStatus(action == 3 ? tr("已创建区域高亮") : tr("已创建矩形标注"));
    }
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
    addTool(PdfView::Tool::RegionSelect, QStringLiteral("scan"), tr("框选"));
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
    m_widthSpin->setKeyboardTracking(true);
    widthLayout->addWidget(m_widthSpin);
    connect(m_widthSpin, &QDoubleSpinBox::valueChanged, this, [this] { scheduleStyleApply(); });
    layout->addWidget(m_widthWidget);

    m_lineStyleWidget = new QWidget(m_contextWidget);
    auto *lineStyleLayout = new QHBoxLayout(m_lineStyleWidget);
    lineStyleLayout->setContentsMargins(0, 0, 0, 0);
    lineStyleLayout->setSpacing(4);
    lineStyleLayout->addWidget(new QLabel(tr("线型"), m_lineStyleWidget));
    m_lineStyleCombo = new QComboBox(m_lineStyleWidget);
    for (LineStyle style : {LineStyle::Solid, LineStyle::Dash, LineStyle::Dot,
                            LineStyle::DashDot, LineStyle::DashDotDot}) {
        m_lineStyleCombo->addItem(lineStyleDisplayName(style), static_cast<int>(style));
    }
    lineStyleLayout->addWidget(m_lineStyleCombo);
    m_strokeCheck = new QCheckBox(tr("描边"), m_lineStyleWidget);
    m_strokeCheck->setChecked(true);
    lineStyleLayout->addWidget(m_strokeCheck);
    connect(m_strokeCheck, &QCheckBox::toggled, this, [this] { scheduleStyleApply(); });
    connect(m_lineStyleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { scheduleStyleApply(); });
    layout->addWidget(m_lineStyleWidget);

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

    m_eraserWidget = new QWidget(m_contextWidget);
    auto *eraserLayout = new QHBoxLayout(m_eraserWidget);
    eraserLayout->setContentsMargins(0, 0, 0, 0);
    eraserLayout->setSpacing(4);
    eraserLayout->addWidget(new QLabel(tr("橡皮半径"), m_eraserWidget));
    m_eraserRadiusSpin = new QSpinBox(m_eraserWidget);
    m_eraserRadiusSpin->setRange(4, 25);
    m_eraserRadiusSpin->setSuffix(tr(" px"));
    eraserLayout->addWidget(m_eraserRadiusSpin);
    connect(m_eraserRadiusSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int radius) {
                m_view->setEraserRadius(radius);
                if (m_session && m_session->settings()) {
                    m_session->settings()->toolPresets()->setEraserRadius(radius);
                    m_session->settings()->save();
                }
            });
    layout->addWidget(m_eraserWidget);
    m_eraserWidget->hide();
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
    for (int percent : {25, 50, 75, 100, 125, 150, 200, 300, 400})
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

    layout->addSpacing(6);
    m_layoutCombo = new QComboBox(bar);
    m_layoutCombo->setFixedWidth(112);
    m_layoutCombo->addItem(tr("单页"), static_cast<int>(PageLayoutMode::SinglePage));
    m_layoutCombo->addItem(tr("连续单页"), static_cast<int>(PageLayoutMode::ContinuousSingle));
    m_layoutCombo->addItem(tr("双页"), static_cast<int>(PageLayoutMode::FacingPages));
    m_layoutCombo->addItem(tr("连续双页"), static_cast<int>(PageLayoutMode::ContinuousFacing));
    m_layoutCombo->setCurrentIndex(1);
    layout->addWidget(m_layoutCombo);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            m_view->setLayoutMode(static_cast<PageLayoutMode>(m_layoutCombo->itemData(index).toInt()));
    });
    m_firstPageAloneCheck = new QCheckBox(tr("首页单独"), bar);
    m_firstPageAloneCheck->setChecked(true);
    layout->addWidget(m_firstPageAloneCheck);
    connect(m_firstPageAloneCheck, &QCheckBox::toggled, this,
            &ReaderWorkspace::onFirstPageAloneChanged);

    m_backAction = new QAction(QStringLiteral("←"), this);
    m_backAction->setToolTip(tr("后退 (Alt+Left)"));
    m_backAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_backAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_backAction);
    connect(m_backAction, &QAction::triggered, this, &ReaderWorkspace::navigateBack);
    m_forwardAction = new QAction(QStringLiteral("→"), this);
    m_forwardAction->setToolTip(tr("前进 (Alt+Right)"));
    m_forwardAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    m_forwardAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_forwardAction);
    connect(m_forwardAction, &QAction::triggered, this, &ReaderWorkspace::navigateForward);
    m_searchAction = new QAction(tr("搜索"), this);
    m_searchAction->setShortcut(QKeySequence::Find);
    m_searchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_searchAction);
    connect(m_searchAction, &QAction::triggered, this, &ReaderWorkspace::toggleSearchBar);
    m_bookmarkAction = new QAction(tr("书签"), this);
    m_bookmarkAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_bookmarkAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_bookmarkAction);
    connect(m_bookmarkAction, &QAction::triggered, this, &ReaderWorkspace::addBookmarkHere);
    m_fullscreenAction = new QAction(tr("全屏"), this);
    m_fullscreenAction->setToolTip(tr("全屏 (F11)"));
    addAction(m_fullscreenAction);
    connect(m_fullscreenAction, &QAction::triggered, this, [this] { emit fullscreenRequested(); });

    auto addActionButton = [&](QAction *action) {
        auto *button = new QToolButton(bar);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        layout->addWidget(button);
    };
    addActionButton(m_backAction);
    addActionButton(m_forwardAction);
    auto *searchButton = new QToolButton(bar);
    searchButton->setDefaultAction(m_searchAction);
    searchButton->setIcon(IconManager::instance()->icon(QStringLiteral("search")));
    searchButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    searchButton->setAutoRaise(true);
    layout->addWidget(searchButton);
    auto *bookmarkButton = new QToolButton(bar);
    bookmarkButton->setDefaultAction(m_bookmarkAction);
    bookmarkButton->setIcon(IconManager::instance()->icon(QStringLiteral("bookmark")));
    bookmarkButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bookmarkButton->setAutoRaise(true);
    layout->addWidget(bookmarkButton);
    auto *fullButton = new QToolButton(bar);
    fullButton->setDefaultAction(m_fullscreenAction);
    fullButton->setText(QStringLiteral("⛶"));
    fullButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fullButton->setAutoRaise(true);
    layout->addWidget(fullButton);

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
    m_leftTabs->addTab(outlinePage, tr("目录"));

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
    m_leftTabs->addTab(m_thumbnailList, tr("页面"));

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
    m_leftTabs->addTab(annotationsPage, tr("标注"));
    m_leftTabs->addTab(buildBookmarkTab(), tr("书签"));

    m_leftTabs->setMinimumWidth(340);
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
    refreshBookmarks();
    if (m_searchModel)
        m_searchModel->setDocument(m_view->hasDocument() ? m_view->document()->pdf() : nullptr);
    if (m_history.isEmpty() && m_view->hasDocument()) {
        int page = m_view->currentPage();
        const QPointF anchor = m_view->viewportAnchor(&page);
        m_history.append({page, anchor});
        m_historyIndex = 0;
    }
    updateHistoryActions();
    if (m_firstPageAloneCheck)
        m_firstPageAloneCheck->setEnabled(m_view->layoutMode() == PageLayoutMode::FacingPages
                                         || m_view->layoutMode() == PageLayoutMode::ContinuousFacing);
    if (m_layoutCombo) {
        const QSignalBlocker blocker(m_layoutCombo);
        const int index = m_layoutCombo->findData(static_cast<int>(m_view->layoutMode()));
        if (index >= 0)
            m_layoutCombo->setCurrentIndex(index);
    }
    if (m_firstPageAloneCheck) {
        const QSignalBlocker blocker(m_firstPageAloneCheck);
        m_firstPageAloneCheck->setChecked(m_view->firstPageAlone());
    }
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
    flushPendingStyle();
    if (isEditingNote() && m_editingId != annotationId)
        commitNoteEdit();

    refreshAnnotationList();
    updateDetailPanel();
    updateNotePanel();
    updateContextToolBar();
    // Selection is deliberately not navigation.  The annotation list, outline,
    // thumbnails and explicit "reveal" actions call revealAnnotation().
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
    updateFloatingToolBars();
    flushPendingStyle();
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
    case PdfView::Tool::RegionSelect:
        return QStringLiteral("region_highlight");
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
        || tool == PdfView::Tool::RegionSelect || tool == PdfView::Tool::Eraser)
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
    if (m_lineStyleCombo)
        style.lineStyle = static_cast<LineStyle>(m_lineStyleCombo->currentData().toInt());
    if (m_strokeCheck)
        style.strokeEnabled = m_strokeCheck->isChecked();
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
    if (m_strokeCheck)
        m_strokeCheck->setChecked(style.strokeEnabled);
    if (m_lineStyleCombo) {
        const int index = m_lineStyleCombo->findData(static_cast<int>(style.lineStyle));
        if (index >= 0)
            m_lineStyleCombo->setCurrentIndex(index);
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
    if (m_syncingControls || m_applyingStyle)
        return;
    const PdfView::Tool tool = m_view->tool();
    m_styleTargetObject = tool == PdfView::Tool::Select && currentAnnotation();
    m_styleTargetId = m_styleTargetObject ? currentAnnotation()->id : QString();
    m_styleTargetTool = presetIdFor(tool);
    m_styleSnapshot = styleFromControls();
    m_styleSnapshotValid = true;
    if (m_styleTimer)
        m_styleTimer->start();
}

void ReaderWorkspace::applyStyleFromControls()
{
    if (m_syncingControls || m_applyingStyle)
        return;
    if (!m_styleSnapshotValid) {
        const PdfView::Tool tool = m_view->tool();
        m_styleTargetObject = tool == PdfView::Tool::Select && currentAnnotation();
        m_styleTargetId = m_styleTargetObject ? currentAnnotation()->id : QString();
        m_styleTargetTool = presetIdFor(tool);
        m_styleSnapshot = styleFromControls();
        m_styleSnapshotValid = true;
    }
    flushPendingStyle();
}

void ReaderWorkspace::schedulePresetSave()
{
    if (m_presetTimer)
        m_presetTimer->start();
}

void ReaderWorkspace::flushPendingStyle()
{
    if (!m_styleSnapshotValid)
        return;
    if (m_styleTimer)
        m_styleTimer->stop();

    const QString targetId = m_styleTargetId;
    const QString targetTool = m_styleTargetTool;
    const AnnotationStyle style = m_styleSnapshot;
    const bool objectTarget = m_styleTargetObject;
    m_styleSnapshotValid = false;
    m_styleTargetId.clear();
    m_styleTargetTool.clear();

    m_applyingStyle = true;
    if (objectTarget && !targetId.isEmpty()) {
        m_view->applyStyleToAnnotation(targetId, style);
        if (m_view->selectedAnnotationId() == targetId) {
            updateDetailPanel();
            updateContextToolBar();
        }
    } else if (!targetTool.isEmpty() && m_session && m_session->toolPresets()) {
        m_session->toolPresets()->setStyle(targetTool, style);
        if (presetIdFor(m_view->tool()) == targetTool)
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
    if (m_eraserRadiusSpin && m_view)
        m_eraserRadiusSpin->setValue(m_view->eraserRadius());

    bool color = false;
    bool fill = false;
    bool width = false;
    bool opacity = false;
    bool blend = false;
    bool lineStyle = false;
    bool eraser = false;
    QString hint;
    if (tool == PdfView::Tool::TextSelect || tool == PdfView::Tool::Hand) {
        hint = tool == PdfView::Tool::Hand ? tr("拖动画布滚动手感：按住左键移动页面。")
                                           : tr("拖选文字；Ctrl+C 复制。");
    } else if (tool == PdfView::Tool::Eraser) {
        hint = tr("按住拖动连续擦除；带笔记的对象会先确认。");
        eraser = true;
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
        lineStyle = true;
        opacity = true;
        blend = true;
    } else if (tool == PdfView::Tool::Line
               || (objectMode && annotation->type == AnnotationType::Line)) {
        hint = objectMode ? tr("编辑选中直线") : tr("拖动绘制直线");
        color = true;
        width = true;
        lineStyle = true;
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
    if (m_lineStyleWidget)
        m_lineStyleWidget->setVisible(lineStyle);
    m_opacityWidget->setVisible(opacity);
    m_blendWidget->setVisible(blend);
    if (m_eraserWidget)
        m_eraserWidget->setVisible(eraser);
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


QWidget *ReaderWorkspace::buildSearchBar()
{
    auto *bar = new QWidget(this);
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(8, 4, 8, 4);
    outer->setSpacing(4);

    auto *row = new QHBoxLayout;
    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setObjectName(QStringLiteral("readerSearchEdit"));
    m_searchEdit->setPlaceholderText(tr("在当前 PDF 中搜索文本…"));
    m_searchEdit->setClearButtonEnabled(true);
    row->addWidget(m_searchEdit, 1);
    m_searchCount = new QLabel(bar);
    m_searchCount->setMinimumWidth(64);
    row->addWidget(m_searchCount);
    auto *prev = new QToolButton(bar);
    prev->setText(QStringLiteral("↑"));
    prev->setToolTip(tr("上一个结果"));
    prev->setAutoRaise(true);
    row->addWidget(prev);
    auto *next = new QToolButton(bar);
    next->setText(QStringLiteral("↓"));
    next->setToolTip(tr("下一个结果"));
    next->setAutoRaise(true);
    row->addWidget(next);
    auto *close = new QToolButton(bar);
    close->setIcon(IconManager::instance()->icon(QStringLiteral("x")));
    close->setAutoRaise(true);
    close->setToolTip(tr("关闭搜索"));
    row->addWidget(close);
    outer->addLayout(row);

    m_searchResults = new QListWidget(bar);
    m_searchResults->setObjectName(QStringLiteral("readerSearchResults"));
    m_searchResults->setMaximumHeight(130);
    m_searchResults->setWordWrap(false);
    outer->addWidget(m_searchResults);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this] {
        m_searchTimer->start();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ReaderWorkspace::nextSearchResult);
    connect(prev, &QToolButton::clicked, this, &ReaderWorkspace::previousSearchResult);
    connect(next, &QToolButton::clicked, this, &ReaderWorkspace::nextSearchResult);
    connect(close, &QToolButton::clicked, this, &ReaderWorkspace::toggleSearchBar);
    connect(m_searchResults, &QListWidget::itemActivated, this,
            &ReaderWorkspace::activateSearchResult);
    connect(m_searchResults, &QListWidget::itemClicked, this,
            &ReaderWorkspace::activateSearchResult);
    return bar;
}

QWidget *ReaderWorkspace::buildBookmarkTab()
{
    auto *page = new QWidget(m_leftTabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    m_bookmarkList = new QListWidget(page);
    m_bookmarkList->setObjectName(QStringLiteral("readerBookmarkList"));
    m_bookmarkList->setWordWrap(true);
    layout->addWidget(m_bookmarkList, 1);

    auto *buttons = new QHBoxLayout;
    m_bookmarkAddButton = new QPushButton(tr("添加"), page);
    m_bookmarkDeleteButton = new QPushButton(tr("删除"), page);
    auto *sortButton = new QPushButton(tr("按页排序"), page);
    buttons->addWidget(m_bookmarkAddButton);
    buttons->addWidget(m_bookmarkDeleteButton);
    buttons->addWidget(sortButton);
    layout->addLayout(buttons);
    connect(m_bookmarkAddButton, &QPushButton::clicked, this,
            &ReaderWorkspace::addBookmarkHere);
    connect(m_bookmarkDeleteButton, &QPushButton::clicked, this,
            &ReaderWorkspace::deleteSelectedBookmark);
    connect(sortButton, &QPushButton::clicked, this, [this] {
        if (m_session && m_session->bookmarks()->sortByPage()) {
            m_session->saveBookmarks();
            refreshBookmarks();
        }
    });
    connect(m_bookmarkList, &QListWidget::itemActivated, this,
            &ReaderWorkspace::activateBookmark);
    connect(m_bookmarkList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::activateBookmark);
    m_bookmarkList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_bookmarkList, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QListWidgetItem *item = m_bookmarkList->itemAt(pos);
                if (!item)
                    return;
                m_bookmarkList->setCurrentItem(item);
                const QString id = item->data(Qt::UserRole).toString();
                QMenu menu(this);
                menu.addAction(tr("跳转"), this, [this, id] {
                    if (const Bookmark *bookmark = m_session->bookmarks()->find(id))
                        m_view->navigateTo(bookmark->pageIndex, bookmark->position,
                                           PdfView::NavigationAlignment::Center,
                                           PdfView::NavigationReason::Bookmark);
                });
                menu.addAction(tr("重命名"), this, [this] {
                    renameSelectedBookmark();
                });
                menu.addSeparator();
                menu.addAction(tr("删除"), this, [this] {
                    deleteSelectedBookmark();
                });
                menu.exec(m_bookmarkList->viewport()->mapToGlobal(pos));
            });
    return page;
}

void ReaderWorkspace::toggleSearchBar()
{
    if (!m_searchBar)
        return;
    if (m_searchBar->isVisible()) {
        m_searchBar->hide();
        if (m_searchModel)
            m_searchModel->setSearchString(QString());
        if (m_view)
            m_view->clearSearchHighlights();
        return;
    }
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
}

void ReaderWorkspace::runSearch()
{
    if (!m_searchModel || !m_searchEdit)
        return;
    const QString text = m_searchEdit->text().trimmed();
    if (text.isEmpty()) {
        m_searchModel->setSearchString(QString());
        if (m_searchResults)
            m_searchResults->clear();
        if (m_searchCount)
            m_searchCount->clear();
        if (m_view)
            m_view->clearSearchHighlights();
        return;
    }
    if (m_searchModel->searchString() == text && m_searchModel->count() > 0) {
        refreshSearchResults();
        return;
    }
    if (m_searchResults)
        m_searchResults->clear();
    if (m_searchCount)
        m_searchCount->clear();
    if (m_view)
        m_view->clearSearchHighlights();
    m_searchModel->setSearchString(text);
}

void ReaderWorkspace::refreshSearchResults()
{
    if (!m_searchModel || !m_searchResults || !m_searchEdit)
        return;
    if (m_searchModel->searchString() != m_searchEdit->text().trimmed())
        return;
    const int count = m_searchModel->count();
    m_searchResults->clear();
    QHash<int, QVector<QRectF>> highlights;
    int actual = 0;
    for (int index = 0; index < count && actual < 4000; ++index) {
        const QPdfLink link = m_searchModel->resultAtIndex(index);
        QString excerpt = (link.contextBefore() + link.contextAfter()).simplified();
        excerpt.replace(QChar::ObjectReplacementCharacter, QLatin1Char(' '));
        excerpt.replace(QChar(0x23CE), QLatin1Char(' '));
        excerpt.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")),
                        QStringLiteral(" "));
        excerpt = excerpt.simplified();
        auto *item = new QListWidgetItem(tr("第 %1 页  %2").arg(link.page() + 1).arg(excerpt.left(90)),
                                         m_searchResults);
        item->setData(Qt::UserRole, index);
        for (const QRectF &rect : link.rectangles())
            highlights[link.page()].append(rect);
        ++actual;
    }
    if (m_searchCount)
        m_searchCount->setText(count > 0 ? tr("%1 处").arg(count) : tr("无结果"));
    if (m_view)
        m_view->setSearchHighlights(highlights, -1, QRectF());
    if (m_searchResults->count() > 0 && m_searchResults->currentRow() < 0)
        m_searchResults->setCurrentRow(0);
}

void ReaderWorkspace::activateSearchResult(QListWidgetItem *item)
{
    if (!item || !m_searchModel)
        return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_searchModel->count())
        return;
    const QPdfLink link = m_searchModel->resultAtIndex(index);
    QPointF point;
    if (!link.rectangles().isEmpty())
        point = link.rectangles().first().center();
    m_view->navigateTo(link.page(), point, PdfView::NavigationAlignment::Center,
                       PdfView::NavigationReason::Search);
    if (m_searchResults->currentItem() != item)
        m_searchResults->setCurrentItem(item);
}

void ReaderWorkspace::nextSearchResult()
{
    if (!m_searchResults || m_searchResults->count() == 0)
        return;
    int row = m_searchResults->currentRow() + 1;
    if (row >= m_searchResults->count())
        row = 0;
    m_searchResults->setCurrentRow(row);
    activateSearchResult(m_searchResults->item(row));
}

void ReaderWorkspace::previousSearchResult()
{
    if (!m_searchResults || m_searchResults->count() == 0)
        return;
    int row = m_searchResults->currentRow() - 1;
    if (row < 0)
        row = m_searchResults->count() - 1;
    m_searchResults->setCurrentRow(row);
    activateSearchResult(m_searchResults->item(row));
}

void ReaderWorkspace::refreshBookmarks()
{
    if (!m_bookmarkList)
        return;
    m_bookmarkList->clear();
    if (!m_session || !m_session->bookmarks())
        return;
    const QVector<Bookmark> &values = m_session->bookmarks()->bookmarks();
    for (const Bookmark &bookmark : values) {
        auto *item = new QListWidgetItem(
            tr("%1\n第 %2 页").arg(bookmark.title, QString::number(bookmark.pageIndex + 1)),
            m_bookmarkList);
        item->setData(Qt::UserRole, bookmark.id);
    }
    m_bookmarkDeleteButton->setEnabled(m_bookmarkList->count() > 0);
}

void ReaderWorkspace::activateBookmark(QListWidgetItem *item)
{
    if (!item || !m_session)
        return;
    const Bookmark *bookmark = m_session->bookmarks()->find(item->data(Qt::UserRole).toString());
    if (!bookmark)
        return;
    m_view->navigateTo(bookmark->pageIndex, bookmark->position,
                       PdfView::NavigationAlignment::Center,
                       PdfView::NavigationReason::Bookmark);
}

void ReaderWorkspace::addBookmarkAt(int pageIndex, const QPointF &pagePoint)
{
    if (!m_session || pageIndex < 0)
        return;
    m_session->bookmarks()->addBookmark(QString(), pageIndex, pagePoint);
    if (!m_session->saveBookmarks()) {
        showStatus(tr("书签保存失败"));
        return;
    }
    refreshBookmarks();
    showStatus(tr("已添加书签"));
}

void ReaderWorkspace::addBookmarkHere()
{
    if (!m_view || !m_view->hasDocument())
        return;
    int page = m_view->currentPage();
    QPointF anchor = m_view->viewportAnchor(&page);
    if (page < 0)
        page = m_view->currentPage();
    addBookmarkAt(page, anchor);
}

void ReaderWorkspace::renameSelectedBookmark()
{
    if (!m_bookmarkList || !m_session)
        return;
    QListWidgetItem *item = m_bookmarkList->currentItem();
    if (!item)
        return;
    const QString id = item->data(Qt::UserRole).toString();
    const Bookmark *bookmark = m_session->bookmarks()->find(id);
    if (!bookmark)
        return;
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("重命名书签"), tr("名称"),
                                                QLineEdit::Normal, bookmark->title, &ok);
    if (!ok || title.trimmed().isEmpty())
        return;
    if (m_session->bookmarks()->renameBookmark(id, title.trimmed())) {
        m_session->saveBookmarks();
        refreshBookmarks();
    }
}

void ReaderWorkspace::deleteSelectedBookmark()
{
    if (!m_bookmarkList || !m_session)
        return;
    QListWidgetItem *item = m_bookmarkList->currentItem();
    if (!item)
        return;
    if (QMessageBox::question(this, tr("删除书签"), tr("确定删除这个书签吗？"))
        != QMessageBox::Yes)
        return;
    if (m_session->bookmarks()->removeBookmark(item->data(Qt::UserRole).toString())) {
        m_session->saveBookmarks();
        refreshBookmarks();
    }
}

void ReaderWorkspace::onNavigated(int pageIndex, const QPointF &pagePoint, int reason)
{
    if (reason == static_cast<int>(PdfView::NavigationReason::History)
        || reason == static_cast<int>(PdfView::NavigationReason::Restore)
        || reason == static_cast<int>(PdfView::NavigationReason::Internal))
        return;
    if (m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    m_history.append({pageIndex, pagePoint});
    while (m_history.size() > 100)
        m_history.removeFirst();
    m_historyIndex = m_history.size() - 1;
    updateHistoryActions();
}

void ReaderWorkspace::navigateBack()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    const NavPoint point = m_history.at(m_historyIndex);
    m_view->navigateTo(point.page, point.point, PdfView::NavigationAlignment::Center,
                       PdfView::NavigationReason::History);
    updateHistoryActions();
}

void ReaderWorkspace::navigateForward()
{
    if (m_historyIndex < 0 || m_historyIndex + 1 >= m_history.size())
        return;
    ++m_historyIndex;
    const NavPoint point = m_history.at(m_historyIndex);
    m_view->navigateTo(point.page, point.point, PdfView::NavigationAlignment::Center,
                       PdfView::NavigationReason::History);
    updateHistoryActions();
}

void ReaderWorkspace::updateHistoryActions()
{
    if (m_backAction)
        m_backAction->setEnabled(m_historyIndex > 0);
    if (m_forwardAction)
        m_forwardAction->setEnabled(m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size());
}

void ReaderWorkspace::onLayoutModeChanged(PageLayoutMode mode)
{
    if (m_layoutCombo) {
        const QSignalBlocker blocker(m_layoutCombo);
        const int index = m_layoutCombo->findData(static_cast<int>(mode));
        if (index >= 0)
            m_layoutCombo->setCurrentIndex(index);
    }
    if (m_firstPageAloneCheck) {
        m_firstPageAloneCheck->setEnabled(m_view->layoutMode() == PageLayoutMode::FacingPages
                                         || m_view->layoutMode() == PageLayoutMode::ContinuousFacing);
        const QSignalBlocker blocker(m_firstPageAloneCheck);
        m_firstPageAloneCheck->setChecked(m_view->firstPageAlone());
    }
    updateBottomStatus();
}

void ReaderWorkspace::onFirstPageAloneChanged(bool firstAlone)
{
    if (!m_view)
        return;
    if (m_view->firstPageAlone() != firstAlone)
        m_view->setFirstPageAlone(firstAlone);
}

void ReaderWorkspace::setFullscreenMode(bool fullscreen)
{
    if (m_fullscreen == fullscreen)
        return;
    int page = m_view->currentPage();
    const QPointF anchor = m_view->viewportAnchor(&page);
    if (fullscreen) {
        m_fullscreenSplitterSizes = m_splitter->sizes();
        m_fullscreenLeftVisible = m_leftDock && m_leftDock->isVisible();
        m_fullscreenRightVisible = m_rightDock && m_rightDock->isVisible();
        m_primaryToolBar->hide();
        m_contextToolBar->hide();
        m_searchBar->hide();
        if (m_leftDock)
            m_leftDock->hide();
        if (m_rightDock)
            m_rightDock->hide();
        if (m_bottomBar)
            m_bottomBar->hide();
    } else {
        m_primaryToolBar->show();
        m_contextToolBar->show();
        if (!m_fullscreenSplitterSizes.isEmpty())
            m_splitter->setSizes(m_fullscreenSplitterSizes);
        if (m_leftDock)
            m_leftDock->setVisible(m_fullscreenLeftVisible);
        if (m_rightDock)
            m_rightDock->setVisible(m_fullscreenRightVisible);
        if (m_bottomBar)
            m_bottomBar->show();
        m_view->restoreViewportAnchor(page, anchor);
    }
    m_fullscreen = fullscreen;
}


bool ReaderWorkspace::closeTransientUi()
{
    if (!m_view)
        return false;
    if (m_view->tool() != PdfView::Tool::Select) {
        m_view->setTool(PdfView::Tool::Select);
        return true;
    }
    if (m_view->hasTextSelection() || m_view->hasRegionSelection()) {
        m_view->clearTemporarySelections();
        return true;
    }
    if (m_view->handleEscape())
        return true;
    if (m_searchBar && m_searchBar->isVisible()) {
        toggleSearchBar();
        return true;
    }
    return false;
}

} // namespace pano
