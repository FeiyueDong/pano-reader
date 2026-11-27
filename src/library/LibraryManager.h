#pragma once

#include <QObject>
#include <QVector>

#include "library/LibraryTypes.h"

namespace pano {

class LibraryStore;

// High-level library operations used by the UI. It keeps the in-memory index
// and the on-disk store in sync and never modifies a source PDF.
class LibraryManager : public QObject
{
    Q_OBJECT

public:
    explicit LibraryManager(QObject *parent = nullptr);
    ~LibraryManager() override;

    bool createLibrary(const QString &rootPath, QString *errorMessage);
    bool openLibrary(const QString &rootPath, QString *errorMessage);
    void closeLibrary();

    bool isOpen() const;
    const LibraryData &data() const { return m_data; }
    LibraryStore *store() const { return m_store; }
    QString rootPath() const;
    QString libraryId() const;
    QStringList diagnostics() const;

    QVector<BookInfo> books(bool includeDeleted = false) const;
    BookInfo bookInfo(const QString &bookId) const;
    bool hasBook(const QString &bookId) const;
    QString findBookBySha256(const QString &sha256) const;
    QString sha256ForFile(const QString &path, QString *errorMessage) const;

    bool importBooks(const QStringList &paths, ImportMode mode, bool allowDuplicate,
                     QStringList *importedIds, QStringList *duplicates,
                     QString *errorMessage);
    bool setFavorite(const QString &bookId, bool favorite, QString *errorMessage);
    bool moveToTrash(const QString &bookId, QString *errorMessage);
    bool restoreBook(const QString &bookId, QString *errorMessage);
    bool deletePermanently(const QString &bookId, QString *errorMessage);
    bool setSourcePath(const QString &bookId, const QString &absolutePath,
                       QString *errorMessage);
    bool verifySource(const QString &bookId, QString *errorMessage);

    bool createShelf(const QString &name, const QString &parentId,
                     QString *errorMessage, QString *shelfId = nullptr);
    bool renameShelf(const QString &shelfId, const QString &name, QString *errorMessage);
    bool deleteShelf(const QString &shelfId, QString *errorMessage);
    bool addBookToShelf(const QString &shelfId, const QString &bookId, QString *errorMessage);
    bool removeBookFromShelf(const QString &shelfId, const QString &bookId, QString *errorMessage);

    bool updateReadingState(const QString &bookId, const ReadingState &state,
                            QString *errorMessage);
    ReadingState readingState(const QString &bookId) const;
    void recordHistory(const QString &bookId);

signals:
    void libraryChanged();
    void libraryOpened();
    void libraryClosed();
    void bookChanged(const QString &bookId);
    void errorOccurred(const QString &message);

private:
    QString absoluteSourcePath(const BookRecord &record, const BookDocument &document,
                               QString *errorMessage = nullptr) const;
    QString targetDirectory(const QString &title) const;
    bool copySource(const QString &sourcePath, const QString &targetPath, QString *errorMessage) const;
    bool writeCover(const QString &pdfPath, const QString &targetPath, QString *errorMessage) const;
    bool shelfIsDescendant(const QString &shelfId, const QString &possibleChild) const;
    bool persistLibrary(QString *errorMessage);
    bool persistShelves(QString *errorMessage);

    LibraryStore *m_store = nullptr;
    LibraryData m_data;
};

} // namespace pano
