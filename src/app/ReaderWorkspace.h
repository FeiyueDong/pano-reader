#pragma once

#include <QHash>
#include <QList>
#include <QWidget>
#include <QPoint>

#include "annotation/Annotation.h"
#include "reader/PdfView.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QIntValidator;
class QLineEdit;
class QPdfSearchModel;
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
class QTreeWidget;
class QTreeWidgetItem;

namespace pano {

class DocumentSession;
class NoteEditDialog;
class MarkdownPreview;
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

public:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

    PdfView *view() const { return m_view; }
    DocumentSession *session() const { return m_session; }

    bool isEditingNote() const;
    void beginNoteEdit();
    void commitNoteEdit();
    void cancelNoteEdit();
    bool commitPendingNote(QString *errorMessage = nullptr);
    void insertImageIntoNote();
    void startRegionCapture();
    void refreshAll();
    void refreshReadingData();
    void flushPendingStyle();
    void reloadToolPreset();
    void setFullscreenMode(bool fullscreen);
    bool closeTransientUi();
    void toggleSearchBar();
    void openSearchPanel();
    void navigateBack();
    void navigateForward();
    void clearNavigationHistory();
    void addBookmarkHere();

signals:
    void statusMessage(const QString &message);
    void fullscreenRequested();

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
    void onThumbnailFailed(int page, const QSize &size, quint64 requestId);
    void onThumbnailActivated(QListWidgetItem *item);
    void onOutlineActivated(const QModelIndex &index);
    void onPageChanged(int page);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int degrees);
    void goToPageFromSpin(int oneBasedPage);
    void goToPreviousPage();
    void goToNextPage();
    void onLayoutControlsChanged();
    void zoomEdited();
    void saveCurrent();
    void deleteSelected();
    void updateBottomStatus();
    void scheduleStyleApply();
    void schedulePresetSave();
    void updatePanelVisibility();
    void persistPanelState();
    void updateFloatingToolBars();
    void onLayoutModeChanged(PageLayoutMode mode);
    void onFirstPageAloneChanged(bool firstAlone);
    void onNavigated(int pageIndex, const QPointF &pagePoint, int reason);
    void onAboutToNavigate(int fromPage, const QPointF &fromPoint, int targetPage,
                           const QPointF &targetPoint, int reason);
    void runSearch();
    void refreshSearchResults();
    void activateSearchResult(QListWidgetItem *item);
    void nextSearchResult();
    void previousSearchResult();
    void refreshBookmarks();
    void activateBookmark(QListWidgetItem *item);
    void addBookmarkAt(int pageIndex, const QPointF &pagePoint);
    void renameSelectedBookmark();
    void deleteSelectedBookmark();
    void updateHistoryActions();

