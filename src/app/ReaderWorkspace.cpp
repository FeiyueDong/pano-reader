#include "app/ReaderWorkspace.h"

#include <QAction>
#include <functional>
#include <QActionGroup>
#include <QApplication>
#include <algorithm>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
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
#include <QScrollBar>
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
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "annotation/AnnotationRenderer.h"
#include "common/TimeUtils.h"
#include "app/IconManager.h"
#include "app/NoteEditDialog.h"
#include "app/RegionCaptureOverlay.h"
#include "document/BookmarkStore.h"
#include "document/DocumentSession.h"
#include "note/MarkdownPreview.h"
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
    m_view->setNoteQuery([this](const QString &annotationId) {
        return m_session && m_session->hasLinkedNote(annotationId);
    });
    m_noteDialog = nullptr;
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
    if (ToolPresets *presets = m_session ? m_session->toolPresets() : nullptr) {
        m_view->setEraserRadius(presets->eraserRadius());
        if (m_eraserRadiusSpin)
            m_eraserRadiusSpin->setValue(presets->eraserRadius());
    }

    connect(m_searchModel, &QPdfSearchModel::countChanged, this, [this]() {
        if (m_searchCount && m_searchModel && m_searchEdit
            && !m_searchModel->searchString().isEmpty()
            && m_searchModel->searchString() == m_searchEdit->text().trimmed())
            m_searchCount->setText(tr("搜索中… %1").arg(m_searchModel->count()));
    });
    connect(m_searchModel, &QPdfSearchModel::statusChanged, this, [this](QPdfSearchModel::Status status) {
        if (status == QPdfSearchModel::Status::Finished)
            refreshSearchResults();
    });

    connect(m_view, &PdfView::pageChanged, this, &ReaderWorkspace::onPageChanged);
    connect(m_view, &PdfView::zoomChanged, this, &ReaderWorkspace::onZoomChanged);
    connect(m_view, &PdfView::rotationChanged, this, &ReaderWorkspace::onRotationChanged);
    connect(m_view, &PdfView::navigated, this, &ReaderWorkspace::onNavigated);
    connect(m_view, &PdfView::aboutToNavigate, this, &ReaderWorkspace::onAboutToNavigate);
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
    connect(m_view, &PdfView::notePointRequested, this, &ReaderWorkspace::onNotePointRequested);
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

    connect(m_captureOverlay, &RegionCaptureOverlay::regionSelected, this,
            &ReaderWorkspace::finishCapture);
    connect(m_captureOverlay, &RegionCaptureOverlay::cancelled, this, [this] {
        m_view->setSelectionOverlayVisible(true);
    });

    connect(m_thumbRenderer, &PageRenderer::pageReady, this,
            &ReaderWorkspace::onThumbnailRendered);
    connect(m_thumbRenderer, &PageRenderer::pageFailed, this,
            &ReaderWorkspace::onThumbnailFailed);
    connect(m_thumbRenderer, &PageRenderer::cacheChanged, this, [this] {
        if (m_thumbRefreshTimer)
            m_thumbRefreshTimer->start();
    });

    m_thumbRefreshTimer = new QTimer(this);
    m_thumbRefreshTimer->setSingleShot(true);
    m_thumbRefreshTimer->setInterval(250);
    connect(m_thumbRefreshTimer, &QTimer::timeout, this, [this] { requestNearbyThumbnails(); });
    if (m_thumbnailList) {
        connect(m_thumbnailList->verticalScrollBar(), &QScrollBar::valueChanged,
                m_thumbRefreshTimer, [this] { m_thumbRefreshTimer->start(); });
    }

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
    blockSignals(true);
    if (m_view) {
        m_view->blockSignals(true);
        disconnect(m_view, nullptr, this, nullptr);
    }
    if (m_noteDialog) {
        disconnect(m_noteDialog, nullptr, this, nullptr);
        m_noteDialog->deleteLater();
        m_noteDialog = nullptr;
    }
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
        m_splitter->setSizes({250, 900, 340});
    }
    root->addWidget(m_splitter, 1);

    m_bottomBar = buildBottomBar();
    root->addWidget(m_bottomBar);

    m_exitFullscreenButton = new QToolButton(m_view);
    m_exitFullscreenButton->setObjectName(QStringLiteral("readerExitFullscreen"));
    m_exitFullscreenButton->setIcon(IconManager::instance()->icon(QStringLiteral("x")));
    m_exitFullscreenButton->setText(tr("退出全屏"));
    m_exitFullscreenButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_exitFullscreenButton->setAutoRaise(false);
    m_exitFullscreenButton->setToolTip(tr("退出全屏 (F11 / Esc)"));
    m_exitFullscreenButton->setStyleSheet(QStringLiteral(
        "QToolButton#readerExitFullscreen { background: rgba(20, 24, 32, 190);"
        " color: white; border: 1px solid rgba(255,255,255,90); border-radius: 6px;"
        " padding: 4px 10px; } QToolButton#readerExitFullscreen:hover {"
        " background: rgba(47, 111, 237, 220); }"));
    m_exitFullscreenButton->hide();
    connect(m_exitFullscreenButton, &QToolButton::clicked, this,
            [this] { emit fullscreenRequested(); });

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
            if (m_noteDialog)
                m_noteDialog->insertAssetMarkdown(imageRelative, tr("区域截图"));
    } else {
        showStatus(action == 3 ? tr("已创建区域高亮") : tr("已创建矩形标注"));
    }
}

QToolBar *ReaderWorkspace::buildPrimaryToolBar()
{
    auto *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("readerPrimaryToolBar"));
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

    // Reading navigation: history, previous/next page, page input and total.
    m_backAction = new QAction(tr("导航后退"), this);
    m_backAction->setIcon(IconManager::instance()->icon(QStringLiteral("chevron-right")));
    m_backAction->setToolTip(tr("导航后退 (Alt+Left)"));
    m_backAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Left));
    m_backAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_backAction);
    connect(m_backAction, &QAction::triggered, this, &ReaderWorkspace::navigateBack);
    m_forwardAction = new QAction(tr("导航前进"), this);
    m_forwardAction->setIcon(IconManager::instance()->icon(QStringLiteral("chevron-right")));
    m_forwardAction->setToolTip(tr("导航前进 (Alt+Right)"));
    m_forwardAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Right));
    m_forwardAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(m_forwardAction);
    connect(m_forwardAction, &QAction::triggered, this, &ReaderWorkspace::navigateForward);
    // Mirror the chevron for Back so the two history buttons are unambiguous.
    m_backAction->setIcon(QIcon(IconManager::instance()->icon(QStringLiteral("chevron-right"))
                                    .pixmap(18, 18).transformed(QTransform::fromScale(-1, 1))));
    bar->addAction(m_backAction);
    bar->addAction(m_forwardAction);

    m_prevPageButton = new QToolButton(bar);
    m_prevPageButton->setText(QStringLiteral("‹"));
    m_prevPageButton->setToolTip(tr("上一页 / 上一组 (PageUp)"));
    m_prevPageButton->setAutoRaise(true);
    connect(m_prevPageButton, &QToolButton::clicked, this, &ReaderWorkspace::goToPreviousPage);
    bar->addWidget(m_prevPageButton);

    m_pageSpin = new QLineEdit(bar);
    m_pageSpin->setObjectName(QStringLiteral("readerPageEdit"));
    m_pageValidator = new QIntValidator(1, 1, m_pageSpin);
    m_pageSpin->setValidator(m_pageValidator);
    m_pageSpin->setAlignment(Qt::AlignCenter);
    m_pageSpin->setFixedWidth(54);
    m_pageSpin->setToolTip(tr("输入页码后回车"));
    connect(m_pageSpin, &QLineEdit::returnPressed, this, [this] {
        bool ok = false;
        const int page = m_pageSpin->text().toInt(&ok);
        if (ok)
            goToPageFromSpin(page);
        else
            onPageChanged(m_view ? m_view->currentPage() : 0);
    });
    bar->addWidget(m_pageSpin);
    m_pageTotalLabel = new QLabel(QStringLiteral("/ 1"), bar);
    m_pageTotalLabel->setMinimumWidth(34);
    bar->addWidget(m_pageTotalLabel);

    m_nextPageButton = new QToolButton(bar);
    m_nextPageButton->setText(QStringLiteral("›"));
    m_nextPageButton->setToolTip(tr("下一页 / 下一组 (PageDown)"));
    m_nextPageButton->setAutoRaise(true);
    connect(m_nextPageButton, &QToolButton::clicked, this, &ReaderWorkspace::goToNextPage);
    bar->addWidget(m_nextPageButton);
    bar->addSeparator();

    // Zoom group.
    auto *zoomOut = bar->addAction(IconManager::instance()->icon(QStringLiteral("zoom-out")),
                                   tr("缩小"));
    connect(zoomOut, &QAction::triggered, m_view, &PdfView::zoomOut);
    m_zoomCombo = new QComboBox(bar);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setFixedWidth(84);
    for (int percent : {25, 50, 75, 100, 125, 150, 200, 300, 400})
        m_zoomCombo->addItem(QStringLiteral("%1%").arg(percent), percent);
    connect(m_zoomCombo, &QComboBox::activated, this, &ReaderWorkspace::zoomEdited);
    if (m_zoomCombo->lineEdit())
        connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this,
                &ReaderWorkspace::zoomEdited);
    bar->addWidget(m_zoomCombo);
    auto *zoomIn = bar->addAction(IconManager::instance()->icon(QStringLiteral("zoom-in")),
                                  tr("放大"));
    connect(zoomIn, &QAction::triggered, m_view, &PdfView::zoomIn);
    auto *fitWidth = bar->addAction(tr("适宽"), m_view, &PdfView::fitWidth);
    fitWidth->setToolTip(tr("适合宽度"));
    auto *fitPage = bar->addAction(tr("适页"), m_view, &PdfView::fitPage);
    fitPage->setToolTip(tr("适合页面"));
    bar->addSeparator();

    // Layout: orthogonal single/double + continuous toggle.
    auto *layoutGroup = new QActionGroup(this);
    layoutGroup->setExclusive(true);
    m_singlePageAction = bar->addAction(IconManager::instance()->icon(QStringLiteral("file-text")),
                                        tr("单页"));
    m_singlePageAction->setCheckable(true);
    m_singlePageAction->setToolTip(tr("单页阅读"));
    layoutGroup->addAction(m_singlePageAction);
    m_doublePageAction = bar->addAction(IconManager::instance()->icon(QStringLiteral("book-open")),
                                        tr("双页"));
    m_doublePageAction->setCheckable(true);
    m_doublePageAction->setToolTip(tr("双页/页对阅读"));
    layoutGroup->addAction(m_doublePageAction);
    connect(m_singlePageAction, &QAction::triggered, this, &ReaderWorkspace::onLayoutControlsChanged);
    connect(m_doublePageAction, &QAction::triggered, this, &ReaderWorkspace::onLayoutControlsChanged);
    m_continuousCheck = new QCheckBox(tr("连续"), bar);
    m_continuousCheck->setToolTip(tr("连续滚动阅读"));
    connect(m_continuousCheck, &QCheckBox::toggled, this, &ReaderWorkspace::onLayoutControlsChanged);
    bar->addWidget(m_continuousCheck);
    m_firstPageAloneCheck = new QCheckBox(tr("首页单独"), bar);
    m_firstPageAloneCheck->setToolTip(tr("双页模式下首页单独显示"));
    connect(m_firstPageAloneCheck, &QCheckBox::toggled, this,
            &ReaderWorkspace::onFirstPageAloneChanged);
    bar->addWidget(m_firstPageAloneCheck);
    auto *rotate = bar->addAction(IconManager::instance()->icon(QStringLiteral("rotate-cw")),
                                  tr("旋转"));
    connect(rotate, &QAction::triggered, this, [this] { m_view->rotateBy(90); });
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
    bar->addSeparator();

    m_searchAction = new QAction(tr("查找"), this);
    m_searchAction->setIcon(IconManager::instance()->icon(QStringLiteral("search")));
    m_searchAction->setShortcut(QKeySequence::Find);
    m_searchAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_searchAction->setToolTip(tr("查找 (Ctrl+F)"));
    addAction(m_searchAction);
    connect(m_searchAction, &QAction::triggered, this, &ReaderWorkspace::openSearchPanel);
    bar->addAction(m_searchAction);
    m_bookmarkAction = new QAction(tr("添加书签"), this);
    m_bookmarkAction->setIcon(IconManager::instance()->icon(QStringLiteral("bookmark")));
    m_bookmarkAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    m_bookmarkAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_bookmarkAction->setToolTip(tr("添加书签 (Ctrl+B)"));
    addAction(m_bookmarkAction);
    connect(m_bookmarkAction, &QAction::triggered, this, &ReaderWorkspace::addBookmarkHere);
    bar->addAction(m_bookmarkAction);

    m_leftToggle = new QToolButton(bar);
    m_leftToggle->setIcon(IconManager::instance()->icon(QStringLiteral("panel-left-close")));
    m_leftToggle->setCheckable(true);
    m_leftToggle->setChecked(true);
    m_leftToggle->setAutoRaise(true);
    m_leftToggle->setToolTip(tr("显示/隐藏左侧栏"));
    connect(m_leftToggle, &QToolButton::toggled, this, &ReaderWorkspace::updatePanelVisibility);
    bar->addWidget(m_leftToggle);
    m_rightToggle = new QToolButton(bar);
    m_rightToggle->setIcon(IconManager::instance()->icon(QStringLiteral("panel-right-close")));
    m_rightToggle->setCheckable(true);
    m_rightToggle->setChecked(true);
    m_rightToggle->setAutoRaise(true);
    m_rightToggle->setToolTip(tr("显示/隐藏右侧栏"));
    connect(m_rightToggle, &QToolButton::toggled, this, &ReaderWorkspace::updatePanelVisibility);
    bar->addWidget(m_rightToggle);
    m_fullscreenAction = new QAction(tr("全屏"), this);
    m_fullscreenAction->setIcon(IconManager::instance()->icon(QStringLiteral("layout-grid")));
    m_fullscreenAction->setToolTip(tr("全屏 (F11)"));
    connect(m_fullscreenAction, &QAction::triggered, this, [this] { emit fullscreenRequested(); });
    bar->addAction(m_fullscreenAction);

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
        scheduleStyleApply();
    });
    connect(m_opacitySlider, &QSlider::sliderReleased, this,
            &ReaderWorkspace::flushPendingStyle);
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
    updateContextBarHeight();
    return bar;
}

