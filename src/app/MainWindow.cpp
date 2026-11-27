#include "app/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QSignalBlocker>
#include <QSysInfo>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "app/LibraryView.h"
#include "app/ReaderWorkspace.h"
#include "document/DocumentSession.h"
#include "library/LibraryManager.h"
#include "pdf/PageRenderer.h"
#include "reader/PdfView.h"
#include "settings/SettingsDialog.h"
#include "settings/SettingsManager.h"
#include "settings/ThemeManager.h"
#include "storage/LibraryStore.h"

namespace pano {

namespace {

QString libraryDisplayName(const QString &path)
{
    if (path.isEmpty())
        return QStringLiteral("书库");
    return QFileInfo(path).fileName();
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_manager = new LibraryManager(this);
    m_settings = new SettingsManager(this);
    m_themes = new ThemeManager(this);
    m_settings->load();

    buildUi();
    buildActions();
    buildMenus();
    buildToolBar();
    buildStatusBar();
    applyThemeFromSettings();

    connect(m_manager, &LibraryManager::libraryChanged, this, &MainWindow::updateLibraryTab);
    connect(m_manager, &LibraryManager::libraryChanged, this, &MainWindow::updateWindowTitle);
    connect(m_settings, &SettingsManager::settingsChanged, this, &MainWindow::applyThemeFromSettings);

    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("PANO"));
    resize(1440, 900);
    m_tabs->setCurrentIndex(0);
    updateLibraryTab();
    updateActions();
}

MainWindow::~MainWindow()
{
    for (DocumentSession *session : m_sessions)
        session->flush();
    while (!m_sessions.isEmpty()) {
        DocumentSession *session = m_sessions.takeFirst();
        const int index = m_tabs ? m_tabs->indexOf(session->workspace()) : -1;
        if (index > 0)
            m_tabs->removeTab(index);
        delete session;
    }
}

void MainWindow::buildUi()
{
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->setMovable(false);
    m_tabs->setTabsClosable(true);
    m_tabs->setElideMode(Qt::ElideRight);
    setCentralWidget(m_tabs);

    m_libraryPage = new QStackedWidget(m_tabs);
    m_welcome = buildWelcomePage();
    m_libraryView = new LibraryView(m_manager, m_libraryPage);
    m_libraryPage->addWidget(m_welcome);
    m_libraryPage->addWidget(m_libraryView);
    m_tabs->addTab(m_libraryPage, tr("书库"));
    m_tabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);

    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(m_libraryView, &LibraryView::bookOpenRequested, this, &MainWindow::openBookFromLibrary);
    connect(m_libraryView, &LibraryView::statusMessage, this, &MainWindow::setStatus);
}

QWidget *MainWindow::buildWelcomePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);
    auto *title = new QLabel(tr("PANO"), page);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(28);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    auto *subtitle = new QLabel(tr("本地优先的 PDF 阅读与标注工作台"), page);
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addSpacing(24);
    auto *newButton = new QPushButton(tr("新建书库…"), page);
    newButton->setMinimumWidth(220);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::newLibrary);
    layout->addWidget(newButton, 0, Qt::AlignCenter);

    auto *openButton = new QPushButton(tr("打开已有书库…"), page);
    openButton->setMinimumWidth(220);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openLibrary);
    layout->addWidget(openButton, 0, Qt::AlignCenter);

    layout->addSpacing(16);
    auto *hint = new QLabel(tr("书库数据保存在你选择的目录中；PANO 不依赖账号或网络。"), page);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    layout->addWidget(hint);
    return page;
}

