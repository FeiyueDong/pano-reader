#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QMouseEvent>
#include <QTimer>
#include <QToolButton>

#include <cstdio>

#include "app/MainWindow.h"
#include "library/LibraryManager.h"
#include "reader/PdfView.h"

namespace {
void waitFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPointF &pos,
               Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers mods)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, mods);
    QApplication::sendEvent(widget, &event);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PANO"));

    const QString root = QStringLiteral("build/probe/interaction-runtime");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    const QString libraryPath = root + QStringLiteral("/library");
    qputenv("PANO_CONFIG_DIR", (root + QStringLiteral("/config")).toUtf8());

    pano::LibraryManager manager;
    QString error;
    if (!manager.createLibrary(libraryPath, &error))
        return 1;
    QStringList imported;
    QStringList duplicates;
    if (!manager.importBooks({QFileInfo(QStringLiteral(PANO_SAMPLE_PDF)).absoluteFilePath()},
                             pano::ImportMode::Copy, false, &imported, &duplicates, &error))
        return 1;
    const QString bookId = imported.value(0);
    manager.closeLibrary();

    auto *window = new pano::MainWindow;
    window->resize(1440, 900);
    window->show();
    window->openLibraryPath(libraryPath, false);
    waitFor(400);
    window->openBook(bookId);
    auto *view = window->findChild<pano::PdfView *>();
    window->grab();
    for (int i = 0; i < 80 && view && view->renderer()->cacheBytes() == 0; ++i)
        waitFor(100);
    waitFor(200);
    if (!view)
        return 1;
    QWidget *viewport = view->viewport();
    std::printf("viewport %dx%d\n", viewport->width(), viewport->height());

    bool textFound = false;
    for (int y = 70; y < viewport->height() - 80 && !textFound; y += 18) {
        for (int x = 60; x < viewport->width() - 220 && !textFound; x += 60) {
            sendMouse(viewport, QEvent::MouseButtonPress, QPointF(x, y),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            sendMouse(viewport, QEvent::MouseMove, QPointF(x + 160, y),
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            sendMouse(viewport, QEvent::MouseButtonRelease, QPointF(x + 160, y),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            waitFor(15);
            textFound = view->hasTextSelection();
        }
    }
    std::printf("%s: smart text selection\n", textFound ? "PASS" : "FAIL");
    if (auto *bar = window->findChild<QWidget *>(QStringLiteral("textSelectionToolbar"))) {
        std::printf("text toolbar visible=%d geometry=(%d,%d %dx%d) parent=%s\n",
                    bar->isVisible(), bar->x(), bar->y(), bar->width(), bar->height(),
                    bar->parentWidget() ? bar->parentWidget()->metaObject()->className() : "null");
    }
    window->grab().save(QStringLiteral("build/probe/interaction-text.png"));
    const int beforeTextAction = view->annotations()->annotations().size();
    if (auto *bar = window->findChild<QWidget *>(QStringLiteral("textSelectionToolbar"))) {
        for (QToolButton *button : bar->findChildren<QToolButton *>()) {
            if (button->text() == QStringLiteral("高亮")) {
                button->click();
                break;
            }
        }
    }
    waitFor(80);
    const bool textAction = view->annotations()->annotations().size() == beforeTextAction + 1;
    std::printf("%s: text floating action creates annotation\n", textAction ? "PASS" : "FAIL");

    view->setTool(pano::PdfView::Tool::RegionSelect);
    sendMouse(viewport, QEvent::MouseButtonPress, QPointF(220, 230),
              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(viewport, QEvent::MouseMove, QPointF(470, 390),
              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    sendMouse(viewport, QEvent::MouseButtonRelease, QPointF(470, 390),
              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    waitFor(100);
    const bool regionFound = view->hasRegionSelection();
    std::printf("%s: region selection\n", regionFound ? "PASS" : "FAIL");
    const QImage regionImage = view->regionImage();
    std::printf("%s: region image rendered (%dx%d)\n",
                regionImage.isNull() ? "FAIL" : "PASS", regionImage.width(), regionImage.height());
    window->grab().save(QStringLiteral("build/probe/interaction-region.png"));
    const int beforeRegionAction = view->annotations()->annotations().size();
    if (auto *bar = window->findChild<QWidget *>(QStringLiteral("regionSelectionToolbar"))) {
        for (QToolButton *button : bar->findChildren<QToolButton *>()) {
            if (button->text() == QStringLiteral("矩形")) {
                button->click();
                break;
            }
        }
    }
    waitFor(80);
    const bool regionAction = view->annotations()->annotations().size() == beforeRegionAction + 1;
    std::printf("%s: region floating action creates annotation\n", regionAction ? "PASS" : "FAIL");
    std::printf("screenshots: build/probe/interaction-text.png build/probe/interaction-region.png\n");
    delete window;
    return (textFound && regionFound && textAction && !regionImage.isNull() && regionAction)
               ? 0
               : 1;
}
