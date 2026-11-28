#include "BookmarkStore.h"

#include "common/JsonUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace pano {

BookmarkStore::BookmarkStore(QObject *parent)
    : QObject(parent)
{
}

bool BookmarkStore::load(const QString &bookDirectory, const QString &bookId, QString *errorMessage)
{
    m_bookDirectory = bookDirectory;
    m_bookId = bookId;
    m_filePath = QDir(bookDirectory).filePath(QStringLiteral("bookmarks.json"));
    m_bookmarks.clear();
    m_loadedFingerprint.clear();
    m_dirty = false;

    if (!QFileInfo::exists(m_filePath)) {
        m_loaded = true;
        emit bookmarksChanged();
        emit dirtyChanged(false);
        return true;
    }

    QJsonObject root;
    if (!readJsonObject(m_filePath, &root, errorMessage))
        return false;
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("pano-bookmarks")
        || root.value(QStringLiteral("schema_version")).toInt() != 1) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书签文件格式或版本不受支持：%1").arg(m_filePath);
        return false;
    }
    if (root.value(QStringLiteral("book_id")).toString() != bookId) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书签文件属于其他书籍，已停止加载。");
        return false;
    }

    const QJsonArray array = root.value(QStringLiteral("bookmarks")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Bookmark bookmark;
        bookmark.id = object.value(QStringLiteral("id")).toString();
        bookmark.title = object.value(QStringLiteral("title")).toString();
        bookmark.pageIndex = object.value(QStringLiteral("page_index")).toInt(-1);
        const QJsonObject position = object.value(QStringLiteral("position")).toObject();
        bookmark.position = QPointF(position.value(QStringLiteral("x")).toDouble(),
                                    position.value(QStringLiteral("y")).toDouble());
        bookmark.createdAt = object.value(QStringLiteral("created_at")).toString();
        if (bookmark.id.isEmpty() || bookmark.pageIndex < 0)
            continue;
        m_bookmarks.append(bookmark);
    }

    m_loadedFingerprint = fingerprint().toUtf8();
    m_loaded = true;
    emit bookmarksChanged();
    emit dirtyChanged(false);
    return true;
}

QString BookmarkStore::fingerprint() const
{
    QFile file(m_filePath);
    if (!file.exists())
        return QString();
    QByteArray digest;
    QString error;
    if (!sha256File(m_filePath, &digest, &error))
        return QString();
    return QString::fromLatin1(digest.toHex());
}

bool BookmarkStore::save(QString *errorMessage)
{
    if (!m_loaded)
        return false;
    if (!m_dirty)
        return true;
    if (!m_loadedFingerprint.isEmpty() && fingerprint() != QString::fromUtf8(m_loadedFingerprint)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书签文件已被外部修改，已停止保存以避免覆盖。");
        return false;
    }

    QJsonArray array;
    for (const Bookmark &bookmark : m_bookmarks) {
        QJsonObject object;
        object.insert(QStringLiteral("id"), bookmark.id);
        object.insert(QStringLiteral("title"), bookmark.title);
        object.insert(QStringLiteral("page_index"), bookmark.pageIndex);
        QJsonObject position;
        position.insert(QStringLiteral("x"), bookmark.position.x());
        position.insert(QStringLiteral("y"), bookmark.position.y());
        object.insert(QStringLiteral("position"), position);
        object.insert(QStringLiteral("created_at"), bookmark.createdAt);
        array.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("pano-bookmarks"));
    root.insert(QStringLiteral("schema_version"), 1);
    root.insert(QStringLiteral("book_id"), m_bookId);
    root.insert(QStringLiteral("bookmarks"), array);

    if (!writeJsonObjectAtomic(m_filePath, root, errorMessage))
        return false;
    m_loadedFingerprint = fingerprint().toUtf8();
    setDirty(false);
    return true;
}

QString BookmarkStore::addBookmark(const QString &title, int pageIndex, const QPointF &position)
{
    Bookmark bookmark;
    bookmark.id = newStableId();
    bookmark.title = title.trimmed().isEmpty()
        ? QStringLiteral("第 %1 页").arg(pageIndex + 1)
        : title.trimmed();
    bookmark.pageIndex = pageIndex;
    bookmark.position = position;
    bookmark.createdAt = utcNowIso();
    m_bookmarks.append(bookmark);
    setDirty(true);
    emit bookmarksChanged();
    return bookmark.id;
}

bool BookmarkStore::renameBookmark(const QString &id, const QString &title)
{
    for (Bookmark &bookmark : m_bookmarks) {
        if (bookmark.id != id)
            continue;
        const QString next = title.trimmed();
        if (next.isEmpty() || next == bookmark.title)
            return false;
        bookmark.title = next;
        setDirty(true);
        emit bookmarksChanged();
        return true;
    }
    return false;
}

bool BookmarkStore::removeBookmark(const QString &id)
{
    const auto it = std::find_if(m_bookmarks.begin(), m_bookmarks.end(),
                                 [&](const Bookmark &b) { return b.id == id; });
    if (it == m_bookmarks.end())
        return false;
    m_bookmarks.erase(it);
    setDirty(true);
    emit bookmarksChanged();
    return true;
}

bool BookmarkStore::sortByPage()
{
    QVector<Bookmark> sorted = m_bookmarks;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Bookmark &a, const Bookmark &b) {
        if (a.pageIndex != b.pageIndex)
            return a.pageIndex < b.pageIndex;
        if (a.position.y() != b.position.y())
            return a.position.y() < b.position.y();
        return a.position.x() < b.position.x();
    });
    if (sorted == m_bookmarks)
        return false;
    m_bookmarks = sorted;
    setDirty(true);
    emit bookmarksChanged();
    return true;
}

const Bookmark *BookmarkStore::find(const QString &id) const
{
    for (const Bookmark &bookmark : m_bookmarks) {
        if (bookmark.id == id)
            return &bookmark;
    }
    return nullptr;
}

void BookmarkStore::setDirty(bool dirty)
{
    if (m_dirty == dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged(dirty);
}

} // namespace pano