QWidget *ReaderWorkspace::buildBottomBar()
{
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("readerStatusBar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(12);

    m_saveStateLabel = new QLabel(bar);
    m_saveStateLabel->setObjectName(QStringLiteral("readerSaveState"));
    layout->addWidget(m_saveStateLabel);
    m_statusLabel = new QLabel(bar);
    m_statusLabel->setObjectName(QStringLiteral("readerStatusMessage"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statusLabel, 1);
    m_statusInfoLabel = new QLabel(bar);
    m_statusInfoLabel->setObjectName(QStringLiteral("readerStatusInfo"));
    m_statusInfoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_statusInfoLabel);
    return bar;
}

void ReaderWorkspace::goToPreviousPage()
{
    m_view->navigatePageStep(-1);
}

void ReaderWorkspace::goToNextPage()
{
    m_view->navigatePageStep(1);
}

void ReaderWorkspace::onLayoutControlsChanged()
{
    if (!m_view || m_syncingControls)
        return;
    const bool doublePage = m_doublePageAction && m_doublePageAction->isChecked();
    const bool continuous = m_continuousCheck && m_continuousCheck->isChecked();
    PageLayoutMode mode = PageLayoutMode::ContinuousSingle;
    if (doublePage)
        mode = continuous ? PageLayoutMode::ContinuousFacing : PageLayoutMode::FacingPages;
    else
        mode = continuous ? PageLayoutMode::ContinuousSingle : PageLayoutMode::SinglePage;
    if (m_view->layoutMode() != mode)
        m_view->setLayoutMode(mode);
    updateLayoutControls();
}

void ReaderWorkspace::updateLayoutControls()
{
    if (!m_view)
        return;
    m_syncingControls = true;
    const PageLayoutMode mode = m_view->layoutMode();
    const bool facing = mode == PageLayoutMode::FacingPages
                        || mode == PageLayoutMode::ContinuousFacing;
    const bool continuous = mode == PageLayoutMode::ContinuousSingle
                            || mode == PageLayoutMode::ContinuousFacing;
    if (m_singlePageAction)
        m_singlePageAction->setChecked(!facing);
    if (m_doublePageAction)
        m_doublePageAction->setChecked(facing);
    if (m_continuousCheck)
        m_continuousCheck->setChecked(continuous);
    if (m_firstPageAloneCheck) {
        m_firstPageAloneCheck->setVisible(facing);
        m_firstPageAloneCheck->setEnabled(facing);
        m_firstPageAloneCheck->setChecked(m_view->firstPageAlone());
    }
    m_syncingControls = false;
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
    m_thumbnailList->setIconSize(QSize(160, 226));
    m_thumbnailList->setGridSize(QSize(176, 244));
    m_thumbnailList->setResizeMode(QListView::Adjust);
    m_thumbnailList->setMovement(QListView::Static);
    m_thumbnailList->setSpacing(6);
    m_thumbnailList->setUniformItemSizes(true);
    connect(m_thumbnailList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::onThumbnailActivated);
    m_leftTabs->addTab(m_thumbnailList, tr("页面"));

    m_leftTabs->addTab(buildBookmarkTab(), tr("书签"));
    m_leftTabs->addTab(m_searchBar, tr("查找"));
    m_previousLeftTab = 0;

    m_leftTabs->tabBar()->setExpanding(true);
    m_leftTabs->tabBar()->setUsesScrollButtons(false);
    m_leftTabs->tabBar()->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding-left: 3px; padding-right: 3px; }"));
    connect(m_leftTabs, &QTabWidget::currentChanged, this, [this](int) {
        requestNearbyThumbnails();
    });
    m_leftTabs->setMinimumWidth(160);
    return m_leftTabs;
}

