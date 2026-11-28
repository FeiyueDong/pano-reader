#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QPdfDocument>
#include <QSizeF>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::printf("usage: %s file.pdf\n", argc ? argv[0] : "pano-pdf-timing");
        return 2;
    }
    const QString path = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    QPdfDocument pdf;
    QElapsedTimer timer;
    timer.start();
    const QPdfDocument::Error error = pdf.load(path);
    const qint64 loadMs = timer.restart();
    if (error != QPdfDocument::Error::None) {
        std::printf("load failed: %d\n", int(error));
        return 1;
    }
    const int pages = pdf.pageCount();
    QSizeF total;
    for (int page = 0; page < pages; ++page)
        total += pdf.pagePointSize(page);
    const qint64 geometryMs = timer.restart();
    const QImage image = pdf.render(0, QSize(960, 1280));
    const qint64 firstRenderMs = timer.elapsed();
    std::printf("file=%s size=%lld pages=%d load_ms=%lld geometry_ms=%lld first_render_ms=%lld image=%dx%d\n",
                qPrintable(path), static_cast<long long>(QFileInfo(path).size()), pages,
                static_cast<long long>(loadMs), static_cast<long long>(geometryMs),
                static_cast<long long>(firstRenderMs), image.width(), image.height());
    return 0;
}
