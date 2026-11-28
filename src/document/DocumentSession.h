#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointF>
#include <QString>

#include "library/LibraryTypes.h"
#include "document/BookmarkStore.h"

class QTimer;

namespace pano {

class LibraryManager;
class PdfView;
class SettingsManager;
class ToolPresets;
class ReaderWorkspace;

// One open book: owns the reader widget, the annotation document and the
// debounced persistence policy. A session exists once per book and is flushed
// before its tab is destroyed or the library is switched.
class DocumentSession : public QObject
{
    Q_OBJECT

public:
    DocumentSession(LibraryManager *manager, const BookInfo &book, SettingsManager *settings,
                    QObject *parent = nullptr);
    ~DocumentSession() override;

    bool load(QString *errorMessage);
    bool saveNow(QString *errorMessage = nullptr);
    void flush();

    bool isDirty() const { return m_dirty; }
    bool isLoaded() const { return m_loaded; }
    QString bookId() const { return m_book.id; }
    QString title() const { return m_book.title; }
    BookInfo book() const { return m_book; }
    PdfView *view() const;
    ReaderWorkspace *workspace() const { return m_workspace; }
    SettingsManager *settings() const { return m_settings; }
    ToolPresets *toolPresets() const;
    BookmarkStore *bookmarks() { return &m_bookmarkStore; }
    bool saveBookmarks(QString *errorMessage = nullptr);
    QString assetsDirectory() const;
    QString bookDirectory() const;

    bool writeAsset(const QString &prefix, const QString &extension, const QByteArray &bytes,
                    QString *relativePath, QString *errorMessage);

    ReadingState readingState() const;
    void setZoomMode(ZoomMode mode);
    void setAutosaveSeconds(int seconds);
    ZoomMode zoomMode() const { return m_zoomMode; }
    void noteActivity();

signals:
    void dirtyChanged(bool dirty);
    void titleChanged(const QString &title);
    void statusMessage(const QString &message);
    void saveFailed(const QString &message);

private slots:
    void markDirty();
    void scheduleAutosave();
    void autosave();
    void onPageChanged(int pageIndex);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int degrees);
    void scheduleReadingSave();

private:
    void captureReadingState();
    void saveReadingState();
    QByteArray annotationsFingerprint() const;


    LibraryManager *m_manager = nullptr;
    SettingsManager *m_settings = nullptr;
    BookInfo m_book;
    ReaderWorkspace *m_workspace = nullptr;
    QTimer *m_autosaveTimer = nullptr;
    QTimer *m_maxTimer = nullptr;
    QTimer *m_readingTimer = nullptr;
    bool m_loaded = false;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_autosaveActive = false;
    ZoomMode m_zoomMode = ZoomMode::Fixed;
    int m_autosaveDelayMs = 2000;
    QByteArray m_annotationsFingerprint;
    BookmarkStore m_bookmarkStore;
};

} // namespace pano
