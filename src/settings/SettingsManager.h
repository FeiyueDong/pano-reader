#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "library/LibraryTypes.h"
#include "settings/ToolPresets.h"

namespace pano {

struct AppSettings {
    QString themeKey = QStringLiteral("modern-light");
    int uiFontSize = 11;
    bool restoreLastLibrary = true;
    bool restoreOpenTabs = true;
    QString lastLibraryPath;
    QString lastBookId;
    QStringList lastOpenBooks;
    int autosaveSeconds = 2;
    ImportMode defaultImportMode = ImportMode::Copy;
    bool libraryCoverView = true;
    bool libraryNavVisible = true;
    bool libraryInfoVisible = true;
    int libraryNavWidth = 230;
    int libraryInfoWidth = 340;
    bool readerLeftVisible = true;
    bool readerRightVisible = true;
    int readerLeftWidth = 240;
    int readerRightWidth = 340;
    QString defaultLayoutMode = QStringLiteral("continuous_single");
    ZoomMode defaultZoomMode = ZoomMode::FitPage;
    bool firstPageAlone = true;
    int pageGap = 16;
    bool wheelTurnsPage = true;
};

class SettingsManager : public QObject
{
    Q_OBJECT

public:
    explicit SettingsManager(QObject *parent = nullptr);

    const AppSettings &settings() const { return m_settings; }
    QString configDirectory() const;
    QString generalFilePath() const;
    QString workspaceFilePath() const;

    void load();
    bool save(QString *errorMessage = nullptr);

    void setThemeKey(const QString &key);
    void setUiFontSize(int points);
    void setRestoreLastLibrary(bool restore);
    void setRestoreOpenTabs(bool restore);
    void setAutosaveSeconds(int seconds);
    void setLastLibraryPath(const QString &path);
    void setLastBookId(const QString &bookId);
    void setLastOpenBooks(const QStringList &bookIds);
    void setDefaultImportMode(ImportMode mode);
    void setLibraryCoverView(bool cover);
    void setLibraryPanelState(bool navVisible, bool infoVisible, int navWidth, int infoWidth);
    void setReaderPanelState(bool leftVisible, bool rightVisible, int leftWidth, int rightWidth);
    void setDefaultReadingLayout(const QString &layoutMode, ZoomMode zoomMode, bool firstPageAlone);
    void setPageGap(int gap);
    void setWheelTurnsPage(bool enabled);

    ToolPresets *toolPresets() { return &m_toolPresets; }
    const ToolPresets *toolPresets() const { return &m_toolPresets; }

signals:
    void settingsChanged();

private:
    AppSettings m_settings;
    ToolPresets m_toolPresets;
};

} // namespace pano
