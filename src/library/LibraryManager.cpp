#include "library/LibraryManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

#include "pdf/PdfDocument.h"
#include "storage/JsonUtils.h"
#include "storage/LibraryStore.h"

namespace pano {

namespace {

QString displayTitle(const QString &title, const QString &fallback)
{
    const QString trimmed = title.trimmed();
    return trimmed.isEmpty() ? fallback : trimmed;
}

QString sanitizedDirectoryName(const QString &title)
{
    QString name = title.simplified();
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(invalid, QStringLiteral("-"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("book");
    return name.left(80);
}

bool validShelfName(const QString &name)
{
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= 80 && !trimmed.contains(QLatin1Char('/'));
}

} // namespace

LibraryManager::LibraryManager(QObject *parent)
    : QObject(parent)
{
    m_store = new LibraryStore(this);
}

LibraryManager::~LibraryManager() = default;

bool LibraryManager::createLibrary(const QString &rootPath, QString *errorMessage)
{
    LibraryData data;
    if (!m_store->create(rootPath, &data, errorMessage))
        return false;
    m_data = data;
    emit libraryChanged();
    emit libraryOpened();
    return true;
}

bool LibraryManager::openLibrary(const QString &rootPath, QString *errorMessage)
{
    LibraryData data;
    if (!m_store->open(rootPath, &data, errorMessage))
        return false;
    m_data = data;
    emit libraryChanged();
    emit libraryOpened();
    return true;
}

void LibraryManager::closeLibrary()
{
    m_store->close();
    m_data = LibraryData();
    emit libraryChanged();
    emit libraryClosed();
}

bool LibraryManager::isOpen() const
{
    return m_store->isOpen();
}

QString LibraryManager::rootPath() const
{
    return m_store->rootPath();
}

QString LibraryManager::libraryId() const
{
    return m_data.id;
}

QStringList LibraryManager::diagnostics() const
{
    return m_store->diagnostics();
}

QVector<BookInfo> LibraryManager::books(bool includeDeleted) const
{
    QVector<BookInfo> result;
    result.reserve(m_data.books.size());
    for (const BookRecord &record : m_data.books) {
        if (!includeDeleted && !record.deletedAt.isEmpty())
            continue;
        BookInfo info;
        info.id = record.id;
        info.directory = record.directory;
        info.favorite = record.favorite;
        info.deleted = !record.deletedAt.isEmpty();
        info.deletedAt = record.deletedAt;

        BookDocument document;
        QString error;
        const QString bookDirectory = m_store->absoluteBookDirectory(record.directory);
        if (!m_store->readDocument(record.directory, &document, &error)) {
            info.title = record.directory.section(QLatin1Char('/'), -1);
            info.sourceError = error;
            result.append(info);
            continue;
        }
        info.title = document.title;
        info.author = document.author;
        info.importedAt = document.importedAt;
        info.mode = document.mode;
        info.sourcePath = document.sourcePath;
        info.sha256 = document.sha256;
        info.sizeBytes = document.sizeBytes;
        const QString cover = QDir(bookDirectory).filePath(QStringLiteral("cover.png"));
        if (QFileInfo::exists(cover))
            info.coverPath = cover;

        QString sourceError;
        const QString source = absoluteSourcePath(record, document, &sourceError);
        info.sourceAvailable = !source.isEmpty() && QFileInfo(source).isFile();
        info.pdfPath = info.sourceAvailable ? source : QString();
        if (!info.sourceAvailable)
            info.sourceError = sourceError.isEmpty() ? QStringLiteral("源 PDF 不存在或不可读。") : sourceError;

        QJsonArray annotations;
        if (m_store->readAnnotations(record.directory, record.id, &annotations, nullptr)) {
            info.annotationCount = annotations.size();
            for (const QJsonValue &value : annotations) {
                if (value.isObject() && !value.toObject().value(QStringLiteral("note")).toObject().isEmpty())
                    ++info.noteCount;
            }
        }
        result.append(info);
    }
    return result;
}

BookInfo LibraryManager::bookInfo(const QString &bookId) const
{
    for (const BookInfo &info : books(true)) {
        if (info.id == bookId)
            return info;
    }
    return BookInfo();
}

bool LibraryManager::hasBook(const QString &bookId) const
{
    return m_data.bookIndex(bookId) >= 0;
}

QString LibraryManager::findBookBySha256(const QString &sha256) const
{
    const QString normalized = sha256.trimmed().toLower();
    if (normalized.isEmpty())
        return QString();
    for (const BookRecord &record : m_data.books) {
        BookDocument document;
        if (!m_store->readDocument(record.directory, &document, nullptr))
            continue;
        if (document.sha256.compare(normalized, Qt::CaseInsensitive) == 0)
            return record.id;
    }
    return QString();
}

QString LibraryManager::sha256ForFile(const QString &path, QString *errorMessage) const
{
    QByteArray digest;
    if (!sha256File(path, &digest, errorMessage))
        return QString();
    return QString::fromLatin1(digest.toHex());
}

bool LibraryManager::importBooks(const QStringList &paths, ImportMode mode, bool allowDuplicate,
                                 QStringList *importedIds, QStringList *duplicates,
                                 QString *errorMessage)
{
    if (!isOpen()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("尚未打开书库。");
        return false;
    }
    if (importedIds)
        importedIds->clear();
    if (duplicates)
        duplicates->clear();

    QSet<QString> batchHashes;
    for (const QString &path : paths) {
        const QFileInfo fileInfo(path);
        if (!fileInfo.isFile()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("文件不存在：%1").arg(path);
            return false;
        }
        const QString absolute = fileInfo.absoluteFilePath();

        QString hashError;
        QByteArray digest;
        if (!sha256File(absolute, &digest, &hashError)) {
            if (errorMessage)
                *errorMessage = hashError.isEmpty() ? QStringLiteral("无法读取文件：%1").arg(absolute) : hashError;
            return false;
        }
        const QString sha256 = QString::fromLatin1(digest.toHex());
        if (batchHashes.contains(sha256) || !findBookBySha256(sha256).isEmpty()) {
            if (duplicates)
                duplicates->append(absolute);
            if (!allowDuplicate)
                continue;
        }
        batchHashes.insert(sha256);

        QPdfDocument pdf;
        pdf.load(absolute);
        if (pdf.pageCount() <= 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法打开 PDF：%1").arg(
                    pdf.error() == QPdfDocument::Error::None ? absolute
                                                             : QStringLiteral("%1（%2）").arg(absolute, pdfErrorText(pdf.error())));
            }
            return false;
        }

        const QString fallback = fileInfo.completeBaseName();
        const QString title = displayTitle(pdf.metaData(QPdfDocument::MetaDataField::Title).toString(), fallback);
        const QString author = pdf.metaData(QPdfDocument::MetaDataField::Author).toString();
        const QString id = newStableId();
        const QString directory = targetDirectory(title);
        const QString absoluteDirectory = QDir(rootPath()).filePath(directory);
        if (!ensureDirectory(absoluteDirectory, errorMessage))
            return false;

        BookDocument document;
        document.id = id;
        document.title = title;
        document.author = author;
        document.importedAt = utcNowIso();
        document.mode = mode;
        document.sha256 = sha256;
        document.sizeBytes = fileInfo.size();

        bool success = false;
        if (mode == ImportMode::Copy) {
            const QString relative = QStringLiteral("source.pdf");
            const QString sourceTarget = QDir(absoluteDirectory).filePath(relative);
            if (copySource(absolute, sourceTarget, errorMessage)) {
                document.sourcePath = relative;
                success = true;
            }
        } else {
            document.sourcePath = absolute;
            success = true;
        }

        if (success) {
            const QString coverPath = QDir(absoluteDirectory).filePath(QStringLiteral("cover.png"));
            QString coverError;
            if (!writeCover(absolute, coverPath, &coverError))
                qWarning("cover generation failed for %s: %s", qPrintable(absolute), qPrintable(coverError));
            if (!m_store->writeDocument(directory, document, errorMessage))
                success = false;
        }

        if (!success) {
            QDir(absoluteDirectory).removeRecursively();
            return false;
        }

        BookRecord record;
        record.id = id;
        record.directory = directory;
        m_data.books.append(record);
        if (importedIds)
            importedIds->append(id);
        if (!persistLibrary(errorMessage))
            return false;
        emit bookChanged(id);
    }

