#include "settings/SettingsDialog.h"

#include "reader/PageLayout.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "settings/SettingsManager.h"
#include "settings/ThemeManager.h"

namespace pano {

SettingsDialog::SettingsDialog(SettingsManager *settings, ThemeManager *themes, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_themes(themes)
{
    setWindowTitle(tr("设置"));
    resize(620, 520);

    auto *layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildAppearanceTab(), tr("界面"));
    m_tabs->addTab(buildDataTab(), tr("数据与存储"));
    m_tabs->addTab(buildReadingTab(), tr("阅读"));
    m_tabs->addTab(buildAnnotationTab(), tr("标注"));
    m_tabs->addTab(buildShortcutsTab(), tr("快捷键"));
    layout->addWidget(m_tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
                                         this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        commitSettings();
        if (!flushPresetEdits())
            return;
        QString error;
        if (!m_settings->save(&error)) {
            QMessageBox::warning(this, tr("保存设置失败"), error);
            return;
        }
        m_appliedSettings = m_settings->settings();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::rejectSettings);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        commitSettings();
        if (!flushPresetEdits())
            return;
        QString error;
        if (!m_settings->save(&error))
            QMessageBox::warning(this, tr("保存设置失败"), error);
        else
            m_appliedSettings = m_settings->settings();
    });
    layout->addWidget(buttons);

    m_loading = true;
    loadValues();
    m_loading = false;
    if (m_settings)
        m_appliedSettings = m_settings->settings();
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *box = new QGroupBox(tr("外观"), widget);
    auto *form = new QFormLayout(box);

    m_themeCombo = new QComboBox(box);
    for (const ThemeInfo &theme : ThemeManager::themes())
        m_themeCombo->addItem(theme.name, theme.key);
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, &SettingsDialog::applyLiveSettings);
    form->addRow(tr("主题"), m_themeCombo);

    m_fontSpin = new QSpinBox(box);
    m_fontSpin->setRange(8, 18);
    m_fontSpin->setSuffix(tr(" pt"));
    connect(m_fontSpin, &QSpinBox::valueChanged, this, &SettingsDialog::applyLiveSettings);
    form->addRow(tr("界面字号"), m_fontSpin);

    layout->addWidget(box);
    auto *note = new QLabel(tr("主题和字号会立即应用到所有已打开的阅读标签；PDF 页面和已有标注颜色不受影响。"),
                            widget);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return widget;
}

QWidget *SettingsDialog::buildDataTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *box = new QGroupBox(tr("书库"), widget);
    auto *form = new QFormLayout(box);
    m_libraryPath = new QLabel(box);
    m_libraryPath->setWordWrap(true);
    m_libraryPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("当前位置"), m_libraryPath);

    auto *openFolder = new QPushButton(tr("打开书库目录"), box);
    connect(openFolder, &QPushButton::clicked, this, &SettingsDialog::chooseLibraryFolder);
    form->addRow(QString(), openFolder);

    m_importModeCombo = new QComboBox(box);
    m_importModeCombo->addItem(tr("复制到书库"), static_cast<int>(ImportMode::Copy));
    m_importModeCombo->addItem(tr("引用原文件（不复制）"), static_cast<int>(ImportMode::Reference));
    form->addRow(tr("默认导入方式"), m_importModeCombo);
    layout->addWidget(box);

    auto *note = new QLabel(
        tr("书库中的 library.json、书籍目录、书架和阅读状态是正式数据；缓存与日志不写入书库。\n"
           "切换书库请使用“文件 → 打开书库”。"),
        widget);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return widget;
}

