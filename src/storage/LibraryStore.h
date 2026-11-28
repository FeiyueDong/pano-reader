#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "common/LibraryTypes.h"

class QLockFile;

namespace pano {

// Owns the on-disk library root: index, shelves, reading state, per-book
// manifests and the local source-path overrides. It never writes PDF files.
class LibraryStore : public QObject
{
    Q_OBJECT

public:
    explicit LibraryStore(QObject *parent = nullptr);
    ~LibraryStore() override;

    bool create(const QString &rootPath, LibraryData *data, QString *errorMessage);
    bool open(const QString &rootPath, LibraryData *data, QString *errorMessage);
    void close();

    bool isOpen() const { return m_open; }
    QString rootPath() const { return m_rootPath; }
    QString libraryId() const { return m_libraryId; }
    QString lockPath() const;
    QStringList diagnostics() const { return m_diagnostics; }

    bool saveLibrary(const LibraryData &data, QString *errorMessage);
    bool saveShelves(const LibraryData &data, QString *errorMessage);
    bool saveReadingState(const LibraryData &data, QString *errorMessage);
    bool saveHistory(const LibraryData &data, QString *errorMessage);

    bool readDocument(const QString &bookDirectory, BookDocument *document,
                      QString *errorMessage) const;
    bool writeDocument(const QString &bookDirectory, const BookDocument &document,
                       QString *errorMessage);

    bool readReadingData(const QString &bookDirectory, const QString &bookId,
                         QJsonArray *annotations, QJsonArray *notes, QJsonArray *noteTree,
                         QString *errorMessage) const;
    bool writeReadingData(const QString &bookDirectory, const QString &bookId,
                          const QJsonArray &annotations, const QJsonArray &notes,
                          const QJsonArray &noteTree, QString *errorMessage);

    QString absoluteBookDirectory(const QString &bookDirectory) const;
    bool isManagedBookDirectory(const QString &absolutePath) const;

    QString sourceOverride(const QString &bookId) const;
    bool setSourceOverride(const QString &bookId, const QString &absolutePath,
                           QString *errorMessage);
    bool clearSourceOverride(const QString &bookId, QString *errorMessage);

private:
    bool acquireLock(const QString &rootPath, QString *errorMessage);
    void releaseLock();
    bool loadLibraryFile(const QString &path, LibraryData *data, QString *errorMessage);
    bool loadShelvesFile(const QString &path, LibraryData *data, QString *errorMessage);
    bool loadReadingFile(const QString &path, LibraryData *data, QString *errorMessage);
    bool loadHistoryFile(const QString &path, LibraryData *data, QString *errorMessage);
    bool loadSources(QString *errorMessage);
    bool saveSources(QString *errorMessage);

    bool writeChecked(const QString &path, const QJsonObject &object, QString *errorMessage);
    void rememberHash(const QString &path);
    QByteArray currentHash(const QString &path) const;
    bool checkExternalChange(const QString &path, QString *errorMessage) const;

    bool m_open = false;
    QString m_rootPath;
    QString m_libraryId;
    QLockFile *m_lock = nullptr;
    QHash<QString, QByteArray> m_fileHashes;
    QStringList m_diagnostics;

    // Local (not library) source overrides for the currently open library.
    QString m_sourcesPath;
    QHash<QString, QString> m_sourceOverrides;
    QJsonObject m_sourcesRoot;
    bool m_sourcesLoaded = false;
};

} // namespace pano
