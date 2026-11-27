#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include "note/MathRenderer.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const QStringList formulas = {
        QStringLiteral("m\\ddot{u}+ku=0"),
        QStringLiteral("\\int_{a}^{b} f(x)\\,dx \\approx \\sum_{i=1}^{n} f(x_i)\\Delta x"),
        QStringLiteral("\\sqrt{x^2+y^2}"),
        QStringLiteral("\\frac{a+b}{c}"),
        QStringLiteral("\\alpha \\in [0,1]"),
        QStringLiteral("\\left( \\frac{a}{b} \\right)^2"),
        QStringLiteral("E = mc^2"),
        QStringLiteral("\\nabla \\cdot \\vec{E} = \\frac{\\rho}{\\varepsilon_0}"),
    };
    QVector<QImage> images;
    int width = 0;
    int height = 0;
    for (const QString &f : formulas) {
        const QImage img = pano::MathRenderer::render(f, true, 2.0);
        images.append(img);
        width = qMax(width, img.width());
        height += img.height() + 30;
    }
    QImage sheet(width + 40, height + 40, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::white);
    QPainter p(&sheet);
    p.setPen(Qt::black);
    int y = 20;
    QFont label(QStringLiteral("DejaVu Sans Mono"));
    label.setPixelSize(14);
    for (int i = 0; i < images.size(); ++i) {
        p.setFont(label);
        p.drawText(20, y + 12, QStringLiteral("%1: %2").arg(i + 1).arg(formulas.at(i)));
        const QImage &img = images.at(i);
        p.drawImage(20, y + 18, img);
        y += img.height() + 30;
    }
    p.end();
    const QString out = QDir::current().absoluteFilePath(QStringLiteral("build/probe/math-sheet.png"));
    sheet.save(out);
    return 0;
}
