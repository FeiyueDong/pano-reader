#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include "app/MainWindow.h"
#include "settings/SettingsManager.h"

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PANO"));
    QApplication::setApplicationName(QStringLiteral("PANO"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));
    QApplication::setApplicationDisplayName(QStringLiteral("PANO"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("PANO — local-first PDF reading and annotation workspace."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("library"),
                                 QStringLiteral("Library directory to open."),
                                 QStringLiteral("[library]"));
    parser.process(application);

    pano::MainWindow window;
    window.show();

    const QStringList positional = parser.positionalArguments();
    QTimer::singleShot(0, &window, [&window, positional]() {
        if (!positional.isEmpty()) {
            const QString path = QFileInfo(positional.first()).absoluteFilePath();
            if (QFileInfo(path).isDir() && window.openLibraryPath(path, false))
                window.restoreLastSession();
            return;
        }
        auto *settings = window.findChild<pano::SettingsManager *>();
        if (settings && settings->settings().restoreLastLibrary) {
            const QString last = settings->settings().lastLibraryPath;
            if (!last.isEmpty() && QFileInfo(last).isDir())
                window.openLibraryPath(last, false);
            window.restoreLastSession();
        }
    });

    return application.exec();
}
