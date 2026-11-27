#pragma once

#include <QWidget>

#include "annotation/Annotation.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSlider;
class QTabWidget;

namespace pano {

class DocumentSession;
class NotePanel;
class PdfView;
class RegionCaptureOverlay;

// Reader tab: PDF canvas, annotation list/properties and the note editor.
// All persistent document state lives in DocumentSession; this class is the
// view/controller for one open book.
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
    void insertImageIntoNote();
    void startRegionCapture();
    void refreshAll();

signals:
    void statusMessage(const QString &message);

private slots:
    void onAnnotationsChanged();
    void onSelectionChanged(const QString &annotationId);
    void onNoteRequested(const QString &annotationId);
    void onNoteDeleteRequested(const QString &annotationId);
    void onNoteListItemActivated(QListWidgetItem *item);
    void chooseStrokeColor();
    void chooseFillColor();
    void applyStyle();
    void updatePropertyPanel();
    void updateNotePanel();
    void finishCapture(const QRect &rect);

private:
    void buildUi();
    QWidget *buildPropertyPanel();
    void refreshAnnotationList();
    void refreshSelectionLabel();
    const Annotation *currentAnnotation() const;
    AnnotationStyle styleFromControls() const;
    void setControlsFromStyle(const AnnotationStyle &style);
    void updateColorButton(QPushButton *button, const QColor &color);
    QString noteTitleFor(const Annotation &annotation) const;

    DocumentSession *m_session = nullptr;
    PdfView *m_view = nullptr;
    NotePanel *m_notePanel = nullptr;
    RegionCaptureOverlay *m_captureOverlay = nullptr;

    QListWidget *m_annotationList = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QPushButton *m_strokeButton = nullptr;
    QPushButton *m_fillButton = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QComboBox *m_blendCombo = nullptr;
    QPushButton *m_applyStyleButton = nullptr;
};

} // namespace pano
