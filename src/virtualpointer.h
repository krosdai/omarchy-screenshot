// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QCoreApplication>
#include <QPointF>
#include <QRectF>
#include <QString>

struct wl_display;
struct wl_registry;
struct zwlr_virtual_pointer_manager_v1;
struct zwlr_virtual_pointer_v1;

class VirtualPointer {
  Q_DECLARE_TR_FUNCTIONS(VirtualPointer)

public:
  VirtualPointer();
  ~VirtualPointer();
  VirtualPointer(const VirtualPointer &) = delete;
  VirtualPointer &operator=(const VirtualPointer &) = delete;

  bool available() const { return m_pointer != nullptr; }
  bool moveTo(const QPointF &point, const QRectF &desktop, QString *error);
  bool refreshFocus(QString *error);
  bool clickAt(const QPointF &point, const QRectF &desktop, QString *error);
  bool setLeftButtonPressed(bool pressed, QString *error);
  bool scrollAt(const QPointF &point, const QRectF &desktop, int steps,
                QString *error);

private:
  bool queueMove(const QPointF &point, const QRectF &desktop, QString *error);
  static void global(void *data, wl_registry *registry, unsigned name,
                     const char *interface, unsigned version);
  static void globalRemoved(void *, wl_registry *, unsigned);

  wl_display *m_display = nullptr;
  wl_registry *m_registry = nullptr;
  zwlr_virtual_pointer_manager_v1 *m_manager = nullptr;
  zwlr_virtual_pointer_v1 *m_pointer = nullptr;
};
