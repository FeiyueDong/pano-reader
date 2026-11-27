#pragma once

#include <QMainWindow>
#include <QVector>

class QAction;
class QActionGroup;
class QCloseEvent;
class QComboBox;
class QLabel;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QToolBar;

namespace pano {

class DocumentSession;
class LibraryView;
class ReaderWorkspace;
class LibraryManager;
class SettingsManager;
class ThemeManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openLibraryPath(const QString &path, bool createIfMissing = false);
    bool openBook(const QString &bookId);
    void restoreLastSession();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void newLibrary();
    void openLibrary();
    void importIntoLibrary();
    void saveCurrent();
    void closeCurrentTab();
    void onTabChanged(int index);
    void importDroppedFiles(const QStringList &paths);
    void onTabCloseRequested(int index);
    void openBookFromLibrary(const QString &bookId);
    void setToolFromAction();
    void updateActions();
    void updateSessionStatus();
    void showPreferences();
    void showShortcuts();
    void showAbout();
    void showDiagnostics();
    void zoomIn();
    void zoomOut();
    void fitWidth();
    void fitPage();
    void rotateClockwise();
    void pageChanged(int page);
    void zoomEdited();

private:
    void buildUi();
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    QWidget *buildWelcomePage();
    DocumentSession *currentSession() const;
    ReaderWorkspace *currentWorkspace() const;
    void attachSession(DocumentSession *session);
    void detachSession(DocumentSession *session);
    void connectSession(DocumentSession *session);
    void addReaderTab(DocumentSession *session);
    void updateOpenTabs();
    bool closeAllSessions(bool prompt);
    void setShortcutScope(QWidget *view);
    bool saveSession(DocumentSession *session, bool promptOnFailure);
    void updateTabTitle(DocumentSession *session);
    void updateWindowTitle();
    void updateToolBarState();
    void applyThemeFromSettings();
    void updatePageControls();
    void setStatus(const QString &message);
    void showError(const QString &title, const QString &message);
    void updateLibraryTab();

    LibraryManager *m_manager = nullptr;
    SettingsManager *m_settings = nullptr;
    ThemeManager *m_themes = nullptr;
    QTabWidget *m_tabs = nullptr;
    LibraryView *m_libraryView = nullptr;
    QStackedWidget *m_libraryPage = nullptr;
    QWidget *m_welcome = nullptr;
    QLabel *m_statusLabel = nullptr;
    QVector<DocumentSession *> m_sessions;

    QAction *m_newLibraryAction = nullptr;
    QAction *m_openLibraryAction = nullptr;
    QAction *m_importAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeTabAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_editNoteAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fitWidthAction = nullptr;
    QAction *m_fitPageAction = nullptr;
    QAction *m_rotateAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_shortcutsAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_diagnosticsAction = nullptr;
    QActionGroup *m_toolGroup = nullptr;
    QVector<QAction *> m_toolActions;
    QSpinBox *m_pageSpin = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QLabel *m_pageLabel = nullptr;
    QWidget *m_shortcutView = nullptr;
};

} // namespace pano
