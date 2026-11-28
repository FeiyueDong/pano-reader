#include "storage/LibraryStore.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSet>

#include "storage/JsonUtils.h"

namespace pano {

namespace {

bool readNonEmptyString(const QJsonObject &object, const char *key, QString *value)
{
    const QJsonValue jsonValue = object.value(QLatin1String(key));
    if (!jsonValue.isString())
        return false;
    const QString text = jsonValue.toString().trimmed();
    if (text.isEmpty())
        return false;
    *value = text;
    return true;
}

bool validateBookDirectory(const QString &directory)
{
    return isSafeLibraryRelativePath(directory) && directory.startsWith(QLatin1String("books/"));
}

} // namespace

LibraryStore::LibraryStore(QObject *parent)
    : QObject(parent)
{
}

LibraryStore::~LibraryStore()
{
    close();
}

QString LibraryStore::lockPath() const
{
    return m_rootPath.isEmpty() ? QString() : QDir(m_rootPath).filePath(QStringLiteral(".pano.lock"));
}

bool LibraryStore::create(const QString &rootPath, LibraryData *data, QString *errorMessage)
{
    close();
    if (!data)
        return false;
    const QString absoluteRoot = QFileInfo(rootPath).absoluteFilePath();
    if (!ensureDirectory(absoluteRoot, errorMessage))
        return false;

    if (!acquireLock(absoluteRoot, errorMessage))
        return false;

    const QDir root(absoluteRoot);
    if (root.exists(QStringLiteral("library.json"))) {
        if (errorMessage)
            *errorMessage = QStringLiteral("该目录已经是一个 PANO 书库，请直接打开。");
        releaseLock();
        return false;
    }
    const QStringList existing = root.entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const QString &name : existing) {
        if (name == QLatin1String(".pano.lock"))
            continue;
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标目录不是空目录，无法初始化为新书库：%1")
                                .arg(absoluteRoot);
        }
        releaseLock();
        return false;
    }

    if (!ensureDirectory(root.filePath(QStringLiteral("books")), errorMessage)
        || !ensureDirectory(root.filePath(QStringLiteral("shelves")), errorMessage)
        || !ensureDirectory(root.filePath(QStringLiteral("state")), errorMessage)) {
        releaseLock();
        return false;
    }

    LibraryData created;
    created.id = newStableId();
    created.rootPath = absoluteRoot;

    QJsonObject libraryJson;
    libraryJson.insert(QStringLiteral("format"), QStringLiteral("pano-library"));
    libraryJson.insert(QStringLiteral("schema_version"), 1);
    libraryJson.insert(QStringLiteral("id"), created.id);
    libraryJson.insert(QStringLiteral("books"), QJsonArray());

    QJsonObject shelvesJson;
    shelvesJson.insert(QStringLiteral("format"), QStringLiteral("pano-bookshelves"));
    shelvesJson.insert(QStringLiteral("schema_version"), 1);
    shelvesJson.insert(QStringLiteral("shelves"), QJsonArray());

    QJsonObject readingJson;
    readingJson.insert(QStringLiteral("format"), QStringLiteral("pano-reading-state"));
    readingJson.insert(QStringLiteral("schema_version"), 1);
    readingJson.insert(QStringLiteral("books"), QJsonObject());

    QJsonObject historyJson;
    historyJson.insert(QStringLiteral("format"), QStringLiteral("pano-reading-history"));
    historyJson.insert(QStringLiteral("schema_version"), 1);
    historyJson.insert(QStringLiteral("books"), QJsonObject());

    if (!writeJsonObjectAtomic(root.filePath(QStringLiteral("library.json")), libraryJson, errorMessage)
        || !writeJsonObjectAtomic(root.filePath(QStringLiteral("shelves/shelves.json")), shelvesJson, errorMessage)
        || !writeJsonObjectAtomic(root.filePath(QStringLiteral("state/reading.json")), readingJson, errorMessage)
        || !writeJsonObjectAtomic(root.filePath(QStringLiteral("state/history.json")), historyJson, errorMessage)) {
        releaseLock();
        return false;
    }

    m_open = true;
    m_rootPath = absoluteRoot;
    m_libraryId = created.id;
    m_diagnostics.clear();
    m_fileHashes.clear();
    for (const QString &relative : {QStringLiteral("library.json"), QStringLiteral("shelves/shelves.json"),
                                    QStringLiteral("state/reading.json"), QStringLiteral("state/history.json")}) {
        rememberHash(root.filePath(relative));
    }
    m_sourcesLoaded = false;
    m_sourceOverrides.clear();
    m_sourcesRoot = QJsonObject();
    *data = created;
    return true;
}

