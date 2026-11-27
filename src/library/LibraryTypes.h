#pragma once

#include <QDateTime>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

namespace pano {

enum class ImportMode {
    Copy,
    Reference
};

QString importModeToString(ImportMode mode);
bool importModeFromString(const QString &value, ImportMode *mode);
QString importModeDisplayName(ImportMode mode);

enum class ZoomMode {
    Fixed,
    FitWidth,
    FitPage
};

QString zoomModeToString(ZoomMode mode);
bool zoomModeFromString(const QString &value, ZoomMode *mode);
QString zoomModeDisplayName(ZoomMode mode);

struct BookRecord {
    QString id;
    QString directory; // library-relative, e.g. "books/Structure Dynamics"
    bool favorite = false;
    QString deletedAt; // empty == not in trash
};

struct ShelfRecord {
    QString id;
    QString name;
    QString parentId; // empty == top level
    QVector<QString> bookIds;
};

struct ReadingState {
    int pageIndex = 0;
    QPointF pagePoint;
    qreal zoom = 1.0;
    ZoomMode zoomMode = ZoomMode::FitPage;
    int rotation = 0;
    QString updatedAt;
    bool valid = false;
};

struct BookDocument {
    QString id;
    QString title;
    QString author;
    QString importedAt;
    ImportMode mode = ImportMode::Copy;
    QString sourcePath; // copy: book-relative; reference: absolute
    QString sha256;
    qint64 sizeBytes = 0;
};

struct BookInfo {
    QString id;
    QString directory;
    bool favorite = false;
    bool deleted = false;
    QString deletedAt;

    QString title;
    QString author;
    QString importedAt;
    ImportMode mode = ImportMode::Copy;
    QString sourcePath;
    QString sha256;
    qint64 sizeBytes = 0;
    QString coverPath;   // absolute, empty when unavailable
    QString pdfPath;     // absolute, resolved and safe (not necessarily hash-verified)
    bool sourceAvailable = false;
    bool sourceVerified = false;
    QString sourceError;

    int annotationCount = 0; // lazily filled from annotations.json
    int noteCount = 0;
};

struct LibraryData {
    QString id;
    QString rootPath;
    QVector<BookRecord> books;
    QVector<ShelfRecord> shelves;
    QHash<QString, ReadingState> reading;
    QHash<QString, QDateTime> history;

    int bookIndex(const QString &bookId) const;
    int shelfIndex(const QString &shelfId) const;
    BookRecord *book(const QString &bookId);
    const BookRecord *book(const QString &bookId) const;
    ShelfRecord *shelf(const QString &shelfId);
    const ShelfRecord *shelf(const QString &shelfId) const;
};

} // namespace pano
