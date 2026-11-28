#include "MarkdownDemoWindow.h"

#include "MarkdownMathRenderer.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QResizeEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextCursor>
#include <QSplitter>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace pano::markdown {
namespace {
constexpr int kAutoRenderMs = 300;
}

MarkdownDemoWindow::MarkdownDemoWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PANO Phase 3.0 · Markdown + Math Rendering Demo"));
    resize(1360, 900);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kAutoRenderMs);
    connect(m_renderTimer, &QTimer::timeout, this, &MarkdownDemoWindow::renderNow);

    buildUi();

    const QString sample = samplePath();
    if (!sample.isEmpty())
        loadText(sample);
    else
        m_editor->setPlainText(QStringLiteral("# 未找到示例\n\n请将 sample.md 放到 demo/markdown/examples/。"));
}

void MarkdownDemoWindow::buildUi()
{
    auto* toolbar = addToolBar(QStringLiteral("Markdown"));
    toolbar->setMovable(false);

    auto* openSample = toolbar->addAction(QStringLiteral("打开样本"));
    connect(openSample, &QAction::triggered, this, &MarkdownDemoWindow::openSample);

    auto* open = toolbar->addAction(QStringLiteral("打开 Markdown…"));
    connect(open, &QAction::triggered, this, &MarkdownDemoWindow::chooseMarkdownFile);

    auto* render = toolbar->addAction(QStringLiteral("重新渲染"));
    render->setShortcut(QKeySequence(QStringLiteral("F5")));
    addAction(render);
    connect(render, &QAction::triggered, this, &MarkdownDemoWindow::renderNow);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("字号 "), this));
    m_fontSize = new QSpinBox(this);
    m_fontSize->setRange(9, 26);
    m_fontSize->setValue(12);
    m_fontSize->setSuffix(QStringLiteral(" pt"));
    toolbar->addWidget(m_fontSize);
    connect(m_fontSize, qOverload<int>(&QSpinBox::valueChanged), this,
            &MarkdownDemoWindow::scheduleRender);

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(QStringLiteral("主题 "), this));
    m_theme = new QComboBox(this);
    m_theme->addItem(QStringLiteral("浅色"), false);
    m_theme->addItem(QStringLiteral("深色"), true);
    toolbar->addWidget(m_theme);
    connect(m_theme, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MarkdownDemoWindow::updateTheme);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    m_editor = new QPlainTextEdit(splitter);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setTabStopDistance(32);
    m_editor->setPlaceholderText(QStringLiteral("Markdown 原文"));

    m_preview = new QTextBrowser(splitter);
    m_preview->setOpenLinks(false);
    m_preview->setOpenExternalLinks(false);
    m_preview->setFrameShape(QFrame::NoFrame);
    connect(m_preview, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        statusBar()->showMessage(QStringLiteral("外部链接已禁用（技术验证）：%1").arg(url.toString()), 4000);
    });

    splitter->addWidget(m_editor);
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    m_editor->setMinimumWidth(240);
    m_preview->setMinimumWidth(220);
    splitter->setSizes({ 360, 900 });
    setCentralWidget(splitter);

    m_status = new QLabel(this);
    m_formulaStatus = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_formulaStatus);

    connect(m_editor, &QPlainTextEdit::textChanged, this, &MarkdownDemoWindow::scheduleRender);
    updateTheme();
}

QString MarkdownDemoWindow::samplePath() const
{
    const QString path = QDir(QStringLiteral(PANO_MARKDOWN_DIR)).filePath(
        QStringLiteral("examples/sample.md"));
    return QFileInfo::exists(path) ? path : QString();
}

void MarkdownDemoWindow::openSample()
{
    const QString path = samplePath();
    if (path.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("示例文件不存在：demo/markdown/examples/sample.md"), 5000);
        return;
    }
    loadText(path);
}

