// Non-visual coordinate contract probe for PageLayout and the PdfView page
// transform. Expected values are written independently from the implementation
// (see docs/development/2.PANO_Phase0_1_Demo_Stabilization.md, 5.3.3).
#include "reader/PageLayout.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QtMath>

#include <cmath>

namespace {

bool nearlyEqual(qreal a, qreal b)
{
    return std::abs(a - b) <= 1e-6;
}

bool checkPoint(const QString &label, const QPointF &actual, const QPointF &expected)
{
    if (nearlyEqual(actual.x(), expected.x()) && nearlyEqual(actual.y(), expected.y()))
        return true;
    qWarning().noquote() << "FAIL" << label << "actual" << actual << "expected" << expected;
    return false;
}

QPointF expectedContent(const QPointF &p, const QSizeF &original, int rotation,
                        const QPointF &pageOrigin, qreal zoom)
{
    qreal x = p.x();
    qreal y = p.y();
    switch (rotation) {
    case 90:
        x = original.height() - p.y();
        y = p.x();
        break;
    case 180:
        x = original.width() - p.x();
        y = original.height() - p.y();
        break;
    case 270:
        x = p.y();
        y = original.width() - p.x();
        break;
    default:
        break;
    }
    return QPointF(pageOrigin.x() + x * zoom, pageOrigin.y() + y * zoom);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QSizeF original(595.0, 842.0);
    const QPointF samples[] = {
        QPointF(0.0, 0.0),
        QPointF(original.width(), 0.0),
        QPointF(original.width(), original.height()),
        QPointF(0.0, original.height()),
        QPointF(137.0, 283.0),
    };

    int failures = 0;
    for (int rotation : {0, 90, 180, 270}) {
        for (qreal zoom : {0.5, 1.0, 1.75}) {
            pano::PageLayout layout;
            layout.setViewportSize(QSizeF(1000.0, 800.0));
            layout.setMargins(24.0, 16.0);
            layout.setPages({original});
            layout.setZoom(zoom);
            layout.setRotation(rotation);

            const QPointF scroll(137.0, 281.0);
            const QRectF pageRect = layout.pageRect(0);
            const QTransform pageToView = layout.pageToContent(0)
                                          * QTransform::fromTranslate(-scroll.x(), -scroll.y());
            bool invertible = false;
            const QTransform viewToPage = pageToView.inverted(&invertible);
            if (!invertible) {
                qWarning() << "FAIL non-invertible transform" << rotation << zoom;
                ++failures;
                continue;
            }

            for (const QPointF &pagePoint : samples) {
                const QPointF expected = expectedContent(pagePoint, original, rotation,
                                                         pageRect.topLeft(), zoom)
                                         - scroll;
                const QPointF actual = pageToView.map(pagePoint);
                if (!checkPoint(QStringLiteral("rotation %1 zoom %2 point %3,%4")
                                    .arg(rotation)
                                    .arg(zoom)
                                    .arg(pagePoint.x())
                                    .arg(pagePoint.y()),
                                actual, expected)) {
                    ++failures;
                }
                const QPointF roundTrip = viewToPage.map(actual);
                if (!checkPoint(QStringLiteral("inverse rotation %1 zoom %2 point %3,%4")
                                    .arg(rotation)
                                    .arg(zoom)
                                    .arg(pagePoint.x())
                                    .arg(pagePoint.y()),
                                roundTrip, pagePoint)) {
                    ++failures;
                }
            }

            const QSizeF viewSize = layout.pageSizeView(0);
            const bool swapped = rotation == 90 || rotation == 270;
            const QSizeF expectedView = (swapped ? QSizeF(original.height(), original.width())
                                                 : original)
                                        * zoom;
            if (viewSize != expectedView) {
                qWarning() << "FAIL view size" << rotation << zoom << viewSize << expectedView;
                ++failures;
            }
        }
    }

    if (failures == 0) {
        qInfo().noquote() << "coordinate probe passed:"
                          << "4 rotations x 3 zooms x 5 points, with scroll (137,281)";
        return 0;
    }
    qCritical() << "coordinate probe failures:" << failures;
    return 1;
}