QWidget *ReaderWorkspace::buildRightDock()
{
    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setDocumentMode(true);
    m_rightTabs->addTab(buildAnnotationTab(),
                        IconManager::instance()->icon(QStringLiteral("list")), tr("标注"));
    m_rightTabs->addTab(buildNoteTab(),
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

QWidget *ReaderWorkspace::buildAnnotationTab()
{
    auto *page = new QWidget(m_rightTabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *filterRow = new QHBoxLayout;
    m_annotationFilter = new QComboBox(page);
    m_annotationFilter->addItem(tr("全部类型"), QString());
    m_annotationFilter->addItem(tr("文本高亮"), QStringLiteral("text_highlight"));
    m_annotationFilter->addItem(tr("下划线"), QStringLiteral("underline"));
    m_annotationFilter->addItem(tr("删除线"), QStringLiteral("strikeout"));
    m_annotationFilter->addItem(tr("直线"), QStringLiteral("line"));
    m_annotationFilter->addItem(tr("矩形"), QStringLiteral("rectangle"));
    m_annotationFilter->addItem(tr("椭圆"), QStringLiteral("ellipse"));
    m_annotationFilter->addItem(tr("铅笔"), QStringLiteral("pencil"));
    m_annotationFilter->addItem(tr("荧光笔"), QStringLiteral("highlighter"));
    m_annotationFilter->addItem(tr("笔记点"), QStringLiteral("note_point"));
    filterRow->addWidget(m_annotationFilter, 1);
    m_annotationSort = new QComboBox(page);
    m_annotationSort->addItem(tr("创建顺序"), 0);
    m_annotationSort->addItem(tr("按页"), 1);
    m_annotationSort->addItem(tr("最近修改"), 2);
    filterRow->addWidget(m_annotationSort);
    layout->addLayout(filterRow);

    m_noteOnlyCheck = new QCheckBox(tr("仅有笔记"), page);
    layout->addWidget(m_noteOnlyCheck);
    m_annotationList = new QListWidget(page);
    m_annotationList->setWordWrap(true);
    m_annotationList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_annotationList, 1);
    m_annotationNotShown = new QLabel(tr("当前标注被筛选条件隐藏"), page);
    m_annotationNotShown->setWordWrap(true);
    m_annotationNotShown->hide();
    layout->addWidget(m_annotationNotShown);

    auto *splitter = new QSplitter(Qt::Vertical, page);
    m_annotationPreview = new QLabel(tr("选择页面中的标注，或从上方列表选择。"), page);
    m_annotationPreview->setWordWrap(true);
    m_annotationPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_annotationPreview->setMinimumHeight(64);
    splitter->addWidget(m_annotationPreview);
    auto *actions = new QWidget(page);
    auto *actionRow = new QHBoxLayout(actions);
    actionRow->setContentsMargins(0, 0, 0, 0);
    auto *locateButton = new QPushButton(tr("定位"), actions);
    auto *noteButton = new QPushButton(tr("笔记"), actions);
    auto *linkButton = new QToolButton(actions);
    linkButton->setIcon(IconManager::instance()->icon(QStringLiteral("link"), 18));
    linkButton->setToolTip(tr("关联/解除关联笔记"));
    linkButton->setPopupMode(QToolButton::InstantPopup);
    auto *linkMenu = new QMenu(linkButton);
    QAction *linkExisting = linkMenu->addAction(tr("关联已有笔记…"));
    QAction *unlinkAction = linkMenu->addAction(tr("解除关联"));
    linkButton->setMenu(linkMenu);
    auto *deleteButton = new QPushButton(tr("删除"), actions);
    actionRow->addWidget(locateButton);
    actionRow->addWidget(noteButton);
    actionRow->addWidget(linkButton);
    actionRow->addWidget(deleteButton);
    actionRow->addStretch(1);
    splitter->addWidget(actions);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    layout->addWidget(splitter, 1);

    connect(m_annotationList, &QListWidget::itemClicked, this,
            &ReaderWorkspace::onNoteListItemActivated);
    connect(m_annotationList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                onNoteListItemActivated(item);
                beginNoteEdit();
            });
    connect(m_annotationFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ReaderWorkspace::refreshAnnotationList);
    connect(m_annotationSort, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &ReaderWorkspace::refreshAnnotationList);
    connect(m_noteOnlyCheck, &QCheckBox::toggled, this,
            &ReaderWorkspace::refreshAnnotationList);
    connect(locateButton, &QPushButton::clicked, this, [this] {
        if (const Annotation *annotation = currentAnnotation())
            m_view->revealAnnotation(annotation->id);
    });
    connect(noteButton, &QPushButton::clicked, this, &ReaderWorkspace::beginNoteEdit);
    connect(deleteButton, &QPushButton::clicked, this, &ReaderWorkspace::deleteSelected);
    connect(linkMenu, &QMenu::aboutToShow, this, [this, linkExisting, unlinkAction] {
        const Annotation *annotation = currentAnnotation();
        const Note *linked = (annotation && m_session)
            ? m_session->noteForAnnotation(annotation->id) : nullptr;
        linkExisting->setEnabled(annotation && !linked);
        unlinkAction->setEnabled(linked != nullptr);
    });
    connect(linkExisting, &QAction::triggered, this, [this] {
        const Annotation *annotation = currentAnnotation();
        if (!annotation || !m_session)
            return;
        QStringList labels;
        QStringList ids;
        for (const Note &note : m_session->notes()->notes()) {
            if (!note.annotationId.isEmpty())
                continue;
            labels << QStringLiteral("%1 [%2]").arg(note.displayTitle(), note.id.left(6));
            ids << note.id;
        }
        if (ids.isEmpty()) {
            showStatus(tr("没有可关联的未关联笔记。"));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, tr("关联已有笔记"), tr("笔记"),
                                                     labels, 0, false, &ok);
        if (!ok)
            return;
        const int index = labels.indexOf(choice);
        if (index < 0)
            return;
        QString error;
        if (!m_session->linkNoteToAnnotation(ids.at(index), annotation->id, &error))
            showStatus(error);
        refreshReadingData();
    });
    connect(unlinkAction, &QAction::triggered, this, [this] {
        const Annotation *annotation = currentAnnotation();
        if (!annotation || !m_session)
            return;
        const Note *note = m_session->noteForAnnotation(annotation->id);
        if (!note)
            return;
        QString error;
        if (annotation->type == AnnotationType::NotePoint) {
            if (!m_session->deleteAnnotationKeepingNote(annotation->id, &error))
                showStatus(error);
            m_view->selectAnnotation(QString());
        } else if (!m_session->unlinkNote(note->id, &error)) {
            showStatus(error);
        }
        refreshReadingData();
    });
    return page;
}

QWidget *ReaderWorkspace::buildNoteTab()
{
    auto *page = new QWidget(m_rightTabs);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    auto makeButton = [&](const QString &icon, const QString &tooltip, const QString &text = QString()) {
        auto *button = new QToolButton(page);
        button->setIcon(IconManager::instance()->icon(icon, 18));
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        if (!text.isEmpty())
            button->setText(text);
        bar->addWidget(button);
        return button;
    };
    auto *newNoteButton = makeButton(QStringLiteral("file-plus"), tr("新建笔记"));
    auto *newGroupButton = makeButton(QStringLiteral("folder-plus"), tr("新建分组"));
    auto *renameButton = makeButton(QStringLiteral("pen-line"), tr("重命名"));
    auto *moveButton = makeButton(QStringLiteral("folder-input"), tr("移动到…"));
    auto *upButton = makeButton(QStringLiteral("chevron-right"), tr("上移"));
    upButton->setText(QStringLiteral("↑"));
    auto *downButton = makeButton(QStringLiteral("chevron-right"), tr("下移"));
    downButton->setText(QStringLiteral("↓"));
    auto *deleteButton = makeButton(QStringLiteral("trash-2"), tr("删除"));
    auto *linkButton = makeButton(QStringLiteral("link"), tr("关联/解除关联所选标注"));
    linkButton->setPopupMode(QToolButton::InstantPopup);
    auto *linkMenu = new QMenu(linkButton);
    QAction *linkToSelection = linkMenu->addAction(tr("关联到已有标注…"));
    QAction *unlinkSelection = linkMenu->addAction(tr("解除关联"));
    linkButton->setMenu(linkMenu);
    auto *locateButton = makeButton(QStringLiteral("scan"), tr("定位关联页"));
    auto *exportButton = new QToolButton(page);
    exportButton->setIcon(IconManager::instance()->icon(QStringLiteral("download"), 18));
    exportButton->setToolTip(tr("导出笔记"));
    exportButton->setAutoRaise(true);
    auto *exportMenu = new QMenu(exportButton);
    exportMenu->addAction(tr("导出当前笔记为 Markdown…"), this, [this] {
        const QString nodeId = selectedNoteNodeId();
        const NoteTreeNode *node = m_session ? m_session->noteTree().findNode(nodeId) : nullptr;
        if (!node || node->type != NoteNodeType::Note) {
            showStatus(tr("请先选择一条笔记。"));
            return;
        }
        const Note *note = m_session->note(node->noteId);
        if (!note)
            return;
        const QString path = QFileDialog::getSaveFileName(
            this, tr("导出 Markdown"), note->displayTitle() + QStringLiteral(".md"),
            tr("Markdown (*.md)"));
        if (path.isEmpty())
            return;
        QString error;
        exportNoteMarkdown(note->id, path, &error);
        if (!error.isEmpty())
            showStatus(error);
        else
            showStatus(tr("已导出笔记：%1").arg(path));
    });
    exportMenu->addAction(tr("导出全部笔记为 Markdown 目录…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("导出全部笔记"));
        if (dir.isEmpty())
            return;
        QString error;
        exportBookMarkdown(dir, &error);
        showStatus(error.isEmpty() ? tr("已导出全部笔记") : error);
    });
    exportMenu->addAction(tr("导出笔记数据为 JSON…"), this, [this] {
        const QString path = QFileDialog::getSaveFileName(
            this, tr("导出笔记数据"), QStringLiteral("notes.json"),
            tr("JSON (*.json)"));
        if (path.isEmpty())
            return;
        QString error;
        exportBookJson(path, &error);
        showStatus(error.isEmpty() ? tr("已导出笔记数据") : error);
    });
    exportButton->setMenu(exportMenu);
    exportButton->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(exportButton);
    bar->addStretch(1);
    layout->addLayout(bar);

    m_noteFilterEdit = new QLineEdit(page);
    m_noteFilterEdit->setPlaceholderText(tr("过滤笔记树…"));
    m_noteFilterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_noteFilterEdit);
    connect(m_noteFilterEdit, &QLineEdit::textChanged, this, [this] {
        applyNoteFilter();
    });

    m_noteTreeWidget = new QTreeWidget(page);
    m_noteTreeWidget->setHeaderHidden(true);
    m_noteTreeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_noteTreeWidget->setMinimumHeight(120);

    m_noteInfoLabel = new QLabel(page);
    m_noteInfoLabel->setWordWrap(true);
    m_noteInfoLabel->setTextFormat(Qt::RichText);
    m_noteInfoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_noteInfoLabel->setStyleSheet(
        QStringLiteral("QLabel { border: 1px solid palette(mid); border-radius: 4px; padding: 6px; }"));
    layout->addWidget(m_noteInfoLabel);

    m_notePreview = new MarkdownPreview(page);
    m_notePreview->setMinimumHeight(100);

    auto *noteSplitter = new QSplitter(Qt::Vertical, page);
    noteSplitter->addWidget(m_noteTreeWidget);
    noteSplitter->addWidget(m_notePreview);
    noteSplitter->setSizes({180, 360});
    noteSplitter->setStretchFactor(0, 1);
    noteSplitter->setStretchFactor(1, 2);
    layout->addWidget(noteSplitter, 1);

    connect(newNoteButton, &QToolButton::clicked, this, [this] {
        createIndependentNote(selectedParentGroupId());
    });
    connect(newGroupButton, &QToolButton::clicked, this, [this] {
        createGroupUnder(selectedParentGroupId());
    });
    connect(renameButton, &QToolButton::clicked, this,
            &ReaderWorkspace::renameSelectedNoteNode);
    connect(moveButton, &QToolButton::clicked, this,
            &ReaderWorkspace::moveSelectedNoteNode);
    connect(upButton, &QToolButton::clicked, this, [this] {
        const QString nodeId = selectedNoteNodeId();
        if (nodeId.isEmpty() || !m_session)
            return;
        QString error;
        if (!m_session->moveTreeNodeUp(nodeId, &error))
            showStatus(error);
        refreshNoteTree();
    });
    connect(downButton, &QToolButton::clicked, this, [this] {
        const QString nodeId = selectedNoteNodeId();
        if (nodeId.isEmpty() || !m_session)
            return;
        QString error;
        if (!m_session->moveTreeNodeDown(nodeId, &error))
            showStatus(error);
        refreshNoteTree();
    });
    connect(deleteButton, &QToolButton::clicked, this,
            &ReaderWorkspace::deleteSelectedNoteNode);
    connect(linkMenu, &QMenu::aboutToShow, this, [this, linkToSelection, unlinkSelection] {
        const QString nodeId = selectedNoteNodeId();
        const NoteTreeNode *node = m_session ? m_session->noteTree().findNode(nodeId) : nullptr;
        const Note *note = (node && node->type == NoteNodeType::Note)
            ? m_session->note(node->noteId) : nullptr;
        linkToSelection->setEnabled(note && note->annotationId.isEmpty());
        unlinkSelection->setEnabled(note && !note->annotationId.isEmpty());
    });
    connect(linkToSelection, &QAction::triggered, this, [this] {
        if (!m_session)
            return;
        const QString nodeId = selectedNoteNodeId();
        const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
        const Note *note = (node && node->type == NoteNodeType::Note)
            ? m_session->note(node->noteId) : nullptr;
        if (!note) {
            showStatus(tr("请先选择一条笔记。"));
            return;
        }
        QStringList labels;
        QStringList ids;
        for (const Annotation &annotation : m_session->view()->annotations()->annotations()) {
            if (m_session->hasLinkedNote(annotation.id))
                continue;
            const QString excerpt = annotation.text.simplified().left(28);
            labels << QStringLiteral("%1 · 第%2页%3")
                          .arg(annotationTypeDisplayName(annotation.type))
                          .arg(annotation.pageIndex + 1)
                          .arg(excerpt.isEmpty() ? QString() : QStringLiteral(" · %1").arg(excerpt));
            ids << annotation.id;
        }
        if (ids.isEmpty()) {
            showStatus(tr("没有可关联的未关联标注。"));
            return;
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(this, tr("关联到已有标注"), tr("标注"),
                                                     labels, 0, false, &ok);
        if (!ok)
            return;
        const int index = labels.indexOf(choice);
        if (index < 0 || index >= ids.size())
            return;
        QString error;
        if (!m_session->linkNoteToAnnotation(note->id, ids.at(index), &error))
            showStatus(error);
        refreshReadingData();
    });
    connect(unlinkSelection, &QAction::triggered, this, [this] {
        if (!m_session)
            return;
        const QString nodeId = selectedNoteNodeId();
        const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
        const Note *note = (node && node->type == NoteNodeType::Note)
            ? m_session->note(node->noteId) : nullptr;
        if (!note || note->annotationId.isEmpty())
            return;
        const Annotation *linked = m_session->view()->annotations()->find(note->annotationId);
        QString error;
        if (linked && linked->type == AnnotationType::NotePoint) {
            if (!m_session->deleteAnnotationKeepingNote(linked->id, &error)) {
                showStatus(error);
            } else {
                m_view->selectAnnotation(QString());
            }
        } else if (!m_session->unlinkNote(note->id, &error)) {
            showStatus(error);
        }
        refreshReadingData();
    });
    connect(locateButton, &QToolButton::clicked, this, [this] {
        const QString nodeId = selectedNoteNodeId();
        if (!m_session || nodeId.isEmpty())
            return;
        const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
        if (!node || node->type != NoteNodeType::Note)
            return;
        const Note *note = m_session->note(node->noteId);
        if (!note || note->annotationId.isEmpty())
            return;
        m_view->revealAnnotation(note->annotationId);
    });
    connect(m_noteTreeWidget, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *, int) { updateNotePreview(); });
    connect(m_noteTreeWidget, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                const QString nodeId = item->data(0, Qt::UserRole).toString();
                const NoteTreeNode *node = m_session ? m_session->noteTree().findNode(nodeId) : nullptr;
                if (!node || node->type != NoteNodeType::Note)
                    return;
                openNoteEditor(node->noteId);
                if (const Note *note = m_session->note(node->noteId); note && !note->annotationId.isEmpty())
                    m_view->revealAnnotation(note->annotationId);
            });
    connect(m_noteTreeWidget, &QTreeWidget::customContextMenuRequested, this,
            &ReaderWorkspace::showNoteContextMenu);
    return page;
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
        m_thumbRenderer->setDocument(m_view->document());
        connect(m_session, &DocumentSession::readingDataChanged, this,
                &ReaderWorkspace::refreshReadingData, Qt::UniqueConnection);
    }
    refreshOutline();
    refreshThumbnails();
    refreshAnnotationList();
    refreshBookmarks();
    if (m_searchModel && m_view && m_view->hasDocument())
        m_searchModel->setDocument(m_view->document()->pdf());
    if (m_history.isEmpty() && m_view->hasDocument()) {
        int page = m_view->currentPage();
        const QPointF anchor = m_view->viewportAnchor(&page);
        m_history.append({page, anchor});
        m_historyIndex = 0;
    }
    updateHistoryActions();
    updateLayoutControls();
    updateBottomStatus();
    updateDetailPanel();
    refreshNoteTree();
    updateNotePreview();
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
    updateThumbnailLayout();
    if (!m_thumbnailList)
        return;
    const QSignalBlocker blocker(m_thumbnailList);
    m_thumbnailList->clear();
    m_thumbRequests.clear();
    m_thumbGenerated.clear();
    m_thumbFailed.clear();
    m_thumbRetries.clear();
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
        item->setSizeHint(QSize(176, 244));
        QPixmap placeholder(160, 226);
        placeholder.fill(palette.color(QPalette::Base));
        item->setIcon(QIcon(placeholder));
    }
    if (m_view->currentPage() >= 0 && m_view->currentPage() < m_thumbnailList->count())
        m_thumbnailList->setCurrentRow(m_view->currentPage());
    requestNearbyThumbnails();
}

