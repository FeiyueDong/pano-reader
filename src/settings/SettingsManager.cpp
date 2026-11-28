#include "settings/SettingsManager.h"

#include "reader/PageLayout.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "storage/JsonUtils.h"

namespace pano {

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    QString toolWarning;
    m_toolPresets.load(&toolWarning);
}

QString SettingsManager::configDirectory() const
{
    return QDir(appConfigDirectory()).filePath(QStringLiteral("settings"));
}

QString SettingsManager::generalFilePath() const
{
    return QDir(configDirectory()).filePath(QStringLiteral("general.json"));
}

QString SettingsManager::workspaceFilePath() const
{
    return QDir(configDirectory()).filePath(QStringLiteral("workspace.json"));
}

void SettingsManager::load()
{
    m_settings = AppSettings();
    QString error;
    QFileInfo general(generalFilePath());
    if (general.exists()) {
        QJsonObject root;
        if (readJsonObject(general.absoluteFilePath(), &root, &error)
            && root.value(QStringLiteral("schema_version")).toInt(-1) == 1) {
            const QString theme = root.value(QStringLiteral("theme")).toString();
            if (!theme.isEmpty())
                m_settings.themeKey = theme;
            m_settings.uiFontSize = qBound(8, root.value(QStringLiteral("ui_font_size")).toInt(11), 18);
            m_settings.restoreLastLibrary =
                root.value(QStringLiteral("restore_last_library")).toBool(true);
            m_settings.restoreOpenTabs = root.value(QStringLiteral("restore_open_tabs")).toBool(true);
            m_settings.autosaveSeconds =
                qBound(1, root.value(QStringLiteral("autosave_seconds")).toInt(2), 30);
            ImportMode mode = ImportMode::Copy;
            importModeFromString(root.value(QStringLiteral("default_import_mode")).toString(), &mode);
            m_settings.defaultImportMode = mode;
            m_settings.libraryCoverView =
                root.value(QStringLiteral("library_cover_view")).toBool(true);
            const QString layout = root.value(QStringLiteral("default_layout_mode"))
                                       .toString(QStringLiteral("continuous_single"));
            PageLayoutMode layoutMode = PageLayoutMode::ContinuousSingle;
            if (pageLayoutModeFromString(layout, &layoutMode))
                m_settings.defaultLayoutMode = pageLayoutModeToString(layoutMode);
            ZoomMode defaultZoom = ZoomMode::FitPage;
            zoomModeFromString(root.value(QStringLiteral("default_zoom_mode"))
                                   .toString(QStringLiteral("fit_page")), &defaultZoom);
            m_settings.defaultZoomMode = defaultZoom;
            m_settings.firstPageAlone =
                root.value(QStringLiteral("first_page_alone")).toBool(true);
            m_settings.pageGap = qBound(8, root.value(QStringLiteral("page_gap")).toInt(16), 64);
            m_settings.wheelTurnsPage =
                root.value(QStringLiteral("wheel_turns_page")).toBool(true);
        }
    }

    QFileInfo workspace(workspaceFilePath());
    if (workspace.exists()) {
        QJsonObject root;
        if (readJsonObject(workspace.absoluteFilePath(), &root, &error)
            && root.value(QStringLiteral("schema_version")).toInt(-1) == 1) {
            m_settings.lastLibraryPath = root.value(QStringLiteral("last_library")).toString();
            m_settings.lastBookId = root.value(QStringLiteral("last_book_id")).toString();
            m_settings.libraryNavVisible =
                root.value(QStringLiteral("library_nav_visible")).toBool(true);
            m_settings.libraryInfoVisible =
                root.value(QStringLiteral("library_info_visible")).toBool(true);
            m_settings.libraryNavWidth =
                qBound(160, root.value(QStringLiteral("library_nav_width")).toInt(230), 420);
            m_settings.libraryInfoWidth =
                qBound(240, root.value(QStringLiteral("library_info_width")).toInt(340), 560);
            m_settings.readerLeftVisible =
                root.value(QStringLiteral("reader_left_visible")).toBool(true);
            m_settings.readerRightVisible =
                root.value(QStringLiteral("reader_right_visible")).toBool(true);
            m_settings.readerLeftWidth =
                qBound(160, root.value(QStringLiteral("reader_left_width")).toInt(240), 420);
            m_settings.readerRightWidth =
                qBound(260, root.value(QStringLiteral("reader_right_width")).toInt(340), 560);
            m_settings.lastOpenBooks.clear();
            for (const QJsonValue &value : root.value(QStringLiteral("open_books")).toArray()) {
                const QString id = value.toString();
                if (!id.isEmpty() && !m_settings.lastOpenBooks.contains(id))
                    m_settings.lastOpenBooks.append(id);
            }
        }
    }
    emit settingsChanged();
}

