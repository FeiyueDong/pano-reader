#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace pano {

struct AppSettings {
    QString themeKey = QStringLiteral("modern-light");
    int uiFontSize = 10;
    bool restoreLastLibrary = true;
    bool restoreOpenTabs = true;
    QString lastLibraryPath;
    QString lastBookId;
    QStringList lastOpenBooks;
    int autosaveSeconds = 2;
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

signals:
    void settingsChanged();

private:
    AppSettings m_settings;
};

} // namespace pano
