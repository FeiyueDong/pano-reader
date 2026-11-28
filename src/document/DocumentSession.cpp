#include "document/DocumentSession.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QSet>
#include <algorithm>
#include <QtGlobal>
#include <QFileInfo>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUndoStack>

#include "app/ReaderWorkspace.h"
#include "library/LibraryManager.h"
#include "settings/SettingsManager.h"
#include "reader/PageLayout.h"
#include "reader/PdfView.h"
#include "common/JsonUtils.h"
#include "storage/LibraryStore.h"

namespace pano {

class ReadingSnapshotCommand : public QUndoCommand
{
public:
    ReadingSnapshotCommand(DocumentSession *session, const QString &text,
                           QVector<Annotation> beforeAnnotations,
                           QVector<Note> beforeNotes, NoteTree beforeTree,
                           QVector<Annotation> afterAnnotations,
                           QVector<Note> afterNotes, NoteTree afterTree)
        : QUndoCommand(text)
        , m_session(session)
        , m_beforeAnnotations(std::move(beforeAnnotations))
        , m_beforeNotes(std::move(beforeNotes))
        , m_beforeTree(std::move(beforeTree))
        , m_afterAnnotations(std::move(afterAnnotations))
        , m_afterNotes(std::move(afterNotes))
        , m_afterTree(std::move(afterTree))
    {
    }

    void undo() override
    {
        if (m_session)
            m_session->restoreReadingSnapshot(m_beforeAnnotations, m_beforeNotes, m_beforeTree);
    }