void MainWindow::buildActions()
{
    m_newLibraryAction = new QAction(tr("新建书库…"), this);
    connect(m_newLibraryAction, &QAction::triggered, this, &MainWindow::newLibrary);
    m_openLibraryAction = new QAction(tr("打开书库…"), this);
    connect(m_openLibraryAction, &QAction::triggered, this, &MainWindow::openLibrary);

    m_importAction = new QAction(tr("导入 PDF…"), this);
    m_importAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
    connect(m_importAction, &QAction::triggered, this, &MainWindow::importIntoLibrary);

    m_saveAction = new QAction(tr("保存标注"), this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveCurrent);

    m_closeTabAction = new QAction(tr("关闭当前标签"), this);
    m_closeTabAction->setShortcut(QKeySequence::Close);
    connect(m_closeTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);

    m_quitAction = new QAction(tr("退出"), this);
    m_quitAction->setShortcut(QKeySequence::Quit);
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);

    m_undoAction = new QAction(tr("撤销"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (currentSession())
            currentSession()->view()->undoStack()->undo();
    });

    m_redoAction = new QAction(tr("重做"), this);
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (currentSession())
            currentSession()->view()->undoStack()->redo();
    });

    m_deleteAction = new QAction(tr("删除标注"), this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_deleteAction, &QAction::triggered, this, [this]() {
        if (currentSession())
            currentSession()->view()->deleteSelected();
    });

    m_editNoteAction = new QAction(tr("编辑笔记"), this);
    m_editNoteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    m_editNoteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(m_editNoteAction, &QAction::triggered, this, [this]() {
        if (currentWorkspace())
            currentWorkspace()->beginNoteEdit();
    });

    m_zoomInAction = new QAction(tr("放大"), this);
    m_zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered, this, &MainWindow::zoomIn);
    m_zoomOutAction = new QAction(tr("缩小"), this);
    m_zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered, this, &MainWindow::zoomOut);
    m_fitWidthAction = new QAction(tr("适合宽度"), this);
    m_fitWidthAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    connect(m_fitWidthAction, &QAction::triggered, this, &MainWindow::fitWidth);
    m_fitPageAction = new QAction(tr("适合页面"), this);
    m_fitPageAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    connect(m_fitPageAction, &QAction::triggered, this, &MainWindow::fitPage);
    m_rotateAction = new QAction(tr("顺时针旋转"), this);
    m_rotateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(m_rotateAction, &QAction::triggered, this, &MainWindow::rotateClockwise);

    m_settingsAction = new QAction(tr("首选项…"), this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showPreferences);

    m_shortcutsAction = new QAction(tr("快捷键"), this);
    connect(m_shortcutsAction, &QAction::triggered, this, &MainWindow::showShortcuts);
    m_aboutAction = new QAction(tr("关于 PANO"), this);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    m_diagnosticsAction = new QAction(tr("诊断信息"), this);
    connect(m_diagnosticsAction, &QAction::triggered, this, &MainWindow::showDiagnostics);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    const struct {
        PdfView::Tool tool;
        const char *text;
        const char *shortcut;
    } tools[] = {
        {PdfView::Tool::Select, "选择", "V"},
        {PdfView::Tool::TextSelect, "文本选择", "T"},
        {PdfView::Tool::Highlight, "高亮", "H"},
        {PdfView::Tool::Underline, "下划线", "U"},
        {PdfView::Tool::StrikeOut, "删除线", "S"},
        {PdfView::Tool::Line, "直线", "L"},
        {PdfView::Tool::Rectangle, "矩形", "R"},
        {PdfView::Tool::Ellipse, "椭圆", "O"},
        {PdfView::Tool::Pencil, "铅笔", "P"},
        {PdfView::Tool::Highlighter, "荧光笔", "M"},
        {PdfView::Tool::Eraser, "橡皮", "E"},
        {PdfView::Tool::Note, "笔记", "N"},
    };
    for (const auto &entry : tools) {
        auto *action = new QAction(QString::fromUtf8(entry.text), this);
        action->setCheckable(true);
        action->setData(static_cast<int>(entry.tool));
        action->setShortcut(QKeySequence(QString::fromLatin1(entry.shortcut)));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setToolTip(QStringLiteral("%1 (%2)").arg(QString::fromUtf8(entry.text),
                                                          QString::fromLatin1(entry.shortcut)));
        m_toolGroup->addAction(action);
        m_toolActions.append(action);
        connect(action, &QAction::triggered, this, &MainWindow::setToolFromAction);
    }
    m_toolActions.first()->setChecked(true);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(m_newLibraryAction);
    fileMenu->addAction(m_openLibraryAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_closeTabAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("编辑"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_deleteAction);
    editMenu->addAction(m_editNoteAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addAction(m_fitPageAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_rotateAction);

    QMenu *annotationMenu = menuBar()->addMenu(tr("标注"));
    for (QAction *action : m_toolActions)
        annotationMenu->addAction(action);
    annotationMenu->addSeparator();
    annotationMenu->addAction(m_editNoteAction);
    annotationMenu->addAction(m_deleteAction);

    QMenu *settingsMenu = menuBar()->addMenu(tr("设置"));
    settingsMenu->addAction(m_settingsAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(m_shortcutsAction);
    helpMenu->addAction(m_diagnosticsAction);
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::buildToolBar()
{
    auto *mainToolBar = addToolBar(tr("主工具栏"));
    mainToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    mainToolBar->addAction(m_openLibraryAction);
    mainToolBar->addAction(m_importAction);
    mainToolBar->addAction(m_saveAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(m_undoAction);
    mainToolBar->addAction(m_redoAction);
    mainToolBar->addAction(m_deleteAction);
    mainToolBar->addSeparator();

    m_pageLabel = new QLabel(tr("页码"), this);
    mainToolBar->addWidget(m_pageLabel);
    m_pageSpin = new QSpinBox(this);
    m_pageSpin->setRange(1, 1);
    m_pageSpin->setMaximumWidth(80);
    connect(m_pageSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::pageChanged);
    mainToolBar->addWidget(m_pageSpin);

    m_zoomCombo = new QComboBox(this);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setMinimumWidth(100);
    m_zoomCombo->addItem(QStringLiteral("50%"), 50);
    m_zoomCombo->addItem(QStringLiteral("75%"), 75);
    m_zoomCombo->addItem(QStringLiteral("100%"), 100);
    m_zoomCombo->addItem(QStringLiteral("125%"), 125);
    m_zoomCombo->addItem(QStringLiteral("150%"), 150);
    m_zoomCombo->addItem(QStringLiteral("200%"), 200);
    m_zoomCombo->addItem(QStringLiteral("300%"), 300);
    connect(m_zoomCombo->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::zoomEdited);
    connect(m_zoomCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
        if (!currentSession())
            return;
        currentSession()->setZoomMode(ZoomMode::Fixed);
        currentSession()->view()->setZoom(m_zoomCombo->itemData(index).toInt() / 100.0);
    });
    mainToolBar->addWidget(m_zoomCombo);
    mainToolBar->addAction(m_fitWidthAction);
    mainToolBar->addAction(m_fitPageAction);
    mainToolBar->addAction(m_rotateAction);

    auto *toolsToolBar = new QToolBar(tr("标注工具"), this);
    addToolBar(Qt::LeftToolBarArea, toolsToolBar);
    toolsToolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolsToolBar->setMovable(false);
    for (QAction *action : m_toolActions)
        toolsToolBar->addAction(action);
}

void MainWindow::buildStatusBar()
{
    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::applyThemeFromSettings()
{
    ThemeId id = ThemeId::ModernLight;
    if (!ThemeManager::fromKey(m_settings->settings().themeKey, &id))
        id = ThemeId::ModernLight;
    m_themes->setCurrentTheme(id, qApp);
    QFont font = QApplication::font();
    font.setPointSize(m_settings->settings().uiFontSize);
    QApplication::setFont(font);
}

void MainWindow::newLibrary()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择新书库目录"), documents.isEmpty() ? QDir::homePath() : documents,
        QFileDialog::ShowDirsOnly);
    if (directory.isEmpty())
        return;
    openLibraryPath(directory, true);
}

void MainWindow::openLibrary()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("打开书库"), m_settings->settings().lastLibraryPath, QFileDialog::ShowDirsOnly);
    if (!directory.isEmpty())
        openLibraryPath(directory, false);
}

