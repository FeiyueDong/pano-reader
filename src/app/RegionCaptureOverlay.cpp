#include "app/RegionCaptureOverlay.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

namespace pano {

RegionCaptureOverlay::RegionCaptureOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
}

QRect RegionCaptureOverlay::normalisedSelection() const
{
    if (m_selection.isNull())
        return QRect();
    return m_selection.normalized().intersected(rect());
}

void RegionCaptureOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(10, 16, 26, 120));

    const QRect selection = normalisedSelection();
    if (selection.isValid() && selection.width() > 1 && selection.height() > 1) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(selection, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        QPen pen(QColor(255, 255, 255), 1.5, Qt::DashLine);
        pen.setDashPattern({5, 4});
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(selection);

        painter.setPen(Qt::white);
        painter.drawText(selection.adjusted(6, 4, -6, -4), Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height()));
    }

    painter.setPen(QColor(255, 255, 255, 230));
    QFont font = painter.font();
    font.setPointSizeF(11.0);
    painter.setFont(font);
    painter.drawText(QRect(16, 14, width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
                     tr("拖动框选区域，释放鼠标完成；Esc 取消"));
}

void RegionCaptureOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_origin = event->position().toPoint();
    m_selection = QRect(m_origin, m_origin);
    setFocus();
    update();
}

void RegionCaptureOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    m_selection = QRect(m_origin, event->position().toPoint());
    update();
}

void RegionCaptureOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    const QRect selection = normalisedSelection();
    if (selection.width() < 4 || selection.height() < 4) {
        m_selection = QRect();
        update();
        return;
    }
    hide();
    emit regionSelected(selection);
}

void RegionCaptureOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit cancelled();
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace pano