    void redo() override
    {
        if (m_session)
            m_session->restoreReadingSnapshot(m_afterAnnotations, m_afterNotes, m_afterTree);
    }

private:
    DocumentSession *m_session = nullptr;
    QVector<Annotation> m_beforeAnnotations;
    QVector<Note> m_beforeNotes;
    NoteTree m_beforeTree;
    QVector<Annotation> m_afterAnnotations;
    QVector<Note> m_afterNotes;
    NoteTree m_afterTree;
};

DocumentSession::DocumentSession(LibraryManager *manager, const BookInfo &book,
                                 SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_settings(settings)
    , m_book(book)
{
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setSingleShot(true);
    connect(m_autosaveTimer, &QTimer::timeout, this, &DocumentSession::autosave);

    m_maxTimer = new QTimer(this);
    m_maxTimer->setSingleShot(true);
    connect(m_maxTimer, &QTimer::timeout, this, &DocumentSession::autosave);

    m_readingTimer = new QTimer(this);
    m_readingTimer->setSingleShot(true);
    m_readingTimer->setInterval(1200);
    connect(m_readingTimer, &QTimer::timeout, this, &DocumentSession::saveReadingState);

    m_workspace = new ReaderWorkspace(this);
    connect(view(), &PdfView::annotationsChanged, this, &DocumentSession::markDirty);
    connect(view()->annotations(), &AnnotationModel::structureChanged,
            this, &DocumentSession::markDirty);
    connect(view()->annotations(), &AnnotationModel::annotationUpdated,
            this, &DocumentSession::markDirty);
    connect(view()->annotations(), &AnnotationModel::annotationsReset,
            this, &DocumentSession::markDirty);
    connect(view(), &PdfView::pageChanged, this, &DocumentSession::onPageChanged);
    connect(view(), &PdfView::zoomChanged, this, &DocumentSession::onZoomChanged);
    connect(view(), &PdfView::rotationChanged, this, &DocumentSession::onRotationChanged);
    connect(view(), &PdfView::zoomModeChanged, this, [this](ZoomMode mode) {
        m_zoomMode = mode;
        scheduleReadingSave();
    });
    connect(view(), &PdfView::layoutModeChanged, this, [this](PageLayoutMode) {
        scheduleReadingSave();
    });
    connect(view(), &PdfView::firstPageAloneChanged, this, [this](bool) {
        scheduleReadingSave();
    });
    connect(view(), &PdfView::readingAnchorChanged, this, &DocumentSession::scheduleReadingSave);
}

DocumentSession::~DocumentSession()
{
    flush();
    if (m_workspace) {
        m_workspace->setParent(nullptr);
        delete m_workspace;
        m_workspace = nullptr;
    }
}

ToolPresets *DocumentSession::toolPresets() const
{
    return m_settings ? m_settings->toolPresets() : nullptr;
}

PdfView *DocumentSession::view() const
{
    return m_workspace ? m_workspace->view() : nullptr;
}

QString DocumentSession::bookDirectory() const
{
    return m_manager && m_manager->store() ? m_manager->store()->absoluteBookDirectory(m_book.directory)
                                           : QString();
}

QString DocumentSession::assetsDirectory() const
{
    return QDir(bookDirectory()).filePath(QStringLiteral("assets"));
}

bool DocumentSession::load(QString *errorMessage)
{
    if (!m_manager || !m_manager->isOpen()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书库尚未打开。");
        return false;
    }
    m_book = m_manager->bookInfo(m_book.id);
    if (m_book.id.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("书籍记录不存在。");
        return false;
    }
    if (!m_book.sourceAvailable) {
        if (errorMessage) {
            *errorMessage = m_book.sourceError.isEmpty() ? QStringLiteral("源 PDF 不可用。")
                                                         : m_book.sourceError;
        }
        return false;
    }
    const bool timing = qEnvironmentVariableIsSet("PANO_OPEN_TIMING");
    QElapsedTimer sourceTimer;
    if (timing)
        sourceTimer.start();
    if (!m_manager->verifySource(m_book.id, errorMessage))
        return false;
    if (timing)
        qInfo().noquote() << QStringLiteral("[PANO_TIMING] source verified sha256 ms=%1")
                                 .arg(sourceTimer.elapsed());

    QElapsedTimer timer;
    if (timing)
        timer.start();
    m_loading = true;
    if (!view()->openFile(m_book.pdfPath, errorMessage)) {
        m_loading = false;
        return false;
    }
    if (timing)
        qInfo().noquote() << QStringLiteral("[PANO_TIMING] pdf opened ms=%1 pages=%2")
                                 .arg(timer.elapsed()).arg(view()->pageCount());

    QJsonArray annotations;
    QJsonArray noteObjects;
    QJsonArray treeObjects;
    if (!m_manager->store()->readReadingData(m_book.directory, m_book.id, &annotations,
                                              &noteObjects, &treeObjects, errorMessage)) {
        view()->closeDocument();
        m_loading = false;
        return false;
    }

    m_annotationsFingerprint = annotationsFingerprint();
    if (timing)
        qInfo().noquote() << QStringLiteral("[PANO_TIMING] annotations loaded count=%1 ms=%2")
                                 .arg(annotations.size()).arg(timer.elapsed());
    QString bookmarkError;
    if (!m_bookmarkStore.load(bookDirectory(), m_book.id, &bookmarkError)) {
        emit statusMessage(bookmarkError);
    }
    connect(&m_bookmarkStore, &BookmarkStore::dirtyChanged, this, [this](bool dirty) {
        if (dirty)
            markDirty();
    });

    QVector<Annotation> parsed;
    parsed.reserve(annotations.size());
    for (const QJsonValue &value : annotations) {
        if (!value.isObject()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("annotations.json 包含非对象标注。");
            view()->closeDocument();
            m_loading = false;
            return false;
        }
        Annotation annotation;
        QString annotationError;
        if (!Annotation::fromJson(value.toObject(), &annotation, &annotationError)) {
            if (errorMessage)
                *errorMessage = annotationError;
            view()->closeDocument();
            m_loading = false;
            return false;
        }
        if (annotation.pageIndex >= view()->pageCount()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("标注 %1 指向不存在的页面 %2。")
                                    .arg(annotation.id)
                                    .arg(annotation.pageIndex + 1);
            }
            view()->closeDocument();
            m_loading = false;
            return false;
        }
        parsed.append(annotation);
    }

    NoteModel parsedNotes;
    for (const QJsonValue &value : noteObjects) {
        if (!value.isObject()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("annotations.json 的 notes 包含非对象笔记。");
            view()->closeDocument();
            m_loading = false;
            return false;
        }
        Note noteValue;
        QString noteError;
        if (!Note::fromJson(value.toObject(), &noteValue, &noteError)
            || !parsedNotes.insert(noteValue, &noteError)) {
            if (errorMessage)
                *errorMessage = noteError;
            view()->closeDocument();
            m_loading = false;
            return false;
        }
        if (!noteValue.annotationId.isEmpty()) {
            const bool annotationExists = std::any_of(parsed.cbegin(), parsed.cend(),
                                                      [&](const Annotation &annotation) {
                                                          return annotation.id == noteValue.annotationId;
                                                      });
            if (!annotationExists) {
                if (errorMessage)
                    *errorMessage = QStringLiteral("笔记 %1 关联了不存在的标注 %2。")
                                        .arg(noteValue.id, noteValue.annotationId);
                view()->closeDocument();
                m_loading = false;
                return false;
            }
        }
    }

    NoteTree parsedTree;
    QString treeError;
    if (!NoteTree::fromJson(treeObjects, &parsedTree, &treeError)) {
        if (errorMessage)
            *errorMessage = treeError;
        view()->closeDocument();
        m_loading = false;
        return false;
    }
    QSet<QString> noteIds;
    for (const Note &noteValue : parsedNotes.notes())
        noteIds.insert(noteValue.id);
    if (!parsedTree.validate(noteIds, &treeError)) {
        if (errorMessage)
            *errorMessage = treeError;
        view()->closeDocument();
        m_loading = false;
        return false;
    }

    view()->annotations()->setAnnotations(parsed);
    m_notes.setNotes(parsedNotes.notes());
    m_noteTree = parsedTree;
    view()->undoStack()->clear();
    if (m_settings)
        view()->setWheelTurnsPage(m_settings->settings().wheelTurnsPage);

    if (timing)
        qInfo().noquote() << QStringLiteral("[PANO_TIMING] session data loaded ms=%1")
                                 .arg(timer.elapsed());
    const ReadingState state = m_manager->readingState(m_book.id);
    if (state.valid) {
        PageLayoutMode layoutMode = PageLayoutMode::ContinuousSingle;
        if (pageLayoutModeFromString(state.layoutMode, &layoutMode)) {
            view()->setLayoutMode(layoutMode);
            view()->setFirstPageAlone(state.firstPageAlone);
        }
        if (state.rotation == 90 || state.rotation == 180 || state.rotation == 270)
            view()->setRotation(state.rotation);
        m_zoomMode = state.zoomMode;
        if (m_zoomMode == ZoomMode::FitWidth)
            view()->fitWidth();
        else if (m_zoomMode == ZoomMode::FitPage)
            view()->fitPage();
        else
            view()->setZoom(state.zoom);
        if (!state.pagePoint.isNull())
            view()->navigateTo(state.pageIndex, state.pagePoint,
                               PdfView::NavigationAlignment::Center,
                               PdfView::NavigationReason::Restore);
        else
            view()->navigateTo(state.pageIndex, QPointF(),
                               PdfView::NavigationAlignment::PageTop,
                               PdfView::NavigationReason::Restore);
    } else if (m_settings) {
        PageLayoutMode layoutMode = PageLayoutMode::ContinuousSingle;
        pageLayoutModeFromString(m_settings->settings().defaultLayoutMode, &layoutMode);
        view()->setLayoutMode(layoutMode);
        view()->setFirstPageAlone(m_settings->settings().firstPageAlone);
        view()->setPageGap(m_settings->settings().pageGap);
        if (m_settings->settings().defaultZoomMode == ZoomMode::FitWidth)
            view()->fitWidth();
        else if (m_settings->settings().defaultZoomMode == ZoomMode::FitPage)
            view()->fitPage();
    }

    m_loaded = true;
    m_loading = false;
    m_dirty = false;
    emit dirtyChanged(false);
    m_manager->recordHistory(m_book.id);
    emit statusMessage(QStringLiteral("已打开：%1").arg(m_book.title));
    return true;
}

bool DocumentSession::saveNow(QString *errorMessage)
{
    if (!m_loaded || !m_manager || !m_manager->isOpen())
        return true;
    if (m_workspace) {
        m_workspace->flushPendingStyle();
        QString noteError;
        if (!m_workspace->commitPendingNote(&noteError)) {
            if (errorMessage)
                *errorMessage = noteError;
            emit saveFailed(noteError);
            return false;
        }
    }
    if (!m_dirty) {
        saveReadingState();
        if (m_bookmarkStore.isDirty()) {
            QString bookmarkError;
            if (!m_bookmarkStore.save(&bookmarkError)) {
                if (errorMessage)
                    *errorMessage = bookmarkError;
                emit saveFailed(bookmarkError);
                return false;
            }
        }
        return true;
    }

    const QByteArray currentFingerprint = annotationsFingerprint();
    if (currentFingerprint != m_annotationsFingerprint) {
        if (errorMessage)
            *errorMessage = QStringLiteral("标注文件已被外部修改，已停止保存以避免覆盖。请关闭并重新打开本书后重试。");
        emit saveFailed(errorMessage ? *errorMessage : QString());
        return false;
    }

    QJsonArray annotations;
    const QVector<Annotation> &values = view()->annotations()->annotations();
    for (int index = 0; index < values.size(); ++index) {
        QJsonObject object = values.at(index).toJson();
        object.insert(QStringLiteral("z_order"), index);
        annotations.append(object);
    }
    QJsonArray notes;
    for (const Note &note : m_notes.notes())
        notes.append(note.toJson());
    if (!m_manager->store()->writeReadingData(m_book.directory, m_book.id, annotations, notes,
                                               m_noteTree.toJson(), errorMessage)) {
        if (errorMessage)
            emit saveFailed(*errorMessage);
        return false;
    }

    m_annotationsFingerprint = annotationsFingerprint();
    saveReadingState();
    if (m_bookmarkStore.isDirty()) {
        QString bookmarkError;
        if (!m_bookmarkStore.save(&bookmarkError)) {
            if (errorMessage)
                *errorMessage = bookmarkError;
            emit saveFailed(bookmarkError);
            return false;
        }
    }
    m_dirty = false;
    m_autosaveActive = false;
    m_autosaveTimer->stop();
    m_maxTimer->stop();
    emit dirtyChanged(false);
    emit statusMessage(QStringLiteral("标注已保存"));
    return true;
}

void DocumentSession::flush()
{
    if (!m_loaded || !m_manager || !m_manager->isOpen())
        return;
    QString error;
    if (m_dirty && !saveNow(&error) && !error.isEmpty())
        emit saveFailed(error);
    else
        saveReadingState();
    if (m_bookmarkStore.isDirty()) {
        QString bookmarkError;
        if (!m_bookmarkStore.save(&bookmarkError))
            emit saveFailed(bookmarkError);
    }
    m_autosaveTimer->stop();
    m_maxTimer->stop();
}

void DocumentSession::markDirty()
{
    if (m_loading || !m_loaded)
        return;
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged(true);
    }
    scheduleAutosave();
}