QSize ReaderWorkspace::thumbnailRequestSize(int page) const
{
    const QSizeF pageSize = m_view->layout().pageSizePt(page);
    if (pageSize.isEmpty())
        return QSize(160, 220);
    qreal width = 360.0;
    qreal height = width * pageSize.height() / pageSize.width();
    if (height > 510.0) {
        height = 510.0;
        width = height * pageSize.width() / pageSize.height();
    }
    return QSize(qMax(48, qRound(width)), qMax(48, qRound(height)));
}

void ReaderWorkspace::showThumbnail(int page, const QImage &image)
{
    if (!m_thumbnailList || page < 0 || page >= m_thumbnailList->count() || image.isNull())
        return;
    m_thumbGenerated.insert(page);
    m_thumbFailed.remove(page);
    QImage oriented = image;
    if (m_view->rotation() != 0) {
        QTransform transform;
        transform.rotate(m_view->rotation());
        oriented = image.transformed(transform, Qt::SmoothTransformation);
    }
    // PDF rasterization at thumbnail scale gives thin glyphs very low alpha;
    // boost the coverage before compositing so sparse pages remain legible.
    QImage adjusted = oriented.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < adjusted.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(adjusted.scanLine(y));
        for (int x = 0; x < adjusted.width(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha != 0 && alpha < 255)
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]),
                                qMin(255, alpha * 2));
        }
    }
    QImage flattened(adjusted.size(), QImage::Format_ARGB32_Premultiplied);
    flattened.fill(Qt::white);
    {
        QPainter painter(&flattened);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(0, 0, adjusted);
    }

    const QPixmap fullPixmap = QPixmap::fromImage(flattened);
    const QPixmap pixmap = fullPixmap.scaled(
        QSize(160, 226), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (QListWidgetItem *item = m_thumbnailList->item(page)) {
        item->setIcon(QIcon(pixmap));
        item->setSizeHint(QSize(176, 244));
        m_thumbnailList->viewport()->update();
    }
}

void ReaderWorkspace::requestNearbyThumbnails()
{
    if (!m_view->hasDocument() || !m_thumbnailList || !m_leftTabs
        || m_leftTabs->currentWidget() != m_thumbnailList || !m_thumbnailList->isVisible())
        return;
    const QRect viewportRect = m_thumbnailList->viewport()->rect();
    QSet<int> pages;
    int minVisible = m_thumbnailList->count();
    int maxVisible = -1;
    for (int row = 0; row < m_thumbnailList->count(); ++row) {
        const QRect rect = m_thumbnailList->visualItemRect(m_thumbnailList->item(row));
        if (rect.intersects(viewportRect)) {
            pages.insert(row);
            minVisible = qMin(minVisible, row);
            maxVisible = qMax(maxVisible, row);
        }
    }
    if (minVisible <= maxVisible) {
        for (int page = qMax(0, minVisible - 2); page <= minVisible - 1; ++page)
            pages.insert(page);
        for (int page = maxVisible + 1;
             page <= qMin(m_thumbnailList->count() - 1, maxVisible + 2); ++page)
            pages.insert(page);
    }
    const int current = m_view->currentPage();
    for (int page = qMax(0, current - 1);
         page <= qMin(m_thumbnailList->count() - 1, current + 1); ++page)
        pages.insert(page);

    int issued = 0;
    for (int page : pages) {
        if (issued >= 32)
            break;
        if (page < 0 || page >= m_thumbnailList->count()
            || m_thumbGenerated.contains(page) || m_thumbFailed.contains(page)
            || m_thumbRequests.contains(page))
            continue;
        const QSize pixelSize = thumbnailRequestSize(page);
        const QImage cached = m_thumbRenderer->cachedImage(page, pixelSize);
        if (!cached.isNull()) {
            showThumbnail(page, cached);
            continue;
        }
        const quint64 request = m_thumbRenderer->requestPage(page, pixelSize);
            if (request != 0) {
            m_thumbRequests.insert(page, request);
            ++issued;
        }
    }
}

void ReaderWorkspace::onThumbnailRendered(int page, const QSize &size, const QImage &image,
                                          quint64 requestId)
{
    Q_UNUSED(size);
    if (!m_thumbnailList || page < 0 || page >= m_thumbnailList->count())
        return;
    if (!m_thumbRequests.contains(page) || m_thumbRequests.value(page) != requestId)
        return;
    m_thumbRequests.remove(page);
    showThumbnail(page, image);
}

void ReaderWorkspace::onThumbnailFailed(int page, const QSize &size, quint64 requestId)
{
    Q_UNUSED(size);
    if (!m_thumbnailList || page < 0 || page >= m_thumbnailList->count())
        return;
    if (!m_thumbRequests.contains(page) || m_thumbRequests.value(page) != requestId)
        return;
    m_thumbRequests.remove(page);
    const int retries = m_thumbRetries.value(page, 0);
    if (retries < 1) {
        m_thumbRetries.insert(page, retries + 1);
        const quint64 request = m_thumbRenderer->requestPage(page, thumbnailRequestSize(page));
        if (request != 0) {
            m_thumbRequests.insert(page, request);
            return;
        }
    }
    m_thumbFailed.insert(page);
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
    m_view->navigateTo(page, location,
                       location.isNull() ? PdfView::NavigationAlignment::PageTop
                                         : PdfView::NavigationAlignment::Center,
                       PdfView::NavigationReason::Outline);
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
    QVector<Annotation> annotations = m_view->annotations()->annotations();
    const int sortMode = m_annotationSort ? m_annotationSort->currentData().toInt() : 0;
    if (sortMode == 1) {
        std::stable_sort(annotations.begin(), annotations.end(),
                         [](const Annotation &a, const Annotation &b) {
                             return a.pageIndex < b.pageIndex;
                         });
    } else if (sortMode == 2) {
        std::stable_sort(annotations.begin(), annotations.end(),
                         [](const Annotation &a, const Annotation &b) {
                             return a.modifiedAt > b.modifiedAt;
                         });
    }
    bool selectedWasShown = false;
    for (const Annotation &annotation : annotations) {
        if (!filter.isEmpty() && annotationTypeName(annotation.type) != filter)
            continue;
        if (noteOnly && !(m_session && m_session->hasLinkedNote(annotation.id)))
            continue;
        QListWidgetItem *item = makeAnnotationItem(annotation);
        m_annotationList->addItem(item);
        if (annotation.id == id) {
            m_annotationList->setCurrentItem(item);
            selectedWasShown = true;
        }
    }
    if (m_annotationNotShown)
        m_annotationNotShown->setVisible(!id.isEmpty() && !selectedWasShown);
    refreshAnnotationPreview();
}

void ReaderWorkspace::refreshAnnotationPreview()
{
    if (!m_annotationPreview)
        return;
    const Annotation *annotation = currentAnnotation();
    if (!annotation) {
        m_annotationPreview->setText(tr("选择页面中的标注，或从上方列表选择。"));
        return;
    }
    QString text = QStringLiteral("P%1 · %2\n%3")
                       .arg(annotation->pageIndex + 1)
                       .arg(annotationTypeDisplayName(annotation->type))
                       .arg(annotationExcerpt(*annotation));
    if (m_session) {
        if (const Note *note = m_session->noteForAnnotation(annotation->id))
            text += QStringLiteral("\n笔记：%1").arg(note->markdown.left(160));
    }
    m_annotationPreview->setText(text);
}

QListWidgetItem *ReaderWorkspace::makeAnnotationItem(const Annotation &annotation) const
{
    const Note *note = m_session ? m_session->noteForAnnotation(annotation.id) : nullptr;
    QString title = note ? note->displayTitle() : annotationTypeDisplayName(annotation.type);
    const QString excerpt = annotationExcerpt(annotation);
    QString text = QStringLiteral("P%1 · %2").arg(annotation.pageIndex + 1).arg(title);
    if (!excerpt.isEmpty())
        text += QStringLiteral("\n%1").arg(excerpt);
    if (note)
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
    item->setToolTip(note ? note->markdown : annotation.text);
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
    updateNotePreview();
    updateContextToolBar();
    updateBottomStatus();
}

void ReaderWorkspace::onSelectionChanged(const QString &annotationId)
{
    Q_UNUSED(annotationId);
    if (m_applyingSelection)
        return;
    flushPendingStyle();
    refreshAnnotationList();
    updateDetailPanel();
    updateNotePreview();
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
        updateDetailPanel();
    }
    if (m_rightTabs)
        m_rightTabs->setCurrentIndex(1);
    openNoteForAnnotation(annotationId);
}