bool MainWindow::openLibraryPath(const QString &path, bool createIfMissing)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const QString previousPath = m_manager->isOpen() ? m_manager->rootPath() : QString();
    if (createIfMissing) {
        const QFileInfo info(absolutePath);
        if (info.exists() && (!info.isDir()
                              || !QDir(absolutePath).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())) {
            showError(tr("新建书库失败"), tr("所选目录必须是空目录或不存在的目录。"));
            return false;
        }
    } else if (absolutePath != previousPath) {
        // Validate and lock the target before touching the current sessions.
        // A bad path or a locked library must not close the open library.
        LibraryStore probeStore;
        LibraryManager probe(&probeStore);
        QString probeError;
        if (!probe.openLibrary(absolutePath, &probeError)) {
            showError(tr("打开书库失败"), probeError);
            return false;
        }
        probe.closeLibrary();
    }

    if (!closeAllSessions(true))
        return false;
    QString error;
    const bool ok = createIfMissing ? m_manager->createLibrary(absolutePath, &error)
                                    : m_manager->openLibrary(absolutePath, &error);
    if (!ok) {
        if (!previousPath.isEmpty() && previousPath != absolutePath) {
            QString ignored;
            m_manager->openLibrary(previousPath, &ignored);
            m_libraryView->refresh();
            updateLibraryTab();
        }
        showError(createIfMissing ? tr("新建书库失败") : tr("打开书库失败"), error);
        return false;
    }
    m_settings->setLastLibraryPath(m_manager->rootPath());
    QString saveError;
    m_settings->save(&saveError);
    m_libraryView->refresh();
    updateLibraryTab();
    updateWindowTitle();
    setStatus(tr("书库已打开：%1").arg(m_manager->rootPath()));
    return true;
}