void DocumentSession::scheduleAutosave()
{
    if (!m_loaded)
        return;
    m_autosaveTimer->start(m_autosaveDelayMs);
    if (!m_autosaveActive) {
        m_autosaveActive = true;
        m_maxTimer->start(30000);
    }
}

void DocumentSession::autosave()
{
    if (!m_dirty)
        return;
    QString error;
    if (!saveNow(&error) && !error.isEmpty())
        emit saveFailed(error);
}

void DocumentSession::onPageChanged(int)
{
    scheduleReadingSave();
}

void DocumentSession::onZoomChanged(qreal)
{
    scheduleReadingSave();
}

void DocumentSession::onRotationChanged(int)
{
    scheduleReadingSave();
}

void DocumentSession::scheduleReadingSave()
{
    if (m_loaded && m_readingTimer)
        m_readingTimer->start();
}

bool DocumentSession::saveBookmarks(QString *errorMessage)
{
    return m_bookmarkStore.save(errorMessage);
}

ReadingState DocumentSession::readingState() const
{
    ReadingState state;
    if (!view() || !view()->hasDocument())
        return state;
    state.valid = true;
    state.pageIndex = view()->currentPage();
    state.zoom = view()->zoom();
    state.rotation = view()->rotation();
    state.zoomMode = view()->zoomMode();
    state.layoutMode = pageLayoutModeToString(view()->layoutMode());
    state.firstPageAlone = view()->firstPageAlone();
    int anchorPage = 0;
    state.pagePoint = view()->viewportAnchor(&anchorPage);
    if (anchorPage >= 0)
        state.pageIndex = anchorPage;
    return state;
}