bool LibraryStore::open(const QString &rootPath, LibraryData *data, QString *errorMessage)
{
    close();
    if (!data)
        return false;
    QString absoluteRoot = QFileInfo(rootPath).absoluteFilePath();
    if (!QFileInfo(absoluteRoot).isDir()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书库目录不存在：%1").arg(absoluteRoot);
        return false;
    }
    absoluteRoot = QFileInfo(absoluteRoot).canonicalFilePath();
    if (absoluteRoot.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法解析书库目录：%1").arg(rootPath);
        return false;
    }

    if (!acquireLock(absoluteRoot, errorMessage))
        return false;

    const QDir root(absoluteRoot);
    const QString libraryPath = root.filePath(QStringLiteral("library.json"));
    if (!QFileInfo::exists(libraryPath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("该目录不是 PANO 书库（缺少 library.json）：%1")
                                .arg(absoluteRoot);
        releaseLock();
        return false;
    }

    LibraryData loaded;
    loaded.rootPath = absoluteRoot;
    if (!loadLibraryFile(libraryPath, &loaded, errorMessage)) {
        releaseLock();
        return false;
    }
    if (!loadShelvesFile(root.filePath(QStringLiteral("shelves/shelves.json")), &loaded, errorMessage)) {
        releaseLock();
        return false;
    }

    const QString readingPath = root.filePath(QStringLiteral("state/reading.json"));
    const QString historyPath = root.filePath(QStringLiteral("state/history.json"));
    if (QFileInfo::exists(readingPath)
        && !loadReadingFile(readingPath, &loaded, errorMessage)) {
        releaseLock();
        return false;
    }
    if (QFileInfo::exists(historyPath)
        && !loadHistoryFile(historyPath, &loaded, errorMessage)) {
        releaseLock();
        return false;
    }

    // Missing book directories are reported but do not destroy metadata.
    for (const BookRecord &book : loaded.books) {
        const QString directory = QDir(absoluteRoot).filePath(book.directory);
        if (!QFileInfo(directory).isDir())
            m_diagnostics.append(QStringLiteral("书籍目录缺失：%1（书籍 %2）").arg(book.directory, book.id));
    }

    m_open = true;
    m_rootPath = absoluteRoot;
    m_libraryId = loaded.id;
    m_fileHashes.clear();
    for (const QString &path : {libraryPath, root.filePath(QStringLiteral("shelves/shelves.json")),
                                readingPath, historyPath}) {
        if (QFileInfo::exists(path))
            rememberHash(path);
    }
    m_sourcesLoaded = false;
    m_sourceOverrides.clear();
    m_sourcesRoot = QJsonObject();
    if (!loadSources(errorMessage)) {
        releaseLock();
        m_open = false;
        return false;
    }

    *data = loaded;
    return true;
}

void LibraryStore::close()
{
    releaseLock();
    m_open = false;
    m_rootPath.clear();
    m_libraryId.clear();
    m_fileHashes.clear();
    m_diagnostics.clear();
    m_sourcesLoaded = false;
    m_sourceOverrides.clear();
    m_sourcesRoot = QJsonObject();
}

bool LibraryStore::acquireLock(const QString &rootPath, QString *errorMessage)
{
    releaseLock();
    auto *lock = new QLockFile(QDir(rootPath).filePath(QStringLiteral(".pano.lock")));
    lock->setStaleLockTime(30000);
    if (!lock->tryLock(0)) {
        qint64 pid = 0;
        QString host;
        QString appName;
        lock->getLockInfo(&pid, &host, &appName);
        if (errorMessage) {
            *errorMessage = QStringLiteral("书库正在被另一个 PANO 实例使用（%1，PID %2）。"
                                           "请关闭那个实例或确认进程已退出后重试。")
                                .arg(host.isEmpty() ? QStringLiteral("本机") : host)
                                .arg(pid);
        }
        delete lock;
        return false;
    }
    m_lock = lock;
    return true;
}

void LibraryStore::releaseLock()
{
    if (m_lock) {
        m_lock->unlock();
        delete m_lock;
        m_lock = nullptr;
    }
}

bool LibraryStore::loadLibraryFile(const QString &path, LibraryData *data, QString *errorMessage)
{
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-library")) {
        if (errorMessage)
            *errorMessage = QStringLiteral("library.json 格式标识无效，拒绝加载。");
        return false;
    }
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("不支持的 library.json 版本：%1。请升级 PANO，程序不会覆盖该文件。")
                                .arg(root.value(QStringLiteral("schema_version")).toInt());
        }
        return false;
    }
    if (!readNonEmptyString(root, "id", &data->id)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("library.json 缺少书库 ID。");
        return false;
    }

    const QJsonValue booksValue = root.value(QStringLiteral("books"));
    if (!booksValue.isArray()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("library.json 的 books 字段无效。");
        return false;
    }
    QSet<QString> ids;
    QSet<QString> directories;
    for (const QJsonValue &value : booksValue.toArray()) {
        if (!value.isObject()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("library.json 包含无效书籍记录。");
            return false;
        }
        const QJsonObject object = value.toObject();
        BookRecord book;
        if (!readNonEmptyString(object, "id", &book.id)
            || !readNonEmptyString(object, "directory", &book.directory)
            || !validateBookDirectory(book.directory)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("library.json 包含非法书籍 ID 或目录。");
            return false;
        }
        if (ids.contains(book.id) || directories.contains(book.directory)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("library.json 包含重复书籍 ID/目录。");
            return false;
        }
        ids.insert(book.id);
        directories.insert(book.directory);
        book.favorite = object.value(QStringLiteral("favorite")).toBool(false);
        const QJsonValue deleted = object.value(QStringLiteral("deleted_at"));
        if (!deleted.isNull() && !deleted.isUndefined()) {
            if (!deleted.isString()) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("library.json 的 deleted_at 类型无效。");
                return false;
            }
            book.deletedAt = deleted.toString();
        }
        data->books.append(book);
    }
    return true;
}

