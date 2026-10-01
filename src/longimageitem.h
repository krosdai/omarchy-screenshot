// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QQuickPaintedItem>
#include <QRectF>

class CaptureController;

class LongImageItem : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(QRectF sourceRect READ sourceRect WRITE setSourceRect NOTIFY sourceRectChanged)

public:
  explicit LongImageItem(QQuickItem *parent = nullptr);
  QObject *controller() const;
  void setController(QObject *controller);
  QRectF sourceRect() const { return m_sourceRect; }
  void setSourceRect(const QRectF &rect);
  void paint(QPainter *painter) override;

signals:
  void controllerChanged();
  void sourceRectChanged();

private:
  CaptureController *m_controller = nullptr;
  QRectF m_sourceRect;
};