void DocumentSession::captureReadingState()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    ReadingState state = readingState();
    if (state.valid)
        m_manager->updateReadingState(m_book.id, state, nullptr);
}

void DocumentSession::saveReadingState()
{
    if (!m_manager || !m_manager->isOpen())
        return;
    captureReadingState();
}

void DocumentSession::setZoomMode(ZoomMode mode)
{
    m_zoomMode = mode;
}

void DocumentSession::setAutosaveSeconds(int seconds)
{
    m_autosaveDelayMs = qBound(1, seconds, 30) * 1000;
    if (m_autosaveActive && m_autosaveTimer->isActive())
        m_autosaveTimer->start(m_autosaveDelayMs);
}

void DocumentSession::noteActivity()
{
    if (!m_loaded)
        return;
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged(true);
    }
    scheduleAutosave();
}

const Note *DocumentSession::note(const QString &noteId) const
{
    return m_notes.find(noteId);
}

const Note *DocumentSession::noteForAnnotation(const QString &annotationId) const
{
    return m_notes.forAnnotation(annotationId);
}

const Annotation *DocumentSession::annotationForNote(const QString &noteId) const
{
    const Note *noteValue = m_notes.find(noteId);
    if (!noteValue || noteValue->annotationId.isEmpty() || !view())
        return nullptr;
    return view()->annotations()->find(noteValue->annotationId);
}

