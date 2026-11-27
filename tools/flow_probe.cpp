#include "app/MainWindow.h"
#include "app/NotePanel.h"
#include "annotation/AnnotationCommands.h"
#include "pdf/PdfDocument.h"
#include "reader/PdfView.h"
#include "storage/AnnotationStore.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QPixmap>
#include <QTimer>
#include <QtDebug>
#include <optional>
#include <QtPdf/QPdfSelection>

using namespace pano;

static QByteArray fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result();
}

static Annotation makeRect()
{
    Annotation a;
    a.id = newAnnotationId();
    a.pageIndex = 0;
    a.type = AnnotationType::Rectangle;
    a.rect = QRectF(110, 500, 360, 92);
    a.p1 = a.rect.topLeft();
    a.p2 = a.rect.bottomRight();
    a.style.strokeColor = QColor(QStringLiteral("#d94a4a"));
    a.style.strokeWidthPt = 1.6;
    a.style.opacity = 1.0;
    a.style.blend = BlendMode::Normal;
    a.createdAt = nowIsoUtc();
    a.modifiedAt = a.createdAt;
    return a;
}

static Annotation makeNotePin()
{
    Annotation a;
    a.id = newAnnotationId();
    a.pageIndex = 0;
    a.type = AnnotationType::NotePoint;
    const QPointF point(520, 128);
    a.p1 = point;
    a.rect = QRectF(point, QSizeF(0, 0));
    a.style.strokeColor = QColor(QStringLiteral("#2f6fed"));
    a.style.strokeWidthPt = 1.5;
    a.style.opacity = 1.0;
    a.createdAt = nowIsoUtc();
    a.modifiedAt = a.createdAt;
    a.note.title = QStringLiteral("阅读笔记");
    a.note.markdown = QStringLiteral(
        "# 阅读笔记\n\n"
        "这段原文说明：标注保存的是**页内坐标与选区多边形**，而不是屏幕像素。\n\n"
        "行内公式 $m\\ddot{u}+ku=0$ 与显示公式：\n\n"
        "$$\\nabla\\cdot\\vec{E}=\\frac{\\rho}{\\varepsilon_0}$$\n\n"
        "| 项目 | 约定 |\n| --- | --- |\n| 坐标 | 页面 pt，左上原点 |\n| 存储 | `.pano/annotations.json` |\n\n"
        "> 笔记解释原文，不替代原文。\n");
    return a;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO"));
    if (argc < 3) {
        qWarning() << "usage: pano-flow-probe <pdf> <screenshot.png>";
        return 1;
    }
    const QString sourcePdf = QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath();
    const QString outputPng = QString::fromLocal8Bit(argv[2]);

    const QString tempDir = QDir::tempPath() + QStringLiteral("/pano-flow-probe");
    QDir(tempDir).removeRecursively();
    QDir().mkpath(tempDir);
    const QString pdf = tempDir + QStringLiteral("/sample-paper.pdf");
    if (!QFile::copy(sourcePdf, pdf)) {
        qWarning() << "cannot copy sample pdf";
        return 1;
    }

    MainWindow window;
    window.resize(1440, 900);
    window.show();
    if (!window.openDocument(pdf)) {
        qWarning() << "openDocument failed";
        return 2;
    }

    auto *view = window.findChild<PdfView *>();
    auto *panel = window.findChild<NotePanel *>();
    if (!view || !panel) {
        qWarning() << "cannot find PdfView/NotePanel";
        return 3;
    }

    std::optional<QPdfSelection> sampleSelection;
    for (int y = 180; y <= 620 && !sampleSelection.has_value(); y += 20) {
        QPdfSelection selection = view->document()->pdf()->getSelection(
            0, QPointF(90, y), QPointF(500, y));
        if (selection.isValid())
            sampleSelection = selection;
    }
    qInfo() << "sample text selection valid:" << sampleSelection.has_value()
            << "chars:" << (sampleSelection ? sampleSelection->text().size() : 0)
            << "polygons:" << (sampleSelection ? sampleSelection->bounds().size() : 0);
    if (!sampleSelection || sampleSelection->text().trimmed().isEmpty()
        || sampleSelection->bounds().isEmpty()) {
        qWarning() << "sample PDF did not expose selectable text";
        return 11;
    }

    const Annotation rect = makeRect();
    view->undoStack()->push(new AddAnnotationCommand(view->annotations(), rect));
    qInfo() << "after add rect:" << view->annotations()->count()
            << "undo:" << view->undoStack()->canUndo() << "redo:" << view->undoStack()->canRedo();
    view->undoStack()->undo();
    qInfo() << "after undo:" << view->annotations()->count();
    view->undoStack()->redo();
    qInfo() << "after redo:" << view->annotations()->count();

    const Annotation pin = makeNotePin();
    view->undoStack()->push(new AddAnnotationCommand(view->annotations(), pin));
    view->selectAnnotation(pin.id);
    view->revealAnnotation(pin.id);
    qInfo() << "selected:" << view->selectedAnnotationId() << "note panel editing:" << panel->isEditing();

    const int originalNoteSize = panel->noteMarkdown().size();
    panel->beginEdit();
    panel->insertMarkdown(QStringLiteral("\n\n- 保存后重新打开仍然可读。"));
    QMetaObject::invokeMethod(&window, "commitNoteEdit", Qt::DirectConnection);
    const Annotation *edited = view->annotations()->find(pin.id);
    qInfo() << "note after edit chars:" << edited->note.markdown.size()
            << "editing:" << panel->isEditing();
    view->undoStack()->undo();
    const Annotation *reverted = view->annotations()->find(pin.id);
    qInfo() << "note after undo chars:" << reverted->note.markdown.size();
    if (reverted->note.markdown.size() != originalNoteSize) {
        qWarning() << "note undo verification failed";
        return 7;
    }
    view->undoStack()->redo();
    qInfo() << "note after redo chars:" << view->annotations()->find(pin.id)->note.markdown.size();

    view->goToPosition(1, QPointF(300.0, 400.0));
    const bool navigationReachedPage = view->currentPage() == 1;
    view->revealAnnotation(pin.id);
    qInfo() << "precise navigation reached page 1:" << navigationReachedPage
            << "back to pin page:" << view->currentPage();
    if (!navigationReachedPage || view->currentPage() != 0) {
        qWarning() << "precise navigation verification failed";
        return 12;
    }

    const QString noteBeforeDelete = view->annotations()->find(pin.id)->note.markdown;
    view->selectAnnotation(pin.id);
    view->deleteAnnotation(pin.id, true);
    const bool deletedWithNote = view->annotations()->count() == 1;
    view->undoStack()->undo();
    const Annotation *restored = view->annotations()->find(pin.id);
    const bool undoRestoredNote = restored && restored->note.markdown == noteBeforeDelete;
    qInfo() << "delete annotation+note:" << deletedWithNote
            << "undo restores note:" << undoRestoredNote;
    if (!deletedWithNote || !undoRestoredNote || view->annotations()->count() != 2) {
        qWarning() << "annotation/note delete-undo verification failed";
        return 13;
    }

    const QRectF pinBeforeRotation = view->annotations()->find(pin.id)->rect;
    view->setRotation(90);
    view->setRotation(0);
    view->setZoom(2.0);
    view->fitPage();
    const QRectF pinAfterRotation = view->annotations()->find(pin.id)->rect;
    qInfo() << "rotation/zoom roundtrip keeps annotation:"
            << (pinBeforeRotation == pinAfterRotation) << "rotation:" << view->rotation();
    if (pinBeforeRotation != pinAfterRotation) {
        qWarning() << "annotation moved during rotation/zoom roundtrip";
        return 9;
    }

    const QByteArray pdfHashBefore = fileSha256(pdf);
    QMetaObject::invokeMethod(&window, "saveDocument", Qt::DirectConnection);
    const QString storeDir = pdf + QStringLiteral(".pano");
    qInfo() << "store exists:" << QDir(storeDir).exists()
            << "annotations exists:" << QFile::exists(storeDir + "/annotations.json")
            << "manifest exists:" << QFile::exists(storeDir + "/document.json")
            << "pdf unchanged:" << (fileSha256(pdf) == pdfHashBefore);
    if (fileSha256(pdf) != pdfHashBefore) {
        qWarning() << "source PDF was modified";
        return 8;
    }

    // A copied PDF with different bytes must not bind to the existing store.
    const QString tamperedPdf = tempDir + QStringLiteral("/tampered.pdf");
    QFile::copy(pdf, tamperedPdf);
    {
        QFile tamper(tamperedPdf);
        if (tamper.open(QIODevice::Append))
            tamper.write("\n% probe tamper\n");
    }
    const QString tamperedStore = tamperedPdf + QStringLiteral(".pano");
    QDir(tamperedStore).removeRecursively();
    QDir().mkpath(tamperedStore);
    QFile::copy(storeDir + QStringLiteral("/document.json"),
                tamperedStore + QStringLiteral("/document.json"));
    QFile::copy(storeDir + QStringLiteral("/annotations.json"),
                tamperedStore + QStringLiteral("/annotations.json"));
    PdfDocument tamperedDocument;
    QString tamperedLoadError;
    if (tamperedDocument.load(tamperedPdf, &tamperedLoadError)) {
        AnnotationStore mismatchedStore;
        QString mismatchError;
        const bool bound = mismatchedStore.attachToDocument(tamperedDocument, &mismatchError);
        qInfo() << "mismatched PDF rejected:" << !bound << "error:" << mismatchError;
        if (bound) {
            qWarning() << "store silently bound to a different PDF";
            return 10;
        }
    } else {
        qInfo() << "tampered sample could not be loaded, mismatch check skipped:" << tamperedLoadError;
    }

    if (!window.openDocument(pdf)) {
        qWarning() << "reopen failed";
        return 4;
    }
    auto *view2 = window.findChild<PdfView *>();
    auto *panel2 = window.findChild<NotePanel *>();
    qInfo() << "after reopen annotations:" << view2->annotations()->count()
            << "selection before select:" << view2->selectedAnnotationId();
    if (view2->annotations()->count() != 2) {
        qWarning() << "round-trip verification failed: annotation count";
        return 5;
    }
    view2->selectAnnotation(pin.id);
    view2->revealAnnotation(pin.id);
    qInfo() << "note markdown chars after select:" << panel2->noteMarkdown().size();
    if (panel2->noteMarkdown().isEmpty()) {
        qWarning() << "round-trip verification failed: note not loaded";
        return 5;
    }

    QTimer::singleShot(2400, &app, [&] {
        const QPixmap pixmap = window.grab();
        if (!pixmap.save(outputPng)) {
            qWarning() << "cannot save screenshot" << outputPng;
            app.exit(6);
            return;
        }
        qInfo() << "saved screenshot:" << outputPng << pixmap.size();
        app.quit();
    });
    return app.exec();
}