    emit libraryChanged();
    return true;
}

bool LibraryManager::setFavorite(const QString &bookId, bool favorite, QString *errorMessage)
{
    BookRecord *record = m_data.book(bookId);
    if (!record) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    if (record->favorite == favorite)
        return true;
    record->favorite = favorite;
    if (!persistLibrary(errorMessage))
        return false;
    emit bookChanged(bookId);
    emit libraryChanged();
    return true;
}

bool LibraryManager::moveToTrash(const QString &bookId, QString *errorMessage)
{
    BookRecord *record = m_data.book(bookId);
    if (!record) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    if (!record->deletedAt.isEmpty())
        return true;
    record->deletedAt = utcNowIso();
    if (!persistLibrary(errorMessage))
        return false;
    emit bookChanged(bookId);
    emit libraryChanged();
    return true;
}

bool LibraryManager::restoreBook(const QString &bookId, QString *errorMessage)
{
    BookRecord *record = m_data.book(bookId);
    if (!record) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    record->deletedAt.clear();
    if (!persistLibrary(errorMessage))
        return false;
    emit bookChanged(bookId);
    emit libraryChanged();
    return true;
}

bool LibraryManager::deletePermanently(const QString &bookId, QString *errorMessage)
{
    const int index = m_data.bookIndex(bookId);
    if (index < 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    const BookRecord record = m_data.books.at(index);
    const QString directory = m_store->absoluteBookDirectory(record.directory);
    if (!directory.isEmpty() && QFileInfo::exists(directory)
        && m_store->isManagedBookDirectory(directory)) {
        if (!QDir(directory).removeRecursively()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("无法删除书籍目录：%1").arg(directory);
            return false;
        }
    }
    m_data.books.removeAt(index);
    for (ShelfRecord &shelf : m_data.shelves)
        shelf.bookIds.removeAll(bookId);
    m_data.reading.remove(bookId);
    m_data.history.remove(bookId);

    if (!persistLibrary(errorMessage) || !persistShelves(errorMessage)) {
        return false;
    }
    QString stateError;
    m_store->saveReadingState(m_data, &stateError);
    m_store->saveHistory(m_data, &stateError);
    emit libraryChanged();
    return true;
}

bool LibraryManager::setSourcePath(const QString &bookId, const QString &absolutePath,
                                   QString *errorMessage)
{
    const int index = m_data.bookIndex(bookId);
    if (index < 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    const BookRecord &record = m_data.books.at(index);
    BookDocument document;
    if (!m_store->readDocument(record.directory, &document, errorMessage))
        return false;
    const QFileInfo info(absolutePath);
    if (!info.isFile()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("指定的源文件不存在：%1").arg(absolutePath);
        return false;
    }
    const QString newHash = sha256ForFile(info.absoluteFilePath(), errorMessage);
    if (newHash.isEmpty())
        return false;
    if (newHash.compare(document.sha256, Qt::CaseInsensitive) != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("新文件的 SHA-256 与书籍记录不一致，已拒绝替换。"
                                           "如需换书，请作为新书导入。");
        }
        return false;
    }
    if (document.mode == ImportMode::Copy) {
        if (errorMessage)
            *errorMessage = QStringLiteral("拷贝模式书籍不能指向外部文件。");
        return false;
    }
    if (!m_store->setSourceOverride(bookId, info.absoluteFilePath(), errorMessage))
        return false;
    emit bookChanged(bookId);
    return true;
}

bool LibraryManager::verifySource(const QString &bookId, QString *errorMessage)
{
    const BookInfo info = bookInfo(bookId);
    if (!info.sourceAvailable) {
        if (errorMessage)
            *errorMessage = info.sourceError.isEmpty() ? QStringLiteral("源文件不可用。") : info.sourceError;
        return false;
    }
    const QString hash = sha256ForFile(info.pdfPath, errorMessage);
    if (hash.isEmpty())
        return false;
    if (hash.compare(info.sha256, Qt::CaseInsensitive) != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("源文件内容已变化（SHA-256 不匹配）。为避免错误绑定，本次不打开。");
        return false;
    }
    return true;
}

bool LibraryManager::createShelf(const QString &name, const QString &parentId,
                                 QString *errorMessage, QString *shelfId)
{
    if (!validShelfName(name)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架名称必须为 1–80 个字符，且不能包含斜杠。");
        return false;
    }
    if (!parentId.isEmpty() && !m_data.shelf(parentId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("上级书架不存在。");
        return false;
    }
    ShelfRecord shelf;
    shelf.id = newStableId();
    shelf.name = name.trimmed();
    shelf.parentId = parentId;
    m_data.shelves.append(shelf);
    if (!persistShelves(errorMessage))
        return false;
    if (shelfId)
        *shelfId = shelf.id;
    emit libraryChanged();
    return true;
}

bool LibraryManager::renameShelf(const QString &shelfId, const QString &name, QString *errorMessage)
{
    ShelfRecord *shelf = m_data.shelf(shelfId);
    if (!shelf) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架不存在。");
        return false;
    }
    if (!validShelfName(name)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架名称必须为 1–80 个字符，且不能包含斜杠。");
        return false;
    }
    shelf->name = name.trimmed();
    if (!persistShelves(errorMessage))
        return false;
    emit libraryChanged();
    return true;
}

bool LibraryManager::deleteShelf(const QString &shelfId, QString *errorMessage)
{
    const int index = m_data.shelfIndex(shelfId);
    if (index < 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架不存在。");
        return false;
    }
    const QString parent = m_data.shelves.at(index).parentId;
    m_data.shelves.removeAt(index);
    for (ShelfRecord &shelf : m_data.shelves) {
        if (shelf.parentId == shelfId)
            shelf.parentId = parent;
    }
    if (!persistShelves(errorMessage))
        return false;
    emit libraryChanged();
    return true;
}

bool LibraryManager::addBookToShelf(const QString &shelfId, const QString &bookId,
                                    QString *errorMessage)
{
    ShelfRecord *shelf = m_data.shelf(shelfId);
    if (!shelf) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架不存在。");
        return false;
    }
    if (!m_data.book(bookId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍不存在。");
        return false;
    }
    if (!shelf->bookIds.contains(bookId))
        shelf->bookIds.append(bookId);
    if (!persistShelves(errorMessage))
        return false;
    emit libraryChanged();
    return true;
}

bool LibraryManager::removeBookFromShelf(const QString &shelfId, const QString &bookId,
                                         QString *errorMessage)
{
    ShelfRecord *shelf = m_data.shelf(shelfId);
    if (!shelf) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书架不存在。");
        return false;
    }
    shelf->bookIds.removeAll(bookId);
    if (!persistShelves(errorMessage))
        return false;
    emit libraryChanged();
    return true;
}

bool LibraryManager::updateReadingState(const QString &bookId, const ReadingState &state,
                                        QString *errorMessage)
{
    if (!m_data.book(bookId))
        return false;
    ReadingState updated = state;
    updated.valid = true;
    updated.updatedAt = utcNowIso();
    m_data.reading.insert(bookId, updated);
    return m_store->saveReadingState(m_data, errorMessage);
}

ReadingState LibraryManager::readingState(const QString &bookId) const
{
    return m_data.reading.value(bookId);
}

void LibraryManager::recordHistory(const QString &bookId)
{
    if (!m_data.book(bookId))
        return;
    m_data.history.insert(bookId, QDateTime::currentDateTimeUtc());
    QString error;
    m_store->saveHistory(m_data, &error);
    emit bookChanged(bookId);
}

QString LibraryManager::absoluteSourcePath(const BookRecord &record, const BookDocument &document,
                                           QString *errorMessage) const
{
    if (document.mode == ImportMode::Reference) {
        const QString override = m_store->sourceOverride(record.id);
        const QString path = override.isEmpty() ? document.sourcePath : override;
        if (path.isEmpty() || !QFileInfo(path).isAbsolute()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("映射模式书籍的源路径无效。");
            return QString();
        }
        return QFileInfo(path).absoluteFilePath();
    }

    const QString directory = m_store->absoluteBookDirectory(record.directory);
    if (directory.isEmpty() || !isSafeLibraryRelativePath(document.sourcePath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("拷贝模式书籍的源路径无效。");
        return QString();
    }
    const QString path = QDir(directory).filePath(document.sourcePath);
    if (!isPathInside(path, directory)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("源路径逃逸出书籍目录，已拒绝。");
        return QString();
    }
    return path;
}

QString LibraryManager::targetDirectory(const QString &title) const
{
    const QString base = QStringLiteral("books/%1").arg(sanitizedDirectoryName(title));
    QString candidate = base;
    int suffix = 2;
    const QDir root(rootPath());
    while (root.exists(candidate) || QFileInfo::exists(root.filePath(candidate))) {
        candidate = QStringLiteral("%1-%2").arg(base).arg(suffix++);
    }
    return candidate;
}

bool LibraryManager::copySource(const QString &sourcePath, const QString &targetPath,
                                QString *errorMessage) const
{
    QFile::remove(targetPath);
    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("复制 PDF 失败：%1 -> %2").arg(sourcePath, targetPath);
        return false;
    }
    return true;
}

bool LibraryManager::writeCover(const QString &pdfPath, const QString &targetPath,
                                QString *errorMessage) const
{
    QPdfDocument pdf;
    pdf.load(pdfPath);
    if (pdf.pageCount() <= 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法读取封面页。");
        return false;
    }
    const QSizeF pageSize = pdf.pagePointSize(0);
    if (pageSize.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("页面尺寸无效。");
        return false;
    }
    const int width = 480;
    const int height = std::max(1, qRound(width * pageSize.height() / pageSize.width()));
    const QImage image = pdf.render(0, QSize(width, height), QPdfDocumentRenderOptions(),
                                         QImage::Format_ARGB32_Premultiplied, QColor(Qt::white));
    if (image.isNull() || !image.save(targetPath, "PNG")) {
        if (errorMessage)
            *errorMessage = QStringLiteral("保存封面失败：%1").arg(targetPath);
        return false;
    }
    return true;
}

bool LibraryManager::shelfIsDescendant(const QString &shelfId, const QString &possibleChild) const
{
    QString cursor = possibleChild;
    QSet<QString> seen;
    while (!cursor.isEmpty()) {
        if (cursor == shelfId)
            return true;
        if (seen.contains(cursor))
            return false;
        seen.insert(cursor);
        const ShelfRecord *shelf = m_data.shelf(cursor);
        cursor = shelf ? shelf->parentId : QString();
    }
    return false;
}

bool LibraryManager::persistLibrary(QString *errorMessage)
{
    if (!m_store->saveLibrary(m_data, errorMessage))
        return false;
    return true;
}

bool LibraryManager::persistShelves(QString *errorMessage)
{
    return m_store->saveShelves(m_data, errorMessage);
}

} // namespace pano