bool LibraryStore::loadShelvesFile(const QString &path, LibraryData *data, QString *errorMessage)
{
    if (!QFileInfo::exists(path)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书库缺少 shelves/shelves.json，拒绝加载以避免覆盖书架数据。");
        return false;
    }
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-bookshelves")) {
        if (errorMessage)
            *errorMessage = QStringLiteral("shelves.json 格式标识无效。");
        return false;
    }
    if (root.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("不支持的 shelves.json 版本。");
        return false;
    }
    const QJsonValue shelvesValue = root.value(QStringLiteral("shelves"));
    if (!shelvesValue.isArray()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("shelves.json 的 shelves 字段无效。");
        return false;
    }
    QSet<QString> ids;
    for (const QJsonValue &value : shelvesValue.toArray()) {
        if (!value.isObject()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("shelves.json 包含无效书架记录。");
            return false;
        }
        const QJsonObject object = value.toObject();
        ShelfRecord shelf;
        if (!readNonEmptyString(object, "id", &shelf.id)
            || !readNonEmptyString(object, "name", &shelf.name)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("shelves.json 包含缺少 ID 或名称的书架。");
            return false;
        }
        if (ids.contains(shelf.id)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("shelves.json 包含重复书架 ID。");
            return false;
        }
        ids.insert(shelf.id);
        const QJsonValue parent = object.value(QStringLiteral("parent_id"));
        if (!parent.isNull() && !parent.isUndefined()) {
            if (!parent.isString()) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("书架 parent_id 类型无效。");
                return false;
            }
            shelf.parentId = parent.toString();
        }
        const QJsonValue members = object.value(QStringLiteral("book_ids"));
        if (!members.isArray()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("书架 book_ids 字段无效。");
            return false;
        }
        QSet<QString> memberIds;
        for (const QJsonValue &member : members.toArray()) {
            if (!member.isString() || member.toString().isEmpty()
                || memberIds.contains(member.toString())) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("书架成员列表包含非法或重复 ID。");
                return false;
            }
            memberIds.insert(member.toString());
            shelf.bookIds.append(member.toString());
        }
        data->shelves.append(shelf);
    }

    // Validate parent references and cycles after all shelves are known.
    for (const ShelfRecord &shelf : data->shelves) {
        if (shelf.parentId.isEmpty())
            continue;
        if (!ids.contains(shelf.parentId) || shelf.parentId == shelf.id) {
            if (errorMessage)
                *errorMessage = QStringLiteral("书架父级关系无效：%1").arg(shelf.name);
            return false;
        }
        QSet<QString> seen;
        QString cursor = shelf.id;
        while (!cursor.isEmpty()) {
            if (seen.contains(cursor)) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("书架父级关系存在循环：%1").arg(shelf.name);
                return false;
            }
            seen.insert(cursor);
            const int index = data->shelfIndex(cursor);
            cursor = index >= 0 ? data->shelves.at(index).parentId : QString();
        }
    }
    return true;
}

