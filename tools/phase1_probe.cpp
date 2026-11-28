#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTimer>

#include <cstdio>

#include "annotation/AnnotationCommands.h"
#include "app/MainWindow.h"
#include "document/DocumentSession.h"
#include "reader/PdfView.h"
#include "library/LibraryManager.h"
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

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

    const QString root = QStringLiteral("build/probe/phase1-runtime");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    const QString libraryPath = root + QStringLiteral("/library");
    qputenv("PANO_CONFIG_DIR", (root + QStringLiteral("/config")).toUtf8());

    pano::LibraryManager manager;
    QString error;
    check(manager.createLibrary(libraryPath, &error), "create library");
    if (!error.isEmpty())
        std::printf("  error: %s\n", qPrintable(error));
    check(QFileInfo::exists(libraryPath + QStringLiteral("/library.json")), "library.json exists");

    QStringList imported;
    QStringList duplicates;
    const bool importedOk = manager.importBooks({samplePdf()}, pano::ImportMode::Copy, false,
                                                &imported, &duplicates, &error);
    check(importedOk && imported.size() == 1, "import sample PDF");
    if (!error.isEmpty())
        std::printf("  error: %s\n", qPrintable(error));
    const QString bookId = imported.value(0);
    check(!bookId.isEmpty(), "imported book id");
    const pano::BookInfo book = manager.bookInfo(bookId);
    check(book.sourceAvailable, "source available");
    check(QFileInfo::exists(book.coverPath), "cover generated");
    check(QFileInfo::exists(book.pdfPath), "managed source exists");

    duplicates.clear();
    imported.clear();
    manager.importBooks({samplePdf()}, pano::ImportMode::Copy, false, &imported, &duplicates, &error);
    check(duplicates.size() == 1 && imported.isEmpty(), "duplicate detected");

    QString shelfError;
    QString shelfId;
    check(manager.createShelf(QStringLiteral("论文"), QString(), &shelfError, &shelfId),
          "create shelf");
    check(manager.addBookToShelf(shelfId, bookId, &shelfError), "book added to shelf");
    check(manager.setFavorite(bookId, true, &shelfError), "favorite set");
    manager.closeLibrary();

    auto *window = new pano::MainWindow;
    window->resize(1440, 900);
    window->show();
    check(window->openLibraryPath(libraryPath, false), "MainWindow open library");
    waitFor(500);
    window->grab().save(QStringLiteral("build/probe/phase1-library.png"));
    check(window->openBook(bookId), "MainWindow open book");
    // In an offscreen probe the first paint happens inside grab(), so trigger
    // one discarded paint to start the asynchronous page render, then wait for
    // the renderer to put the page into its cache before taking evidence.
    auto *view = window->findChild<pano::PdfView *>();
    window->grab(); // trigger the first paint so the async render starts
    for (int i = 0; i < 80 && view && view->renderer()->cacheBytes() == 0; ++i)
        waitFor(100);
    waitFor(150);
    check(view != nullptr && view->hasDocument(), "PDF view loaded");
    check(view && view->pageCount() == 3, "page count 3");

    if (view) {
        auto *session = window->findChild<pano::DocumentSession *>();
        pano::Annotation annotation;
        annotation.id = pano::newAnnotationId();
        annotation.pageIndex = 0;
        annotation.type = pano::AnnotationType::Rectangle;
        annotation.style.strokeColor = QColor(QStringLiteral("#d94f4f"));
        annotation.style.strokeWidthPt = 2.0;
        annotation.style.opacity = 0.8;
        annotation.rect = QRectF(80, 120, 300, 90);
        view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), annotation));
        view->selectAnnotation(annotation.id);
        pano::Note note;
        note.id = pano::newNoteId();
        note.title = QStringLiteral("测试笔记");
        note.markdown = QStringLiteral("## 测试笔记\n\n这是 Phase 1 UI 探针写入的笔记。\n\n$x^2+y^2=1$");
        note.annotationId = annotation.id;
        QString noteError;
        QString noteId;
        check(session && session->createNote(note, QString(), &noteId, &noteError),
              "note created");
        view->goToPage(1);
        waitFor(300);
        check(view->currentPage() == 1, "precise page navigation");
        view->goToPage(0);
        check(view->annotations()->annotations().size() == 1, "annotation added");
        QMetaObject::invokeMethod(window, "saveCurrent", Qt::DirectConnection);
        waitFor(250);
        window->grab().save(QStringLiteral("build/probe/phase1-ui.png"));
        // Exercise reading-state persistence: page, zoom and rotation are saved
        // separately from annotations and restored with the session.
        view->goToPage(1);
        view->setZoom(1.5);
        view->setRotation(90);
        QMetaObject::invokeMethod(window, "saveCurrent", Qt::DirectConnection);
        waitFor(300);

        // External modification protection: touch annotations.json after the
        // session loaded it, try to save, and verify the file is not overwritten.
        if (session) {
            const QString annotationsPath =
                QDir(libraryPath).filePath(book.directory + QStringLiteral("/annotations.json"));
            QFile file(annotationsPath);
            QByteArray original;
            if (file.open(QIODevice::ReadOnly)) {
                original = file.readAll();
                file.close();
            }
            if (!original.isEmpty() && file.open(QIODevice::Append)) {
                file.write("\n");
                file.close();
                pano::Note changed = note;
                changed.id = noteId;
                changed.markdown += QStringLiteral("\n\n外部修改测试");
                QString updateError;
                check(session->updateNote(changed, &updateError), "note updated");
                // The application normally shows a modal warning for this
                // failure; the probe disconnects it to stay non-interactive.
                QObject::disconnect(session, &pano::DocumentSession::saveFailed,
                                    nullptr, nullptr);
                QString saveError;
                const bool blocked = !session->saveNow(&saveError);
                check(blocked && saveError.contains(QStringLiteral("外部修改")),
                      "external annotation modification blocked");
                if (QFile restore(annotationsPath); restore.open(QIODevice::WriteOnly)) {
                    restore.write(original);
                    restore.close();
                }
                QString retryError;
                check(session->saveNow(&retryError), "save resumes after restore");
            }
        }
    } else {
        window->grab().save(QStringLiteral("build/probe/phase1-ui.png"));
    }

    std::printf("screenshot: build/probe/phase1-ui.png\n");
    delete window;
    waitFor(200);

    pano::LibraryManager verify;
    check(verify.openLibrary(libraryPath, &error), "reopen library");
    if (!error.isEmpty())
        std::printf("  error: %s\n", qPrintable(error));
    const pano::BookInfo verifiedBook = verify.bookInfo(bookId);
    check(verifiedBook.annotationCount == 1, "annotation persisted");
    check(verifiedBook.noteCount == 1, "note persisted");
    check(verifiedBook.favorite, "favorite persisted");

    check(verify.books(false).size() == 1, "verify one visible book");
    const pano::ReadingState state = verify.readingState(bookId);
    check(state.valid && state.pageIndex == 1, "reading page persisted");
    check(state.valid && std::abs(state.zoom - 1.5) < 0.001, "reading zoom persisted");
    check(state.valid && state.rotation == 90, "reading rotation persisted");

    std::printf("phase1 probe %s (%d failures)\n", failures == 0 ? "passed" : "failed", failures);
    return failures == 0 ? 0 : 1;
}