void ReaderWorkspace::onNoteDeleteRequested(const QString &annotationId)
{
    if (!m_session || !m_view)
        return;
    const Annotation *annotation = m_view->annotations()->find(annotationId);
    if (!annotation)
        return;
    const Note *note = m_session->noteForAnnotation(annotationId);
    QMessageBox box(QMessageBox::Question, tr("删除附带笔记的批注"),
                    tr("该批注关联了笔记“%1”。删除时如何处理笔记？")
                        .arg(note ? note->displayTitle() : tr("未命名笔记")),
                    QMessageBox::NoButton, this);
    QPushButton *keepButton = box.addButton(tr("删除批注，保留笔记"), QMessageBox::AcceptRole);
    QPushButton *deleteBothButton = box.addButton(tr("批注和笔记都删除"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != keepButton && box.clickedButton() != deleteBothButton)
        return;
    QString error;
    if (box.clickedButton() == keepButton) {
        if (m_session->deleteAnnotationKeepingNote(annotationId, &error))
            m_view->selectAnnotation(QString());
        else
            showStatus(error);
    } else if (note) {
        if (!m_session->deleteAnnotationAndNote(annotationId, &error))
            showStatus(error);
        else
            m_view->selectAnnotation(QString());
    }
    refreshReadingData();
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
    const Note *note = m_session ? m_session->noteForAnnotation(annotation->id) : nullptr;
    m_detailTitle->setText(note ? note->displayTitle()
                                : annotationTypeDisplayName(annotation->type));
    m_detailMeta->setText(tr("%1 · 第 %2 页\n创建：%3\n修改：%4")
                              .arg(annotationTypeDisplayName(annotation->type))
                              .arg(annotation->pageIndex + 1)
                              .arg(annotation->createdAt)
                              .arg(annotation->modifiedAt));
    m_detailGeometry->setText(geometryText(*annotation));
}

void ReaderWorkspace::updateNotePanel()
{
    updateNotePreview();
}

void ReaderWorkspace::refreshNoteTree()
{
    if (!m_noteTreeWidget)
        return;
    const QString selected = selectedNoteNodeId();
    QSet<QString> expandedNodes;
    if (m_noteTreeInitialized) {
        QTreeWidgetItemIterator it(m_noteTreeWidget);
        while (*it) {
            if ((*it)->childCount() > 0 && (*it)->isExpanded())
                expandedNodes.insert((*it)->data(0, Qt::UserRole).toString());
            ++it;
        }
    }
    m_noteTreeInitialized = true;
    const int oldScroll = m_noteTreeWidget ? m_noteTreeWidget->verticalScrollBar()->value() : 0;
    m_noteTreeWidget->clear();
    if (!m_session)
        return;

    std::function<void(const QString &, QTreeWidgetItem *)> addChildren =
        [&](const QString &parentId, QTreeWidgetItem *parentItem) {
            QStringList childIds = m_session->noteTree().childIds(parentId);
            const int sortMode = m_noteSortCombo ? m_noteSortCombo->currentIndex() : 0;
            if (sortMode == 1) {
                std::sort(childIds.begin(), childIds.end(), [this](const QString &a, const QString &b) {
                    const NoteTreeNode *na = m_session->noteTree().findNode(a);
                    const NoteTreeNode *nb = m_session->noteTree().findNode(b);
                    const QString ta = na && na->type == NoteNodeType::Note && m_session->note(na->noteId)
                                           ? m_session->note(na->noteId)->displayTitle()
                                           : (na ? na->title : QString());
                    const QString tb = nb && nb->type == NoteNodeType::Note && m_session->note(nb->noteId)
                                           ? m_session->note(nb->noteId)->displayTitle()
                                           : (nb ? nb->title : QString());
                    return QString::localeAwareCompare(ta, tb) < 0;
                });
            } else if (sortMode == 2) {
                std::sort(childIds.begin(), childIds.end(), [this](const QString &a, const QString &b) {
                    const NoteTreeNode *na = m_session->noteTree().findNode(a);
                    const NoteTreeNode *nb = m_session->noteTree().findNode(b);
                    const Note *noteA = na && na->type == NoteNodeType::Note ? m_session->note(na->noteId) : nullptr;
                    const Note *noteB = nb && nb->type == NoteNodeType::Note ? m_session->note(nb->noteId) : nullptr;
                    if (noteA && noteB)
                        return noteA->modifiedAt > noteB->modifiedAt;
                    return noteA != nullptr;
                });
            }
            for (const QString &nodeId : childIds) {
                const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
                if (!node)
                    continue;
                if (node->type == NoteNodeType::Group) {
                    auto *item = parentItem ? new QTreeWidgetItem(parentItem)
                                            : new QTreeWidgetItem(m_noteTreeWidget);
                    item->setText(0, node->title.isEmpty() ? tr("未命名分组") : node->title);
                    item->setIcon(0, IconManager::instance()->icon(QStringLiteral("folder"), 18));
                    item->setData(0, Qt::UserRole, node->id);
                    addChildren(node->id, item);
                    item->setExpanded(expandedNodes.isEmpty() || expandedNodes.contains(node->id));
                } else {
                    const Note *note = m_session->note(node->noteId);
                    if (!note)
                        continue;
                    auto *item = parentItem ? new QTreeWidgetItem(parentItem)
                                            : new QTreeWidgetItem(m_noteTreeWidget);
                    QString label = note->displayTitle();
                    if (!note->annotationId.isEmpty()) {
                        if (const Annotation *annotation = m_session->annotationForNote(note->id))
                            label += tr("  · 第 %1 页").arg(annotation->pageIndex + 1);
                    }
                    item->setText(0, label);
                    item->setIcon(0, IconManager::instance()->icon(QStringLiteral("sticky-note"), 18));
                    item->setData(0, Qt::UserRole, node->id);
                    item->setToolTip(0, note->markdown);
                }
            }
        };
    // R12: the book is a virtual root. It is never persisted, never renamed and
    // never moved; it lets the tree always start from an unambiguous parent.
    auto *rootItem = new QTreeWidgetItem(m_noteTreeWidget);
    rootItem->setText(0, m_session->title().isEmpty() ? tr("本书") : m_session->title());
    rootItem->setIcon(0, IconManager::instance()->icon(QStringLiteral("book-open"), 18));
    rootItem->setData(0, Qt::UserRole, QString());
    rootItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    addChildren(QString(), rootItem);
    rootItem->setExpanded(true);
    if (rootItem->childCount() == 0) {
        auto *empty = new QTreeWidgetItem(rootItem);
        empty->setText(0, tr("暂无笔记，点击“新建笔记”开始"));
        empty->setFlags(Qt::NoItemFlags);
    }
    applyNoteFilter();
    m_noteTreeWidget->verticalScrollBar()->setValue(oldScroll);
    if (!selected.isEmpty()) {
        QTreeWidgetItemIterator it(m_noteTreeWidget);
        while (*it) {
            if ((*it)->data(0, Qt::UserRole).toString() == selected) {
                m_noteTreeWidget->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }
}

namespace {

QString safeExportName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty())
        name = QStringLiteral("未命名");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\r\\n]")), QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("未命名");
    static const QSet<QString> reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9")};
    if (reserved.contains(name.toUpper()))
        name.prepend(QLatin1Char('_'));
    return name.left(80);
}

bool copyDirectoryContents(const QString &source, const QString &target, QString *error)
{
    QDir sourceDir(source);
    if (!sourceDir.exists())
        return true;
    if (!QDir().mkpath(target)) {
        if (error) *error = QStringLiteral("无法创建导出目录：%1").arg(target);
        return false;
    }
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink()) {
            if (error) *error = QStringLiteral("导出时拒绝符号链接：%1").arg(entry.fileName());
            return false;
        }
        const QString destination = QDir(target).filePath(entry.fileName());
        if (QFileInfo::exists(destination)) {
            if (error) *error = QStringLiteral("导出目标已存在：%1").arg(entry.fileName());
            return false;
        }
        if (entry.isDir()) {
            if (!copyDirectoryContents(entry.absoluteFilePath(), destination, error))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            if (error) *error = QStringLiteral("无法复制附件：%1").arg(entry.fileName());
            return false;
        }
    }
    return true;
}

QString rewriteAssetLinks(QString markdown, const QString &prefix)
{
    markdown.replace(QRegularExpression(QStringLiteral(R"(\]\(assets/)")),
                     QStringLiteral("](%1assets/").arg(prefix));
    return markdown;
}

} // namespace