private:
    void buildUi();
    void positionFullscreenExit();
    void updateLayoutControls();
    void buildFloatingToolBars();
    void applyTextFloatingAction(int action);
    void applyRegionFloatingAction(int action);
    QToolBar *buildPrimaryToolBar();
    QToolBar *buildContextToolBar();
    QWidget *buildBottomBar();
    QWidget *buildSearchBar();
    QWidget *buildBookmarkTab();
    QWidget *buildAnnotationTab();
    QWidget *buildNoteTab();
    QWidget *buildLeftDock();
    QWidget *buildRightDock();
    QWidget *buildDetailPane();
    void refreshAnnotationList();
    void refreshAnnotationPreview();
    void updateThumbnailLayout();
    QSize thumbnailRequestSize(int page) const;
    void showThumbnail(int page, const QImage &image);
    QListWidgetItem *makeAnnotationItem(const Annotation &annotation) const;
    const Annotation *currentAnnotation() const;
    AnnotationStyle styleFromControls() const;
    void setControlsFromStyle(const AnnotationStyle &style);
    void updateColorButton(QPushButton *button, const QColor &color);
    void applyToolPreset(PdfView::Tool tool);
    QString presetIdFor(PdfView::Tool tool) const;
    void updateToolActions();
    void updateContextToolBar();
    void updateContextBarHeight();
    void refreshNoteTree();
    void updateNotePreview();
    void openNoteForAnnotation(const QString &annotationId);
    void openNoteEditor(const QString &noteId);
    void createIndependentNote(const QString &parentGroupId = QString());
    void createGroupUnder(const QString &parentGroupId = QString());
    void renameSelectedNoteNode();
    void moveSelectedNoteNode();
    void deleteSelectedNoteNode();
    void showNoteContextMenu(const QPoint &pos);
    QString selectedNoteNodeId() const;
    QString selectedParentGroupId() const;
    void showStatus(const QString &text);

    DocumentSession *m_session = nullptr;
    PdfView *m_view = nullptr;
    NoteEditDialog *m_noteDialog = nullptr;
    QTreeWidget *m_noteTreeWidget = nullptr;
    MarkdownPreview *m_notePreview = nullptr;
    QLabel *m_noteInfoLabel = nullptr;
    QString m_pendingNoteAnnotationId;
    bool m_pendingNoteAnnotationIsNew = false;
    RegionCaptureOverlay *m_captureOverlay = nullptr;
    QWidget *m_textFloat = nullptr;
    QWidget *m_regionFloat = nullptr;

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
    QSet<int> m_thumbGenerated;
    QSet<int> m_thumbFailed;
    QHash<int, int> m_thumbRetries;
    QListWidget *m_annotationList = nullptr;
    QListWidget *m_bookmarkList = nullptr;
    QPushButton *m_bookmarkAddButton = nullptr;
    QPushButton *m_bookmarkDeleteButton = nullptr;
    QComboBox *m_annotationFilter = nullptr;
    QComboBox *m_annotationSort = nullptr;
    QCheckBox *m_noteOnlyCheck = nullptr;
    QLabel *m_annotationPreview = nullptr;
    QLabel *m_annotationNotShown = nullptr;

    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailMeta = nullptr;
    QLabel *m_detailGeometry = nullptr;
    QLabel *m_detailPlaceholder = nullptr;
    QWidget *m_detailFields = nullptr;

    QListWidget *m_toolList = nullptr; // unused placeholder for legacy code
    QToolButton *m_leftToggle = nullptr;
    QToolButton *m_rightToggle = nullptr;
    QToolButton *m_exitFullscreenButton = nullptr;
    int m_fullscreenAnchorPage = -1;
    QPointF m_fullscreenAnchorPoint;

    QLineEdit *m_pageSpin = nullptr;
    QIntValidator *m_pageValidator = nullptr;
    QLabel *m_pageTotalLabel = nullptr;
    QToolButton *m_prevPageButton = nullptr;
    QToolButton *m_nextPageButton = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QAction *m_singlePageAction = nullptr;
    QAction *m_doublePageAction = nullptr;
    QCheckBox *m_continuousCheck = nullptr;
    QCheckBox *m_firstPageAloneCheck = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_saveStateLabel = nullptr;
    QLabel *m_statusInfoLabel = nullptr;

    QAction *m_saveAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_searchAction = nullptr;
    QAction *m_bookmarkAction = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_fullscreenAction = nullptr;
    QHash<QAction *, PdfView::Tool> m_toolActions;

    QWidget *m_contextWidget = nullptr;
    QLabel *m_contextHint = nullptr;
    QWidget *m_colorWidget = nullptr;
    QWidget *m_fillWidget = nullptr;
    QWidget *m_widthWidget = nullptr;
    QWidget *m_opacityWidget = nullptr;
    QWidget *m_lineStyleWidget = nullptr;
    QWidget *m_blendWidget = nullptr;
    QPushButton *m_colorButton = nullptr;
    QPushButton *m_fillButton = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QComboBox *m_lineStyleCombo = nullptr;
    QCheckBox *m_strokeCheck = nullptr;
    QComboBox *m_blendCombo = nullptr;
    QWidget *m_eraserWidget = nullptr;
    QSpinBox *m_eraserRadiusSpin = nullptr;

    QTimer *m_styleTimer = nullptr;
    QTimer *m_presetTimer = nullptr;
    QTimer *m_panelSaveTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_searchTimer = nullptr;
    QPdfSearchModel *m_searchModel = nullptr;
    QWidget *m_searchBar = nullptr;
    QWidget *m_bottomBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchCount = nullptr;
    QListWidget *m_searchResults = nullptr;
    QHash<int, QVector<QRectF>> m_searchHighlightRects;

    QString m_styleTargetId;
    QString m_styleTargetTool;
    AnnotationStyle m_styleSnapshot;
    bool m_styleSnapshotValid = false;
    bool m_styleTargetObject = false;
    bool m_applyingStyle = false;
    bool m_applyingSelection = false;
    bool m_syncingControls = false;
    bool m_syncingTool = false;
    bool m_fullscreen = false;
    QList<int> m_fullscreenSplitterSizes;
    bool m_fullscreenLeftVisible = true;
    bool m_fullscreenRightVisible = true;
    struct NavPoint { int page = 0; QPointF point; };
    int m_previousLeftTab = 0;
    QVector<NavPoint> m_history;
    int m_historyIndex = -1;
    NavPoint m_pendingHistoryStart;
    bool m_pendingHistoryValid = false;
};

} // namespace pano