void MainWindow::importIntoLibrary()
{
    if (!m_manager->isOpen()) {
        setStatus(tr("请先新建或打开书库。"));
        return;
    }
    m_tabs->setCurrentIndex(0);
    m_libraryView->importBooks();
}

void MainWindow::updateLibraryTab()
{
    if (!m_libraryPage)
        return;
    if (m_manager->isOpen()) {
        m_libraryPage->setCurrentWidget(m_libraryView);
        m_tabs->setTabText(0, tr("书库 · %1").arg(libraryDisplayName(m_manager->rootPath())));
    } else {
        m_libraryPage->setCurrentWidget(m_welcome);
        m_tabs->setTabText(0, tr("书库"));
    }
    updateActions();
}

bool MainWindow::openBook(const QString &bookId)
{
    if (!m_manager->isOpen()) {
        setStatus(tr("请先打开书库。"));
        return false;
    }
    for (int index = 1; index < m_tabs->count(); ++index) {
        auto *workspace = qobject_cast<ReaderWorkspace *>(m_tabs->widget(index));
        if (workspace && workspace->session() && workspace->session()->bookId() == bookId) {
            m_tabs->setCurrentIndex(index);
            return true;
        }
    }

    const BookInfo book = m_manager->bookInfo(bookId);
    auto *session = new DocumentSession(m_manager, book, this);
    QString error;
    if (!session->load(&error)) {
        delete session;
        showError(tr("无法打开书籍"), error);
        return false;
    }
    session->setAutosaveSeconds(m_settings->settings().autosaveSeconds);
    attachSession(session);
    m_settings->setLastBookId(bookId);
    QString saveError;
    m_settings->save(&saveError);
    return true;
}

void MainWindow::openBookFromLibrary(const QString &bookId)
{
    openBook(bookId);
}

void MainWindow::attachSession(DocumentSession *session)
{
    m_sessions.append(session);
    addReaderTab(session);
    connectSession(session);
    updateOpenTabs();
    updateActions();
}

void MainWindow::addReaderTab(DocumentSession *session)
{
    const int index = m_tabs->addTab(session->workspace(), session->title());
    m_tabs->setTabToolTip(index, session->book().pdfPath);
    m_tabs->setCurrentIndex(index);
    updateTabTitle(session);
}

