#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPdfPageRenderer>
#include <QStringList>

namespace pano {

class PdfDocument;

// Asynchronous page rasterizer with a bounded raw-image cache.
// Only raw page pixels are cached; annotations are always composited live.
class PageRenderer : public QObject
{
    Q_OBJECT

public:
    explicit PageRenderer(QObject *parent = nullptr);
    ~PageRenderer() override;

    void setDocument(PdfDocument *document);
    void clearCache();

    // A cache hit updates LRU order, because this is the normal paint path.
    QImage cachedImage(int pageIndex, const QSize &pixelSize);
    bool hasCachedImage(int pageIndex, const QSize &pixelSize) const;

    // Returns the request id, or 0 if the request could not be issued.
    // A request for the same page/size that is already in flight is coalesced.
    quint64 requestPage(int pageIndex, const QSize &pixelSize);

    qint64 cacheBytes() const { return m_cacheBytes; }
    qint64 cacheLimit() const { return m_cacheLimit; }
    void setCacheLimit(qint64 bytes);

signals:
    void pageReady(int pageIndex, const QSize &pixelSize, const QImage &image, quint64 requestId);
    void pageFailed(int pageIndex, const QSize &pixelSize, quint64 requestId);
    void cacheChanged();

private slots:
    void onPageRendered(int pageIndex, QSize imageSize, const QImage &image,
                        QPdfDocumentRenderOptions options, quint64 requestId);

private:
    static QString cacheKey(int pageIndex, const QSize &pixelSize);
    void touch(const QString &key);
    void trimCache();
    void dropEntry(const QString &key);

    PdfDocument *m_document = nullptr;
    QPdfPageRenderer m_renderer;
    QHash<QString, QImage> m_cache;
    QStringList m_lru; // least recently used first
    qint64 m_cacheBytes = 0;
    qint64 m_cacheLimit = 128LL * 1024 * 1024;
    QHash<quint64, QString> m_pendingById;
    QHash<QString, quint64> m_pendingByKey;
};

} // namespace pano
