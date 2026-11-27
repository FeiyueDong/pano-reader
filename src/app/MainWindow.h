#pragma once

#include <QColor>
#include <QMainWindow>
#include <QVector>

#include <memory>

#include "annotation/Annotation.h"
#include "reader/PdfView.h"

class QAction;
class QActionGroup;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolBar;

namespace pano {

class AnnotationStore;
class NotePanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openDocument(const QString &filePath);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void openFile();
    void saveDocument();
    void closeDocument();
    void requestOpenFile(const QString &filePath);

    void onDocumentChanged();
    void onAnnotationsChanged();
    void onSelectionChanged(const QString &annotationId);
    void onNoteRequested(const QString &annotationId);
    void onPageChanged(int pageIndex);
    void onZoomChanged(qreal zoom);
    void onRotationChanged(int degrees);
    void onToolChanged(PdfView::Tool tool);
    void onStatusMessage(const QString &message);
    void onNoteListItemActivated(QListWidgetItem *item);

    void beginNoteEdit();
    void commitNoteEdit();
    void cancelNoteEdit();
    void insertImageIntoNote();
    void startRegionCapture();
    void applyStyleToSelected();
    void chooseStrokeColor();
    void chooseFillColor();
    void styleControlsChanged();
    void showDiagnostics();
    void showShortcuts();

private:
    void buildActions();
    void buildToolBars();
    void buildDocks();
    void buildStatusBar();
    void buildMenus();
    void connectSignals();

    void updateWindowTitle();
    void updateActions();
    void updatePropertyPanel();
    void updateNotePanel();
    void updateCacheLabel();
    void refreshNoteList();
    void setDirty(bool dirty);
    bool confirmSaveIfNeeded();
    bool saveToStore(QString *error);
    void unloadDocument();
    void resetDocumentUi();
    void finishCapture(const QRect &rect);
    void insertImageFile(const QString &path);

    AnnotationStyle styleFromControls() const;
    void setControlsFromStyle(const AnnotationStyle &style);
    void updateColorButton(QPushButton *button, const QColor &color);
    const Annotation *currentAnnotation() const;
    QString baseDirectory() const;
    QString noteTitleFor(const Annotation &annotation) const;

    PdfView *m_view = nullptr;
    NotePanel *m_notePanel = nullptr;
    std::unique_ptr<AnnotationStore> m_store;

    QDockWidget *m_listDock = nullptr;
    QDockWidget *m_noteDock = nullptr;
    QDockWidget *m_propertyDock = nullptr;
    QToolBar *m_mainToolBar = nullptr;
    QToolBar *m_toolBar = nullptr;

    QListWidget *m_noteList = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QPushButton *m_strokeButton = nullptr;
    QPushButton *m_fillButton = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QComboBox *m_blendCombo = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QPushButton *m_applyStyleButton = nullptr;

    QSpinBox *m_pageSpin = nullptr;
    QLabel *m_pageCountLabel = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    QLabel *m_statusPage = nullptr;
    QLabel *m_statusZoom = nullptr;
    QLabel *m_statusCache = nullptr;

    QActionGroup *m_toolGroup = nullptr;
    QVector<QAction *> m_toolActions;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_closeAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_editNoteAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fitWidthAction = nullptr;
    QAction *m_fitPageAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QAction *m_rotateCwAction = nullptr;
    QAction *m_rotateCcwAction = nullptr;
    QAction *m_captureAction = nullptr;
    QAction *m_diagnosticsAction = nullptr;
    QAction *m_shortcutsAction = nullptr;

    QColor m_strokeColor = QColor(QStringLiteral("#f2b705"));
    QColor m_fillColor;

    QString m_noteId;
    bool m_storeReady = false;
    bool m_dirty = false;
    bool m_loading = false;
    bool m_syncingUi = false;
};

} // namespace pano