bool LibraryStore::loadReadingFile(const QString &path, LibraryData *data, QString *errorMessage)
{
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-reading-state")
        || root.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("state/reading.json 格式或版本无效。");
        return false;
    }
    const QJsonObject books = root.value(QStringLiteral("books")).toObject();
    for (auto it = books.constBegin(); it != books.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject object = it.value().toObject();
        ReadingState state;
        state.pageIndex = object.value(QStringLiteral("page_index")).toInt(-1);
        state.zoom = object.value(QStringLiteral("zoom")).toDouble(1.0);
        state.rotation = object.value(QStringLiteral("rotation")).toInt(0);
        state.updatedAt = object.value(QStringLiteral("updated_at")).toString();
        if (state.pageIndex < 0 || !qIsFinite(state.zoom) || state.zoom < 0.05
            || state.zoom > 12.0 || state.rotation % 90 != 0
            || state.rotation < 0 || state.rotation >= 360) {
            m_diagnostics.append(QStringLiteral("忽略无效阅读状态：%1").arg(it.key()));
            continue;
        }
        const QJsonArray point = object.value(QStringLiteral("page_point")).toArray();
        if (point.size() == 2)
            state.pagePoint = QPointF(point.at(0).toDouble(), point.at(1).toDouble());
        ZoomMode mode = ZoomMode::Fixed;
        if (!zoomModeFromString(object.value(QStringLiteral("zoom_mode")).toString(), &mode))
            mode = ZoomMode::Fixed;
        state.zoomMode = mode;
        state.layoutMode = object.value(QStringLiteral("layout_mode")).toString(QStringLiteral("continuous"));
        if (state.layoutMode != QLatin1String("single")
            && state.layoutMode != QLatin1String("continuous")
            && state.layoutMode != QLatin1String("facing")
            && state.layoutMode != QLatin1String("continuous_facing")) {
            state.layoutMode = QStringLiteral("continuous");
        }
        state.firstPageAlone = object.value(QStringLiteral("first_page_alone")).toBool(true);
        state.valid = true;
        data->reading.insert(it.key(), state);
    }
    return true;
}

bool LibraryStore::loadHistoryFile(const QString &path, LibraryData *data, QString *errorMessage)
{
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-reading-history")
        || root.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("state/history.json 格式或版本无效。");
        return false;
    }
    const QJsonObject books = root.value(QStringLiteral("books")).toObject();
    for (auto it = books.constBegin(); it != books.constEnd(); ++it) {
        const QDateTime time = QDateTime::fromString(it.value().toString(), Qt::ISODateWithMs);
        if (time.isValid())
            data->history.insert(it.key(), time.toUTC());
    }
    return true;
}

bool LibraryStore::saveLibrary(const LibraryData &data, QString *errorMessage)
{
    if (!m_open)
        return false;
    QJsonArray books;
    for (const BookRecord &book : data.books) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), book.id);
        object.insert(QStringLiteral("directory"), book.directory);
        object.insert(QStringLiteral("favorite"), book.favorite);
        if (book.deletedAt.isEmpty())
            object.insert(QStringLiteral("deleted_at"), QJsonValue::Null);
        else
            object.insert(QStringLiteral("deleted_at"), book.deletedAt);
        books.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-library"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("id"), data.id);
    root.insert(QStringLiteral("books"), books);
    return writeChecked(QDir(m_rootPath).filePath(QStringLiteral("library.json")), root, errorMessage);
}

