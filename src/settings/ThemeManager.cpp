#include "settings/ThemeManager.h"

#include <QApplication>
#include <QPalette>

namespace pano {

namespace {

QPalette paletteFor(ThemeId id)
{
    QPalette palette;
    switch (id) {
    case ThemeId::Classic:
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#dfe3e8")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#e2e6eb")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#e7ebf0")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f6fed")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#aeb8c4")));
        break;
    case ThemeId::Warm:
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f2e9dc")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#3f342a")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#fffaf2")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#eee0cf")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#3f342a")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#eadfce")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#3f342a")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f6fed")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#c9b69c")));
        break;
    case ThemeId::ModernDark:
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#1c2027")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e5e7eb")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#232831")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#15181e")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#e5e7eb")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#2b303a")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e5e7eb")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#3b82f6")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#454c58")));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#6b7280")));
        palette.setColor(QPalette::Disabled, QPalette::WindowText,
                         QColor(QStringLiteral("#6b7280")));
        break;
    case ThemeId::ModernLight:
    default:
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f5f7fa")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#eceef2")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#eef1f5")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#1f2937")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f6fed")));
        palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
        palette.setColor(QPalette::Mid, QColor(QStringLiteral("#c9ced8")));
        break;
    }
    return palette;
}

} // namespace

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
}

QVector<ThemeInfo> ThemeManager::themes()
{
    return {
        {ThemeId::ModernLight, QStringLiteral("modern-light"), QStringLiteral("现代浅色"),
         QStringLiteral("清爽的中性浅色界面，适合白天阅读。")},
        {ThemeId::Classic, QStringLiteral("classic"), QStringLiteral("经典"),
         QStringLiteral("接近传统桌面应用的灰蓝配色。")},
        {ThemeId::Warm, QStringLiteral("warm"), QStringLiteral("暖色"),
         QStringLiteral("低对比暖色纸张感，适合长时间阅读。")},
        {ThemeId::ModernDark, QStringLiteral("modern-dark"), QStringLiteral("现代深色"),
         QStringLiteral("低亮度深色界面，适合夜间。")},
    };
}

QString ThemeManager::keyFor(ThemeId id)
{
    for (const ThemeInfo &info : themes()) {
        if (info.id == id)
            return info.key;
    }
    return QStringLiteral("modern-light");
}

bool ThemeManager::fromKey(const QString &key, ThemeId *id)
{
    for (const ThemeInfo &info : themes()) {
        if (info.key == key) {
            if (id)
                *id = info.id;
            return true;
        }
    }
    return false;
}

QString ThemeManager::displayName(ThemeId id)
{
    for (const ThemeInfo &info : themes()) {
        if (info.id == id)
            return info.name;
    }
    return QStringLiteral("现代浅色");
}

