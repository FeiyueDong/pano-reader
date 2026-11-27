#pragma once

#include <QHash>
#include <QWidget>

#include "annotation/Annotation.h"
#include "reader/PdfView.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeView;
class QSplitter;
class QPdfBookmarkModel;

namespace pano {

class DocumentSession;
class NotePanel;
class PageRenderer;
class RegionCaptureOverlay;

// Reader tab: PDF canvas, navigation/annotation docks, note editor and the
// two horizontal toolbars required by the Phase 1.1 interaction design.
class ReaderWorkspace : public QWidget
{
    Q_OBJECT

public:
    explicit ReaderWorkspace(DocumentSession *session, QWidget *parent = nullptr);
    ~ReaderWorkspace() override;

    PdfView *view() const { return m_view; }
    NotePanel *notePanel() const { return m_notePanel; }
    DocumentSession *session() const { return m_session; }

    bool isEditingNote() const;
    void beginNoteEdit();
    void commitNoteEdit();
    void cancelNoteEdit();
    void commitPendingNote();
    void insertImageIntoNote();
    void startRegionCapture();
    void refreshAll();
    void reloadToolPreset();

signals:
    void statusMessage(const QString &message);

private slots:
    void onAnnotationsChanged();
    void onSelectionChanged(const QString &annotationId);
    void onNoteRequested(const QString &annotationId);
    void onNoteDeleteRequested(const QString &annotationId);
    void onNoteListItemActivated(QListWidgetItem *item);
    void onToolChanged(PdfView::Tool tool);
    void chooseStrokeColor();
    void chooseFillColor();
    void applyStyleFromControls();
    void updateStylePanel();
    void updateNotePanel();
    void updateDetailPanel();
    void finishCapture(const QRect &rect);
    void refreshOutline();
    void refreshThumbnails();
    void requestNearbyThumbnails();
    void onThumbnailRendered(int page, const QSize &size, const QImage &image, quint64 requestId);
    void onThumbnailActivated(QListWidgetItem *item);
    void onOutlineActivated(const QModelIndex &index);
    void onPageChanged(int page);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int degrees);
    void goToPageFromSpin(int oneBasedPage);
    void zoomEdited();
    void saveCurrent();
    void deleteSelected();
    void updateBottomStatus();
    void scheduleStyleApply();
    void schedulePresetSave();
    void updatePanelVisibility();
    void persistPanelState();

private:
    void buildUi();
    QToolBar *buildPrimaryToolBar();
    QToolBar *buildContextToolBar();
    QWidget *buildBottomBar();
    QWidget *buildLeftDock();
    QWidget *buildRightDock();
    QWidget *buildDetailPane();
    void refreshAnnotationList();
    QListWidgetItem *makeAnnotationItem(const Annotation &annotation) const;
    const Annotation *currentAnnotation() const;
    AnnotationStyle styleFromControls() const;
    void setControlsFromStyle(const AnnotationStyle &style);
    void updateColorButton(QPushButton *button, const QColor &color);
    void applyToolPreset(PdfView::Tool tool);
    QString presetIdFor(PdfView::Tool tool) const;
    void updateToolActions();
    void updateContextToolBar();
    QString noteTitleFor(const Annotation &annotation) const;
    void markNoteDirty();
    void stopNoteTimers();
    void setEditingAnnotation(const QString &id);
    void showStatus(const QString &text);

    DocumentSession *m_session = nullptr;
    PdfView *m_view = nullptr;
    NotePanel *m_notePanel = nullptr;
    RegionCaptureOverlay *m_captureOverlay = nullptr;

    QToolBar *m_primaryToolBar = nullptr;
    QToolBar *m_contextToolBar = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_leftDock = nullptr;
    QWidget *m_rightDock = nullptr;
    QWidget *m_leftPanel = nullptr;
    QWidget *m_rightPanel = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_rightTabs = nullptr;

    QTreeView *m_outlineView = nullptr;
    QPdfBookmarkModel *m_outlineModel = nullptr;
    QLabel *m_outlineEmpty = nullptr;
    QListWidget *m_thumbnailList = nullptr;
    PageRenderer *m_thumbRenderer = nullptr;
    QTimer *m_thumbRefreshTimer = nullptr;
    QHash<int, quint64> m_thumbRequests;
    QListWidget *m_annotationList = nullptr;
    QComboBox *m_annotationFilter = nullptr;
    QCheckBox *m_noteOnlyCheck = nullptr;

    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailMeta = nullptr;
    QLabel *m_detailGeometry = nullptr;
    QLabel *m_detailPlaceholder = nullptr;
    QWidget *m_detailFields = nullptr;

    QListWidget *m_toolList = nullptr; // unused placeholder for legacy code
    QToolButton *m_leftToggle = nullptr;
    QToolButton *m_rightToggle = nullptr;

    QSpinBox *m_pageSpin = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_saveStateLabel = nullptr;

    QAction *m_saveAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_captureAction = nullptr;
    QHash<QAction *, PdfView::Tool> m_toolActions;

    QWidget *m_contextWidget = nullptr;
    QLabel *m_contextHint = nullptr;
    QWidget *m_colorWidget = nullptr;
    QWidget *m_fillWidget = nullptr;
    QWidget *m_widthWidget = nullptr;
    QWidget *m_opacityWidget = nullptr;
    QWidget *m_blendWidget = nullptr;
    QPushButton *m_colorButton = nullptr;
    QPushButton *m_fillButton = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QComboBox *m_blendCombo = nullptr;

    QTimer *m_styleTimer = nullptr;
    QTimer *m_presetTimer = nullptr;
    QTimer *m_noteTimer = nullptr;
    QTimer *m_noteMaxTimer = nullptr;
    QTimer *m_panelSaveTimer = nullptr;
    QTimer *m_statusTimer = nullptr;

    QString m_editingId;
    bool m_applyingStyle = false;
    bool m_applyingSelection = false;
    bool m_syncingControls = false;
    bool m_syncingTool = false;
    bool m_noteDirty = false;
    bool m_noteMaxStarted = false;
};

} // namespace pano
