#include "pdf/PageRenderer.h"
#include "pdf/PdfDocument.h"

#include <QtGlobal>

namespace pano {

PageRenderer::PageRenderer(QObject *parent)
    : QObject(parent)
{
    m_renderer.setRenderMode(QPdfPageRenderer::RenderMode::MultiThreaded);
    connect(&m_renderer, &QPdfPageRenderer::pageRendered, this, &PageRenderer::onPageRendered);
}

PageRenderer::~PageRenderer()
{
    m_renderer.setDocument(nullptr);
}

void PageRenderer::setDocument(PdfDocument *document)
{
    if (m_document == document)
        return;
    m_renderer.setDocument(nullptr);
    m_document = document;
    m_pendingById.clear();
    m_pendingByKey.clear();
    m_renderer.setDocument(document ? document->pdf() : nullptr);
    clearCache();
}

void PageRenderer::clearCache()
{
    m_cache.clear();
    m_lru.clear();
    m_cacheBytes = 0;
    emit cacheChanged();
}

QString PageRenderer::cacheKey(int pageIndex, const QSize &pixelSize)
{
    return QStringLiteral("%1/%2x%3").arg(pageIndex).arg(pixelSize.width()).arg(pixelSize.height());
}

bool PageRenderer::hasCachedImage(int pageIndex, const QSize &pixelSize) const
{
    return m_cache.contains(cacheKey(pageIndex, pixelSize));
}

QImage PageRenderer::cachedImage(int pageIndex, const QSize &pixelSize)
{
    const QString key = cacheKey(pageIndex, pixelSize);
    const auto it = m_cache.constFind(key);
    if (it == m_cache.constEnd())
        return QImage();
    touch(key);
    return it.value();
}

quint64 PageRenderer::requestPage(int pageIndex, const QSize &pixelSize)
{
    if (!m_document || !m_document->isLoaded() || pageIndex < 0 || pageIndex >= m_document->pageCount())
        return 0;
    if (pixelSize.isEmpty())
        return 0;

    const QString key = cacheKey(pageIndex, pixelSize);
    if (m_cache.contains(key)) {
        touch(key);
        return 0;
    }
    if (m_pendingByKey.contains(key))
        return m_pendingByKey.value(key);

    const quint64 id = m_renderer.requestPage(pageIndex, pixelSize);
    if (id == 0)
        return 0;
    m_pendingByKey.insert(key, id);
    m_pendingById.insert(id, key);
    return id;
}

void PageRenderer::onPageRendered(int pageIndex, QSize imageSize, const QImage &image,
                                  QPdfDocumentRenderOptions options, quint64 requestId)
{
    Q_UNUSED(options);

    const QString key = m_pendingById.value(requestId);
    if (key.isEmpty()) {
        // Result from a previous document or a coalesced duplicate: drop it.
        return;
    }
    m_pendingById.remove(requestId);
    m_pendingByKey.remove(key);

    if (image.isNull() || !m_document)
        return;

    if (m_cache.contains(key)) {
        m_cacheBytes -= m_cache.value(key).sizeInBytes();
        m_cache[key] = image;
        m_cacheBytes += image.sizeInBytes();
    } else {
        m_cache.insert(key, image);
        m_lru.append(key);
        m_cacheBytes += image.sizeInBytes();
    }
    touch(key);
    trimCache();
    emit cacheChanged();
    emit pageReady(pageIndex, imageSize, image, requestId);
}

void PageRenderer::touch(const QString &key)
{
    m_lru.removeAll(key);
    m_lru.append(key);
}

void PageRenderer::dropEntry(const QString &key)
{
    const auto it = m_cache.find(key);
    if (it == m_cache.end())
        return;
    m_cacheBytes -= it.value().sizeInBytes();
    m_cache.erase(it);
    m_lru.removeAll(key);
}

void PageRenderer::trimCache()
{
    while (m_cacheBytes > m_cacheLimit && !m_lru.isEmpty())
        dropEntry(m_lru.first());
    if (m_cacheBytes < 0)
        m_cacheBytes = 0;
}

void PageRenderer::setCacheLimit(qint64 bytes)
{
    m_cacheLimit = qMax<qint64>(16LL * 1024 * 1024, bytes);
    trimCache();
    emit cacheChanged();
}

} // namespace pano
