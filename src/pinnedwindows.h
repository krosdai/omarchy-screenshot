// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "capturecontroller.h"
#include "pinnedimages.h"

#include <QObject>
#include <QQuickView>
#include <memory>
#include <vector>

class QQmlEngine;

class PinnedWindows final : public QObject {
  Q_OBJECT
public:
  PinnedWindows(QQmlEngine *engine, PinnedImages *images,
                QObject *parent = nullptr);
  bool setMonitors(const QVector<CaptureMonitor> &monitors);
  void setSuspended(bool suspended);
  const std::vector<std::unique_ptr<QQuickView>> &views() const {
    return m_views;
  }

private:
  void update();
  QQmlEngine *m_engine;
  PinnedImages *m_images;
  bool m_suspended = true;
  std::vector<std::unique_ptr<QQuickView>> m_views;
};
