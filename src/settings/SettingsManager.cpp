#include "settings/SettingsManager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "storage/JsonUtils.h"

namespace pano {

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
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
            m_settings.uiFontSize = qBound(8, root.value(QStringLiteral("ui_font_size")).toInt(10), 18);
            m_settings.restoreLastLibrary =
                root.value(QStringLiteral("restore_last_library")).toBool(true);
            m_settings.restoreOpenTabs = root.value(QStringLiteral("restore_open_tabs")).toBool(true);
            m_settings.autosaveSeconds =
                qBound(1, root.value(QStringLiteral("autosave_seconds")).toInt(2), 30);
        }
    }

    QFileInfo workspace(workspaceFilePath());
    if (workspace.exists()) {
        QJsonObject root;
        if (readJsonObject(workspace.absoluteFilePath(), &root, &error)
            && root.value(QStringLiteral("schema_version")).toInt(-1) == 1) {
            m_settings.lastLibraryPath = root.value(QStringLiteral("last_library")).toString();
            m_settings.lastBookId = root.value(QStringLiteral("last_book_id")).toString();
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

    QJsonObject workspace;
    workspace.insert(QStringLiteral("format"), QStringLiteral("pano-settings-workspace"));
    workspace.insert(QStringLiteral("schema_version"), 1);
    workspace.insert(QStringLiteral("last_library"), m_settings.lastLibraryPath);
    workspace.insert(QStringLiteral("last_book_id"), m_settings.lastBookId);
    QJsonArray openBooks;
    for (const QString &bookId : m_settings.lastOpenBooks)
        openBooks.append(bookId);
    workspace.insert(QStringLiteral("open_books"), openBooks);

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

} // namespace pano