QWidget *SettingsDialog::buildReadingTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *box = new QGroupBox(tr("启动与保存"), widget);
    auto *form = new QFormLayout(box);

    m_restoreLibrary = new QCheckBox(tr("启动时恢复上次打开的书库"), box);
    form->addRow(m_restoreLibrary);
    m_restoreTabs = new QCheckBox(tr("恢复上次打开的阅读标签"), box);
    form->addRow(m_restoreTabs);

    m_autosaveSpin = new QSpinBox(box);
    m_autosaveSpin->setRange(1, 30);
    m_autosaveSpin->setSuffix(tr(" 秒"));
    form->addRow(tr("标注自动保存延迟"), m_autosaveSpin);

    m_layoutCombo = new QComboBox(box);
    m_layoutCombo->addItem(tr("单页"), static_cast<int>(PageLayoutMode::SinglePage));
    m_layoutCombo->addItem(tr("连续单页"), static_cast<int>(PageLayoutMode::ContinuousSingle));
    m_layoutCombo->addItem(tr("双页"), static_cast<int>(PageLayoutMode::FacingPages));
    m_layoutCombo->addItem(tr("连续双页"), static_cast<int>(PageLayoutMode::ContinuousFacing));
    form->addRow(tr("默认页面布局"), m_layoutCombo);
    m_defaultZoomCombo = new QComboBox(box);
    m_defaultZoomCombo->addItem(tr("适合页面"), static_cast<int>(ZoomMode::FitPage));
    m_defaultZoomCombo->addItem(tr("适合宽度"), static_cast<int>(ZoomMode::FitWidth));
    m_defaultZoomCombo->addItem(tr("固定比例"), static_cast<int>(ZoomMode::Fixed));
    form->addRow(tr("默认缩放模式"), m_defaultZoomCombo);
    m_firstPageCheck = new QCheckBox(tr("双页布局首页单独显示"), box);
    form->addRow(m_firstPageCheck);
    m_pageGapSpin = new QSpinBox(box);
    m_pageGapSpin->setRange(8, 64);
    m_pageGapSpin->setSuffix(tr(" px"));
    form->addRow(tr("页面间距"), m_pageGapSpin);
    m_wheelTurnsCheck = new QCheckBox(tr("滚动到页边界后滚轮翻页"), box);
    form->addRow(m_wheelTurnsCheck);

    layout->addWidget(box);
    auto *note = new QLabel(tr("自动保存最长等待 30 秒；切换标签、关闭窗口和切换书库时会立即保存。"), widget);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return widget;
}

QWidget *SettingsDialog::buildAnnotationTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *box = new QGroupBox(tr("工具预设"), widget);
    auto *form = new QFormLayout(box);

    m_presetToolCombo = new QComboBox(box);
    for (const QString &toolId : ToolPresets::toolIds()) {
        if (toolId == QStringLiteral("select") || toolId == QStringLiteral("text")
            || toolId == QStringLiteral("eraser"))
            continue;
        m_presetToolCombo->addItem(ToolPresets::displayName(toolId), toolId);
    }
    connect(m_presetToolCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_loading || index < 0)
            return;
        storePresetEditor();
        loadPresetEditor(m_presetToolCombo->itemData(index).toString());
    });
    form->addRow(tr("工具"), m_presetToolCombo);

    m_strokeButton = new QPushButton(tr("选择颜色"), box);
    connect(m_strokeButton, &QPushButton::clicked, this, [this]() {
        const QColor initial = m_style.strokeColor;
        const QColor color = QColorDialog::getColor(initial, this, tr("预设描边颜色"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_style.strokeColor = color;
        m_strokeButton->setText(color.name(QColor::HexArgb));
        m_strokeButton->setStyleSheet(
            QStringLiteral("background:%1; color:%2;")
                .arg(QColor(color.red(), color.green(), color.blue()).name(),
                     color.lightness() > 150 ? QStringLiteral("#20242b") : QStringLiteral("#ffffff")));
    });
    form->addRow(tr("描边"), m_strokeButton);

    m_presetStrokeCheck = new QCheckBox(tr("显示描边"), box);
    m_presetStrokeCheck->setChecked(true);
    form->addRow(QString(), m_presetStrokeCheck);

    m_widthSpin = new QDoubleSpinBox(box);
    m_widthSpin->setRange(0.25, 20.0);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setSuffix(tr(" pt"));
    form->addRow(tr("线宽"), m_widthSpin);

    m_presetLineStyleCombo = new QComboBox(box);
    for (LineStyle style : {LineStyle::Solid, LineStyle::Dash, LineStyle::Dot,
                            LineStyle::DashDot, LineStyle::DashDotDot})
        m_presetLineStyleCombo->addItem(lineStyleDisplayName(style), static_cast<int>(style));
    form->addRow(tr("线型"), m_presetLineStyleCombo);

    auto *fillRow = new QWidget(box);
    auto *fillLayout = new QHBoxLayout(fillRow);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    m_presetFillCheck = new QCheckBox(tr("填充"), fillRow);
    m_presetFillButton = new QPushButton(fillRow);
    m_presetFillButton->setFixedSize(40, 22);
    fillLayout->addWidget(m_presetFillCheck);
    fillLayout->addWidget(m_presetFillButton);
    connect(m_presetFillCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_currentPresetTool.isEmpty()) {
            m_style.fillColor = checked ? m_style.fillColor : QColor();
            storePresetEditor();
        }
    });
    connect(m_presetFillButton, &QPushButton::clicked, this, [this]() {
        QColor initial = m_style.fillColor.isValid() ? m_style.fillColor : QColor(QStringLiteral("#FFE066"));
        const QColor color = QColorDialog::getColor(initial, this, tr("选择填充颜色"),
                                                    QColorDialog::ShowAlphaChannel);
        if (!color.isValid())
            return;
        m_style.fillColor = color;
        m_presetFillCheck->setChecked(true);
        storePresetEditor();
        loadPresetEditor(m_currentPresetTool);
    });
    form->addRow(tr("填充"), fillRow);

    m_opacitySpin = new QSpinBox(box);
    m_opacitySpin->setRange(5, 100);
    m_opacitySpin->setSuffix(QStringLiteral("%"));
    form->addRow(tr("不透明度"), m_opacitySpin);

    m_blendCombo = new QComboBox(box);
    m_blendCombo->addItem(tr("正常"), static_cast<int>(BlendMode::Normal));
    m_blendCombo->addItem(tr("正片叠底"), static_cast<int>(BlendMode::Multiply));
    form->addRow(tr("混合模式"), m_blendCombo);

    m_presetResetButton = new QPushButton(tr("恢复该工具默认值"), box);
    connect(m_presetResetButton, &QPushButton::clicked, this, [this]() {
        const QString toolId = m_presetToolCombo->currentData().toString();
        if (toolId.isEmpty())
            return;
        m_presetEdits[toolId] = ToolPresets::defaultStyle(toolId);
        loadPresetEditor(toolId);
    });
    form->addRow(QString(), m_presetResetButton);

    layout->addWidget(box);
    auto *note = new QLabel(tr("预设按工具独立保存，新标注使用当前工具预设；已有标注不会被改写。"
                               "阅读器属性栏修改会写回同一份预设。"),
                            widget);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);
    return widget;
}

