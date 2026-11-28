#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>

#include "app/MainWindow.h"
#include "library/LibraryManager.h"
#include "storage/LibraryStore.h"
#include "reader/PdfView.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc < 2) {
        qWarning() << "usage: pano-open-timing <large.pdf>";
        return 2;
    }
    const QString pdf = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    if (!QFileInfo::exists(pdf)) {
        qWarning() << "missing" << pdf;
        return 2;
    }
    const QString root = QStringLiteral("/tmp/pano-open-timing");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    qputenv("PANO_CONFIG_DIR", root.toUtf8() + "/config");
    qputenv("PANO_OPEN_TIMING", "1");

    pano::LibraryStore store;
    pano::LibraryManager manager(&store);
    QString error;
    if (!manager.createLibrary(root + QStringLiteral("/library"), &error)) {
        qWarning() << "create library" << error;
        return 3;
    }
    QStringList imported;
    QStringList duplicates;
    QElapsedTimer importTimer;
    importTimer.start();
    if (!manager.importBooks({pdf}, pano::ImportMode::Reference, true, &imported,
                             &duplicates, &error)) {
        qWarning() << "import" << error;
        return 3;
    }
    qInfo().noquote() << QStringLiteral("[PANO_TIMING] import reference ms=%1 id=%2")
                             .arg(importTimer.elapsed()).arg(imported.value(0));
    const QString bookId = imported.value(0);
    manager.closeLibrary();

    pano::MainWindow window;
    window.resize(1280, 900);
    window.show();
    QElapsedTimer openTimer;
    openTimer.start();
    if (!window.openLibraryPath(root + QStringLiteral("/library"), false)
        || !window.openBook(bookId)) {
        qWarning() << "open book failed";
        return 4;
    }
    auto *view = window.findChild<pano::PdfView *>();
    for (int i = 0; i < 100 && view && view->renderer()->cacheBytes() == 0; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    qInfo().noquote() << QStringLiteral("[PANO_TIMING] open+first-cache ms=%1 pages=%2")
                             .arg(openTimer.elapsed()).arg(view ? view->pageCount() : -1);
    return 0;
}
