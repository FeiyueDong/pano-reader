#include "settings/SettingsDialog.h"

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
        applyLiveSettings();
        QString error;
        if (!m_settings->save(&error)) {
            QMessageBox::warning(this, tr("保存设置失败"), error);
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::rejectSettings);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        applyLiveSettings();
        QString error;
        if (!m_settings->save(&error))
            QMessageBox::warning(this, tr("保存设置失败"), error);
    });
    layout->addWidget(buttons);

    loadValues();
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
    auto *box = new QGroupBox(tr("新标注默认样式"), widget);
    auto *form = new QFormLayout(box);

    m_strokeButton = new QPushButton(tr("选择颜色"), box);
    connect(m_strokeButton, &QPushButton::clicked, this, [this]() {
        const QColor initial = m_style.strokeColor;
        const QColor color = QColorDialog::getColor(initial, this, tr("默认描边颜色"),
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

    m_widthSpin = new QDoubleSpinBox(box);
    m_widthSpin->setRange(0.25, 20.0);
    m_widthSpin->setSingleStep(0.25);
    m_widthSpin->setSuffix(tr(" pt"));
    form->addRow(tr("线宽"), m_widthSpin);

    m_opacitySpin = new QSpinBox(box);
    m_opacitySpin->setRange(5, 100);
    m_opacitySpin->setSuffix(QStringLiteral("%"));
    form->addRow(tr("不透明度"), m_opacitySpin);

    m_blendCombo = new QComboBox(box);
    m_blendCombo->addItem(tr("正常"), static_cast<int>(BlendMode::Normal));
    m_blendCombo->addItem(tr("正片叠底"), static_cast<int>(BlendMode::Multiply));
    form->addRow(tr("混合模式"), m_blendCombo);

    layout->addWidget(box);
    auto *note = new QLabel(tr("该样式作为新建标注的初始值；阅读标签中的属性面板仍可随时覆盖。"), widget);
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

    m_widthSpin->setValue(m_style.strokeWidthPt);
    m_opacitySpin->setValue(qRound(m_style.opacity * 100.0));
    const int blendIndex = m_blendCombo->findData(static_cast<int>(m_style.blend));
    if (blendIndex >= 0)
        m_blendCombo->setCurrentIndex(blendIndex);
    if (m_style.strokeColor.isValid()) {
        const QColor color = m_style.strokeColor;
        m_strokeButton->setText(color.name(QColor::HexArgb));
        m_strokeButton->setStyleSheet(
            QStringLiteral("background:%1; color:%2;")
                .arg(QColor(color.red(), color.green(), color.blue()).name(),
                     color.lightness() > 150 ? QStringLiteral("#20242b") : QStringLiteral("#ffffff")));
    }
}

void SettingsDialog::applyLiveSettings()
{
    if (!m_settings || !m_themes)
        return;
    const QString themeKey = m_themeCombo->currentData().toString();
    ThemeId id = ThemeId::ModernLight;
    if (ThemeManager::fromKey(themeKey, &id))
        m_themes->setCurrentTheme(id, qApp);
    m_settings->setThemeKey(themeKey);
    m_settings->setUiFontSize(m_fontSpin->value());
    m_settings->setRestoreLastLibrary(m_restoreLibrary->isChecked());
    m_settings->setRestoreOpenTabs(m_restoreTabs->isChecked());
    m_settings->setAutosaveSeconds(m_autosaveSpin->value());
    m_settings->setLastLibraryPath(m_libraryPath->text() == tr("（未打开书库）")
                                       ? QString()
                                       : m_libraryPath->text());
    applyFont();
}

void SettingsDialog::applyFont()
{
    QFont font = QApplication::font();
    font.setPointSize(m_fontSpin->value());
    QApplication::setFont(font);
}

void SettingsDialog::rejectSettings()
{
    if (m_settings) {
        m_settings->setThemeKey(m_originalSettings.themeKey);
        m_settings->setUiFontSize(m_originalSettings.uiFontSize);
        m_settings->setRestoreLastLibrary(m_originalSettings.restoreLastLibrary);
        m_settings->setRestoreOpenTabs(m_originalSettings.restoreOpenTabs);
        m_settings->setAutosaveSeconds(m_originalSettings.autosaveSeconds);
        m_settings->setLastLibraryPath(m_originalSettings.lastLibraryPath);
    }
    if (m_themes) {
        ThemeId original = ThemeId::ModernLight;
        if (ThemeManager::fromKey(m_originalThemeKey, &original))
            m_themes->setCurrentTheme(original, qApp);
    }
    QFont font = QApplication::font();
    font.setPointSize(m_originalFontSize);
    QApplication::setFont(font);
    reject();
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