QWidget *SettingsDialog::buildShortcutsTab()
{
    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    auto *table = new QTableWidget(widget);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("操作"), tr("快捷键")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);

    const QList<QPair<QString, QString>> shortcuts{
        {tr("选择 / 文本选择"), QStringLiteral("V / T")},
        {tr("高亮 / 下划线 / 删除线"), QStringLiteral("H / U / S")},
        {tr("直线 / 矩形 / 椭圆"), QStringLiteral("L / R / O")},
        {tr("铅笔 / 荧光笔"), QStringLiteral("P / M")},
        {tr("橡皮 / 笔记"), QStringLiteral("E / N")},
        {tr("撤销 / 重做"), QStringLiteral("Ctrl+Z / Ctrl+Shift+Z")},
        {tr("删除选中标注"), QStringLiteral("Delete")},
        {tr("复制选中文本"), QStringLiteral("Ctrl+C")},
        {tr("放大 / 缩小"), QStringLiteral("Ctrl++ / Ctrl+-")},
        {tr("适合宽度 / 适合页面"), QStringLiteral("Ctrl+1 / Ctrl+2")},
        {tr("顺时针旋转"), QStringLiteral("Ctrl+R")},
        {tr("保存标注"), QStringLiteral("Ctrl+S")},
    };
    table->setRowCount(shortcuts.size());
    for (int row = 0; row < shortcuts.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(shortcuts.at(row).first));
        table->setItem(row, 1, new QTableWidgetItem(shortcuts.at(row).second));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);
    return widget;
}

