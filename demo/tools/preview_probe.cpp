#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QTextDocument>
#include <QDir>
#include "note/MarkdownPreview.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QString base = QDir::current().absoluteFilePath(QStringLiteral("build/probe/data"));
    QDir().mkpath(base + "/assets");

    const QString markdown = QStringLiteral(
        "# 悬臂梁模型\n\n"
        "这是一段**加粗**、*斜体*、`inline code` 和行内公式 $m\\ddot{u}+ku=0$ 的说明。\n\n"
        "## 控制方程\n\n"
        "$$\\int_{a}^{b} f(x)\\,dx \\approx \\sum_{i=1}^{n} f(x_i)\\Delta x$$\n\n"
        "其中 $\\alpha \\in [0,1]$，且 $\\sqrt{x^2+y^2}$、$\\frac{a+b}{c}$ 均可显示。\n\n"
        "> 笔记应锚定到原文，而不是替代原文。\n\n"
        "- 第一条\n- 第二条\n\n"
        "1. 打开 PDF\n2. 选择文字\n\n"
        "| 量 | 符号 | 说明 |\n|---|---|---|\n| 质量 | $m$ | kg |\n| 刚度 | $k$ | N/m |\n\n"
        "```cpp\nint main() { return 0; }\n```\n");

    pano::MarkdownPreview preview;
    preview.resize(780, 900);
    preview.setNoteMarkdown(markdown, base);
    preview.document()->setTextWidth(760);
    const qreal height = preview.document()->size().height();
    QImage image(780, qMax(200, int(height) + 20), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.translate(10, 10);
    preview.document()->drawContents(&painter);
    painter.end();
    image.save(QDir::current().absoluteFilePath(QStringLiteral("build/probe/preview.png")));
    return 0;
}
