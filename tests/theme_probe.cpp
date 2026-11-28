#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>
#include <QComboBox>
#include <QTabWidget>
#include <cstdio>
#include <cstdio>

#include "app/MainWindow.h"
#include "library/LibraryManager.h"
#include "settings/ThemeManager.h"
#include "settings/SettingsManager.h"
#include "settings/SettingsDialog.h"

using namespace pano;

static int failures = 0;
static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}
static void waitFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    const QString root = QStringLiteral("build/probe/theme-runtime");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    qputenv("PANO_CONFIG_DIR", (root + QStringLiteral("/config")).toUtf8());
    const QString libraryPath = root + QStringLiteral("/library");

    LibraryManager manager;
    QString error;
    check(manager.createLibrary(libraryPath, &error), "create library");
    QStringList imported;
    QStringList duplicates;
    check(manager.importBooks({QFileInfo(QStringLiteral(PANO_SAMPLE_PDF)).absoluteFilePath()},
                              ImportMode::Copy, false, &imported, &duplicates, &error),
          "import sample");
    const QString bookId = imported.value(0);
    manager.closeLibrary();

    auto *window = new MainWindow;
    window->resize(1440, 900);
    window->show();
    check(window->openLibraryPath(libraryPath, false), "open library");
    waitFor(300);
    auto *themes = window->findChild<ThemeManager *>();
    check(themes != nullptr, "theme manager exists");
    if (themes) {
        themes->setCurrentTheme(ThemeId::Warm, qApp);
        waitFor(200);
        check(qApp->styleSheet().contains(QStringLiteral("#f2e9dc")), "warm stylesheet applied");
        window->grab().save(QStringLiteral("build/probe/theme-warm-library.png"));
        themes->setCurrentTheme(ThemeId::ModernDark, qApp);
        waitFor(200);
        check(qApp->styleSheet().contains(QStringLiteral("#1c2027")), "dark stylesheet applied");
        window->grab().save(QStringLiteral("build/probe/theme-dark-library.png"));
    }
    check(window->openBook(bookId), "open book");
    waitFor(900);
    window->grab().save(QStringLiteral("build/probe/theme-reader.png"));
    SettingsManager dialogSettings;
    dialogSettings.load();
    SettingsDialog dialog(&dialogSettings, window->findChild<ThemeManager *>(), nullptr);
    dialog.show();
    waitFor(120);
    dialog.grab().save(QStringLiteral("build/probe/settings-dialog.png"));
    if (auto *tabs = dialog.findChild<QTabWidget *>()) {
        if (tabs->count() > 2) {
            tabs->setCurrentIndex(2);
            waitFor(80);
            dialog.grab().save(QStringLiteral("build/probe/settings-dialog-reading.png"));
        }
    }
    const QString originalTheme = dialogSettings.settings().themeKey;
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        const int warmIndex = combo->findData(QStringLiteral("warm"));
        if (warmIndex >= 0) {
            combo->setCurrentIndex(warmIndex);
            break;
        }
    }
    waitFor(80);
    check(qApp->styleSheet().contains(QStringLiteral("#f2e9dc")), "settings theme preview");
    dialog.reject();
    waitFor(80);
    check(dialogSettings.settings().themeKey == originalTheme, "settings cancel restores theme key");
    check(qApp->styleSheet().contains(QStringLiteral("#f4f6f9")), "settings cancel restores appearance");
    if (themes) {
        themes->setCurrentTheme(ThemeId::ModernLight, qApp);
        waitFor(100);
    }
    delete window;
    waitFor(100);
    std::printf("theme probe %s (%d failures)\n", failures == 0 ? "passed" : "failed", failures);
    return failures == 0 ? 0 : 1;
}
