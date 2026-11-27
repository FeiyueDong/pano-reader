#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

#include "library/LibraryTypes.h"

class QTimer;

namespace pano {

class LibraryManager;
class PdfView;
class ReaderWorkspace;

// One open book: owns the reader widget, the annotation document and the
// debounced persistence policy. A session exists once per book and is flushed
// before its tab is destroyed or the library is switched.
class DocumentSession : public QObject
{
    Q_OBJECT

public:
    DocumentSession(LibraryManager *manager, const BookInfo &book, QObject *parent = nullptr);
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

private:
    void captureReadingState();
    void saveReadingState();

    LibraryManager *m_manager = nullptr;
    BookInfo m_book;
    ReaderWorkspace *m_workspace = nullptr;
    QTimer *m_autosaveTimer = nullptr;
    QTimer *m_maxTimer = nullptr;
    bool m_loaded = false;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_autosaveActive = false;
    ZoomMode m_zoomMode = ZoomMode::Fixed;
    int m_autosaveDelayMs = 2000;
};

} // namespace pano