bool DocumentSession::hasLinkedNote(const QString &annotationId) const
{
    return m_notes.forAnnotation(annotationId) != nullptr;
}

bool DocumentSession::commitReadingEdit(const QString &undoText,
                                         const std::function<bool(QString *)> &mutation,
                                         QString *errorMessage)
{
    if (!view()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("阅读会话未就绪。");
        return false;
    }
    const QVector<Annotation> beforeAnnotations = view()->annotations()->annotations();
    const QVector<Note> beforeNotes = m_notes.notes();
    const NoteTree beforeTree = m_noteTree;

    QString localError;
    if (!mutation(&localError)) {
        if (errorMessage)
            *errorMessage = localError;
        return false;
    }

    const QVector<Annotation> afterAnnotations = view()->annotations()->annotations();
    const QVector<Note> afterNotes = m_notes.notes();
    const NoteTree afterTree = m_noteTree;
    if (auto *stack = view()->undoStack()) {
        stack->push(new ReadingSnapshotCommand(this, undoText, beforeAnnotations, beforeNotes,
                                               beforeTree, afterAnnotations, afterNotes, afterTree));
    }
    notifyReadingDataChanged();
    return true;
}

void DocumentSession::restoreReadingSnapshot(const QVector<Annotation> &annotations,
                                              const QVector<Note> &notes, const NoteTree &tree)
{
    if (!view())
        return;
    m_restoringReadingData = true;
    view()->annotations()->setAnnotations(annotations);
    m_notes.setNotes(notes);
    m_noteTree = tree;
    m_restoringReadingData = false;
    emit readingDataChanged();
    markDirty();
}

void DocumentSession::notifyReadingDataChanged()
{
    emit readingDataChanged();
    markDirty();
}