bool SettingsManager::save(QString *errorMessage)
{
    if (!ensureDirectory(configDirectory(), errorMessage))
        return false;

    QJsonObject general;
    general.insert(QStringLiteral("format"), QStringLiteral("pano-settings-general"));
    general.insert(QStringLiteral("schema_version"), 1);
    general.insert(QStringLiteral("theme"), m_settings.themeKey);
    general.insert(QStringLiteral("ui_font_size"), m_settings.uiFontSize);
    general.insert(QStringLiteral("restore_last_library"), m_settings.restoreLastLibrary);
    general.insert(QStringLiteral("restore_open_tabs"), m_settings.restoreOpenTabs);
    general.insert(QStringLiteral("autosave_seconds"), m_settings.autosaveSeconds);
    general.insert(QStringLiteral("default_import_mode"), importModeToString(m_settings.defaultImportMode));
    general.insert(QStringLiteral("library_cover_view"), m_settings.libraryCoverView);
    general.insert(QStringLiteral("default_layout_mode"), m_settings.defaultLayoutMode);
    general.insert(QStringLiteral("default_zoom_mode"), zoomModeToString(m_settings.defaultZoomMode));
    general.insert(QStringLiteral("first_page_alone"), m_settings.firstPageAlone);
    general.insert(QStringLiteral("page_gap"), m_settings.pageGap);
    general.insert(QStringLiteral("wheel_turns_page"), m_settings.wheelTurnsPage);

    QJsonObject workspace;
    workspace.insert(QStringLiteral("format"), QStringLiteral("pano-settings-workspace"));
    workspace.insert(QStringLiteral("schema_version"), 1);
    workspace.insert(QStringLiteral("last_library"), m_settings.lastLibraryPath);
    workspace.insert(QStringLiteral("last_book_id"), m_settings.lastBookId);
    QJsonArray openBooks;
    for (const QString &bookId : m_settings.lastOpenBooks)
        openBooks.append(bookId);
    workspace.insert(QStringLiteral("open_books"), openBooks);
    workspace.insert(QStringLiteral("library_nav_visible"), m_settings.libraryNavVisible);
    workspace.insert(QStringLiteral("library_info_visible"), m_settings.libraryInfoVisible);
    workspace.insert(QStringLiteral("library_nav_width"), m_settings.libraryNavWidth);
    workspace.insert(QStringLiteral("library_info_width"), m_settings.libraryInfoWidth);
    workspace.insert(QStringLiteral("reader_left_visible"), m_settings.readerLeftVisible);
    workspace.insert(QStringLiteral("reader_right_visible"), m_settings.readerRightVisible);
    workspace.insert(QStringLiteral("reader_left_width"), m_settings.readerLeftWidth);
    workspace.insert(QStringLiteral("reader_right_width"), m_settings.readerRightWidth);

    return writeJsonObjectAtomic(generalFilePath(), general, errorMessage)
        && writeJsonObjectAtomic(workspaceFilePath(), workspace, errorMessage);
}

void SettingsManager::setThemeKey(const QString &key)
{
    if (m_settings.themeKey == key)
        return;
    m_settings.themeKey = key;
    emit settingsChanged();
}

void SettingsManager::setUiFontSize(int points)
{
    const int bounded = qBound(8, points, 18);
    if (m_settings.uiFontSize == bounded)
        return;
    m_settings.uiFontSize = bounded;
    emit settingsChanged();
}

void SettingsManager::setRestoreLastLibrary(bool restore)
{
    if (m_settings.restoreLastLibrary == restore)
        return;
    m_settings.restoreLastLibrary = restore;
    emit settingsChanged();
}

void SettingsManager::setRestoreOpenTabs(bool restore)
{
    if (m_settings.restoreOpenTabs == restore)
        return;
    m_settings.restoreOpenTabs = restore;
    emit settingsChanged();
}

void SettingsManager::setAutosaveSeconds(int seconds)
{
    const int bounded = qBound(1, seconds, 30);
    if (m_settings.autosaveSeconds == bounded)
        return;
    m_settings.autosaveSeconds = bounded;
    emit settingsChanged();
}

void SettingsManager::setLastLibraryPath(const QString &path)
{
    m_settings.lastLibraryPath = path;
}

void SettingsManager::setLastBookId(const QString &bookId)
{
    m_settings.lastBookId = bookId;
}

void SettingsManager::setLastOpenBooks(const QStringList &bookIds)
{
    m_settings.lastOpenBooks = bookIds;
}

void SettingsManager::setDefaultImportMode(ImportMode mode)
{
    if (m_settings.defaultImportMode == mode)
        return;
    m_settings.defaultImportMode = mode;
    emit settingsChanged();
}

void SettingsManager::setLibraryCoverView(bool cover)
{
    if (m_settings.libraryCoverView == cover)
        return;
    m_settings.libraryCoverView = cover;
    emit settingsChanged();
}

void SettingsManager::setLibraryPanelState(bool navVisible, bool infoVisible,
                                           int navWidth, int infoWidth)
{
    m_settings.libraryNavVisible = navVisible;
    m_settings.libraryInfoVisible = infoVisible;
    m_settings.libraryNavWidth = navWidth;
    m_settings.libraryInfoWidth = infoWidth;
}

void SettingsManager::setDefaultReadingLayout(const QString &layoutMode, ZoomMode zoomMode,
                                              bool firstPageAlone)
{
    PageLayoutMode parsed = PageLayoutMode::ContinuousSingle;
    if (pageLayoutModeFromString(layoutMode, &parsed))
        m_settings.defaultLayoutMode = pageLayoutModeToString(parsed);
    m_settings.defaultZoomMode = zoomMode;
    m_settings.firstPageAlone = firstPageAlone;
    emit settingsChanged();
}

void SettingsManager::setPageGap(int gap)
{
    m_settings.pageGap = qBound(8, gap, 64);
    emit settingsChanged();
}

void SettingsManager::setWheelTurnsPage(bool enabled)
{
    m_settings.wheelTurnsPage = enabled;
    emit settingsChanged();
}

void SettingsManager::setReaderPanelState(bool leftVisible, bool rightVisible,
                                          int leftWidth, int rightWidth)
{
    m_settings.readerLeftVisible = leftVisible;
    m_settings.readerRightVisible = rightVisible;
    m_settings.readerLeftWidth = leftWidth;
    m_settings.readerRightWidth = rightWidth;
}

} // namespace pano
