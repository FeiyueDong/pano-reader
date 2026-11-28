#include "document/DocumentSession.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDir>
#include <QtGlobal>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include "app/ReaderWorkspace.h"
#include "library/LibraryManager.h"
#include "settings/SettingsManager.h"
#include "reader/PageLayout.h"
#include "reader/PdfView.h"
#include "storage/JsonUtils.h"
#include "storage/LibraryStore.h"

namespace pano {

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
    if (!m_manager->verifySource(m_book.id, errorMessage))
        return false;

    m_loading = true;
    if (!view()->openFile(m_book.pdfPath, errorMessage)) {
        m_loading = false;
        return false;
    }

    QJsonArray annotations;
    if (!m_manager->store()->readAnnotations(m_book.directory, m_book.id, &annotations, errorMessage)) {
        view()->closeDocument();
        m_loading = false;
        return false;
    }

    m_annotationsFingerprint = annotationsFingerprint();
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
    view()->annotations()->setAnnotations(parsed);
    view()->undoStack()->clear();
    if (m_settings)
        view()->setWheelTurnsPage(m_settings->settings().wheelTurnsPage);

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
        m_workspace->commitPendingNote();
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
    if (!m_manager->store()->writeAnnotations(m_book.directory, m_book.id, annotations, errorMessage)) {
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
