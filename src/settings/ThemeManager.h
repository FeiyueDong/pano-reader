#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QApplication;

namespace pano {

enum class ThemeId {
    ModernLight,
    Classic,
    Warm,
    ModernDark
};

struct ThemeInfo {
    ThemeId id = ThemeId::ModernLight;
    QString key;
    QString name;
    QString description;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    explicit ThemeManager(QObject *parent = nullptr);

    static QVector<ThemeInfo> themes();
    static QString keyFor(ThemeId id);
    static bool fromKey(const QString &key, ThemeId *id);
    static QString displayName(ThemeId id);
    static QString styleSheet(ThemeId id);

    ThemeId currentTheme() const { return m_current; }
    void setCurrentTheme(ThemeId id, QApplication *application = nullptr);
    void apply(QApplication *application) const;

signals:
    void themeChanged(ThemeId id);

private:
    ThemeId m_current = ThemeId::ModernLight;
};

} // namespace pano