void ReaderWorkspace::exportNoteMarkdown(const QString &noteId, const QString &path, QString *error)
{
    if (!m_session) {
        if (error) *error = QStringLiteral("会话不可用。");
        return;
    }
    const Note *note = m_session->note(noteId);
    if (!note) {
        if (error) *error = QStringLiteral("笔记不存在。");
        return;
    }
    if (QFileInfo::exists(path)) {
        if (error) *error = QStringLiteral("导出目标已存在，请选择其他文件名。");
        return;
    }
    const QFileInfo targetInfo(path);
    const QString targetAbsolute = QDir::cleanPath(targetInfo.absoluteFilePath());
    const QString bookRoot = QDir::cleanPath(QDir(m_session->bookDirectory()).absolutePath());
    if (targetAbsolute == bookRoot || targetAbsolute.startsWith(bookRoot + QLatin1Char('/'))) {
        if (error) *error = QStringLiteral("不能把导出文件写入原书籍目录。");
        return;
    }
    const QString assetDirName = targetInfo.completeBaseName() + QStringLiteral(".assets");
    const QString assetDir = QDir(targetInfo.absolutePath()).filePath(assetDirName);
    if (QFileInfo::exists(assetDir)) {
        if (error) *error = QStringLiteral("导出附件目录已存在：%1").arg(assetDirName);
        return;
    }
    QString copyError;
    if (!copyDirectoryContents(QDir(m_session->bookDirectory()).filePath(QStringLiteral("assets")),
                               assetDir, &copyError)) {
        if (error) *error = copyError;
        return;
    }
    QString markdown = note->markdown;
    markdown = rewriteAssetLinks(markdown, assetDirName + QStringLiteral("/"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("无法写入：%1").arg(path);
        return;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "# " << note->displayTitle() << "\n\n" << markdown;
    if (!markdown.endsWith(QLatin1Char('\n')))
        stream << '\n';
    if (!file.commit() && error)
        *error = QStringLiteral("写入失败：%1").arg(path);
}

void ReaderWorkspace::exportBookMarkdown(const QString &directory, QString *error)
{
    if (!m_session) {
        if (error) *error = QStringLiteral("会话不可用。");
        return;
    }
    const QString targetAbsolute = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    const QString bookRoot = QDir::cleanPath(QDir(m_session->bookDirectory()).absolutePath());
    if (targetAbsolute == bookRoot || targetAbsolute.startsWith(bookRoot + QLatin1Char('/'))) {
        if (error) *error = QStringLiteral("不能把导出目录放在原书籍目录内。");
        return;
    }
    QDir root(directory);
    if (!root.exists() && !QDir().mkpath(directory)) {
        if (error) *error = QStringLiteral("无法创建导出目录。");
        return;
    }
    if (QFileInfo::exists(root.filePath(QStringLiteral("assets")))) {
        if (error) *error = QStringLiteral("导出目录中已存在 assets，请选择空目录。");
        return;
    }
    QString copyError;
    if (!copyDirectoryContents(QDir(m_session->bookDirectory()).filePath(QStringLiteral("assets")),
                               root.filePath(QStringLiteral("assets")), &copyError)) {
        if (error) *error = copyError;
        return;
    }
    int sequence = 0;
    std::function<void(const QString &, const QDir &, const QString &)> writeGroup =
        [&](const QString &parentId, const QDir &dir, const QString &assetPrefix) {
            QStringList childIds = m_session->noteTree().childIds(parentId);
            const int sortMode = m_noteSortCombo ? m_noteSortCombo->currentIndex() : 0;
            if (sortMode == 1) {
                std::sort(childIds.begin(), childIds.end(), [this](const QString &a, const QString &b) {
                    const NoteTreeNode *na = m_session->noteTree().findNode(a);
                    const NoteTreeNode *nb = m_session->noteTree().findNode(b);
                    const QString ta = na && na->type == NoteNodeType::Note && m_session->note(na->noteId)
                                           ? m_session->note(na->noteId)->displayTitle()
                                           : (na ? na->title : QString());
                    const QString tb = nb && nb->type == NoteNodeType::Note && m_session->note(nb->noteId)
                                           ? m_session->note(nb->noteId)->displayTitle()
                                           : (nb ? nb->title : QString());
                    return QString::localeAwareCompare(ta, tb) < 0;
                });
            } else if (sortMode == 2) {
                std::sort(childIds.begin(), childIds.end(), [this](const QString &a, const QString &b) {
                    const NoteTreeNode *na = m_session->noteTree().findNode(a);
                    const NoteTreeNode *nb = m_session->noteTree().findNode(b);
                    const Note *noteA = na && na->type == NoteNodeType::Note ? m_session->note(na->noteId) : nullptr;
                    const Note *noteB = nb && nb->type == NoteNodeType::Note ? m_session->note(nb->noteId) : nullptr;
                    if (noteA && noteB)
                        return noteA->modifiedAt > noteB->modifiedAt;
                    return noteA != nullptr;
                });
            }
            for (const QString &nodeId : childIds) {
                const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
                if (!node)
                    continue;
                if (node->type == NoteNodeType::Group) {
                    QString name = safeExportName(node->title.isEmpty() ? tr("未命名分组") : node->title);
                    QString targetName = name;
                    int suffix = 2;
                    while (QDir(dir.filePath(targetName)).exists())
                        targetName = QStringLiteral("%1-%2").arg(name).arg(suffix++);
                    if (!dir.mkpath(targetName)) {
                        if (error) *error = QStringLiteral("无法创建分组目录：%1").arg(targetName);
                        return;
                    }
                    writeGroup(node->id, QDir(dir.filePath(targetName)), assetPrefix + QStringLiteral("../"));
                } else {
                    const Note *note = m_session->note(node->noteId);
                    if (!note)
                        continue;
                    ++sequence;
                    const QString name = QStringLiteral("%1-%2.md")
                                             .arg(sequence, 2, 10, QLatin1Char('0'))
                                             .arg(safeExportName(note->displayTitle()));
                    QSaveFile file(dir.filePath(name));
                    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        if (error) *error = QStringLiteral("无法写入：%1").arg(name);
                        return;
                    }
                    QTextStream stream(&file);
                    stream.setEncoding(QStringConverter::Utf8);
                    stream << "# " << note->displayTitle() << "\n\n"
                           << rewriteAssetLinks(note->markdown, assetPrefix);
                    if (!note->markdown.endsWith(QLatin1Char('\n')))
                        stream << '\n';
                    if (!file.commit()) {
                        if (error) *error = QStringLiteral("写入失败：%1").arg(name);
                        return;
                    }
                }
            }
        };
    writeGroup(QString(), root, QString());
}

void ReaderWorkspace::exportBookJson(const QString &path, QString *error)
{
    if (!m_session) {
        if (error) *error = QStringLiteral("会话不可用。");
        return;
    }
    QJsonArray notes;
    for (const Note &note : m_session->notes()->notes())
        notes.append(note.toJson());
    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("pano-notes-export")},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("book_id"), m_session->bookId()},
        {QStringLiteral("title"), m_session->title()},
        {QStringLiteral("exported_at"), nowIsoUtc()},
        {QStringLiteral("notes"), notes},
        {QStringLiteral("note_tree"), m_session->noteTree().toJson()},
    };
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入：%1").arg(path);
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit() && error)
        *error = QStringLiteral("写入失败：%1").arg(path);
}

void ReaderWorkspace::applyNoteFilter()
{
    if (!m_noteTreeWidget)
        return;
    const QString filter = m_noteFilterEdit ? m_noteFilterEdit->text().trimmed() : QString();
    std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *item) -> bool {
        bool visible = filter.isEmpty();
        const QString nodeId = item->data(0, Qt::UserRole).toString();
        const NoteTreeNode *node = m_session ? m_session->noteTree().findNode(nodeId) : nullptr;
        if (!visible && node) {
            if (node->type == NoteNodeType::Group) {
                visible = node->title.contains(filter, Qt::CaseInsensitive);
            } else if (const Note *note = m_session->note(node->noteId)) {
                visible = note->displayTitle().contains(filter, Qt::CaseInsensitive)
                          || note->markdown.contains(filter, Qt::CaseInsensitive);
            }
        }
        for (int i = 0; i < item->childCount(); ++i) {
            if (apply(item->child(i)))
                visible = true;
        }
        item->setHidden(!visible);
        return visible;
    };
    for (int i = 0; i < m_noteTreeWidget->topLevelItemCount(); ++i)
        apply(m_noteTreeWidget->topLevelItem(i));
}

void ReaderWorkspace::updateNotePreview()
{
    if (!m_notePreview || !m_noteInfoLabel || !m_session)
        return;
    const Note *note = nullptr;
    const QString nodeId = selectedNoteNodeId();
    if (!nodeId.isEmpty()) {
        if (const NoteTreeNode *node = m_session->noteTree().findNode(nodeId))
            note = m_session->note(node->noteId);
        if (!note && m_session->noteTree().isGroup(nodeId)) {
            const NoteTreeNode *group = m_session->noteTree().findNode(nodeId);
            m_noteInfoLabel->setText(
                tr("<b>%1</b><br><span style=\"color:palette(mid);\">分组 · %2 个子节点</span>")
                    .arg(group->title.toHtmlEscaped())
                    .arg(m_session->noteTree().childIds(nodeId).size()));
            m_notePreview->setNoteMarkdown(QString(), m_session->bookDirectory());
            return;
        }
    }
    if (!note) {
        if (const Annotation *annotation = currentAnnotation())
            note = m_session->noteForAnnotation(annotation->id);
    }
    if (!note) {
        m_noteInfoLabel->setText(tr("选择笔记查看预览，或选择一个标注创建关联笔记。"));
        m_notePreview->setNoteMarkdown(QString(), m_session->bookDirectory());
        return;
    }
    const Annotation *annotation = note->annotationId.isEmpty()
                                       ? nullptr
                                       : m_session->view()->annotations()->find(note->annotationId);
    const QString linkText = annotation
        ? tr("关联：%1 · 第 %2 页")
              .arg(annotationTypeDisplayName(annotation->type))
              .arg(annotation->pageIndex + 1)
        : tr("独立笔记（未关联标注）");
    m_noteInfoLabel->setText(
        tr("<b>%1</b><br><span style=\"color:palette(mid);\">%2</span>"
           "<br><span style=\"color:palette(mid);\">创建 %3 · 修改 %4</span>")
            .arg(note->displayTitle().toHtmlEscaped(), linkText.toHtmlEscaped(),
                 note->createdAt.toHtmlEscaped(), note->modifiedAt.toHtmlEscaped()));
    m_notePreview->setNoteMarkdown(note->markdown, m_session->bookDirectory());
}

QString ReaderWorkspace::selectedNoteNodeId() const
{
    if (!m_noteTreeWidget || !m_noteTreeWidget->currentItem())
        return QString();
    const QString id = m_noteTreeWidget->currentItem()->data(0, Qt::UserRole).toString();
    return id == QStringLiteral("__book_root__") ? QString() : id;
}

QString ReaderWorkspace::selectedParentGroupId() const
{
    if (!m_session)
        return QString();
    const QString nodeId = selectedNoteNodeId();
    if (nodeId.isEmpty())
        return QString();
    if (m_session->noteTree().isGroup(nodeId))
        return nodeId;
    return m_session->noteTree().parentOf(nodeId);
}

void ReaderWorkspace::ensureNoteDialog()
{
    if (m_noteDialog)
        return;
    m_noteDialog = new NoteEditDialog(m_session, this);
    connect(m_noteDialog, &NoteEditDialog::imageRequested, this,
            &ReaderWorkspace::insertImageIntoNote);
    connect(m_noteDialog, &NoteEditDialog::captureRequested, this,
            &ReaderWorkspace::startRegionCapture);
    connect(m_noteDialog, &NoteEditDialog::saved, this, [this](const QString &) {
        refreshReadingData();
        if (m_rightTabs)
            m_rightTabs->setCurrentIndex(1);
    });
    connect(m_noteDialog, &NoteEditDialog::closed, this, [this] {
        refreshReadingData();
    });
}

void ReaderWorkspace::openNoteEditor(const QString &noteId)
{
    if (!m_session || noteId.isEmpty())
        return;
    if (m_noteDialog && m_noteDialog->isVisible() && m_noteDialog->hasUnsavedChanges()
        && m_noteDialog->currentNoteId() != noteId) {
        showStatus(tr("请先保存或取消当前笔记编辑，再切换笔记。"));
        return;
    }
    ensureNoteDialog();
    m_noteDialog->openExistingNote(noteId);
}

void ReaderWorkspace::openNoteForAnnotation(const QString &annotationId)
{
    if (!m_session || annotationId.isEmpty())
        return;
    if (const Note *note = m_session->noteForAnnotation(annotationId)) {
        openNoteEditor(note->id);
        return;
    }
    if (m_noteDialog && m_noteDialog->isVisible() && m_noteDialog->hasUnsavedChanges()) {
        showStatus(tr("请先保存或取消当前笔记编辑，再切换笔记。"));
        return;
    }
    ensureNoteDialog();
    m_noteDialog->openNewNote(selectedParentGroupId(), annotationId);
}

void ReaderWorkspace::createIndependentNote(const QString &parentGroupId)
{
    if (!m_session)
        return;
    if (m_noteDialog && m_noteDialog->isVisible() && m_noteDialog->hasUnsavedChanges()) {
        showStatus(tr("请先保存或取消当前笔记编辑，再新建笔记。"));
        return;
    }
    ensureNoteDialog();
    m_noteDialog->openNewNote(parentGroupId);
}

void ReaderWorkspace::onNotePointRequested(int pageIndex, const QPointF &pagePoint,
                                           const AnnotationStyle &style)
{
    if (!m_session || pageIndex < 0)
        return;
    if (m_noteDialog && m_noteDialog->isVisible() && m_noteDialog->hasUnsavedChanges()) {
        showStatus(tr("请先保存或取消当前笔记编辑，再创建定位笔记。"));
        return;
    }
    ensureNoteDialog();
    m_noteDialog->openNewNoteAtPoint(selectedParentGroupId(), pageIndex, pagePoint, style);
}

bool ReaderWorkspace::prepareToClose()
{
    if (!m_noteDialog || !m_noteDialog->isVisible() || !m_noteDialog->hasUnsavedChanges())
        return true;
    QString error;
    if (!m_noteDialog->prepareToClose(&error)) {
        if (!error.isEmpty())
            showStatus(error);
        return false;
    }
    return true;
}

void ReaderWorkspace::createGroupUnder(const QString &parentGroupId)
{
    if (!m_session)
        return;
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("新建分组"), tr("分组名称"),
                                                QLineEdit::Normal, tr("新建分组"), &ok);
    if (!ok || title.trimmed().isEmpty())
        return;
    QString groupId;
    QString error;
    if (!m_session->createGroup(parentGroupId, title.trimmed(), &groupId, &error)) {
        showStatus(error);
        return;
    }
    refreshNoteTree();
    QTreeWidgetItemIterator it(m_noteTreeWidget);
    while (*it) {
        if ((*it)->data(0, Qt::UserRole).toString() == groupId) {
            m_noteTreeWidget->setCurrentItem(*it);
            break;
        }
        ++it;
    }
    updateNotePreview();
}