bool LibraryStore::saveShelves(const LibraryData &data, QString *errorMessage)
{
    if (!m_open)
        return false;
    QJsonArray shelves;
    for (const ShelfRecord &shelf : data.shelves) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), shelf.id);
        object.insert(QStringLiteral("name"), shelf.name);
        if (shelf.parentId.isEmpty())
            object.insert(QStringLiteral("parent_id"), QJsonValue::Null);
        else
            object.insert(QStringLiteral("parent_id"), shelf.parentId);
        QJsonArray members;
        for (const QString &bookId : shelf.bookIds)
            members.append(bookId);
        object.insert(QStringLiteral("book_ids"), members);
        shelves.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-bookshelves"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("shelves"), shelves);
    return writeChecked(QDir(m_rootPath).filePath(QStringLiteral("shelves/shelves.json")), root, errorMessage);
}

bool LibraryStore::saveReadingState(const LibraryData &data, QString *errorMessage)
{
    if (!m_open)
        return false;
    QJsonObject books;
    for (auto it = data.reading.constBegin(); it != data.reading.constEnd(); ++it) {
        const ReadingState &state = it.value();
        if (!state.valid)
            continue;
        QJsonObject object;
        object.insert(QStringLiteral("page_index"), state.pageIndex);
        QJsonArray point;
        point.append(state.pagePoint.x());
        point.append(state.pagePoint.y());
        object.insert(QStringLiteral("page_point"), point);
        object.insert(QStringLiteral("zoom"), state.zoom);
        object.insert(QStringLiteral("zoom_mode"), zoomModeToString(state.zoomMode));
        object.insert(QStringLiteral("rotation"), state.rotation);
        object.insert(QStringLiteral("layout_mode"), state.layoutMode);
        object.insert(QStringLiteral("first_page_alone"), state.firstPageAlone);
        object.insert(QStringLiteral("updated_at"), state.updatedAt);
        books.insert(it.key(), object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-reading-state"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("books"), books);
    return writeChecked(QDir(m_rootPath).filePath(QStringLiteral("state/reading.json")), root, errorMessage);
}

bool LibraryStore::saveHistory(const LibraryData &data, QString *errorMessage)
{
    if (!m_open)
        return false;
    QJsonObject books;
    for (auto it = data.history.constBegin(); it != data.history.constEnd(); ++it) {
        if (it.value().isValid())
            books.insert(it.key(), utcIso(it.value()));
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-reading-history"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("books"), books);
    return writeChecked(QDir(m_rootPath).filePath(QStringLiteral("state/history.json")), root, errorMessage);
}

bool LibraryStore::readDocument(const QString &bookDirectory, BookDocument *document,
                                QString *errorMessage) const
{
    if (!document)
        return false;
    const QString path = QDir(absoluteBookDirectory(bookDirectory)).filePath(QStringLiteral("document.json"));
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-document")
        || root.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("document.json 格式或版本无效：%1").arg(path);
        return false;
    }
    BookDocument result;
    if (!readNonEmptyString(root, "id", &result.id)
        || !readNonEmptyString(root, "title", &result.title)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("document.json 缺少书籍 ID 或标题：%1").arg(path);
        return false;
    }
    result.author = root.value(QStringLiteral("author")).toString();
    result.importedAt = root.value(QStringLiteral("imported_at")).toString();
    const QJsonObject source = root.value(QStringLiteral("source")).toObject();
    if (!importModeFromString(source.value(QStringLiteral("mode")).toString(), &result.mode)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("document.json 的 source.mode 无效：%1").arg(path);
        return false;
    }
    result.sourcePath = source.value(QStringLiteral("path")).toString();
    result.sha256 = source.value(QStringLiteral("sha256")).toString().toLower();
    result.sizeBytes = static_cast<qint64>(source.value(QStringLiteral("size_bytes")).toDouble());
    if (result.sha256.size() != 64 || result.sourcePath.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("document.json 的源文件信息不完整：%1").arg(path);
        return false;
    }
    if (result.mode == ImportMode::Copy && !isSafeLibraryRelativePath(result.sourcePath)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("拷贝模式源路径必须位于书籍目录内：%1").arg(path);
        return false;
    }
    *document = result;
    return true;
}

bool LibraryStore::writeDocument(const QString &bookDirectory, const BookDocument &document,
                                 QString *errorMessage)
{
    QJsonObject source;
    source.insert(QStringLiteral("mode"), importModeToString(document.mode));
    source.insert(QStringLiteral("path"), document.sourcePath);
    source.insert(QStringLiteral("sha256"), document.sha256);
    source.insert(QStringLiteral("size_bytes"), static_cast<double>(document.sizeBytes));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-document"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("id"), document.id);
    root.insert(QStringLiteral("title"), document.title);
    root.insert(QStringLiteral("author"), document.author);
    root.insert(QStringLiteral("imported_at"), document.importedAt);
    root.insert(QStringLiteral("source"), source);
    const QString path = QDir(absoluteBookDirectory(bookDirectory)).filePath(QStringLiteral("document.json"));
    return writeJsonObjectAtomic(path, root, errorMessage);
}

bool LibraryStore::readReadingData(const QString &bookDirectory, const QString &bookId,
                                   QJsonArray *annotations, QJsonArray *notes,
                                   QJsonArray *noteTree, QString *errorMessage) const
{
    if (!annotations || !notes || !noteTree)
        return false;
    *annotations = QJsonArray();
    *notes = QJsonArray();
    *noteTree = QJsonArray();
    const QString path = QDir(absoluteBookDirectory(bookDirectory)).filePath(QStringLiteral("annotations.json"));
    if (!QFileInfo::exists(path))
        return true;
    QJsonObject root;
    if (!readJsonObject(path, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("pano-annotations")
        || root.value(QStringLiteral("schema_version")).toInt(-1) != 2) {
        if (errorMessage)
            *errorMessage = QStringLiteral("annotations.json 格式或版本无效（需要 schema_version 2）：%1").arg(path);
        return false;
    }
    if (root.value(QStringLiteral("book_id")).toString() != bookId) {
        if (errorMessage)
            *errorMessage = QStringLiteral("annotations.json 的 book_id 与书籍不一致：%1").arg(path);
        return false;
    }
    const QJsonValue annotationValue = root.value(QStringLiteral("annotations"));
    const QJsonValue noteValue = root.value(QStringLiteral("notes"));
    const QJsonValue treeValue = root.value(QStringLiteral("note_tree"));
    if (!annotationValue.isArray() || !noteValue.isArray() || !treeValue.isArray()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("annotations.json 的 annotations/notes/note_tree 字段无效：%1").arg(path);
        return false;
    }
    *annotations = annotationValue.toArray();
    *notes = noteValue.toArray();
    *noteTree = treeValue.toArray();
    return true;
}

bool LibraryStore::writeReadingData(const QString &bookDirectory, const QString &bookId,
                                    const QJsonArray &annotations, const QJsonArray &notes,
                                    const QJsonArray &noteTree, QString *errorMessage)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-annotations"));
    root.insert(QStringLiteral("schema_version"), 2);
    root.insert(QStringLiteral("book_id"), bookId);
    root.insert(QStringLiteral("annotations"), annotations);
    root.insert(QStringLiteral("notes"), notes);
    root.insert(QStringLiteral("note_tree"), noteTree);
    const QString path = QDir(absoluteBookDirectory(bookDirectory)).filePath(QStringLiteral("annotations.json"));
    return writeJsonObjectAtomic(path, root, errorMessage);
}

QString LibraryStore::absoluteBookDirectory(const QString &bookDirectory) const
{
    if (m_rootPath.isEmpty() || !isSafeLibraryRelativePath(bookDirectory))
        return QString();
    return QDir(m_rootPath).filePath(bookDirectory);
}

bool LibraryStore::isManagedBookDirectory(const QString &absolutePath) const
{
    if (m_rootPath.isEmpty())
        return false;
    const QString booksRoot = QDir(m_rootPath).filePath(QStringLiteral("books"));
    return isPathInside(absolutePath, booksRoot);
}

QString LibraryStore::sourceOverride(const QString &bookId) const
{
    return m_sourceOverrides.value(bookId);
}

bool LibraryStore::setSourceOverride(const QString &bookId, const QString &absolutePath,
                                     QString *errorMessage)
{
    if (!m_sourcesLoaded && !loadSources(errorMessage))
        return false;
    m_sourceOverrides.insert(bookId, QFileInfo(absolutePath).absoluteFilePath());
    return saveSources(errorMessage);
}

bool LibraryStore::clearSourceOverride(const QString &bookId, QString *errorMessage)
{
    if (!m_sourcesLoaded && !loadSources(errorMessage))
        return false;
    m_sourceOverrides.remove(bookId);
    return saveSources(errorMessage);
}

bool LibraryStore::loadSources(QString *errorMessage)
{
    m_sourcesPath = QDir(appConfigDirectory()).filePath(QStringLiteral("sources.json"));
    m_sourceOverrides.clear();
    m_sourcesRoot = QJsonObject();
    if (QFileInfo::exists(m_sourcesPath)) {
        if (!readJsonObject(m_sourcesPath, &m_sourcesRoot, errorMessage))
            return false;
        if (m_sourcesRoot.value(QStringLiteral("schema_version")).toInt(-1) != 1) {
            if (errorMessage)
                *errorMessage = QStringLiteral("本机 sources.json 版本不受支持，拒绝覆盖。");
            return false;
        }
    } else {
        m_sourcesRoot.insert(QStringLiteral("format"), QStringLiteral("pano-local-sources"));
        m_sourcesRoot.insert(QStringLiteral("schema_version"), 1);
        m_sourcesRoot.insert(QStringLiteral("libraries"), QJsonObject());
    }
    const QJsonObject libraries = m_sourcesRoot.value(QStringLiteral("libraries")).toObject();
    const QJsonObject library = libraries.value(m_libraryId).toObject();
    const QJsonObject overrides = library.value(QStringLiteral("source_overrides")).toObject();
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.value().isString())
            m_sourceOverrides.insert(it.key(), it.value().toString());
    }
    m_sourcesLoaded = true;
    return true;
}