void MainWindow::connectSession(DocumentSession *session)
{
    connect(session, &DocumentSession::dirtyChanged, this, [this, session](bool dirty) {
        Q_UNUSED(dirty)
        updateTabTitle(session);
        if (session == currentSession()) {
            updateWindowTitle();
            updateActions();
        }
    });
    connect(session, &DocumentSession::statusMessage, this, [this, session](const QString &message) {
        if (session == currentSession())
            setStatus(message);
    });
    connect(session, &DocumentSession::saveFailed, this, [this, session](const QString &message) {
        if (session == currentSession())
            showError(tr("保存失败"), message);
    });
    connect(session->workspace(), &ReaderWorkspace::statusMessage, this,
            [this, session](const QString &message) {
                if (session == currentSession())
                    setStatus(message);
            });

    PdfView *view = session->view();
    connect(view, &PdfView::pageChanged, this, [this, session](int) {
        if (session == currentSession())
            updateSessionStatus();
    });
    connect(view, &PdfView::zoomChanged, this, [this, session](qreal) {
        if (session == currentSession())
            updateSessionStatus();
    });
    connect(view, &PdfView::rotationChanged, this, [this, session](int) {
        if (session == currentSession())
            updateSessionStatus();
    });
    connect(view, &PdfView::toolChanged, this, [this, session](PdfView::Tool tool) {
        if (session != currentSession())
            return;
        for (QAction *action : m_toolActions) {
            if (action->data().toInt() == static_cast<int>(tool))
                action->setChecked(true);
        }
    });
    connect(view, &PdfView::annotationsChanged, this, [this, session]() {
        if (session == currentSession())
            updateActions();
    });
    connect(view, &PdfView::annotationSelectionChanged, this, [this, session](const QString &) {
        if (session == currentSession())
            updateActions();
    });
    connect(view, &PdfView::openFileRequested, this, [this](const QString &path) {
        importDroppedFiles({path});
    });
}

void MainWindow::detachSession(DocumentSession *session)
{
    if (!session)
        return;
    const int index = m_tabs->indexOf(session->workspace());
    if (index > 0)
        m_tabs->removeTab(index);
    m_sessions.removeAll(session);
    if (m_shortcutView && m_shortcutView == session->view())
        setShortcutScope(nullptr);
    delete session;
    updateOpenTabs();
    updateActions();
    updateWindowTitle();
}

void MainWindow::updateOpenTabs()
{
    if (!m_settings)
        return;
    QStringList books;
    for (DocumentSession *session : m_sessions) {
        if (session && !session->bookId().isEmpty() && !books.contains(session->bookId()))
            books.append(session->bookId());
    }
    m_settings->setLastOpenBooks(books);
    QString error;
    m_settings->save(&error);
}

DocumentSession *MainWindow::currentSession() const
{
    if (!m_tabs || m_tabs->currentIndex() <= 0)
        return nullptr;
    auto *workspace = qobject_cast<ReaderWorkspace *>(m_tabs->currentWidget());
    return workspace ? workspace->session() : nullptr;
}

ReaderWorkspace *MainWindow::currentWorkspace() const
{
    DocumentSession *session = currentSession();
    return session ? session->workspace() : nullptr;
}

void MainWindow::onTabChanged(int index)
{
    if (index <= 0)
        setShortcutScope(nullptr);
    else if (auto *workspace = qobject_cast<ReaderWorkspace *>(m_tabs->widget(index)))
        setShortcutScope(workspace->view());

    for (DocumentSession *session : m_sessions) {
        if (session->workspace() == m_tabs->currentWidget())
            continue;
        if (session->workspace()->isEditingNote())
            session->workspace()->commitNoteEdit();
        if (session->isDirty())
            session->flush();
    }
    updateActions();
    updateWindowTitle();
    updateSessionStatus();
}