void MarkdownDemoWindow::chooseMarkdownFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开 Markdown"), QDir::homePath(),
        QStringLiteral("Markdown (*.md *.markdown);;所有文件 (*)"));
    if (!path.isEmpty())
        loadText(path);
}

void MarkdownDemoWindow::loadMarkdownFile(const QString& path)
{
    loadText(path);
}

void MarkdownDemoWindow::setDarkTheme(bool dark)
{
    if (m_theme)
        m_theme->setCurrentIndex(dark ? 1 : 0);
}

void MarkdownDemoWindow::setPreviewFontSize(int pointSize)
{
    if (m_fontSize)
        m_fontSize->setValue(pointSize);
}

void MarkdownDemoWindow::loadText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), file.errorString());
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    m_editor->setPlainText(text);
    m_preview->setProperty("baseDirectory", QFileInfo(path).absolutePath());
    renderNow();
}

void MarkdownDemoWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    scheduleRender();
}

void MarkdownDemoWindow::scheduleRender()
{
    if (m_renderTimer)
        m_renderTimer->start();
}

void MarkdownDemoWindow::updateTheme()
{
    const bool dark = m_theme && m_theme->currentData().toBool();
    if (m_preview) {
        m_preview->setStyleSheet(dark
            ? QStringLiteral("QTextBrowser { background:#171a21; color:#e6e8ee; border:none; }")
            : QStringLiteral("QTextBrowser { background:#ffffff; color:#20242b; border:none; }"));
    }
    renderNow();
}

void MarkdownDemoWindow::renderNow()
{
    if (!m_preview || !m_editor)
        return;
    if (m_renderTimer)
        m_renderTimer->stop();

    const QString baseDir = m_preview->property("baseDirectory").toString();
    const bool dark = m_theme && m_theme->currentData().toBool();
    const qreal fontPt = m_fontSize ? m_fontSize->value() : 12.0;
    const QColor textColor = dark ? QColor(QStringLiteral("#e6e8ee")) : QColor(QStringLiteral("#20242b"));

    const MarkdownRenderResult result = MarkdownMathRenderer::render(
        m_editor->toPlainText(), baseDir, fontPt, textColor, 2.0,
        m_preview->viewport()->width() > 0 ? m_preview->viewport()->width() - 24.0 : 900.0);

    QTextDocument* document = m_preview->document();
    document->clear();
    document->setDefaultStyleSheet(MarkdownMathRenderer::defaultStyleSheet(dark, fontPt));
    document->setDefaultFont(QFont(QStringLiteral("Noto Sans CJK SC"), static_cast<int>(fontPt)));
    for (auto it = result.resources.constBegin(); it != result.resources.constEnd(); ++it)
        document->addResource(QTextDocument::ImageResource, it.key(), it.value());
    document->setHtml(result.html);
    m_preview->moveCursor(QTextCursor::Start);
    m_preview->verticalScrollBar()->setValue(0);
    QTimer::singleShot(0, m_preview, [this]() { m_preview->verticalScrollBar()->setValue(0); });

    m_status->setText(QStringLiteral("解析 %1 ms · 公式 %2 ms · 文档 %3 字符")
                          .arg(result.parseMs)
                          .arg(result.mathMs)
                          .arg(m_editor->toPlainText().size()));
    QString formulaStatus = QStringLiteral("公式 %1 · 失败 %2 · 缺图 %3")
                                .arg(result.formulaCount)
                                .arg(result.formulaFailures)
                                .arg(result.missingImages);
    if (!result.warnings.isEmpty())
        formulaStatus += QStringLiteral(" · 警告 %1").arg(result.warnings.size());
    m_formulaStatus->setText(formulaStatus);
    m_formulaStatus->setToolTip(result.warnings.join(QLatin1Char('\n')));
    if (result.formulaFailures > 0)
        m_status->setText(m_status->text() + QStringLiteral(" · 有公式失败"));
    else if (result.missingImages > 0)
        m_status->setText(m_status->text() + QStringLiteral(" · 有缺失图片"));
}

} // namespace pano::markdown
