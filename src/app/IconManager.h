#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QObject>

namespace pano {

// Loads the small, vendored Lucide subset from resources and tints it with the
// current theme's semantic foreground colour.  Icons are cached by semantic
// name, colour and pixel size, so theme switches do not re-parse SVG files.
class IconManager : public QObject
{
    Q_OBJECT

public:
    static IconManager *instance();

    QIcon icon(const QString &name, int size = 18);
    QIcon icon(const QString &name, const QColor &color, int size = 18);
    QColor foreground() const { return m_foreground; }
    void setForeground(const QColor &color);
    void clearCache();

signals:
    void iconsChanged();

private:
    explicit IconManager(QObject *parent = nullptr);
    QString cacheKey(const QString &name, const QColor &color, int size) const;

    QColor m_foreground;
    QHash<QString, QIcon> m_cache;
};

} // namespace pano