void MainWindow::onTabCloseRequested(int index)
{
    if (index <= 0)
        return;
    auto *workspace = qobject_cast<ReaderWorkspace *>(m_tabs->widget(index));
    DocumentSession *session = workspace ? workspace->session() : nullptr;
    if (!session)
        return;
    if (!saveSession(session, true))
        return;
    m_tabs->removeTab(index);
    m_sessions.removeAll(session);
    if (m_shortcutView == session->view())
        setShortcutScope(nullptr);
    delete session;
    updateOpenTabs();
    updateActions();
    updateWindowTitle();
}


void MainWindow::closeCurrentTab()
{
    onTabCloseRequested(m_tabs->currentIndex());
}

bool MainWindow::saveSession(DocumentSession *session, bool promptOnFailure)
{
    if (!session)
        return true;
    if (session->workspace()->isEditingNote())
        session->workspace()->commitNoteEdit();
    if (!session->isDirty())
        return true;
    QString error;
    if (session->saveNow(&error))
        return true;
    if (!promptOnFailure)
        return false;
    const auto answer = QMessageBox::warning(
        this, tr("保存失败"),
        tr("无法保存《%1》的标注：\n\n%2\n\n如果继续关闭，未保存的修改会丢失。")
            .arg(session->title(), error),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Discard;
}

void MainWindow::saveCurrent()
{
    DocumentSession *session = currentSession();
    if (!session) {
        setStatus(tr("当前没有打开的书籍。"));
        return;
    }
    session->workspace()->commitNoteEdit();
    QString error;
    if (!session->saveNow(&error))
        showError(tr("保存失败"), error);
}

bool MainWindow::closeAllSessions(bool prompt)
{
    const QVector<DocumentSession *> sessions = m_sessions;
    for (DocumentSession *session : sessions) {
        if (!saveSession(session, prompt))
            return false;
    }
    while (!m_sessions.isEmpty())
        detachSession(m_sessions.first());
    return true;
}

void MainWindow::setToolFromAction()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action || !currentSession())
        return;
    const auto tool = static_cast<PdfView::Tool>(action->data().toInt());
    currentSession()->view()->setTool(tool);
    setStatus(tr("工具：%1").arg(action->text()));
}

void MainWindow::zoomIn()
{
    if (!currentSession())
        return;
    currentSession()->setZoomMode(ZoomMode::Fixed);
    currentSession()->view()->zoomIn();
}

void MainWindow::zoomOut()
{
    if (!currentSession())
        return;
    currentSession()->setZoomMode(ZoomMode::Fixed);
    currentSession()->view()->zoomOut();
}

void MainWindow::fitWidth()
{
    if (currentSession())
        currentSession()->setZoomMode(ZoomMode::FitWidth);
        currentSession()->view()->fitWidth();
}

void MainWindow::fitPage()
{
    if (!currentSession())
        return;
    currentSession()->setZoomMode(ZoomMode::FitPage);
    currentSession()->view()->fitPage();
}

void MainWindow::rotateClockwise()
{
    if (currentSession())
        currentSession()->view()->rotateBy(90);
}

void MainWindow::pageChanged(int page)
{
    if (!currentSession())
        return;
    currentSession()->view()->goToPage(page - 1);
}

void MainWindow::zoomEdited()
{
    if (!currentSession())
        return;
    QString text = m_zoomCombo->currentText().trimmed();
    text.remove(QLatin1Char('%'));
    bool ok = false;
    const double value = text.toDouble(&ok);
    if (!ok || value < 25.0 || value > 1200.0) {
        updateSessionStatus();
        return;
    }
    currentSession()->setZoomMode(ZoomMode::Fixed);
    currentSession()->view()->setZoom(value / 100.0);
}

