#pragma once
#include <QMainWindow>

class QPlainTextEdit;
class QTextBrowser;
class QSpinBox;
class QComboBox;
class QLabel;
class QTimer;

namespace pano::markdown {

class MarkdownDemoWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MarkdownDemoWindow(QWidget* parent = nullptr);
    void loadMarkdownFile(const QString& path);
    void setDarkTheme(bool dark);
    void setPreviewFontSize(int pointSize);

private slots:
    void openSample();
    void chooseMarkdownFile();
    void renderNow();
    void scheduleRender();
    void updateTheme();
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void loadText(const QString& path);
    QString samplePath() const;

    QPlainTextEdit* m_editor = nullptr;
    QTextBrowser* m_preview = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QComboBox* m_theme = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_formulaStatus = nullptr;
    QTimer* m_renderTimer = nullptr;
};

} // namespace pano::markdown
