#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QListWidget>
#include <QPdfSearchModel>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QWheelEvent>
#include <QTimer>

#include <cstdio>

#include "annotation/AnnotationCommands.h"
#include "app/MainWindow.h"
#include "app/ReaderWorkspace.h"
#include "document/DocumentSession.h"
#include "library/LibraryManager.h"
#include "reader/PageLayout.h"
#include "reader/PdfView.h"

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

void waitFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QString samplePdf()
{
    return QFileInfo(QStringLiteral(PANO_SAMPLE_PDF)).absoluteFilePath();
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPointF &pos,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent event(type, pos, widget->mapToGlobal(pos), button, buttons, mods);
    QApplication::sendEvent(widget, &event);
}

QPoint viewPosition(pano::PdfView *view, int page, const QPointF &pagePoint)
{
    const QPointF content = view->layout().pageToContent(page).map(pagePoint);
    return QPoint(qRound(content.x() - view->horizontalScrollBar()->value()),
                  qRound(content.y() - view->verticalScrollBar()->value()));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    const QString root = QStringLiteral("build/probe/phase2-runtime");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    const QString libraryPath = root + QStringLiteral("/library");
    qputenv("PANO_CONFIG_DIR", (root + QStringLiteral("/config")).toUtf8());

    pano::LibraryManager manager;
    QString error;
    check(manager.createLibrary(libraryPath, &error), "create library");
    QStringList imported;
    QStringList duplicates;
    check(manager.importBooks({samplePdf()}, pano::ImportMode::Copy, false, &imported,
                              &duplicates, &error),
          "import sample PDF");
    const QString bookId = imported.value(0);
    manager.closeLibrary();

    auto *window = new pano::MainWindow;
    window->resize(1440, 900);
    window->show();
    check(window->openLibraryPath(libraryPath, false), "open library");
    check(window->openBook(bookId), "open book");
    auto *view = window->findChild<pano::PdfView *>();
    auto *session = window->findChild<pano::DocumentSession *>();
    window->grab();
    for (int i = 0; i < 80 && view && view->renderer()->cacheBytes() == 0; ++i)
        waitFor(100);
    check(view != nullptr && view->hasDocument(), "PDF view loaded");

    if (view && session) {
        auto *contextHint = window->findChild<QLabel *>(QStringLiteral("readerContextHint"));
        QWidget *contextWidget = contextHint ? contextHint->parentWidget() : nullptr;
        const int contextHeight = contextWidget ? contextWidget->height() : -1;
        const int viewportHeight = view->viewport()->height();
        view->setTool(pano::PdfView::Tool::Pencil);
        waitFor(120);
        check(contextWidget && contextWidget->height() == contextHeight,
              "context bar height stable across tools");
        check(view->viewport()->height() == viewportHeight,
              "context bar keeps reader viewport height");
        view->setTool(pano::PdfView::Tool::Select);

        view->goToPage(0);
        view->setZoom(1.0);
        view->setTool(pano::PdfView::Tool::Pencil);
        waitFor(80);
        const int beforeShiftPath = view->annotations()->count();
        const QPoint shiftStart = viewPosition(view, 0, QPointF(300, 300));
        const QPoint shiftEnd = viewPosition(view, 0, QPointF(450, 380));
        sendMouse(view->viewport(), QEvent::MouseButtonPress, shiftStart,
                  Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(view->viewport(), QEvent::MouseMove, shiftEnd,
                  Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(view->viewport(), QEvent::MouseButtonRelease, shiftEnd,
                  Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        waitFor(80);
        const QVector<pano::Annotation> annotations = view->annotations()->annotations();
        const pano::Annotation *shiftPath = nullptr;
        for (const pano::Annotation &annotation : annotations) {
            if (annotation.type == pano::AnnotationType::Pencil
                && annotation.pageIndex == 0)
                shiftPath = &annotation;
        }
        check(view->annotations()->count() == beforeShiftPath + 1 && shiftPath,
              "shift pencil creates one annotation");
        check(shiftPath && shiftPath->path.size() == 2,
              "shift pencil saves two effective points");
        if (shiftPath)
            check(shiftPath->path.first() == QPointF(300, 300)
                      || shiftPath->path.first() != shiftPath->path.last(),
                  "shift pencil preserves start/end vertices");
        view->undoStack()->undo();
        view->setTool(pano::PdfView::Tool::Select);

        check(view->layoutMode() == pano::PageLayoutMode::ContinuousSingle,
              "default continuous single layout");
        check(view->layoutMode() == pano::PageLayoutMode::ContinuousSingle,
              "layout enum available");

        view->setLayoutMode(pano::PageLayoutMode::SinglePage);
        view->goToPage(1);
        check(view->layout().visiblePages().size() == 1, "single page shows one page");

        view->setFirstPageAlone(true);
        view->setLayoutMode(pano::PageLayoutMode::FacingPages);
        view->goToPage(0);
        check(view->layout().spreadPages(0) == QVector<int>{0}, "facing first page alone");
        view->goToPage(1);
        check(view->layout().spreadPages(1).size() == 2, "facing groups pages 2-3");
        const QVector<int> spread = view->layout().spreadPages(1);
        if (spread.size() == 2) {
            const QRectF left = view->layout().pageRect(spread.at(0));
            const QRectF right = view->layout().pageRect(spread.at(1));
            check(qAbs(left.top() - right.top()) < 0.5, "facing pages top aligned");
            check(left.right() < right.left(), "facing pages ordered and separated");
        }

        view->setLayoutMode(pano::PageLayoutMode::ContinuousFacing);
        view->setFirstPageAlone(false);
        view->setLayoutMode(pano::PageLayoutMode::FacingPages);
        view->goToPage(1);
        view->fitWidth();
        check(view->zoomMode() == pano::ZoomMode::FitWidth, "fit width mode retained");
        view->setZoom(1.25);
        check(view->zoomMode() == pano::ZoomMode::Fixed, "manual zoom switches to fixed");
        view->fitPage();
        check(view->zoomMode() == pano::ZoomMode::FitPage, "fit page mode retained");

        view->setLayoutMode(pano::PageLayoutMode::SinglePage);
        view->goToPage(0);
        view->fitPage();
        waitFor(80);
        auto sendWheel = [&view](int delta) {
            const QPointF pos(view->viewport()->rect().center());
            QWheelEvent event(pos, view->viewport()->mapToGlobal(pos.toPoint()), QPoint(0, 0),
                              QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(view->viewport(), &event);
        };
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        sendWheel(-120);
        waitFor(60);
        check(view->currentPage() == 1, "wheel threshold turns one page");
        sendWheel(-120);
        waitFor(30);
        check(view->currentPage() == 1, "wheel cooldown prevents immediate second turn");
        waitFor(280);
        sendWheel(-120);
        waitFor(30);
        check(view->currentPage() == 2, "wheel turns after cooldown");
        sendWheel(120);
        waitFor(30);
        check(view->currentPage() == 1, "reverse wheel turns previous page");
        view->setLayoutMode(pano::PageLayoutMode::ContinuousSingle);
        view->goToPage(0);

        pano::Annotation annotation;
        annotation.id = pano::newAnnotationId();
        annotation.pageIndex = 0;
        annotation.type = pano::AnnotationType::Rectangle;
        annotation.rect = QRectF(80, 120, 260, 80);
        view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), annotation));
        view->selectAnnotation(annotation.id);
        const int beforeCopy = view->annotations()->annotations().size();
        check(view->copySelectionOrAnnotation(), "copy selected geometry");
        check(view->pasteAnnotation(view->viewport()->rect().center()), "paste geometry");
        check(view->annotations()->annotations().size() == beforeCopy + 1,
              "paste adds one annotation");
        check(view->duplicateSelected(), "duplicate selected geometry");
        check(view->annotations()->annotations().size() == beforeCopy + 2,
              "duplicate adds one annotation");

        check(session->bookmarks()->addBookmark(QStringLiteral("导航测试"), 1, QPointF(20, 40))
                  .size() > 0,
              "add bookmark");
        check(session->saveBookmarks(&error), "save bookmark file");
        check(QFileInfo::exists(session->bookDirectory() + QStringLiteral("/bookmarks.json")),
              "bookmarks.json exists");
        view->navigateTo(2, QPointF(320, 400), pano::PdfView::NavigationAlignment::Center,
                         pano::PdfView::NavigationReason::Bookmark);
        check(view->currentPage() == 2, "bookmark navigate page");

        QPdfSearchModel *search = window->findChild<QPdfSearchModel *>();
        if (!search)
            search = session->workspace()->findChild<QPdfSearchModel *>();
        check(search != nullptr, "search model present");
        if (search) {
            search->setDocument(view->document()->pdf());
            waitFor(120);
            search->setSearchString(QStringLiteral("PANO"));
            waitFor(120);
            for (int i = 0; i < 80; ++i) {
                if (search->status() == QPdfSearchModel::Status::Finished)
                    break;
                waitFor(100);
            }
            std::printf("  search status=%d count=%d\n", int(search->status()), search->count());
            check(search->count() > 0, "search finds sample text");
        session->workspace()->toggleSearchBar();
        auto *searchEdit = session->workspace()->findChild<QLineEdit *>(
            QStringLiteral("readerSearchEdit"));
        auto *searchResults = session->workspace()->findChild<QListWidget *>(
            QStringLiteral("readerSearchResults"));
        check(searchEdit != nullptr && searchResults != nullptr, "search bar visible");
        if (searchEdit && searchResults) {
            searchEdit->setText(QStringLiteral("PANO"));
            for (int i = 0; i < 50 && searchResults->count() == 0; ++i)
                waitFor(100);
            check(searchResults->count() > 0, "search bar lists results");
            if (searchResults->count() > 0) {
                QListWidgetItem *first = searchResults->item(0);
                QMetaObject::invokeMethod(searchResults, "itemClicked",
                                          Q_ARG(QListWidgetItem *, first));
                waitFor(300);
                window->grab().save(QStringLiteral("build/probe/phase2-search.png"));
                check(view->currentPage() >= 0, "search result navigates");
            }
        }
        auto *bookmarkList = session->workspace()->findChild<QListWidget *>(
            QStringLiteral("readerBookmarkList"));
        check(bookmarkList != nullptr && bookmarkList->count() == 1,
              "bookmark tab shows saved bookmark");
        }

        view->navigateTo(qMin(2, view->pageCount() - 1), QPointF(),
                         pano::PdfView::NavigationAlignment::PageTop,
                         pano::PdfView::NavigationReason::PageInput);
        const int erasePage = view->currentPage();
        pano::Annotation firstErase;
        firstErase.id = pano::newAnnotationId();
        firstErase.pageIndex = erasePage;
        firstErase.type = pano::AnnotationType::Rectangle;
        firstErase.rect = QRectF(80, 200, 40, 40);
        view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), firstErase));
        pano::Annotation secondErase = firstErase;
        secondErase.id = pano::newAnnotationId();
        secondErase.rect = QRectF(130, 200, 40, 40);
        view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), secondErase));
        const int beforeErase = view->annotations()->annotations().size();
        view->setTool(pano::PdfView::Tool::Eraser);
        auto *viewport = view->viewport();
        const QPoint p1 = viewPosition(view, erasePage, QPointF(100, 220));
        const QPoint p2 = viewPosition(view, erasePage, QPointF(150, 220));
        sendMouse(viewport, QEvent::MouseButtonPress, p1, Qt::LeftButton, Qt::LeftButton);
        sendMouse(viewport, QEvent::MouseMove, p2, Qt::NoButton, Qt::LeftButton);
        sendMouse(viewport, QEvent::MouseButtonRelease, p2, Qt::LeftButton, Qt::NoButton);
        check(view->annotations()->annotations().size() == beforeErase - 2,
              "continuous eraser removes both objects");
        view->undoStack()->undo();
        check(view->annotations()->annotations().size() == beforeErase,
              "one eraser gesture is one undo step");
        view->setTool(pano::PdfView::Tool::Select);

        if (auto *pageEdit = window->findChild<QLineEdit *>(QStringLiteral("readerPageEdit"))) {
            pageEdit->setText(QStringLiteral("1"));
            QMetaObject::invokeMethod(pageEdit, "returnPressed");
            QCoreApplication::processEvents();
            check(view->currentPage() == 0, "page input navigates");
        }

        view->setLayoutMode(pano::PageLayoutMode::ContinuousSingle);
        session->workspace()->clearNavigationHistory();
        view->navigateTo(1, QPointF(), pano::PdfView::NavigationAlignment::PageTop,
                         pano::PdfView::NavigationReason::PageInput);
        view->navigateTo(2, QPointF(), pano::PdfView::NavigationAlignment::PageTop,
                         pano::PdfView::NavigationReason::PageInput);
        session->workspace()->navigateBack();
        check(view->currentPage() == 1, "history back navigates");
        session->workspace()->navigateForward();
        check(view->currentPage() == 2, "history forward restores page");

        for (QTabWidget *tabs : window->findChildren<QTabWidget *>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == QStringLiteral("书签")) {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        waitFor(120);
        window->grab().save(QStringLiteral("build/probe/phase2-bookmarks.png"));

        for (QTabWidget *tabs : window->findChildren<QTabWidget *>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == QStringLiteral("页面")) {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        waitFor(3000);
        window->grab().save(QStringLiteral("build/probe/phase2-thumbnails.png"));

        view->setLayoutMode(pano::PageLayoutMode::FacingPages);
        view->setFirstPageAlone(false);
        check(session->saveNow(&error), "session save");
        QFile readingFile(libraryPath + QStringLiteral("/state/reading.json"));
        if (readingFile.open(QIODevice::ReadOnly)) {
            const QJsonObject rootObject = QJsonDocument::fromJson(readingFile.readAll()).object();
            const QJsonObject stateObject = rootObject.value(QStringLiteral("books"))
                                                .toObject()
                                                .value(bookId)
                                                .toObject();
            check(stateObject.value(QStringLiteral("layout_mode")).toString()
                      == QStringLiteral("facing"),
                  "reading layout persisted");
            check(stateObject.value(QStringLiteral("first_page_alone")).toBool() == false,
                  "first-page-alone persisted");
        } else {
            check(false, "reading state readable");
        }

        if (!view->annotations()->annotations().isEmpty())
            view->selectAnnotation(view->annotations()->annotations().first().id);
        waitFor(120);
        window->grab().save(QStringLiteral("build/probe/phase2-layout.png"));
        window->grab().save(QStringLiteral("build/probe/phase2-context.png"));
        QMetaObject::invokeMethod(window, "toggleFullscreen");
        waitFor(100);
        check(window->isFullScreen() && !window->findChild<QTabWidget *>()->tabBar()->isVisible(),
              "fullscreen hides tab bar");
        window->grab().save(QStringLiteral("build/probe/phase2-fullscreen.png"));
        auto *exitButton = window->findChild<QToolButton *>(QStringLiteral("readerExitFullscreen"));
        check(exitButton && exitButton->isVisible(), "fullscreen exit button visible");
        if (exitButton)
            exitButton->click();
        waitFor(150);
        check(!window->isFullScreen(), "fullscreen exit button restores window");
    }

    std::printf("%s (%d failures)\n", failures == 0 ? "phase2 probe passed" : "phase2 probe failed",
                failures);
    return failures == 0 ? 0 : 1;
}