void MainWindow::updateActions()
{
    DocumentSession *session = currentSession();
    const bool reader = session != nullptr;
    m_saveAction->setEnabled(reader);
    m_closeTabAction->setEnabled(reader);
    m_undoAction->setEnabled(reader && session->view()->undoStack()->canUndo());
    m_redoAction->setEnabled(reader && session->view()->undoStack()->canRedo());
    m_deleteAction->setEnabled(reader && !session->view()->selectedAnnotationId().isEmpty());
    m_editNoteAction->setEnabled(reader && !session->view()->selectedAnnotationId().isEmpty());
    m_zoomInAction->setEnabled(reader);
    m_zoomOutAction->setEnabled(reader);
    m_fitWidthAction->setEnabled(reader);
    m_fitPageAction->setEnabled(reader);
    m_rotateAction->setEnabled(reader);
    m_pageSpin->setEnabled(reader);
    m_zoomCombo->setEnabled(reader);
    for (QAction *action : m_toolActions)
        action->setEnabled(reader);
    m_importAction->setEnabled(m_manager && m_manager->isOpen());
    updateSessionStatus();
}

void MainWindow::updateSessionStatus()
{
    DocumentSession *session = currentSession();
    if (!session) {
        m_statusLabel->setText(tr("书库模式"));
        if (m_pageSpin) {
            QSignalBlocker blocker(m_pageSpin);
            m_pageSpin->setRange(1, 1);
            m_pageSpin->setValue(1);
        }
        if (m_zoomCombo) {
            QSignalBlocker blocker(m_zoomCombo);
            m_zoomCombo->setEditText(QString());
        }
        return;
    }
    PdfView *view = session->view();
    const int pageCount = std::max(1, view->pageCount());
    {
        QSignalBlocker blocker(m_pageSpin);
        m_pageSpin->setRange(1, pageCount);
        m_pageSpin->setValue(view->currentPage() + 1);
    }
    {
        QSignalBlocker blocker(m_zoomCombo);
        m_zoomCombo->setEditText(QStringLiteral("%1%").arg(qRound(view->zoom() * 100.0)));
    }
    m_pageLabel->setText(tr("页码 / %1").arg(pageCount));
    m_statusLabel->setText(tr("第 %1 / %2 页 · %3% · 旋转 %4° · 缓存 %5 MiB")
                               .arg(view->currentPage() + 1)
                               .arg(pageCount)
                               .arg(qRound(view->zoom() * 100.0))
                               .arg(view->rotation())
                               .arg(view->renderer()->cacheBytes() / (1024.0 * 1024.0), 0, 'f', 1));
}

void MainWindow::updateTabTitle(DocumentSession *session)
{
    if (!session)
        return;
    const int index = m_tabs->indexOf(session->workspace());
    if (index <= 0)
        return;
    m_tabs->setTabText(index, session->isDirty()
                                 ? tr("%1 *").arg(session->title())
                                 : session->title());
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("PANO");
    if (m_manager && m_manager->isOpen())
        title += QStringLiteral(" — %1").arg(libraryDisplayName(m_manager->rootPath()));
    DocumentSession *session = currentSession();
    if (session)
        title += QStringLiteral(" — %1[*]").arg(session->title());
    setWindowTitle(title);
    setWindowModified(session ? session->isDirty() : false);
}

void MainWindow::updateToolBarState()
{
    updateActions();
}

void MainWindow::updatePageControls()
{
    updateSessionStatus();
}

void MainWindow::setShortcutScope(QWidget *view)
{
    if (m_shortcutView) {
        for (QAction *action : m_toolActions)
            m_shortcutView->removeAction(action);
        for (QAction *action : {m_undoAction, m_redoAction, m_deleteAction, m_editNoteAction})
            m_shortcutView->removeAction(action);
    }
    m_shortcutView = view;
    if (view) {
        for (QAction *action : m_toolActions)
            view->addAction(action);
        for (QAction *action : {m_undoAction, m_redoAction, m_deleteAction, m_editNoteAction})
            view->addAction(action);
    }
}

void MainWindow::setStatus(const QString &message)
{
    statusBar()->showMessage(message, 5000);
}

void MainWindow::showError(const QString &title, const QString &message)
{
    QMessageBox::warning(this, title, message);
}

