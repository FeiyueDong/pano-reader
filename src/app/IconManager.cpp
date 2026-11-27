#include "app/IconManager.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

namespace pano {

IconManager *IconManager::instance()
{
    static IconManager manager;
    return &manager;
}

IconManager::IconManager(QObject *parent)
    : QObject(parent)
    , m_foreground(QColor(QStringLiteral("#6b7280")))
{
}

QString IconManager::cacheKey(const QString &name, const QColor &color, int size) const
{
    return QStringLiteral("%1|%2|%3").arg(name, color.name(QColor::HexArgb)).arg(size);
}

QIcon IconManager::icon(const QString &name, int size)
{
    return icon(name, m_foreground, size);
}

QIcon IconManager::icon(const QString &name, const QColor &color, int size)
{
    const QString key = cacheKey(name, color, size);
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return cached.value();

    QSvgRenderer renderer(QStringLiteral(":/icons/icons/%1.svg").arg(name));
    if (!renderer.isValid())
        return QIcon();

    auto render = [&](const QColor &tint) {
        const int scale = 2;
        QImage image(size * scale, size * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, size * scale, size * scale));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(image.rect(), tint);
        painter.end();
        image.setDevicePixelRatio(scale);
        return QPixmap::fromImage(image);
    };

    QIcon result;
    result.addPixmap(render(color), QIcon::Normal);
    QColor disabled = color;
    disabled.setAlpha(100);
    result.addPixmap(render(disabled), QIcon::Disabled);
    QColor selected = color;
    selected.setAlpha(230);
    result.addPixmap(render(selected), QIcon::Selected);
    m_cache.insert(key, result);
    return result;
}

void IconManager::setForeground(const QColor &color)
{
    if (!color.isValid() || color == m_foreground)
        return;
    m_foreground = color;
    m_cache.clear();
    emit iconsChanged();
}

void IconManager::clearCache()
{
    m_cache.clear();
    emit iconsChanged();
}

} // namespace pano
