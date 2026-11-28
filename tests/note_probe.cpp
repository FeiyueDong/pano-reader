#include <QApplication>
#include <QDir>
#include <QDialog>
#include "app/NoteEditDialog.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTabWidget>
#include <cstdio>

#include "annotation/Annotation.h"
#include "annotation/AnnotationCommands.h"
#include "document/DocumentSession.h"
#include "library/LibraryManager.h"
#include "common/LibraryTypes.h"
#include "note/Note.h"
#include "note/NoteTree.h"
#include "reader/PdfView.h"
#include "app/ReaderWorkspace.h"
#include "settings/SettingsManager.h"

namespace {
int failures = 0;
void check(bool ok, const char *name)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++failures;
}
void waitFor(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString sample = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QStringLiteral(PANO_SAMPLE_PDF);
    const QString root = QStringLiteral("/tmp/pano-note-probe");
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    qputenv("PANO_CONFIG_DIR", QDir(root).filePath(QStringLiteral("config")).toLocal8Bit());

    QString error;
    pano::LibraryManager manager;
    check(manager.createLibrary(QDir(root).filePath(QStringLiteral("library")), &error),
          "create library");
    QStringList imported;
    QStringList duplicates;
    check(manager.importBooks({sample}, pano::ImportMode::Copy, false, &imported, &duplicates,
                              &error), "import sample");
    check(imported.size() == 1, "one imported book");
    const QString bookId = imported.value(0);
    const pano::BookInfo book = manager.bookInfo(bookId);

    pano::SettingsManager settings;
    settings.load();
    pano::DocumentSession session(&manager, book, &settings);
    check(session.load(&error), "session load");
    pano::PdfView *view = session.view();
    check(view && view->pageCount() == 3, "pdf pages");
    if (!view)
        return 2;

    pano::Annotation annotation;
    annotation.id = pano::newAnnotationId();
    annotation.pageIndex = 0;
    annotation.type = pano::AnnotationType::Rectangle;
    annotation.rect = QRectF(80, 120, 240, 70);
    view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), annotation));
    check(view->annotations()->find(annotation.id) != nullptr, "annotation added");

    QString groupA;
    QString groupB;
    check(session.createGroup(QString(), QStringLiteral("论文"), &groupA, &error), "create group A");
    check(session.createGroup(groupA, QStringLiteral("阅读"), &groupB, &error), "create group B");

    pano::Note linked;
    linked.id = pano::newNoteId();
    linked.title = QStringLiteral("关联笔记");
    linked.markdown = QStringLiteral("# 关联笔记\n\n公式：$E=mc^2$");
    linked.annotationId = annotation.id;
    QString linkedId;
    check(session.createNote(linked, groupB, &linkedId, &error), "create linked note");
    check(session.hasLinkedNote(annotation.id), "annotation has linked note");
    check(session.noteForAnnotation(annotation.id) != nullptr, "noteForAnnotation");

    pano::Note independent;
    independent.id = pano::newNoteId();
    independent.title = QStringLiteral("独立笔记");
    independent.markdown.clear();
    QString independentId;
    check(session.createNote(independent, groupA, &independentId, &error), "create independent empty note");

    pano::Note duplicate;
    duplicate.id = pano::newNoteId();
    duplicate.title = QStringLiteral("重复关联");
    duplicate.annotationId = annotation.id;
    QString duplicateId;
    check(!session.createNote(duplicate, QString(), &duplicateId, &error),
          "one-to-one link rejected");

    check(session.moveTreeNode(independentId, groupB, &error), "move note into group");
    check(session.moveTreeNodeUp(independentId, &error), "move note up");

    check(session.saveNow(&error), "save schema 2");
    const QString annotationsPath =
        QDir(root).filePath(QStringLiteral("library/") + book.directory
                            + QStringLiteral("/annotations.json"));
    QFile file(annotationsPath);
    check(file.open(QIODevice::ReadOnly), "read annotations.json");
    const QByteArray saved = file.readAll();
    file.close();
    const QJsonObject savedRoot = QJsonDocument::fromJson(saved).object();
    check(savedRoot.value(QStringLiteral("schema_version")).toInt() == 2, "schema version 2");
    check(savedRoot.value(QStringLiteral("notes")).toArray().size() == 2, "two notes persisted");
    check(savedRoot.value(QStringLiteral("note_tree")).toArray().size() >= 1, "tree persisted");

    // delete annotation but keep its note, then undo.
    check(session.deleteAnnotationKeepingNote(annotation.id, &error), "delete annotation keeps note");
    check(session.noteForAnnotation(annotation.id) == nullptr, "link removed");
    check(session.note(linkedId) != nullptr, "note kept");
    view->undoStack()->undo();
    check(session.noteForAnnotation(annotation.id) != nullptr, "undo restores link");
    view->undoStack()->redo();

    // note-point deletion removes its dedicated annotation.
    pano::Annotation point;
    point.id = pano::newAnnotationId();
    point.pageIndex = 0;
    point.type = pano::AnnotationType::NotePoint;
    point.rect = QRectF(400, 400, 0, 0);
    point.p1 = QPointF(400, 400);
    view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), point));
    pano::Note pointNote;
    pointNote.id = pano::newNoteId();
    pointNote.title = QStringLiteral("笔记点");
    pointNote.annotationId = point.id;
    QString pointNoteId;
    check(session.createNote(pointNote, QString(), &pointNoteId, &error), "create note point note");
    check(session.deleteNote(pointNoteId, true, &error), "delete note and point");
    check(view->annotations()->find(point.id) == nullptr, "point annotation removed");
    view->undoStack()->undo();
    check(view->annotations()->find(point.id) != nullptr, "undo restores point");
    view->undoStack()->undo(); // remove point again
    check(session.saveNow(&error), "save after edits");

    session.flush();

    // Old schema must be rejected without being overwritten.
    QFile mutate(annotationsPath);
    check(mutate.open(QIODevice::ReadOnly), "read for old schema");
    QJsonObject oldRoot = QJsonDocument::fromJson(mutate.readAll()).object();
    mutate.close();
    oldRoot.insert(QStringLiteral("schema_version"), 1);
    check(mutate.open(QIODevice::WriteOnly | QIODevice::Truncate), "write old schema");
    mutate.write(QJsonDocument(oldRoot).toJson(QJsonDocument::Indented));
    mutate.close();
    {
        pano::DocumentSession reopened(&manager, manager.bookInfo(bookId), &settings);
        QString loadError;
        check(!reopened.load(&loadError), "old schema rejected");
        check(loadError.contains(QStringLiteral("schema_version")), "schema error names version");
    }
    QFile restore(annotationsPath);
    check(restore.open(QIODevice::WriteOnly | QIODevice::Truncate), "restore schema 2");
    restore.write(saved);
    restore.close();

    // Reopen and verify the tree/notes round-trip.
    {
        pano::DocumentSession reopened(&manager, manager.bookInfo(bookId), &settings);
        QString loadError;
        check(reopened.load(&loadError), "reopen schema 2");
        check(reopened.notes()->size() == 2, "reopen notes count");
        check(reopened.noteTree().nodes().size() == 4, "reopen tree nodes");
        check(reopened.noteForAnnotation(annotation.id) != nullptr, "reopen link");
    }

    // Visual evidence for the tree and editor.
    auto *workspace = session.workspace();
    workspace->resize(1440, 900);
    workspace->show();
    waitFor(400);
    for (QTabWidget *tabs : workspace->findChildren<QTabWidget *>()) {
        if (tabs->count() == 3) {
            tabs->setCurrentIndex(1);
            break;
        }
    }
    if (auto *tree = workspace->findChild<QTreeWidget *>()) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            if ((*it)->text(0).contains(QStringLiteral("关联笔记"))) {
                tree->setCurrentItem(*it);
                break;
            }
            ++it;
        }
    }
    workspace->refreshReadingData();
    waitFor(200);
    workspace->grab().save(QStringLiteral("build/probe/phase3-note-tree.png"));
    workspace->view()->selectAnnotation(QString());
    workspace->beginNoteEdit();
    auto *noteDialog = workspace->findChild<pano::NoteEditDialog *>();
    check(noteDialog && noteDialog->isVisible(), "existing note editor opened");
    check(noteDialog && noteDialog->currentNoteId() == linkedId, "editor targets selected note");
    if (noteDialog)
        noteDialog->reject();
    waitFor(100);
    workspace->beginNoteEdit();
    waitFor(200);
    if (QWidget *dialog = workspace->findChild<QDialog *>()) {
        dialog->grab().save(QStringLiteral("build/probe/phase3-note-editor.png"));
    }
    workspace->close();
    waitFor(100);
    session.flush();
    manager.closeLibrary();

    std::printf("note probe %s (%d failures)\n", failures == 0 ? "passed" : "failed", failures);
    return failures == 0 ? 0 : 1;
}