QString ThemeManager::styleSheet(ThemeId id)
{
    switch (id) {
    case ThemeId::Classic:
        return QStringLiteral(R"(
            QMainWindow, QDialog { background: #dfe3e8; color: #1f2937; }
            QMenuBar, QMenu, QToolBar, QStatusBar { background: #e8ebef; color: #1f2937; }
            QMenuBar::item:selected, QMenu::item:selected { background: #c7d2e0; }
            QToolBar { border: 0; spacing: 4px; }
            QListWidget::item:selected { background: #2f6fed; color: #ffffff; }
            QListWidget::item:hover { background: #dce8f8; }
            QTabWidget::pane { border: 1px solid #aeb8c4; background: #f2f4f7; }
            QTabBar::tab { background: #d7dce3; border: 1px solid #aeb8c4; padding: 6px 14px; }
            QTabBar::tab:selected { background: #f2f4f7; border-bottom-color: #f2f4f7; }
            QTreeWidget, QListWidget, QTextBrowser, QPlainTextEdit, QLineEdit, QComboBox,
            QSpinBox, QDoubleSpinBox { background: #ffffff; color: #1f2937; border: 1px solid #aeb8c4; }
            QPushButton { background: #e7ebf0; border: 1px solid #9aa7b5; padding: 5px 12px; }
            QPushButton:hover { background: #d7e0ea; }
            QPushButton:pressed { background: #c7d2e0; }
            QGroupBox { border: 1px solid #b5bfca; margin-top: 10px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QSplitter::handle { background: #c3cbd5; }
            QStatusBar QLabel { color: #3b4654; }
        )");
    case ThemeId::Warm:
        return QStringLiteral(R"(
            QMainWindow, QDialog { background: #f2e9dc; color: #3f342a; }
            QMenuBar, QMenu, QToolBar, QStatusBar { background: #eadfce; color: #3f342a; }
            QMenuBar::item:selected, QMenu::item:selected { background: #ddcdb6; }
            QToolBar { border: 0; spacing: 4px; }
            QListWidget::item:selected { background: #2f6fed; color: #ffffff; }
            QListWidget::item:hover { background: #dce8f8; }
            QTabWidget::pane { border: 1px solid #cdbba3; background: #fbf6ee; }
            QTabBar::tab { background: #e5d7c4; border: 1px solid #cdbba3; padding: 6px 14px; }
            QTabBar::tab:selected { background: #fbf6ee; border-bottom-color: #fbf6ee; }
            QTreeWidget, QListWidget, QTextBrowser, QPlainTextEdit, QLineEdit, QComboBox,
            QSpinBox, QDoubleSpinBox { background: #fffaf3; color: #3f342a; border: 1px solid #cdbba3; }
            QPushButton { background: #eadfce; border: 1px solid #c3ab8c; padding: 5px 12px; }
            QPushButton:hover { background: #e0d0b9; }
            QPushButton:pressed { background: #d4c1a5; }
            QGroupBox { border: 1px solid #d2c0a8; margin-top: 10px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QSplitter::handle { background: #d9c8b2; }
            QStatusBar QLabel { color: #5a4939; }
        )");
    case ThemeId::ModernDark:
        return QStringLiteral(R"(
            QMainWindow, QDialog { background: #1c2027; color: #d8dee8; }
            QMenuBar, QMenu, QToolBar, QStatusBar { background: #232831; color: #d8dee8; }
            QMenuBar::item:selected, QMenu::item:selected { background: #35404f; }
            QToolBar { border: 0; spacing: 4px; }
            QListWidget::item:selected { background: #2f6fed; color: #ffffff; }
            QListWidget::item:hover { background: #dce8f8; }
            QTabWidget::pane { border: 1px solid #39414e; background: #20252d; }
            QTabBar::tab { background: #282f39; border: 1px solid #39414e; padding: 6px 14px; }
            QTabBar::tab:selected { background: #20252d; border-bottom-color: #20252d; }
            QTreeWidget, QListWidget, QTextBrowser, QPlainTextEdit, QLineEdit, QComboBox,
            QSpinBox, QDoubleSpinBox { background: #252b34; color: #d8dee8; border: 1px solid #414a58; }
            QPushButton { background: #2e3641; border: 1px solid #46505e; padding: 5px 12px; }
            QPushButton:hover { background: #38424f; }
            QPushButton:pressed { background: #424e5d; }
            QGroupBox { border: 1px solid #414a58; margin-top: 10px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QSplitter::handle { background: #3a4350; }
            QStatusBar QLabel { color: #aab4c2; }
            QToolTip { background: #2e3641; color: #e6ebf2; border: 1px solid #46505e; }
        )");
    case ThemeId::ModernLight:
    default:
        return QStringLiteral(R"(
            QMainWindow, QDialog { background: #f4f6f9; color: #20242b; }
            QMenuBar, QMenu, QToolBar, QStatusBar { background: #ffffff; color: #20242b; }
            QMenuBar::item:selected, QMenu::item:selected { background: #e7edf6; }
            QToolBar { border: 0; spacing: 4px; }
            QListWidget::item:selected { background: #2f6fed; color: #ffffff; }
            QListWidget::item:hover { background: #dce8f8; }
            QTabWidget::pane { border: 1px solid #d9dee7; background: #ffffff; }
            QTabBar::tab { background: #eef1f6; border: 1px solid #d9dee7; padding: 6px 14px; }
            QTabBar::tab:selected { background: #ffffff; border-bottom-color: #ffffff; }
            QTreeWidget, QListWidget, QTextBrowser, QPlainTextEdit, QLineEdit, QComboBox,
            QSpinBox, QDoubleSpinBox { background: #ffffff; color: #20242b; border: 1px solid #cfd6e2; }
            QPushButton { background: #f4f6fa; border: 1px solid #c5cddb; padding: 5px 12px; }
            QPushButton:hover { background: #e8eef7; }
            QPushButton:pressed { background: #dce6f4; }
            QGroupBox { border: 1px solid #d5dbe5; margin-top: 10px; padding-top: 8px; }
            QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }
            QSplitter::handle { background: #dce1e9; }
            QStatusBar QLabel { color: #4b5563; }
        )");
    }
}

void ThemeManager::setCurrentTheme(ThemeId id, QApplication *application)
{
    if (m_current == id && application && application->styleSheet() == styleSheet(id))
        return;
    m_current = id;
    apply(application);
    emit themeChanged(id);
}

void ThemeManager::apply(QApplication *application) const
{
    if (!application)
        application = qobject_cast<QApplication *>(QApplication::instance());
    if (!application)
        return;
    application->setPalette(paletteFor(m_current));
    application->setStyleSheet(styleSheet(m_current));
}

} // namespace pano
