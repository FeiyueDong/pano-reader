#include <QApplication>
#include <QDir>
#include <QDialog>
#include "app/NoteEditDialog.h"
#include "note/MarkdownRenderer.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QComboBox>
#include <QBuffer>
#include <QClipboard>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QImage>
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
    annotation.text = QStringLiteral("摘录内容 $x$");
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

    // Bidirectional link API: one note, one annotation, explicit unlink.
    pano::Annotation second;
    second.id = pano::newAnnotationId();
    second.pageIndex = 0;
    second.type = pano::AnnotationType::Ellipse;
    second.rect = QRectF(500, 200, 80, 60);
    view->undoStack()->push(new pano::AddAnnotationCommand(view->annotations(), second));
    check(session.linkNoteToAnnotation(independentId, second.id, &error),
          "link independent note to annotation");
    check(session.noteForAnnotation(second.id) != nullptr, "reverse link available");
    check(!session.linkNoteToAnnotation(independentId, annotation.id, &error),
          "linked note cannot be linked twice");
    check(session.unlinkNote(independentId, &error), "unlink note");
    check(session.noteForAnnotation(second.id) == nullptr, "unlink clears reverse link");
    check(session.deleteAnnotationAndNote(second.id, &error), "delete temporary annotation");

    // R05/R12: depth checks cover the whole moved subtree, not just one node;
    // sibling groups remain addressable by ID even with identical names.
    {
        pano::NoteTree tree;
        QString parent;
        bool built = true;
        for (int depth = 0; depth < 31; ++depth) {
            QString child;
            if (!tree.addGroup(parent, QStringLiteral("层级%1").arg(depth + 1), &child, &error)) {
                built = false;
                break;
            }
            parent = child;
        }
        check(built, "tree builds 31 nested groups");
        QString topLevel;
        for (const pano::NoteTreeNode &node : tree.nodes()) {
            if (node.type == pano::NoteNodeType::Group && node.parentId.isEmpty()) {
                topLevel = node.id;
                break;
            }
        }
        check(!topLevel.isEmpty(), "nested tree has a root group");
        check(!tree.moveNode(topLevel, parent, &error),
              "move rejects subtree exceeding depth");
        QString sameA;
        QString sameB;
        check(tree.addGroup(QString(), QStringLiteral("同名"), &sameA, &error), "same-name group A");
        check(tree.addGroup(QString(), QStringLiteral("同名"), &sameB, &error), "same-name group B");
        check(tree.moveNode(sameA, sameB, &error), "move chooses target by id");
        check(tree.parentOf(sameA) == sameB, "same-name move hits exact parent");
    }

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
    view->undoStack()->undo(); // restore annotation and link for later checks
    check(session.noteForAnnotation(annotation.id) != nullptr, "annotation/link restored for tests");

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
    QFile latestFile(annotationsPath);
    check(latestFile.open(QIODevice::ReadOnly), "read latest reading data");
    const QByteArray latestSaved = latestFile.readAll();
    latestFile.close();

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
    restore.write(latestSaved);
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

    // R02: a note draft is written before any model is published, and a failed
    // write keeps both the note model and the file untouched.
    {
        const int beforeNotes = session.notes()->size();
        pano::NoteDraft draft;
        draft.noteId = pano::newNoteId();
        draft.title = QStringLiteral("空白笔记示例");
        draft.markdown = QString();
        draft.parentGroupId = groupA;
        draft.isNew = true;
        QString committed;
        check(session.commitNoteDraft(draft, &committed, &error), "draft note commits atomically");
        check(session.notes()->size() == beforeNotes + 1, "draft note published after write");
        check(session.note(committed) && session.note(committed)->markdown.isEmpty(),
              "empty draft body persists");
        QFile draftFile(annotationsPath);
        check(draftFile.open(QIODevice::ReadOnly), "read committed draft");
        const QByteArray afterDraftBytes = draftFile.readAll();
        draftFile.close();

        QFile tampered(annotationsPath);
        check(tampered.open(QIODevice::Append), "tamper reading data");
        tampered.write(" ");
        tampered.close();
        pano::NoteDraft failedDraft = draft;
        failedDraft.noteId = pano::newNoteId();
        failedDraft.title = QStringLiteral("外部冲突草稿");
        const int afterFirst = session.notes()->size();
        check(!session.commitNoteDraft(failedDraft, &committed, &error),
              "external change rejects draft");
        check(session.notes()->size() == afterFirst, "failed draft not published");
        check(session.note(failedDraft.noteId) == nullptr, "failed draft absent from model");
        QFile unchanged(annotationsPath);
        check(unchanged.open(QIODevice::ReadOnly), "read unchanged file");
        check(unchanged.readAll() == afterDraftBytes + QByteArray(" "), "failed draft does not overwrite");
        unchanged.close();
        QFile restoreCurrent(annotationsPath);
        check(restoreCurrent.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "restore current reading data");
        restoreCurrent.write(afterDraftBytes);
        restoreCurrent.close();
    }

    // R04/R05: note-point creation and note+annotation deletion are one commit.
    {
        const int beforeAnnotations = view->annotations()->annotations().size();
        const int beforeNotes = session.notes()->size();
        pano::NoteDraft pointDraft;
        pointDraft.noteId = pano::newNoteId();
        pointDraft.title = QStringLiteral("笔记点示例");
        pointDraft.markdown = QStringLiteral("正文");
        pointDraft.parentGroupId = groupA;
        pointDraft.isNew = true;
        pointDraft.createNotePoint = true;
        pointDraft.pageIndex = 0;
        pointDraft.pagePoint = QPointF(320, 360);
        pano::AnnotationStyle pointStyle;
        pointStyle.strokeColor = QColor(QStringLiteral("#f59e0b"));
        pointDraft.notePointStyle = pointStyle;
        QString pointNoteId;
        check(session.commitNoteDraft(pointDraft, &pointNoteId, &error),
              "note point and note commit together");
        check(session.notes()->size() == beforeNotes + 1, "note point note added");
        check(view->annotations()->annotations().size() == beforeAnnotations + 1,
              "note point annotation added");
        const pano::Note *pointNote = session.note(pointNoteId);
        check(pointNote && !pointNote->annotationId.isEmpty(), "note point linked");
        const QString pointAnnotationId = pointNote->annotationId;
        check(session.deleteAnnotationAndNote(pointAnnotationId, &error),
              "delete annotation and note atomically");
        check(view->annotations()->find(pointAnnotationId) == nullptr, "point removed");
        check(session.note(pointNoteId) == nullptr, "note removed");
        view->undoStack()->undo();
        check(view->annotations()->find(pointAnnotationId) != nullptr, "undo restores annotation");
        check(session.note(pointNoteId) != nullptr, "undo restores note");
        check(session.deleteAnnotationAndNote(pointAnnotationId, &error),
              "delete note point sample after undo");
    }

    // R02/R10: drafts carry assets, and cancel removes only uncommitted assets.
    {
        pano::NoteEditDialog dialog(&session, nullptr);
        dialog.openNewNoteAtPoint(groupA, 0, QPointF(360, 380), pano::AnnotationStyle());
        const QString assetsDir = session.bookDirectory() + QStringLiteral("/assets");
        const int beforeNotes = session.notes()->size();
        const int beforeAnnotations = view->annotations()->annotations().size();
        QImage image(24, 24, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#336699")));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        QString relative;
        check(session.writeAsset(QStringLiteral("image"), QStringLiteral("png"), bytes, &relative, &error),
              "draft asset written");
        dialog.insertAssetMarkdown(relative, QStringLiteral("草稿图"));
        dialog.reject();
        check(session.notes()->size() == beforeNotes, "cancelled note not published");
        check(view->annotations()->annotations().size() == beforeAnnotations,
              "cancelled note point not published");
        check(!QFileInfo::exists(session.bookDirectory() + QLatin1Char('/') + relative),
              "cancelled draft asset removed");
    }

    // R01/R02: the editor saves through the same atomic session commit; a
    // successful save is visible in both the model and annotations.json.
    {
        const int beforeNotes = session.notes()->size();
        pano::NoteEditDialog dialog(&session, nullptr);
        dialog.openNewNote(groupA);
        auto *titleEdit = dialog.findChild<QLineEdit *>();
        auto *editor = dialog.findChild<QPlainTextEdit *>();
        check(titleEdit && editor, "draft save widgets");
        if (titleEdit && editor) {
            titleEdit->setText(QStringLiteral("草稿保存示例"));
            editor->setPlainText(QStringLiteral("## 已保存\n\n保存后应立即可见。"));
            QString saveError;
            check(dialog.commitIfDirty(&saveError), "editor draft saves");
            check(session.notes()->size() == beforeNotes + 1, "editor save publishes note");
            const QString noteId = dialog.currentNoteId();
            const pano::Note *savedNote = session.note(noteId);
            check(savedNote && savedNote->markdown.contains(QStringLiteral("已保存")),
                  "editor save content persisted");
            QFile checkFile(annotationsPath);
            check(checkFile.open(QIODevice::ReadOnly), "read saved draft file");
            const QJsonObject checkRoot = QJsonDocument::fromJson(checkFile.readAll()).object();
            bool inFile = false;
            for (const QJsonValue &value : checkRoot.value(QStringLiteral("notes")).toArray()) {
                if (value.toObject().value(QStringLiteral("id")).toString() == noteId)
                    inFile = true;
            }
            check(inFile, "editor save written before publish");
            const pano::Note *unchanged = session.note(noteId);
            const int undoBefore = view->undoStack()->count();
            pano::NoteDraft same;
            same.noteId = noteId;
            same.title = unchanged->title;
            same.markdown = unchanged->markdown;
            same.annotationId = unchanged->annotationId;
            QString sameError;
            check(session.commitNoteDraft(same, nullptr, &sameError), "no-change draft save succeeds");
            check(view->undoStack()->count() == undoBefore, "no-change save adds no undo command");
            check(session.notes()->size() == beforeNotes + 1, "no-change save does not duplicate");
        }
        dialog.reject();
    }

    // R08/R09: excerpt is not erased by header refresh; one render serves the preview.
    {
        pano::NoteEditDialog dialog(&session, nullptr);
        dialog.openNewNote(groupA, annotation.id);
        auto *editor = dialog.findChild<QPlainTextEdit *>();
        check(editor && editor->toPlainText().startsWith(QStringLiteral(">")),
              "new note keeps escaped excerpt");
        check(editor && editor->toPlainText().contains(QStringLiteral("摘录内容")),
              "excerpt content retained");
        dialog.reject();
    }

    // R07: traversal, symlink escape and external paths are blocked.
    {
        const pano::MarkdownRenderResult traversal =
            pano::MarkdownRenderer::render(QStringLiteral("![x](../../outside.png)"),
                                           session.bookDirectory(), 11.0, QColor(Qt::black));
        check(traversal.missingImages >= 1, "image traversal blocked");
        check(!traversal.warnings.isEmpty(), "image traversal reports warning");

        const QString linkPath = session.bookDirectory() + QStringLiteral("/assets/escape.png");
        QFile::remove(linkPath);
        if (QFile::link(QStringLiteral("/etc/hostname"), linkPath)) {
            const pano::MarkdownRenderResult symlink =
                pano::MarkdownRenderer::render(QStringLiteral("![x](assets/escape.png)"),
                                               session.bookDirectory(), 11.0, QColor(Qt::black));
            check(symlink.missingImages >= 1, "symlink escape blocked");
            QFile::remove(linkPath);
        }
    }

    // Visual evidence for the tree and editor.
    auto *workspace = session.workspace();
    // R03/R07: export must not overwrite and the same target stays stable.
    {
        const QString exportPath = QDir(root).filePath(QStringLiteral("exports/no-overwrite.md"));
        QFile::remove(exportPath);
        QDir().mkpath(QFileInfo(exportPath).absolutePath());
        workspace->exportNoteMarkdown(linkedId, exportPath, &error);
        check(QFileInfo::exists(exportPath), "export created file");
        error.clear();
        workspace->exportNoteMarkdown(linkedId, exportPath, &error);
        check(!error.isEmpty(), "export refuses overwrite");
    }

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
        check(tree->topLevelItemCount() == 1, "note tree has one virtual book root");
        QTreeWidgetItem *rootItem = tree->topLevelItem(0);
        check(rootItem && !(rootItem->flags() & Qt::ItemIsEditable),
              "book root is not editable");
        check(rootItem && rootItem->data(0, Qt::UserRole).toString().isEmpty(),
              "book root has no persisted node id");
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
    if (noteDialog) {
        auto *modeCombo = noteDialog->findChild<QComboBox *>(QStringLiteral("noteModeCombo"));
        auto *editor = noteDialog->findChild<QPlainTextEdit *>();
        check(modeCombo && editor, "editor mode and source widgets");
        if (modeCombo && editor) {
            const QString sourceBeforeModes = editor->toPlainText();
            modeCombo->setCurrentIndex(0);
            check(editor->isVisible(), "editor-only mode keeps source");
            modeCombo->setCurrentIndex(2);
            check(!editor->isVisible(), "preview-only mode hides source");
            modeCombo->setCurrentIndex(1);
            check(editor->toPlainText() == sourceBeforeModes,
                  "mode switches preserve Markdown source");
        }
        if (editor) {
            const QString beforeReopen = editor->toPlainText();
            noteDialog->openExistingNote(linkedId);
            check(editor->toPlainText() == beforeReopen,
                  "same note reopen keeps unsaved body");
            QImage image(40, 24, QImage::Format_ARGB32_Premultiplied);
            image.fill(QColor(QStringLiteral("#66cc99")));
            auto *mime = new QMimeData;
            mime->setImageData(image);
            QApplication::clipboard()->setMimeData(mime);
            QMetaObject::invokeMethod(editor, "paste");
            waitFor(80);
            check(editor->toPlainText().contains(QStringLiteral("剪贴板图片")),
                  "clipboard image inserts markdown");
            check(!QDir(session.bookDirectory() + QStringLiteral("/assets"))
                       .entryList({QStringLiteral("image-*.png")}, QDir::Files)
                       .isEmpty(),
                  "clipboard image saved to book assets");
            bool regenerate = false;
            for (QPushButton *button : noteDialog->findChildren<QPushButton *>()) {
                if (button->text().contains(QStringLiteral("重新生成正文")))
                    regenerate = true;
            }
            check(!regenerate, "regenerate-body action removed");
        }
        noteDialog->reject();
    }
    waitFor(100);
    workspace->beginNoteEdit();
    waitFor(200);
    if (QWidget *dialog = workspace->findChild<QDialog *>()) {
        dialog->grab().save(QStringLiteral("build/probe/phase3-note-editor.png"));
    }
    {
        const QString markdown = QStringLiteral(
            "## 公式\n\n"
            "$E=mc^2$\n\n"
            "$$\n\\begin{bmatrix}1&2\\\\3&4\\end{bmatrix}\n$$\n\n"
            "$$\n\\begin{cases}x=1 & y=2\\\\z=3 & w=4\\end{cases}\n$$");
        const pano::MarkdownRenderResult rendered =
            pano::MarkdownRenderer::render(markdown, session.bookDirectory(), 11.0, QColor(Qt::black));
        check(rendered.mathFailures == 0, "formal MD4C/JKQT renders matrix and cases");
        check(rendered.formulaCount >= 3, "formal renderer counts inline and display formulas");
        check(!rendered.resources.isEmpty(), "formal renderer creates math image resources");
    }
    // Note export: single Markdown, tree directory and JSON metadata.
    {
        const QString exportRoot = QDir(root).filePath(QStringLiteral("exports"));
        QDir(exportRoot).removeRecursively();
        QDir().mkpath(exportRoot);
        QString exportError;
        const QString singlePath = QDir(exportRoot).filePath(QStringLiteral("single.md"));
        workspace->exportNoteMarkdown(linkedId, singlePath, &exportError);
        QFile single(singlePath);
        check(single.open(QIODevice::ReadOnly), "single note export file");
        const QString singleText = QString::fromUtf8(single.readAll());
        check(singleText.contains(QStringLiteral("关联笔记")), "single note export title");
        check(singleText.contains(QStringLiteral("$E=mc^2$")), "single note export keeps TeX");
        check(exportError.isEmpty(), "single note export no error");

        const QString treeDir = QDir(exportRoot).filePath(QStringLiteral("tree"));
        workspace->exportBookMarkdown(treeDir, &exportError);
        check(exportError.isEmpty(), "tree export no error");
        check(QDir(treeDir).exists(), "tree export directory");
        check(!QDir(treeDir).entryList({QStringLiteral("*.md")}, QDir::Files).isEmpty()
                  || !QDir(treeDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(),
              "tree export contains notes");

        const QString jsonPath = QDir(exportRoot).filePath(QStringLiteral("notes.json"));
        workspace->exportBookJson(jsonPath, &exportError);
        QFile jsonFile(jsonPath);
        check(jsonFile.open(QIODevice::ReadOnly), "json export file");
        const QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
        check(doc.isObject() && doc.object().value(QStringLiteral("format")).toString()
                  == QStringLiteral("pano-notes-export"),
              "json export format");
        check(exportError.isEmpty(), "json export no error");
    }
    workspace->close();
    waitFor(100);
    session.flush();
    manager.closeLibrary();

    std::printf("note probe %s (%d failures)\n", failures == 0 ? "passed" : "failed", failures);
    return failures == 0 ? 0 : 1;
}