void ReaderWorkspace::renameSelectedNoteNode()
{
    if (!m_session)
        return;
    const QString nodeId = selectedNoteNodeId();
    const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
    if (!node)
        return;
    if (node->type == NoteNodeType::Group) {
        bool ok = false;
        const QString title = QInputDialog::getText(this, tr("重命名分组"), tr("名称"),
                                                    QLineEdit::Normal, node->title, &ok);
        if (!ok || title.trimmed().isEmpty())
            return;
        QString error;
        if (!m_session->renameGroup(nodeId, title.trimmed(), &error))
            showStatus(error);
    } else if (const Note *note = m_session->note(node->noteId); note) {
        bool ok = false;
        const QString title = QInputDialog::getText(this, tr("重命名笔记"), tr("标题"),
                                                    QLineEdit::Normal, note->title, &ok);
        if (!ok || title.trimmed().isEmpty())
            return;
        Note updated = *note;
        updated.title = title.trimmed();
        QString error;
        if (!m_session->updateNote(updated, &error))
            showStatus(error);
    }
    refreshReadingData();
}

void ReaderWorkspace::moveSelectedNoteNode()
{
    if (!m_session)
        return;
    const QString nodeId = selectedNoteNodeId();
    const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
    if (!node)
        return;
    QStringList labels;
    QStringList ids;
    labels << tr("书库根目录");
    ids << QString();
    std::function<void(const QString &, const QString &)> collect =
        [&](const QString &parentId, const QString &prefix) {
            const QStringList children = m_session->noteTree().childIds(parentId);
            for (const QString &childId : children) {
                const NoteTreeNode *child = m_session->noteTree().findNode(childId);
                if (!child || child->type != NoteNodeType::Group || child->id == nodeId
                    || m_session->noteTree().isDescendant(child->id, nodeId))
                    continue;
                QString label = prefix + (child->title.isEmpty() ? tr("未命名分组") : child->title)
                    + QStringLiteral(" · ") + child->id.left(6);
                labels << label;
                ids << child->id;
                collect(child->id, prefix + (child->title.isEmpty() ? tr("未命名分组") : child->title)
                                      + QStringLiteral(" / "));
            }
        };
    collect(QString(), QString());
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("移动笔记节点"), tr("目标分组"),
                                                 labels, 0, false, &ok);
    if (!ok)
        return;
    const int index = labels.indexOf(choice);
    if (index < 0)
        return;
    QString error;
    if (!m_session->moveTreeNode(nodeId, ids.at(index), &error))
        showStatus(error);
    refreshReadingData();
}

void ReaderWorkspace::deleteSelectedNoteNode()
{
    if (!m_session)
        return;
    const QString nodeId = selectedNoteNodeId();
    const NoteTreeNode *node = m_session->noteTree().findNode(nodeId);
    if (!node)
        return;
    QString error;
    if (node->type == NoteNodeType::Group) {
        if (QMessageBox::question(this, tr("删除分组"),
                                  tr("删除分组“%1”？直接子节点会上移到上一级。")
                                      .arg(node->title),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        if (!m_session->deleteGroup(nodeId, true, &error))
            showStatus(error);
    } else if (const Note *note = m_session->note(node->noteId); note) {
        const Annotation *annotation = note->annotationId.isEmpty()
                                           ? nullptr
                                           : m_session->view()->annotations()->find(note->annotationId);
        const bool removePoint = annotation && annotation->type == AnnotationType::NotePoint;
        const QString message = removePoint
            ? tr("删除笔记“%1”会同时删除对应的笔记点。").arg(note->displayTitle())
            : tr("删除笔记“%1”？关联的标注会保留。").arg(note->displayTitle());
        if (QMessageBox::question(this, tr("删除笔记"), message,
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
        if (m_session->deleteNote(note->id, removePoint, &error)) {
            if (removePoint)
                m_view->selectAnnotation(QString());
        } else {
            showStatus(error);
        }
    }
    refreshReadingData();
}

void ReaderWorkspace::showNoteContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_noteTreeWidget ? m_noteTreeWidget->itemAt(pos) : nullptr;
    if (item)
        m_noteTreeWidget->setCurrentItem(item);
    QMenu menu(this);
    QAction *newNote = menu.addAction(tr("新建笔记"));
    QAction *newGroup = menu.addAction(tr("新建分组"));
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("重命名"));
    QAction *move = menu.addAction(tr("移动到…"));
    QAction *remove = menu.addAction(tr("删除"));
    const QString contextId = item ? item->data(0, Qt::UserRole).toString() : QString();
    if (!item || contextId.isEmpty() || contextId == QStringLiteral("__book_root__")) {
        rename->setEnabled(false);
        move->setEnabled(false);
        remove->setEnabled(false);
    }
    QAction *chosen = menu.exec(m_noteTreeWidget->viewport()->mapToGlobal(pos));
    if (chosen == newNote)
        createIndependentNote(selectedParentGroupId());
    else if (chosen == newGroup)
        createGroupUnder(selectedParentGroupId());
    else if (chosen == rename)
        renameSelectedNoteNode();
    else if (chosen == move)
        moveSelectedNoteNode();
    else if (chosen == remove)
        deleteSelectedNoteNode();
}

bool ReaderWorkspace::isEditingNote() const
{
    return m_noteDialog && m_noteDialog->isVisible() && m_noteDialog->hasUnsavedChanges();
}

void ReaderWorkspace::beginNoteEdit()
{
    if (const Annotation *annotation = currentAnnotation()) {
        openNoteForAnnotation(annotation->id);
        return;
    }
    const QString nodeId = selectedNoteNodeId();
    if (!nodeId.isEmpty()) {
        if (const NoteTreeNode *node = m_session->noteTree().findNode(nodeId)) {
            if (node->type == NoteNodeType::Note) {
                openNoteEditor(node->noteId);
                return;
            }
            createIndependentNote(node->id);
            return;
        }
    }
    createIndependentNote(selectedParentGroupId());
}

void ReaderWorkspace::commitNoteEdit()
{
    QString error;
    if (!commitPendingNote(&error)) {
        if (!error.isEmpty())
            showStatus(error);
        return;
    }
    if (m_noteDialog && m_noteDialog->isVisible())
        m_noteDialog->accept();
    refreshReadingData();
}

bool ReaderWorkspace::commitPendingNote(QString *errorMessage)
{
    if (!m_noteDialog || !m_noteDialog->hasUnsavedChanges())
        return true;
    return m_noteDialog->commitIfDirty(errorMessage);
}

void ReaderWorkspace::cancelNoteEdit()
{
    if (m_noteDialog)
        m_noteDialog->reject();
    refreshReadingData();
}

void ReaderWorkspace::refreshReadingData()
{
    refreshAnnotationList();
    refreshNoteTree();
    updateNotePreview();
    updateDetailPanel();
    updateContextToolBar();
    updateBottomStatus();
}

void ReaderWorkspace::onToolChanged(PdfView::Tool tool)
{
    updateFloatingToolBars();
    flushPendingStyle();
    updateToolActions();
    if (tool != PdfView::Tool::Select && m_view && !m_view->selectedAnnotationId().isEmpty())
        m_view->selectAnnotation(QString());
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

void ReaderWorkspace::updateContextBarHeight()
{
    if (!m_contextWidget || !m_contextToolBar)
        return;
    const QWidget *controls[] = {m_contextHint, m_colorWidget, m_fillWidget,
                                 m_widthWidget, m_opacityWidget, m_lineStyleWidget,
                                 m_blendWidget, m_eraserWidget};
    int contentHeight = 0;
    for (const QWidget *widget : controls) {
        if (widget)
            contentHeight = qMax(contentHeight, widget->sizeHint().height());
    }
    int margins = 4;
    if (QLayout *layout = m_contextWidget->layout()) {
        const QMargins m = layout->contentsMargins();
        margins = m.top() + m.bottom();
    }
    const int widgetHeight = contentHeight + margins;
    m_contextWidget->setFixedHeight(widgetHeight);
    int barExtra = 4;
    if (QLayout *barLayout = m_contextToolBar->layout()) {
        const QMargins m = barLayout->contentsMargins();
        barExtra = m.top() + m.bottom();
    }
    m_contextToolBar->setFixedHeight(widgetHeight + barExtra);
}

void ReaderWorkspace::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        updateContextBarHeight();
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
        hint = tr("按住拖动连续擦除；附有笔记的标注将被跳过。");
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
        m_pageSpin->setText(QString::number(page + 1));
    }
    if (m_prevPageButton)
        m_prevPageButton->setEnabled(page > 0);
    if (m_nextPageButton)
        m_nextPageButton->setEnabled(page + 1 < m_view->pageCount());
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
    if (!m_view || !m_view->hasDocument())
        return;
    m_view->goToPage(qBound(1, oneBasedPage, qMax(1, m_view->pageCount())) - 1);
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
        if (m_pageValidator)
            m_pageValidator->setTop(qMax(1, m_view->pageCount()));
        const QSignalBlocker blocker(m_pageSpin);
        m_pageSpin->setText(QString::number(m_view->currentPage() + 1));
    }
    if (m_pageTotalLabel)
        m_pageTotalLabel->setText(QStringLiteral("/ %1").arg(qMax(1, m_view->pageCount())));
    const QVector<int> spread = m_view->layout().spreadPages(m_view->currentPage());
    const int spreadFirst = spread.isEmpty() ? m_view->currentPage() : spread.first();
    const int spreadLast = spread.isEmpty() ? m_view->currentPage() : spread.last();
    if (m_prevPageButton)
        m_prevPageButton->setEnabled(spreadFirst > 0);
    if (m_nextPageButton)
        m_nextPageButton->setEnabled(spreadLast + 1 < m_view->pageCount());
    if (m_zoomCombo) {
        const QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setEditText(QStringLiteral("%1%").arg(qRound(m_view->zoom() * 100.0)));
    }
    if (m_saveStateLabel)
        m_saveStateLabel->setText(m_session && m_session->isDirty() ? tr("未保存") : tr("已保存"));
    if (m_statusInfoLabel) {
        const PageLayoutMode mode = m_view->layoutMode();
        const QString layoutName = pageLayoutModeDisplayName(mode);
        m_statusInfoLabel->setText(tr("第 %1 / %2 页 · %3 · %4%")
                                       .arg(m_view->currentPage() + 1)
                                       .arg(qMax(1, m_view->pageCount()))
                                       .arg(layoutName)
                                       .arg(qRound(m_view->zoom() * 100.0)));
    }
}

