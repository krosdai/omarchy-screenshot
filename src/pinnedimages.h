// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QAbstractListModel>
#include <QImage>
#include <QRectF>
#include <QRegion>
#include <QVector>

// Owns finished images independently of the current capture. Positions are
// desktop logical coordinates, so moving between outputs never resizes a pin.
class PinnedImages final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(int draggingId READ draggingId NOTIFY draggingIdChanged)

public:
  enum Role { IdRole = Qt::UserRole + 1, RectRole, SourceRole, OrderRole };
  explicit PinnedImages(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return m_images.size(); }
  int draggingId() const { return m_draggingId; }
  int add(const QImage &image, const QRectF &rect);
  QImage image(int id) const;
  QRectF geometry(int id) const;
  void setScreens(const QList<QRectF> &screens);
  QRegion inputRegion(const QRectF &screen) const;
  static QRectF closeRect(const QRectF &rect);

  Q_INVOKABLE void close(int id);
  Q_INVOKABLE void beginDrag(int id, const QPointF &point);
  Q_INVOKABLE void moveDrag(const QPointF &point);
  Q_INVOKABLE void endDrag();

signals:
  void countChanged();
  void contentChanged();
  void draggingIdChanged();

private:
  struct Pin {
    int id;
    QImage image;
    QRectF rect;
    int order;
  };
  int rowForId(int id) const;
  QRectF reachable(const QRectF &rect) const;
  void setGeometry(int row, const QRectF &rect);
  QVector<Pin> m_images;
  QList<QRectF> m_screens;
  int m_nextId = 1;
  int m_order = 0;
  int m_draggingId = -1;
  QPointF m_dragOrigin;
  QRectF m_dragRect;
};
