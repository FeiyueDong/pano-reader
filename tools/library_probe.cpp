#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "library/LibraryManager.h"
#include "library/LibraryTypes.h"
#include "storage/LibraryStore.h"

using namespace pano;

static int failures = 0;

static void check(bool condition, const QString &message)
{
    QTextStream out(stdout);
    out << (condition ? "PASS: " : "FAIL: ") << message << '\n';
    if (!condition)
        ++failures;
}

static QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    const QString root = QDir::current().filePath(QStringLiteral("build/probe/library-probe"));
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    qputenv("PANO_CONFIG_DIR", QDir(root).filePath(QStringLiteral("config")).toUtf8());

    const QString sample = QStringLiteral(PANO_SAMPLE_PDF);
    check(QFileInfo::exists(sample), QStringLiteral("sample PDF exists"));

    LibraryManager manager;
    QString error;
    check(manager.createLibrary(QDir(root).filePath(QStringLiteral("library")), &error),
          QStringLiteral("create library: %1").arg(error));
    const QString libraryRoot = manager.rootPath();

    QStringList imported;
    QStringList duplicates;
    check(manager.importBooks({sample}, ImportMode::Copy, false, &imported, &duplicates, &error),
          QStringLiteral("import copy: %1").arg(error));
    check(imported.size() == 1, QStringLiteral("one imported book"));
    const QString bookId = imported.value(0);
    BookInfo book = manager.bookInfo(bookId);
    check(!book.title.isEmpty(), QStringLiteral("book title filled"));
    check(book.mode == ImportMode::Copy, QStringLiteral("copy mode stored"));
    check(book.sourceAvailable, QStringLiteral("copy source available"));
    check(QFileInfo::exists(book.coverPath), QStringLiteral("cover generated"));
    check(QFileInfo(book.coverPath).size() > 1024, QStringLiteral("cover has content"));
    check(book.sha256.size() == 64, QStringLiteral("sha256 recorded"));
    check(manager.verifySource(bookId, &error), QStringLiteral("source verifies: %1").arg(error));

    check(manager.findBookBySha256(book.sha256) == bookId, QStringLiteral("find by sha256"));
    duplicates.clear();
    imported.clear();
    check(manager.importBooks({sample}, ImportMode::Copy, false, &imported, &duplicates, &error),
          QStringLiteral("duplicate import returns success: %1").arg(error));
    check(imported.isEmpty() && duplicates.size() == 1, QStringLiteral("duplicate reported, no import"));

    // Shelf hierarchy: 论文 -> 阅读 -> 本周.
    QString shelfA;
    QString shelfB;
    QString shelfC;
    check(manager.createShelf(QStringLiteral("论文"), QString(), &error, &shelfA),
          QStringLiteral("create root shelf: %1").arg(error));
    check(manager.createShelf(QStringLiteral("阅读"), shelfA, &error, &shelfB),
          QStringLiteral("create child shelf: %1").arg(error));
    check(manager.createShelf(QStringLiteral("本周"), shelfB, &error, &shelfC),
          QStringLiteral("create grandchild shelf: %1").arg(error));
    check(manager.addBookToShelf(shelfC, bookId, &error),
          QStringLiteral("add book to grandchild shelf: %1").arg(error));
    check(manager.data().shelf(shelfC) && manager.data().shelf(shelfC)->bookIds.contains(bookId),
          QStringLiteral("shelf membership stored"));
    check(manager.moveShelf(shelfC, QString(), &error),
          QStringLiteral("move shelf to root: %1").arg(error));
    check(manager.data().shelf(shelfC) && manager.data().shelf(shelfC)->parentId.isEmpty(),
          QStringLiteral("shelf parent cleared after move"));
    check(manager.moveShelf(shelfC, shelfB, &error),
          QStringLiteral("move shelf under parent: %1").arg(error));
    check(!manager.moveShelf(shelfA, shelfC, &error),
          QStringLiteral("cycle move rejected"));
    check(manager.deleteShelf(shelfB, &error), QStringLiteral("delete middle shelf: %1").arg(error));
    check(manager.data().shelf(shelfC) && manager.data().shelf(shelfC)->parentId == shelfA,
          QStringLiteral("grandchild promoted to deleted shelf parent"));

    // Trash, restore, permanent delete.
    check(manager.setFavorite(bookId, true, &error), QStringLiteral("favorite: %1").arg(error));
    check(manager.bookInfo(bookId).favorite, QStringLiteral("favorite visible"));
    check(manager.moveToTrash(bookId, &error), QStringLiteral("trash: %1").arg(error));
    check(manager.books(false).isEmpty() && manager.books(true).size() == 1,
          QStringLiteral("trashed book hidden from normal list"));
    check(manager.restoreBook(bookId, &error), QStringLiteral("restore: %1").arg(error));
    check(manager.books(false).size() == 1, QStringLiteral("restored book visible"));

    // External modification is detected and must not be overwritten.
    const QString libraryFile = QDir(libraryRoot).filePath(QStringLiteral("library.json"));
    const QByteArray before = readAll(libraryFile);
    QJsonObject changed = QJsonDocument::fromJson(before).object();
    changed.insert(QStringLiteral("test_external_marker"), true);
    QFile tamper(libraryFile);
    check(tamper.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("open library.json for tamper"));
    tamper.write(QJsonDocument(changed).toJson(QJsonDocument::Indented));
    tamper.close();
    check(!manager.setFavorite(bookId, false, &error), QStringLiteral("external change rejected"));
    check(readAll(libraryFile) == QJsonDocument(changed).toJson(QJsonDocument::Indented),
          QStringLiteral("external file left untouched after rejected write"));
    QFile restoreFile(libraryFile);
    check(restoreFile.open(QIODevice::WriteOnly | QIODevice::Truncate), QStringLiteral("restore library.json"));
    restoreFile.write(before);
    restoreFile.close();
    // A second store must not acquire the lock.
    LibraryStore second;
    LibraryData lockData;
    QString lockError;
    check(!second.open(libraryRoot, &lockData, &lockError), QStringLiteral("second store lock rejected"));
    check(!lockError.isEmpty(), QStringLiteral("lock error has text"));

    // Reopen after the external-change drill; the store deliberately requires a
    // fresh open before it will write again.
    manager.closeLibrary();
    check(manager.openLibrary(libraryRoot, &error),
          QStringLiteral("reopen library after external-change drill: %1").arg(error));

    // Reference mode: missing source keeps metadata, and a different hash is rejected.
    const QString referenceSource = QDir(root).filePath(QStringLiteral("reference.pdf"));
    check(QFile::copy(sample, referenceSource), QStringLiteral("copy reference source"));
    QStringList referenceImported;
    check(manager.importBooks({referenceSource}, ImportMode::Reference, true,
                              &referenceImported, nullptr, &error),
          QStringLiteral("import reference: %1").arg(error));
    const QString referenceId = referenceImported.value(0);
    BookInfo reference = manager.bookInfo(referenceId);
    check(reference.mode == ImportMode::Reference && reference.sourceAvailable,
          QStringLiteral("reference source available"));
    check(QFile::remove(referenceSource), QStringLiteral("remove reference source"));
    reference = manager.bookInfo(referenceId);
    check(!reference.sourceAvailable, QStringLiteral("missing reference reported"));
    check(!reference.title.isEmpty(), QStringLiteral("metadata retained for missing source"));
    const QString altered = QDir(root).filePath(QStringLiteral("altered.pdf"));
    QFile alteredFile(altered);
    alteredFile.open(QIODevice::WriteOnly);
    alteredFile.write("not the same pdf");
    alteredFile.close();
    check(!manager.setSourcePath(referenceId, altered, &error), QStringLiteral("different hash source rejected"));

    QString deleteError;
    check(!manager.deletePermanently(bookId, &deleteError),
          QStringLiteral("permanent delete rejects non-trash book"));
    check(manager.moveToTrash(bookId, &deleteError), QStringLiteral("trash before permanent delete: %1").arg(deleteError));
    check(manager.data().book(bookId) && !manager.data().book(bookId)->deletedAt.isEmpty(),
          QStringLiteral("book marked trashed in memory"));
    check(manager.deletePermanently(bookId, &deleteError),
          QStringLiteral("permanent delete: %1").arg(deleteError));
    check(manager.bookInfo(bookId).id.isEmpty(), QStringLiteral("permanent delete removes record"));

    out << (failures == 0 ? "library probe passed\n" : QStringLiteral("library probe failed: %1\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