void ReaderWorkspace::saveCurrent()
{
    if (!m_session)
        return;
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
    if (m_noteDialog)
        m_noteDialog->insertAssetMarkdown(relative, QFileInfo(path).completeBaseName());
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
    if (m_noteDialog)
        m_noteDialog->insertAssetMarkdown(relative, tr("区域截图"));
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
    outer->addLayout(row);

    auto *controls = new QHBoxLayout;
    m_searchCount = new QLabel(bar);
    controls->addWidget(m_searchCount, 1);
    auto *prev = new QToolButton(bar);
    prev->setText(QStringLiteral("↑"));
    prev->setToolTip(tr("上一个结果"));
    prev->setAutoRaise(true);
    controls->addWidget(prev);
    auto *next = new QToolButton(bar);
    next->setText(QStringLiteral("↓"));
    next->setToolTip(tr("下一个结果"));
    next->setAutoRaise(true);
    controls->addWidget(next);
    auto *close = new QToolButton(bar);
    close->setIcon(IconManager::instance()->icon(QStringLiteral("x")));
    close->setAutoRaise(true);
    close->setToolTip(tr("关闭搜索"));
    controls->addWidget(close);
    outer->addLayout(controls);

    m_searchResults = new QListWidget(bar);
    m_searchResults->setObjectName(QStringLiteral("readerSearchResults"));
    m_searchResults->setWordWrap(false);
    outer->addWidget(m_searchResults, 1);

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

    auto *nextResult = new QShortcut(QKeySequence(Qt::Key_F3), this);
    nextResult->setContext(Qt::WidgetWithChildrenShortcut);
    connect(nextResult, &QShortcut::activated, this, &ReaderWorkspace::nextSearchResult);
    auto *previousResult = new QShortcut(
        QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    previousResult->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousResult, &QShortcut::activated, this,
            &ReaderWorkspace::previousSearchResult);
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
    if (!m_searchBar || !m_leftTabs)
        return;
    const int searchIndex = m_leftTabs->indexOf(m_searchBar);
    if (searchIndex < 0)
        return;
    if (m_leftToggle && !m_leftToggle->isChecked())
        m_leftToggle->setChecked(true);
    if (m_leftDock && !m_leftDock->isVisible())
        m_leftDock->show();

    if (m_leftTabs->currentIndex() == searchIndex) {
        if (m_searchModel)
            m_searchModel->setSearchString(QString());
        if (m_view)
            m_view->clearSearchHighlights();
        if (m_searchResults)
            m_searchResults->clear();
        if (m_searchCount)
            m_searchCount->clear();
        m_searchHighlightRects.clear();
        m_leftTabs->setCurrentIndex(qBound(0, m_previousLeftTab, m_leftTabs->count() - 1));
        return;
    }
    m_previousLeftTab = m_leftTabs->currentIndex();
    m_leftTabs->setCurrentIndex(searchIndex);
    if (m_searchEdit) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void ReaderWorkspace::openSearchPanel()
{
    if (!m_searchBar || !m_leftTabs)
        return;
    if (m_fullscreen)
        emit fullscreenRequested();
    const int searchIndex = m_leftTabs->indexOf(m_searchBar);
    if (searchIndex < 0)
        return;
    if (m_leftToggle && !m_leftToggle->isChecked())
        m_leftToggle->setChecked(true);
    if (m_leftDock && !m_leftDock->isVisible())
        m_leftDock->show();
    if (m_leftTabs->currentIndex() != searchIndex) {
        m_previousLeftTab = m_leftTabs->currentIndex();
        m_leftTabs->setCurrentIndex(searchIndex);
    }
    if (m_searchEdit) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
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
    const QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty())
        return;
    if (m_searchModel->searchString() != query)
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
    if (m_searchCount) {
        if (count > 0 && actual < count)
            m_searchCount->setText(tr("%1 / %2 处").arg(actual).arg(count));
        else
            m_searchCount->setText(count > 0 ? tr("%1 处").arg(count) : tr("无结果"));
    }
    if (count == 0 && m_view)
        showStatus(tr("没有匹配文本，可能没有可搜索文字层"));
    m_searchHighlightRects = highlights;
    if (m_searchResults->count() > 0 && m_searchResults->currentRow() < 0)
        m_searchResults->setCurrentRow(0);
    if (m_view) {
        int currentPage = -1;
        QVector<QRectF> currentRects;
        if (QListWidgetItem *item = m_searchResults->currentItem()) {
            const int index = item->data(Qt::UserRole).toInt();
            if (index >= 0 && index < m_searchModel->count()) {
                const QPdfLink link = m_searchModel->resultAtIndex(index);
                currentPage = link.page();
                currentRects = link.rectangles();
            }
        }
        m_view->setSearchHighlights(m_searchHighlightRects, currentPage, currentRects);
    }
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
    const QVector<QRectF> currentRects = link.rectangles();
    if (!currentRects.isEmpty())
        point = currentRects.first().center();
    m_view->setSearchHighlights(m_searchHighlightRects, link.page(), currentRects);
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

void ReaderWorkspace::onAboutToNavigate(int fromPage, const QPointF &fromPoint,
                                         int targetPage, const QPointF &targetPoint,
                                         int reason)
{
    Q_UNUSED(targetPage)
    Q_UNUSED(targetPoint)
    const auto navReason = static_cast<PdfView::NavigationReason>(reason);
    m_pendingHistoryValid = false;
    if (navReason == PdfView::NavigationReason::History
        || navReason == PdfView::NavigationReason::Restore
        || navReason == PdfView::NavigationReason::Internal
        || navReason == PdfView::NavigationReason::Programmatic)
        return;
    m_pendingHistoryStart = {fromPage, fromPoint, QStringLiteral("当前位置")};
    m_pendingHistoryValid = true;
}

namespace {
QString navigationReasonLabel(PdfView::NavigationReason reason)
{
    switch (reason) {
    case PdfView::NavigationReason::PageInput: return QStringLiteral("页码跳转");
    case PdfView::NavigationReason::Outline: return QStringLiteral("目录跳转");
    case PdfView::NavigationReason::Thumbnail: return QStringLiteral("缩略图跳转");
    case PdfView::NavigationReason::Search: return QStringLiteral("搜索结果");
    case PdfView::NavigationReason::Bookmark: return QStringLiteral("书签");
    case PdfView::NavigationReason::Annotation: return QStringLiteral("批注定位");
    case PdfView::NavigationReason::History: return QStringLiteral("历史导航");
    case PdfView::NavigationReason::Restore: return QStringLiteral("恢复阅读位置");
    case PdfView::NavigationReason::Internal: return QStringLiteral("内部调整");
    case PdfView::NavigationReason::Programmatic: return QStringLiteral("程序跳转");
    }
    return QStringLiteral("跳转");
}
} // namespace

void ReaderWorkspace::onNavigated(int pageIndex, const QPointF &pagePoint, int reason)
{
    const auto navReason = static_cast<PdfView::NavigationReason>(reason);
    if (navReason == PdfView::NavigationReason::History
        || navReason == PdfView::NavigationReason::Restore
        || navReason == PdfView::NavigationReason::Internal
        || navReason == PdfView::NavigationReason::Programmatic) {
        m_pendingHistoryValid = false;
        return;
    }

    NavPoint target{pageIndex, pagePoint};
    target.label = navigationReasonLabel(navReason);
    if (m_pendingHistoryValid) {
        if (m_historyIndex < 0) {
            m_history.append(m_pendingHistoryStart);
            m_historyIndex = 0;
        } else if (m_history.at(m_historyIndex).page != m_pendingHistoryStart.page
                   || m_history.at(m_historyIndex).point != m_pendingHistoryStart.point) {
            // The user may have scrolled after the last explicit jump. Use the
            // real departure anchor so Back returns to where reading actually
            // happened, not to a stale history node.
            m_history[m_historyIndex] = m_pendingHistoryStart;
        }
    }
    m_pendingHistoryValid = false;

    if (m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size())
        m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
    if (m_historyIndex >= 0) {
        const NavPoint &current = m_history.at(m_historyIndex);
        if (current.page == target.page && current.point == target.point) {
            updateHistoryActions();
            return;
        }
    }
    m_history.append(target);
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

void ReaderWorkspace::clearNavigationHistory()
{
    m_history.clear();
    m_historyIndex = -1;
    m_pendingHistoryValid = false;
    if (m_view && m_view->hasDocument()) {
        int page = m_view->currentPage();
        const QPointF point = m_view->viewportAnchor(&page);
        m_history.append({page, point, QStringLiteral("当前位置")});
        m_historyIndex = 0;
    }
    updateHistoryActions();
}

void ReaderWorkspace::updateHistoryActions()
{
    if (m_backAction) {
        m_backAction->setEnabled(m_historyIndex > 0);
        if (m_historyIndex > 0) {
            const NavPoint &p = m_history.at(m_historyIndex - 1);
            m_backAction->setToolTip(QStringLiteral("返回：%1 · 第 %2 页").arg(p.label).arg(p.page + 1));
        } else {
            m_backAction->setToolTip(QStringLiteral("导航后退"));
        }
    }
    if (m_forwardAction) {
        const bool canForward = m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size();
        m_forwardAction->setEnabled(canForward);
        if (canForward) {
            const NavPoint &p = m_history.at(m_historyIndex + 1);
            m_forwardAction->setToolTip(QStringLiteral("前进：%1 · 第 %2 页").arg(p.label).arg(p.page + 1));
        } else {
            m_forwardAction->setToolTip(QStringLiteral("导航前进"));
        }
    }
}

void ReaderWorkspace::onLayoutModeChanged(PageLayoutMode mode)
{
    Q_UNUSED(mode)
    updateLayoutControls();
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
    if (fullscreen) {
        m_fullscreenAnchorPage = m_view->currentPage();
        m_fullscreenAnchorPoint = m_view->viewportAnchor(&m_fullscreenAnchorPage);
        m_fullscreenSplitterSizes = m_splitter->sizes();
        m_fullscreenLeftVisible = m_leftDock && m_leftDock->isVisible();
        m_fullscreenRightVisible = m_rightDock && m_rightDock->isVisible();
        m_primaryToolBar->hide();
        m_contextToolBar->hide();
        if (m_leftDock)
            m_leftDock->hide();
        if (m_rightDock)
            m_rightDock->hide();
        if (m_bottomBar)
            m_bottomBar->hide();
        if (m_exitFullscreenButton) {
            m_exitFullscreenButton->show();
            m_exitFullscreenButton->raise();
            positionFullscreenExit();
        }
        // Fullscreen resizes the viewport; restore the same page anchor after
        // the new layout has been computed instead of jumping to page top.
        QTimer::singleShot(0, this, [this]() {
            if (m_fullscreen && m_fullscreenAnchorPage >= 0)
                m_view->restoreViewportAnchor(m_fullscreenAnchorPage, m_fullscreenAnchorPoint);
        });
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
        if (m_exitFullscreenButton)
            m_exitFullscreenButton->hide();
    }
    m_fullscreen = fullscreen;
}

void ReaderWorkspace::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionFullscreenExit();
    updateThumbnailLayout();
}

void ReaderWorkspace::updateThumbnailLayout()
{
    if (!m_thumbnailList)
        return;
    const int available = qMax(176, m_thumbnailList->viewport()->width() - 12);
    const int columns = qMax(1, available / 176);
    const int cellWidth = columns == 1 ? available : available / columns;
    m_thumbnailList->setGridSize(QSize(cellWidth, 244));
}

void ReaderWorkspace::positionFullscreenExit()
{
    if (!m_exitFullscreenButton || !m_view)
        return;
    const QSize hint = m_exitFullscreenButton->sizeHint();
    const int x = qMax(8, m_view->width() - hint.width() - 16);
    m_exitFullscreenButton->move(x, 12);
    m_exitFullscreenButton->resize(hint);
    m_exitFullscreenButton->raise();
}


bool ReaderWorkspace::closeTransientUi()
{
    if (!m_view)
        return false;
    if (m_view->handleEscape())
        return true;
    if (m_searchBar && m_searchBar->isVisible()) {
        toggleSearchBar();
        return true;
    }
    return false;
}

} // namespace pano