void MainWindow::showPreferences()
{
    SettingsDialog dialog(m_settings, m_themes, this);
    dialog.setAnnotationStyle(currentSession() ? currentSession()->view()->defaultStyle()
                                               : AnnotationStyle());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const AnnotationStyle style = dialog.annotationStyle();
    for (DocumentSession *session : m_sessions) {
        session->setAutosaveSeconds(m_settings->settings().autosaveSeconds);
        session->view()->setDefaultStyle(style);
    }
    updateActions();
    setStatus(tr("设置已保存。"));
}

void MainWindow::showShortcuts()
{
    QMessageBox::information(
        this, tr("快捷键"),
        tr("<b>工具</b><br>V 选择 · T 文本选择 · H 高亮 · U 下划线 · S 删除线<br>"
           "L 直线 · R 矩形 · O 椭圆 · P 铅笔 · M 荧光笔 · E 橡皮 · N 笔记<br><br>"
           "<b>编辑</b><br>Ctrl+Z 撤销 · Ctrl+Shift+Z 重做 · Delete 删除<br>"
           "Ctrl+C 复制选中文本 · Ctrl+E 编辑笔记<br><br>"
           "<b>视图</b><br>Ctrl++ 放大 · Ctrl+- 缩小 · Ctrl+1 适合宽度 · Ctrl+2 适合页面 · "
           "Ctrl+R 顺时针旋转<br><br><b>文件</b><br>Ctrl+O 导入 PDF · Ctrl+S 保存标注 · Ctrl+W 关闭标签"));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("关于 PANO"),
        tr("<h3>PANO</h3><p>版本 %1</p>"
           "<p>本地优先的 PDF 阅读与标注工作台。</p>"
           "<p>基于 Qt %2 · Qt PDF · C++20</p>")
            .arg(QApplication::applicationVersion(), QString::fromLatin1(qVersion())));
}

void MainWindow::showDiagnostics()
{
    QStringList lines;
    lines << tr("PANO %1").arg(QApplication::applicationVersion());
    lines << tr("Qt：%1").arg(QString::fromLatin1(qVersion()));
    lines << tr("平台：%1").arg(QSysInfo::prettyProductName());
    if (m_manager->isOpen()) {
        lines << tr("书库：%1").arg(m_manager->rootPath());
        lines << tr("书库 ID：%1").arg(m_manager->libraryId());
        lines << tr("书籍：%1 本").arg(m_manager->books(true).size());
        for (const QString &line : m_manager->diagnostics())
            lines << QStringLiteral("  · %1").arg(line);
    } else {
        lines << tr("书库：未打开");
    }
    lines << tr("打开会话：%1").arg(m_sessions.size());
    QMessageBox::information(this, tr("诊断信息"), lines.join(QLatin1Char('\n')));
}

void MainWindow::restoreLastSession()
{
    if (!m_manager->isOpen())
        return;
    const AppSettings &settings = m_settings->settings();
    QStringList books;
    if (settings.restoreOpenTabs)
        books = settings.lastOpenBooks;
    if (books.isEmpty() && !settings.lastBookId.isEmpty())
        books.append(settings.lastBookId);
    for (const QString &bookId : books) {
        if (m_manager->hasBook(bookId))
            openBook(bookId);
    }
    if (!settings.lastBookId.isEmpty() && m_manager->hasBook(settings.lastBookId))
        openBook(settings.lastBookId);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!closeAllSessions(true)) {
        event->ignore();
        return;
    }
    QString error;
    m_settings->save(&error);
    event->accept();
}


void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event || !m_manager->isOpen())
        return;
    if (!event->mimeData()->hasUrls())
        return;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()
            && url.toLocalFile().endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event || !m_manager->isOpen())
        return;
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QString path = url.toLocalFile();
        if (path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)
            && !paths.contains(path))
            paths.append(path);
    }
    if (paths.isEmpty())
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    importDroppedFiles(paths);
}

void MainWindow::importDroppedFiles(const QStringList &paths)
{
    if (!m_manager->isOpen() || paths.isEmpty())
        return;
    if (m_tabs->currentIndex() != 0)
        m_tabs->setCurrentIndex(0);
    m_libraryView->importFiles(paths);
}
} // namespace pano
