#pragma once

#include <QDateTime>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVector>

namespace pano {

struct Bookmark {
    QString id;
    QString title;
    int pageIndex = 0;
    QPointF position;
    QString createdAt;

    bool operator==(const Bookmark &other) const
    {
        return id == other.id && title == other.title && pageIndex == other.pageIndex
            && position == other.position && createdAt == other.createdAt;
    }
};

// Per-book bookmarks. The file is independent from annotations and is written
// only when bookmark data actually changes.
class BookmarkStore : public QObject
{
    Q_OBJECT

public:
    explicit BookmarkStore(QObject *parent = nullptr);

    bool load(const QString &bookDirectory, const QString &bookId, QString *errorMessage = nullptr);
    bool save(QString *errorMessage = nullptr);
    bool isLoaded() const { return m_loaded; }
    bool isDirty() const { return m_dirty; }
    const QString &filePath() const { return m_filePath; }
    const QVector<Bookmark> &bookmarks() const { return m_bookmarks; }

    QString addBookmark(const QString &title, int pageIndex, const QPointF &position);
    bool renameBookmark(const QString &id, const QString &title);
    bool removeBookmark(const QString &id);
    bool sortByPage();
    const Bookmark *find(const QString &id) const;

signals:
    void bookmarksChanged();
    void dirtyChanged(bool dirty);

private:
    void setDirty(bool dirty);
    QString fingerprint() const;

    QVector<Bookmark> m_bookmarks;
    QString m_bookDirectory;
    QString m_bookId;
    QString m_filePath;
    QByteArray m_loadedFingerprint;
    bool m_loaded = false;
    bool m_dirty = false;
};

} // namespace pano