bool LibraryStore::saveSources(QString *errorMessage)
{
    if (m_sourcesPath.isEmpty())
        m_sourcesPath = QDir(appConfigDirectory()).filePath(QStringLiteral("sources.json"));
    QJsonObject libraries = m_sourcesRoot.value(QStringLiteral("libraries")).toObject();
    QJsonObject library = libraries.value(m_libraryId).toObject();
    QJsonObject overrides;
    for (auto it = m_sourceOverrides.constBegin(); it != m_sourceOverrides.constEnd(); ++it)
        overrides.insert(it.key(), it.value());
    library.insert(QStringLiteral("source_overrides"), overrides);
    libraries.insert(m_libraryId, library);
    m_sourcesRoot.insert(QStringLiteral("format"), QStringLiteral("pano-local-sources"));
    m_sourcesRoot.insert(QStringLiteral("schema_version"), 1);
    m_sourcesRoot.insert(QStringLiteral("libraries"), libraries);
    return writeJsonObjectAtomic(m_sourcesPath, m_sourcesRoot, errorMessage);
}

bool LibraryStore::writeChecked(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    if (!m_open) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书库未打开。");
        return false;
    }
    if (!checkExternalChange(path, errorMessage))
        return false;
    if (!writeJsonObjectAtomic(path, object, errorMessage))
        return false;
    rememberHash(path);
    return true;
}

void LibraryStore::rememberHash(const QString &path)
{
    const QByteArray digest = currentHash(path);
    if (digest.isEmpty())
        m_fileHashes.remove(path);
    else
        m_fileHashes.insert(path, digest);
}

QByteArray LibraryStore::currentHash(const QString &path) const
{
    if (!QFileInfo::exists(path))
        return {};
    return sha256File(path);
}

bool LibraryStore::checkExternalChange(const QString &path, QString *errorMessage) const
{
    const QByteArray remembered = m_fileHashes.value(path);
    if (remembered.isEmpty())
        return true;
    if (!QFileInfo::exists(path)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("文件在打开书库后被外部删除，已停止写入：%1").arg(path);
        return false;
    }
    const QByteArray current = currentHash(path);
    if (current != remembered) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("文件在外部被修改，已停止写入以保护数据：%1。"
                                           "请关闭书库并重新打开后再操作。")
                                .arg(path);
        }
        return false;
    }
    return true;
}

} // namespace pano
