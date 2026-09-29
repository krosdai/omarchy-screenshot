#include "scrollpreview.h"

#include "capturecontroller.h"

#include <QPainter>

ScrollPreview::ScrollPreview(QQuickItem *parent) : QQuickPaintedItem(parent) {}

void ScrollPreview::setController(CaptureController *controller) {
  if (m_controller == controller)
    return;
  if (m_controller)
    disconnect(m_controller, nullptr, this, nullptr);
  m_controller = controller;
  if (m_controller) {
    connect(m_controller, &CaptureController::scrollImageChanged, this,
            [this] { update(); });
    connect(m_controller, &CaptureController::scrollOffsetChanged, this,
            [this] { update(); });
    connect(m_controller, &CaptureController::selectionChanged, this,
            [this] { update(); });
  }
  emit controllerChanged();
  update();
}

void ScrollPreview::setScreenIndex(int index) {
  if (m_screenIndex == index)
    return;
  m_screenIndex = index;
  emit screenIndexChanged();
  update();
}

void ScrollPreview::paint(QPainter *painter) {
  if (!m_controller || !m_controller->hasScrollImage() ||
      m_screenIndex != m_controller->scrollMonitor() || width() <= 0 ||
      height() <= 0 || m_controller->selection().width() <= 0)
    return;
  const QImage &image = m_controller->scrollImage();
  const qreal scale = image.width() / m_controller->selection().width();
  const qreal sourceY = m_controller->scrollOffset() * scale;
  const qreal sourceHeight =
      qMin<qreal>(height() * scale, image.height() - sourceY);
  if (sourceHeight <= 0)
    return;
  painter->setRenderHint(QPainter::SmoothPixmapTransform);
  painter->drawImage(QRectF(0, 0, width(), sourceHeight / scale), image,
                     QRectF(0, sourceY, image.width(), sourceHeight));
}
