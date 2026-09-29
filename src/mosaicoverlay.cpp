#include "mosaicoverlay.h"

#include "capturecontroller.h"

#include <QPainter>
#include <QPainterPath>

MosaicOverlay::MosaicOverlay(QQuickItem *parent) : QQuickPaintedItem(parent) {
  setAntialiasing(true);
}

void MosaicOverlay::setController(CaptureController *controller) {
  if (m_controller == controller)
    return;
  if (m_controller)
    disconnect(m_controller, nullptr, this, nullptr);
  m_controller = controller;
  if (m_controller) {
    const auto changed = [this] {
      emit hasMosaicChanged();
      update();
    };
    connect(m_controller, &CaptureController::annotationsChanged, this,
            changed);
    connect(m_controller, &CaptureController::draftChanged, this, changed);
    connect(m_controller, &CaptureController::scrollImageChanged, this,
            changed);
    connect(m_controller, &CaptureController::scrollOffsetChanged, this,
            [this] { update(); });
    connect(m_controller, &CaptureController::selectionChanged, this,
            [this] { update(); });
  }
  emit controllerChanged();
  emit hasMosaicChanged();
  update();
}

void MosaicOverlay::setScreenIndex(int index) {
  if (m_screenIndex == index)
    return;
  m_screenIndex = index;
  emit screenIndexChanged();
  update();
}

bool MosaicOverlay::hasMosaic() const {
  if (!m_controller)
    return false;
  if (m_controller->draft().value(QStringLiteral("type")).toString() ==
      QStringLiteral("mosaic"))
    return true;
  for (const QVariant &annotation : m_controller->annotations())
    if (annotation.toMap().value(QStringLiteral("type")).toString() ==
        QStringLiteral("mosaic"))
      return true;
  return false;
}

bool MosaicOverlay::imageReady() const {
  if (m_controller && !m_controller->scrollImage().isNull())
    return !m_controller->scrollMosaicImage().isNull();
  return m_controller && m_screenIndex >= 0 &&
         m_screenIndex < m_controller->monitors().size() &&
         !m_controller->monitors()[m_screenIndex].mosaicImage.isNull();
}

void MosaicOverlay::paint(QPainter *painter) {
  if (!imageReady() || !m_controller->selected() || !hasMosaic())
    return;
  const auto &monitor = m_controller->monitors()[m_screenIndex];
  const QPointF offset = monitor.geometry.topLeft();
  const QPointF scrollShift(
      0, m_controller->hasScrollImage() ? m_controller->scrollOffset() : 0);
  QPainterPath mask;
  mask.setFillRule(Qt::WindingFill);
  for (const QVariant &annotation : m_controller->annotations())
    mask.addPath(CaptureController::mosaicPath(annotation.toMap())
                     .translated(-offset - scrollShift));
  mask.addPath(CaptureController::mosaicPath(m_controller->draft())
                   .translated(-offset - scrollShift));
  if (mask.isEmpty())
    return;

  painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter->setClipRect(m_controller->selection().translated(-offset));
  painter->setClipPath(mask, Qt::IntersectClip);
  if (!m_controller->scrollImage().isNull())
    painter->drawImage(
        QRectF(m_controller->selection().topLeft() - offset - scrollShift,
               QSizeF(m_controller->selection().width(),
                      m_controller->scrollDocumentHeight())),
        m_controller->scrollMosaicImage());
  else
    painter->drawImage(QRectF(0, 0, width(), height()), monitor.mosaicImage);
}