void SettingsDialog::loadValues()
{
    if (!m_settings)
        return;
    const AppSettings &settings = m_settings->settings();
    m_originalSettings = settings;
    m_originalThemeKey = settings.themeKey;
    m_originalFontSize = settings.uiFontSize;
    const int themeIndex = m_themeCombo->findData(settings.themeKey);
    if (themeIndex >= 0)
        m_themeCombo->setCurrentIndex(themeIndex);
    m_fontSpin->setValue(settings.uiFontSize);
    m_libraryPath->setText(settings.lastLibraryPath.isEmpty() ? tr("（未打开书库）")
                                                               : settings.lastLibraryPath);
    m_restoreLibrary->setChecked(settings.restoreLastLibrary);
    m_restoreTabs->setChecked(settings.restoreOpenTabs);
    m_autosaveSpin->setValue(settings.autosaveSeconds);
    if (m_layoutCombo) {
        PageLayoutMode layoutMode = PageLayoutMode::ContinuousSingle;
        pageLayoutModeFromString(settings.defaultLayoutMode, &layoutMode);
        const int index = m_layoutCombo->findData(static_cast<int>(layoutMode));
        if (index >= 0)
            m_layoutCombo->setCurrentIndex(index);
        const int zoomIndex = m_defaultZoomCombo->findData(static_cast<int>(settings.defaultZoomMode));
        if (zoomIndex >= 0)
            m_defaultZoomCombo->setCurrentIndex(zoomIndex);
        m_firstPageCheck->setChecked(settings.firstPageAlone);
        m_pageGapSpin->setValue(settings.pageGap);
        m_wheelTurnsCheck->setChecked(settings.wheelTurnsPage);
    }
    if (m_importModeCombo) {
        const int index = m_importModeCombo->findData(static_cast<int>(settings.defaultImportMode));
        if (index >= 0)
            m_importModeCombo->setCurrentIndex(index);
    }

    m_presetEdits.clear();
    if (m_settings) {
        for (const QString &toolId : ToolPresets::toolIds())
            m_presetEdits.insert(toolId, m_settings->toolPresets()->style(toolId));
    }
    if (m_presetToolCombo && m_presetToolCombo->count() > 0) {
        m_presetToolCombo->setCurrentIndex(0);
        loadPresetEditor(m_presetToolCombo->itemData(0).toString());
    }
}

void SettingsDialog::storePresetEditor()
{
    if (m_currentPresetTool.isEmpty() || !m_widthSpin)
        return;
    AnnotationStyle style = m_style;
    style.strokeWidthPt = m_widthSpin->value();
    style.opacity = m_opacitySpin->value() / 100.0;
    style.blend = static_cast<BlendMode>(m_blendCombo->currentData().toInt());
    if (m_presetStrokeCheck)
        style.strokeEnabled = m_presetStrokeCheck->isChecked();
    if (m_presetLineStyleCombo)
        style.lineStyle = static_cast<LineStyle>(m_presetLineStyleCombo->currentData().toInt());
    if (m_presetFillCheck && !m_presetFillCheck->isChecked())
        style.fillColor = QColor();
    m_presetEdits.insert(m_currentPresetTool, style);
}

void SettingsDialog::loadPresetEditor(const QString &toolId)
{
    if (toolId.isEmpty())
        return;
    m_currentPresetTool = toolId;
    const AnnotationStyle style = m_presetEdits.value(toolId, ToolPresets::defaultStyle(toolId));
    m_style = style;
    m_widthSpin->setValue(style.strokeWidthPt);
    if (m_presetStrokeCheck)
        m_presetStrokeCheck->setChecked(style.strokeEnabled);
    if (m_presetLineStyleCombo) {
        const int lineIndex = m_presetLineStyleCombo->findData(static_cast<int>(style.lineStyle));
        if (lineIndex >= 0)
            m_presetLineStyleCombo->setCurrentIndex(lineIndex);
    }
    if (m_presetFillCheck) {
        m_presetFillCheck->setChecked(style.fillColor.isValid());
        m_presetFillButton->setText(style.fillColor.isValid()
                                        ? style.fillColor.name(QColor::HexArgb)
                                        : tr("无填充"));
    }
    m_opacitySpin->setValue(qRound(style.opacity * 100.0));
    const int blendIndex = m_blendCombo->findData(static_cast<int>(style.blend));
    if (blendIndex >= 0)
        m_blendCombo->setCurrentIndex(blendIndex);
    if (style.strokeColor.isValid()) {
        const QColor color = style.strokeColor;
        m_strokeButton->setText(color.name(QColor::HexArgb));
        m_strokeButton->setStyleSheet(
            QStringLiteral("background:%1; color:%2;")
                .arg(QColor(color.red(), color.green(), color.blue()).name(),
                     color.lightness() > 150 ? QStringLiteral("#20242b") : QStringLiteral("#ffffff")));
    }
}

bool SettingsDialog::flushPresetEdits()
{
    if (!m_settings)
        return true;
    storePresetEditor();
    for (auto it = m_presetEdits.constBegin(); it != m_presetEdits.constEnd(); ++it)
        m_settings->toolPresets()->setStyle(it.key(), it.value());
    QString error;
    if (!m_settings->toolPresets()->save(&error)) {
        QMessageBox::warning(this, tr("保存工具预设失败"), error);
        return false;
    }
    return true;
}

