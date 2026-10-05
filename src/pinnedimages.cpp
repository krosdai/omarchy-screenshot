// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "pinnedimages.h"

#include <algorithm>
#include <limits>

PinnedImages::PinnedImages(QObject *parent) : QAbstractListModel(parent) {}

int PinnedImages::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : count();
}

QVariant PinnedImages::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= count())
    return {};
  const auto &pin = m_images[index.row()];
  switch (role) {
  case IdRole:
    return pin.id;
  case RectRole:
    return pin.rect;
  case SourceRole:
    return QStringLiteral("image://pins/%1").arg(pin.id);
  case OrderRole:
    return pin.order;
  default:
    return {};
  }
}

QHash<int, QByteArray> PinnedImages::roleNames() const {
  return {{IdRole, "imageId"},
          {RectRole, "imageRect"},
          {SourceRole, "imageSource"},
          {OrderRole, "stackingOrder"}};
}

int PinnedImages::rowForId(int id) const {
  for (int row = 0; row < count(); ++row)
    if (m_images[row].id == id)
      return row;
  return -1;
}

QImage PinnedImages::image(int id) const {
  const int row = rowForId(id);
  return row >= 0 ? m_images[row].image : QImage();
}

QRectF PinnedImages::geometry(int id) const {
  const int row = rowForId(id);
  return row >= 0 ? m_images[row].rect : QRectF();
}

QRectF PinnedImages::closeRect(const QRectF &rect) {
  // Keep the button usable even for a screenshot smaller than the button.
  return QRectF(rect.right() - 28, rect.top(), 28, 28);
}

QRectF PinnedImages::reachable(const QRectF &rect) const {
  if (m_screens.isEmpty())
    return rect;
  const QRectF button = closeRect(rect);
  QRectF best = rect;
  qreal nearest = std::numeric_limits<qreal>::max();
  for (const auto &screen : m_screens) {
    if (screen.contains(button))
      return rect;
    const qreal x =
        std::clamp(button.x(), screen.left(),
                   std::max(screen.left(), screen.right() - button.width()));
    const qreal y =
        std::clamp(button.y(), screen.top(),
                   std::max(screen.top(), screen.bottom() - button.height()));
    const QPointF delta(x - button.x(), y - button.y());
    const qreal distance = delta.x() * delta.x() + delta.y() * delta.y();
    if (distance < nearest) {
      nearest = distance;
      best = rect.translated(delta);
    }
  }
  return best;
}

int PinnedImages::add(const QImage &image, const QRectF &rect) {
  if (image.isNull() || rect.isEmpty())
    return -1;
  const int id = m_nextId++;
  beginInsertRows({}, count(), count());
  m_images.append({id, image, reachable(rect), ++m_order});
  endInsertRows();
  emit contentChanged();
  emit countChanged();
  return id;
}

void PinnedImages::close(int id) {
  const int row = rowForId(id);
  if (row < 0)
    return;
  if (m_draggingId == id) {
    m_draggingId = -1;
    emit draggingIdChanged();
  }
  beginRemoveRows({}, row, row);
  m_images.removeAt(row);
  endRemoveRows();
  emit contentChanged();
  emit countChanged();
}

void PinnedImages::setGeometry(int row, const QRectF &rect) {
  if (m_images[row].rect == rect)
    return;
  m_images[row].rect = rect;
  emit dataChanged(index(row), index(row), {RectRole});
  emit contentChanged();
}

void PinnedImages::setScreens(const QList<QRectF> &screens) {
  m_screens = screens;
  for (int row = 0; row < count(); ++row)
    setGeometry(row, reachable(m_images[row].rect));
}

QRegion PinnedImages::inputRegion(const QRectF &screen) const {
  QRegion region;
  for (const auto &pin : m_images) {
    for (const auto &rect : {pin.rect, closeRect(pin.rect)}) {
      const QRectF visible = rect.intersected(screen);
      if (!visible.isEmpty())
        region += visible.translated(-screen.topLeft()).toAlignedRect();
    }
  }
  return region;
}

void PinnedImages::beginDrag(int id, const QPointF &point) {
  const int row = rowForId(id);
  if (row < 0)
    return;
  m_draggingId = id;
  m_dragOrigin = point;
  m_dragRect = m_images[row].rect;
  m_images[row].order = ++m_order;
  // Updating roles preserves delegates and their Wayland pointer grab.
  emit dataChanged(index(row), index(row), {OrderRole});
  emit draggingIdChanged();
}

void PinnedImages::moveDrag(const QPointF &point) {
  const int row = rowForId(m_draggingId);
  if (row >= 0)
    setGeometry(row, m_dragRect.translated(point - m_dragOrigin));
}

void PinnedImages::endDrag() {
  const int row = rowForId(m_draggingId);
  if (row >= 0)
    setGeometry(row, reachable(m_images[row].rect));
  m_draggingId = -1;
  emit draggingIdChanged();
}
