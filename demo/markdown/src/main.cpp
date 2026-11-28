#include "MarkdownDemoWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QPixmap>
#include <QTextBrowser>
#include <QScrollBar>
#include <QTimer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PANO Markdown Demo"));
    QApplication::setOrganizationName(QStringLiteral("PANO"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 10));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PANO Phase 3.0 Markdown + math rendering demo"));
    parser.addHelpOption();
    const QCommandLineOption fileOption(
        QStringLiteral("file"), QStringLiteral("启动时打开指定 Markdown 文件。"), QStringLiteral("path"));
    parser.addOption(fileOption);
    const QCommandLineOption widthOption(
        QStringLiteral("width"), QStringLiteral("窗口宽度（技术验证截图用）。"), QStringLiteral("px"));
    parser.addOption(widthOption);
    const QCommandLineOption fontOption(
        QStringLiteral("font"), QStringLiteral("预览字号（磅）。"), QStringLiteral("pt"));
    parser.addOption(fontOption);
    const QCommandLineOption darkOption(
        QStringLiteral("dark"), QStringLiteral("使用深色主题。"));
    parser.addOption(darkOption);
    const QCommandLineOption scrollOption(
        QStringLiteral("scroll"), QStringLiteral("截图前将预览滚动到指定像素。"), QStringLiteral("px"));
    parser.addOption(scrollOption);
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("渲染完成后保存窗口截图并退出（技术验证用）。"),
        QStringLiteral("path"));
    parser.addOption(screenshotOption);
    parser.process(app);

    pano::markdown::MarkdownDemoWindow window;
    if (parser.isSet(fileOption))
        window.loadMarkdownFile(parser.value(fileOption));
    if (parser.isSet(fontOption))
        window.setPreviewFontSize(parser.value(fontOption).toInt());
    if (parser.isSet(darkOption))
        window.setDarkTheme(true);
    if (parser.isSet(widthOption))
        window.resize(parser.value(widthOption).toInt(), window.height());
    window.show();
    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        const int scroll = parser.isSet(scrollOption) ? parser.value(scrollOption).toInt() : -1;
        QTimer::singleShot(1500, &window, [&window, path, scroll]() {
            QDir().mkpath(QFileInfo(path).absolutePath());
            if (scroll >= 0) {
                if (auto* preview = window.findChild<QTextBrowser*>())
                    preview->verticalScrollBar()->setValue(scroll);
            }
            window.repaint();
            QPixmap pixmap(window.size());
            pixmap.fill(Qt::transparent);
            window.render(&pixmap);
            pixmap.save(path);
            QApplication::quit();
        });
    }
    return app.exec();
}
