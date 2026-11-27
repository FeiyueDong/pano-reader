#pragma once

#include <QPoint>
#include <QRect>
#include <QWidget>

namespace pano {

// Full-viewport rubber-band overlay used to capture a region into a note.
// The overlay itself is never included in the captured image: the owner hides
// it before grabbing the viewport.
class RegionCaptureOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RegionCaptureOverlay(QWidget *parent = nullptr);

signals:
    void regionSelected(const QRect &rect);
    void cancelled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect normalisedSelection() const;

    QPoint m_origin;
    QRect m_selection;
    bool m_dragging = false;
};

} // namespace pano