void SettingsDialog::applyLiveSettings()
{
    if (!m_settings || !m_themes || m_loading)
        return;
    const QString themeKey = m_themeCombo->currentData().toString();
    ThemeId id = ThemeId::ModernLight;
    if (!ThemeManager::fromKey(themeKey, &id))
        id = ThemeId::ModernLight;
    m_settings->setThemeKey(themeKey);
    m_settings->setUiFontSize(m_fontSpin->value());
    m_themes->applyAppearance(id, m_fontSpin->value(), qApp);
}

void SettingsDialog::commitSettings()
{
    if (!m_settings || m_loading)
        return;
    applyLiveSettings();
    m_settings->setRestoreLastLibrary(m_restoreLibrary->isChecked());
    m_settings->setRestoreOpenTabs(m_restoreTabs->isChecked());
    m_settings->setAutosaveSeconds(m_autosaveSpin->value());
    if (m_layoutCombo) {
        PageLayoutMode layoutMode = static_cast<PageLayoutMode>(
            m_layoutCombo->currentData().toInt());
        const ZoomMode zoomMode = static_cast<ZoomMode>(m_defaultZoomCombo->currentData().toInt());
        m_settings->setDefaultReadingLayout(pageLayoutModeToString(layoutMode),
                                            zoomMode, m_firstPageCheck->isChecked());
        m_settings->setPageGap(m_pageGapSpin->value());
        m_settings->setWheelTurnsPage(m_wheelTurnsCheck->isChecked());
    }
    if (m_importModeCombo)
        m_settings->setDefaultImportMode(
            static_cast<ImportMode>(m_importModeCombo->currentData().toInt()));
    m_settings->setLastLibraryPath(m_libraryPath->text() == tr("（未打开书库）")
                                       ? QString()
                                       : m_libraryPath->text());
}

void SettingsDialog::applyFont()
{
    if (m_themes)
        m_themes->applyAppearance(m_themes->currentTheme(), m_fontSpin->value(), qApp);
}

void SettingsDialog::reject()
{
    restoreAppliedSettings();
    QDialog::reject();
}

void SettingsDialog::rejectSettings()
{
    reject();
}

void SettingsDialog::restoreAppliedSettings()
{
    if (m_settings) {
        m_settings->setThemeKey(m_appliedSettings.themeKey);
        m_settings->setUiFontSize(m_appliedSettings.uiFontSize);
        m_settings->setRestoreLastLibrary(m_appliedSettings.restoreLastLibrary);
        m_settings->setRestoreOpenTabs(m_appliedSettings.restoreOpenTabs);
        m_settings->setAutosaveSeconds(m_appliedSettings.autosaveSeconds);
        m_settings->setDefaultImportMode(m_appliedSettings.defaultImportMode);
        m_settings->setLastLibraryPath(m_appliedSettings.lastLibraryPath);
        m_settings->setDefaultReadingLayout(m_appliedSettings.defaultLayoutMode,
                                            m_appliedSettings.defaultZoomMode,
                                            m_appliedSettings.firstPageAlone);
        m_settings->setPageGap(m_appliedSettings.pageGap);
        m_settings->setWheelTurnsPage(m_appliedSettings.wheelTurnsPage);
    }
    if (m_themes) {
        ThemeId original = ThemeId::ModernLight;
        if (ThemeManager::fromKey(m_appliedSettings.themeKey, &original))
            m_themes->applyAppearance(original, m_appliedSettings.uiFontSize, qApp);
    }
}

void SettingsDialog::chooseLibraryFolder()
{
    if (!m_settings)
        return;
    const QString path = m_settings->settings().lastLibraryPath;
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

AnnotationStyle SettingsDialog::annotationStyle() const
{
    AnnotationStyle style = m_style;
    style.strokeWidthPt = m_widthSpin->value();
    style.opacity = m_opacitySpin->value() / 100.0;
    style.blend = static_cast<BlendMode>(m_blendCombo->currentData().toInt());
    return style;
}

void SettingsDialog::setAnnotationStyle(const AnnotationStyle &style)
{
    m_style = style;
    if (m_widthSpin) {
        m_widthSpin->setValue(style.strokeWidthPt);
        m_opacitySpin->setValue(qRound(style.opacity * 100.0));
        const int blendIndex = m_blendCombo->findData(static_cast<int>(style.blend));
        if (blendIndex >= 0)
            m_blendCombo->setCurrentIndex(blendIndex);
    }
}

} // namespace pano
