// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "longimageitem.h"

#include "capturecontroller.h"
#include <QPainter>

LongImageItem::LongImageItem(QQuickItem *parent) : QQuickPaintedItem(parent) {
  setOpaquePainting(false);
}

QObject *LongImageItem::controller() const { return m_controller; }

void LongImageItem::setController(QObject *controller) {
  auto *next = qobject_cast<CaptureController *>(controller);
  if (m_controller == next)
    return;
  if (m_controller)
    disconnect(m_controller, nullptr, this, nullptr);
  m_controller = next;
  if (m_controller) {
    connect(m_controller, &CaptureController::scrollImageChanged, this,
            [this] { update(); });
    connect(m_controller, &CaptureController::annotationsChanged, this,
            [this] { update(); });
    connect(m_controller, &CaptureController::draftChanged, this,
            [this] { update(); });
  }
  emit controllerChanged();
  update();
}

void LongImageItem::setSourceRect(const QRectF &rect) {
  if (m_sourceRect == rect)
    return;
  m_sourceRect = rect;
  emit sourceRectChanged();
  update();
}

void LongImageItem::paint(QPainter *painter) {
  if (m_controller)
    m_controller->paintScrollPreview(*painter, m_sourceRect, boundingRect());
}
