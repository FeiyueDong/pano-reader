#pragma once

#include <QDialog>
#include <QHash>

#include "annotation/Annotation.h"
#include "settings/SettingsManager.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace pano {

class SettingsManager;
class ThemeManager;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    SettingsDialog(SettingsManager *settings, ThemeManager *themes, QWidget *parent = nullptr);

    AnnotationStyle annotationStyle() const;
    void setAnnotationStyle(const AnnotationStyle &style);
    void reject() override;

signals:
    void annotationDefaultsChanged(const AnnotationStyle &style);

private slots:
    void applyLiveSettings();
    void commitSettings();
    void rejectSettings();
    void restoreAppliedSettings();
    void chooseLibraryFolder();

private:
    QWidget *buildAppearanceTab();
    QWidget *buildDataTab();
    QWidget *buildReadingTab();
    QWidget *buildAnnotationTab();
    QWidget *buildShortcutsTab();
    void loadValues();
    void applyFont();
    void loadPresetEditor(const QString &toolId);
    void storePresetEditor();
    bool flushPresetEdits();

    SettingsManager *m_settings = nullptr;
    ThemeManager *m_themes = nullptr;
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QSpinBox *m_fontSpin = nullptr;
    QLabel *m_libraryPath = nullptr;
    QCheckBox *m_restoreTabs = nullptr;
    QCheckBox *m_restoreLibrary = nullptr;
    QSpinBox *m_autosaveSpin = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QComboBox *m_defaultZoomCombo = nullptr;
    QCheckBox *m_firstPageCheck = nullptr;
    QSpinBox *m_pageGapSpin = nullptr;
    QCheckBox *m_wheelTurnsCheck = nullptr;
    QComboBox *m_importModeCombo = nullptr;
    QPushButton *m_strokeButton = nullptr;
    QCheckBox *m_presetStrokeCheck = nullptr;
    QComboBox *m_presetLineStyleCombo = nullptr;
    QCheckBox *m_presetFillCheck = nullptr;
    QPushButton *m_presetFillButton = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_opacitySpin = nullptr;
    QComboBox *m_blendCombo = nullptr;
    QComboBox *m_presetToolCombo = nullptr;
    QPushButton *m_presetResetButton = nullptr;
    QHash<QString, AnnotationStyle> m_presetEdits;
    QString m_currentPresetTool;
    QString m_originalThemeKey;
    int m_originalFontSize = 10;
    AppSettings m_originalSettings;
    AppSettings m_appliedSettings;
    bool m_loading = false;
    AnnotationStyle m_style;
};

} // namespace pano