bool DocumentSession::createNote(Note noteValue, const QString &parentGroupId, QString *noteId,
                                  QString *errorMessage)
{
    if (!view() || !view()->hasDocument()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("请先打开书籍。");
        return false;
    }
    if (noteValue.id.isEmpty())
        noteValue.id = newNoteId();
    if (noteValue.title.trimmed().isEmpty())
        noteValue.title = QStringLiteral("未命名笔记");
    noteValue.createdAt = noteValue.createdAt.isEmpty() ? nowIsoUtc() : noteValue.createdAt;
    noteValue.modifiedAt = nowIsoUtc();
    if (!parentGroupId.isEmpty() && !m_noteTree.isGroup(parentGroupId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("目标分组成员无效，请重新选择分组。");
        return false;
    }
    if (!noteValue.annotationId.isEmpty()) {
        if (!view()->annotations()->find(noteValue.annotationId)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("关联的标注不存在。");
            return false;
        }
        if (m_notes.forAnnotation(noteValue.annotationId)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("该标注已经有关联笔记。");
            return false;
        }
    }
    const QString newId = noteValue.id;
    if (!commitReadingEdit(QStringLiteral("新建笔记"), [&](QString *error) {
            if (!m_notes.insert(noteValue, error))
                return false;
            if (!m_noteTree.addNote(parentGroupId, noteValue.id, error)) {
                m_notes.remove(noteValue.id);
                return false;
            }
            return true;
        }, errorMessage)) {
        return false;
    }
    if (noteId)
        *noteId = newId;
    return true;
}

bool DocumentSession::updateNote(const Note &noteValue, QString *errorMessage)
{
    const Note *existing = m_notes.find(noteValue.id);
    if (!existing) {
        if (errorMessage)
            *errorMessage = QStringLiteral("笔记不存在。");
        return false;
    }
    if (!noteValue.annotationId.isEmpty()) {
        if (!view() || !view()->annotations()->find(noteValue.annotationId)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("关联的标注不存在。");
            return false;
        }
        const Note *other = m_notes.forAnnotation(noteValue.annotationId);
        if (other && other->id != noteValue.id) {
            if (errorMessage)
                *errorMessage = QStringLiteral("该标注已经有关联笔记。");
            return false;
        }
    }
    Note updated = noteValue;
    if (updated.title.trimmed().isEmpty())
        updated.title = QStringLiteral("未命名笔记");
    updated.createdAt = existing->createdAt;
    updated.modifiedAt = nowIsoUtc();
    return commitReadingEdit(QStringLiteral("修改笔记"), [&](QString *error) {
        return m_notes.replace(updated.id, updated, error);
    }, errorMessage);
}

bool DocumentSession::deleteNote(const QString &noteId, bool removeNotePoint, QString *errorMessage)
{
    const Note *existing = m_notes.find(noteId);
    if (!existing) {
        if (errorMessage)
            *errorMessage = QStringLiteral("笔记不存在。");
        return false;
    }
    const QString annotationId = existing->annotationId;
    return commitReadingEdit(QStringLiteral("删除笔记"), [&](QString *error) {
        m_notes.remove(noteId);
        m_noteTree.removeNode(noteId, false, error);
        if (removeNotePoint && !annotationId.isEmpty()) {
            const Annotation *annotation = view()->annotations()->find(annotationId);
            if (annotation && annotation->type == AnnotationType::NotePoint)
                view()->annotations()->removeById(annotationId);
        }
        return true;
    }, errorMessage);
}

bool DocumentSession::deleteAnnotationKeepingNote(const QString &annotationId,
                                                   QString *errorMessage)
{
    const Annotation *annotation = view() ? view()->annotations()->find(annotationId) : nullptr;
    if (!annotation) {
        if (errorMessage)
            *errorMessage = QStringLiteral("标注不存在。");
        return false;
    }
    Note *linked = nullptr;
    if (Note *noteValue = m_notes.forAnnotation(annotationId))
        linked = noteValue;
    Note updated;
    const bool hasLinked = linked != nullptr;
    if (hasLinked) {
        updated = *linked;
        updated.annotationId.clear();
        updated.modifiedAt = nowIsoUtc();
    }
    return commitReadingEdit(QStringLiteral("删除标注"), [&](QString *error) {
        if (hasLinked && !m_notes.replace(updated.id, updated, error))
            return false;
        return view()->annotations()->removeById(annotationId);
    }, errorMessage);
}

bool DocumentSession::linkNoteToAnnotation(const QString &noteId, const QString &annotationId,
                                           QString *errorMessage)
{
    const Note *existing = m_notes.find(noteId);
    if (!existing) {
        if (errorMessage)
            *errorMessage = QStringLiteral("笔记不存在。");
        return false;
    }
    if (!view() || !view()->annotations()->find(annotationId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("关联的标注不存在。");
        return false;
    }
    const Note *other = m_notes.forAnnotation(annotationId);
    if (other && other->id != noteId) {
        if (errorMessage)
            *errorMessage = QStringLiteral("该标注已经有关联笔记，不能隐式覆盖。");
        return false;
    }
    Note updated = *existing;
    updated.annotationId = annotationId;
    updated.modifiedAt = nowIsoUtc();
    return commitReadingEdit(QStringLiteral("关联笔记"), [&](QString *error) {
        return m_notes.replace(noteId, updated, error);
    }, errorMessage);
}

bool DocumentSession::unlinkNote(const QString &noteId, QString *errorMessage)
{
    const Note *existing = m_notes.find(noteId);
    if (!existing) {
        if (errorMessage)
            *errorMessage = QStringLiteral("笔记不存在。");
        return false;
    }
    const QString annotationId = existing->annotationId;
    Note updated = *existing;
    updated.annotationId.clear();
    updated.modifiedAt = nowIsoUtc();
    return commitReadingEdit(QStringLiteral("取消笔记关联"), [&](QString *error) {
        if (!m_notes.replace(noteId, updated, error))
            return false;
        if (!annotationId.isEmpty()) {
            const Annotation *annotation = view()->annotations()->find(annotationId);
            if (annotation && annotation->type == AnnotationType::NotePoint)
                view()->annotations()->removeById(annotationId);
        }
        return true;
    }, errorMessage);
}

bool DocumentSession::createGroup(const QString &parentGroupId, const QString &title,
                                  QString *groupId, QString *errorMessage)
{
    if (!parentGroupId.isEmpty() && !m_noteTree.isGroup(parentGroupId)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("目标分组成员无效，请重新选择分组。");
        return false;
    }
    QString newId;
    if (!commitReadingEdit(QStringLiteral("新建分组"), [&](QString *error) {
            return m_noteTree.addGroup(parentGroupId, title, &newId, error);
        }, errorMessage)) {
        return false;
    }
    if (groupId)
        *groupId = newId;
    return true;
}

bool DocumentSession::renameGroup(const QString &groupId, const QString &title, QString *errorMessage)
{
    return commitReadingEdit(QStringLiteral("重命名分组"), [&](QString *error) {
        return m_noteTree.renameNode(groupId, title, error);
    }, errorMessage);
}

bool DocumentSession::moveTreeNode(const QString &nodeId, const QString &newParentId,
                                   QString *errorMessage)
{
    return commitReadingEdit(QStringLiteral("移动笔记"), [&](QString *error) {
        return m_noteTree.moveNode(nodeId, newParentId, error);
    }, errorMessage);
}

bool DocumentSession::moveTreeNodeUp(const QString &nodeId, QString *errorMessage)
{
    return commitReadingEdit(QStringLiteral("上移笔记"), [&](QString *error) {
        return m_noteTree.moveUp(nodeId, error);
    }, errorMessage);
}

bool DocumentSession::moveTreeNodeDown(const QString &nodeId, QString *errorMessage)
{
    return commitReadingEdit(QStringLiteral("下移笔记"), [&](QString *error) {
        return m_noteTree.moveDown(nodeId, error);
    }, errorMessage);
}

bool DocumentSession::deleteGroup(const QString &groupId, bool promoteChildren, QString *errorMessage)
{
    return commitReadingEdit(QStringLiteral("删除分组"), [&](QString *error) {
        return m_noteTree.removeNode(groupId, promoteChildren, error);
    }, errorMessage);
}

bool DocumentSession::writeAsset(const QString &prefix, const QString &extension,
                                 const QByteArray &bytes, QString *relativePath,
                                 QString *errorMessage)
{
    const QString directory = assetsDirectory();
    if (!ensureDirectory(directory, errorMessage))
        return false;
    QString suffix = extension.trimmed().toLower();
    while (suffix.startsWith(QLatin1Char('.')))
        suffix.remove(0, 1);
    if (suffix.isEmpty())
        suffix = QStringLiteral("bin");

    QString fileName;
    QString absolute;
    for (int attempt = 0; attempt < 100; ++attempt) {
        fileName = QStringLiteral("%1-%2.%3")
                       .arg(prefix,
                            newStableId().left(8),
                            suffix);
        absolute = QDir(directory).filePath(fileName);
        if (!QFileInfo::exists(absolute))
            break;
    }
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(bytes) != bytes.size()
        || !file.commit()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("无法写入附件：%1").arg(absolute);
        return false;
    }
    if (relativePath)
        *relativePath = QStringLiteral("assets/%1").arg(fileName);
    return true;
}

QByteArray DocumentSession::annotationsFingerprint() const
{
    const QString path = QDir(bookDirectory()).filePath(QStringLiteral("annotations.json"));
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return QByteArray();
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

} // namespace pano
