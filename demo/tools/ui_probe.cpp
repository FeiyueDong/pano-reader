#include "app/MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QPixmap>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QString documentPath;
    QString outputPath = QStringLiteral("build/probe/ui.png");
    const QStringList arguments = QApplication::arguments();
    if (arguments.size() > 1)
        documentPath = arguments.at(1);
    if (arguments.size() > 2)
        outputPath = arguments.at(2);

    pano::MainWindow window;
    window.resize(1440, 900);
    window.show();

    if (documentPath.isEmpty() || !window.openDocument(documentPath)) {
        qWarning() << "openDocument failed:" << documentPath;
        return 2;
    }

    QTimer::singleShot(2200, &application, [&window, outputPath] {
        const QPixmap screenshot = window.grab();
        const bool saved = screenshot.save(outputPath, "PNG");
        qInfo() << "screenshot" << outputPath << saved << screenshot.size();
        QApplication::quit();
    });

    return application.exec();
}
