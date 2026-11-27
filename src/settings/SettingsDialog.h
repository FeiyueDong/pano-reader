#pragma once

#include <QDialog>

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

signals:
    void annotationDefaultsChanged(const AnnotationStyle &style);

private slots:
    void applyLiveSettings();
    void rejectSettings();
    void chooseLibraryFolder();

private:
    QWidget *buildAppearanceTab();
    QWidget *buildDataTab();
    QWidget *buildReadingTab();
    QWidget *buildAnnotationTab();
    QWidget *buildShortcutsTab();
    void loadValues();
    void applyFont();

    SettingsManager *m_settings = nullptr;
    ThemeManager *m_themes = nullptr;
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QSpinBox *m_fontSpin = nullptr;
    QLabel *m_libraryPath = nullptr;
    QCheckBox *m_restoreTabs = nullptr;
    QCheckBox *m_restoreLibrary = nullptr;
    QSpinBox *m_autosaveSpin = nullptr;
    QPushButton *m_strokeButton = nullptr;
    QDoubleSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_opacitySpin = nullptr;
    QComboBox *m_blendCombo = nullptr;
    QString m_originalThemeKey;
    int m_originalFontSize = 10;
    AppSettings m_originalSettings;
    AnnotationStyle m_style;
};

} // namespace pano
