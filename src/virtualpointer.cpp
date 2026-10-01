// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "virtualpointer.h"

#include "wlr-virtual-pointer-client-protocol.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <wayland-client.h>

namespace {
uint32_t pointerTime() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

VirtualPointer::VirtualPointer() {
  m_display = wl_display_connect(nullptr);
  if (!m_display)
    return;
  m_registry = wl_display_get_registry(m_display);
  if (!m_registry)
    return;
  static const wl_registry_listener listener = {global, globalRemoved};
  wl_registry_add_listener(m_registry, &listener, this);
  if (wl_display_roundtrip(m_display) < 0 || !m_manager)
    return;
  m_pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(
      m_manager, nullptr);
  wl_display_flush(m_display);
}

VirtualPointer::~VirtualPointer() {
  if (m_pointer)
    zwlr_virtual_pointer_v1_destroy(m_pointer);
  if (m_manager)
    zwlr_virtual_pointer_manager_v1_destroy(m_manager);
  if (m_registry)
    wl_registry_destroy(m_registry);
  if (m_display)
    wl_display_disconnect(m_display);
}

void VirtualPointer::global(void *data, wl_registry *registry, unsigned name,
                            const char *interface, unsigned version) {
  auto *self = static_cast<VirtualPointer *>(data);
  if (self->m_manager ||
      std::strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name))
    return;
  self->m_manager = static_cast<zwlr_virtual_pointer_manager_v1 *>(
      wl_registry_bind(registry, name,
                       &zwlr_virtual_pointer_manager_v1_interface,
                       std::min(version, 2u)));
}

void VirtualPointer::globalRemoved(void *, wl_registry *, unsigned) {}

bool VirtualPointer::queueMove(const QPointF &point, const QRectF &desktop,
                               QString *error) {
  if (!m_pointer || desktop.isEmpty()) {
    if (error)
      *error = tr("This compositor does not support a virtual pointer");
    return false;
  }
  const auto position = [](qreal value, qreal start, qreal length) {
    return static_cast<uint32_t>(std::clamp(
        qRound((value - start) * 65535 / length), 0, 65535));
  };
  const uint32_t now = pointerTime();
  zwlr_virtual_pointer_v1_motion_absolute(
      m_pointer, now, position(point.x(), desktop.left(), desktop.width()),
      position(point.y(), desktop.top(), desktop.height()), 65535, 65535);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  return true;
}

bool VirtualPointer::moveTo(const QPointF &point, const QRectF &desktop,
                            QString *error) {
  if (!queueMove(point, desktop, error))
    return false;
  if (wl_display_roundtrip(m_display) < 0) {
    if (error)
      *error = tr("Cannot position the virtual pointer");
    return false;
  }
  return true;
}

bool VirtualPointer::clickAt(const QPointF &point, const QRectF &desktop,
                             QString *error) {
  if (!queueMove(point, desktop, error))
    return false;
  const uint32_t now = pointerTime();
  zwlr_virtual_pointer_v1_button(m_pointer, now, 0x110,
                                 WL_POINTER_BUTTON_STATE_PRESSED);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  zwlr_virtual_pointer_v1_button(m_pointer, now, 0x110,
                                 WL_POINTER_BUTTON_STATE_RELEASED);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  if (wl_display_roundtrip(m_display) < 0) {
    if (error)
      *error = tr("Cannot send a mouse click");
    return false;
  }
  return true;
}

bool VirtualPointer::refreshFocus(QString *error) {
  if (!m_pointer)
    return false;
  // Hyprland skips hit testing when the pointer stays at the same pixel.
  // Recheck the committed input region without changing the final position.
  const uint32_t now = pointerTime();
  zwlr_virtual_pointer_v1_motion(m_pointer, now, wl_fixed_from_int(2), 0);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  zwlr_virtual_pointer_v1_motion(m_pointer, now, wl_fixed_from_int(-2), 0);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  if (wl_display_roundtrip(m_display) < 0) {
    if (error)
      *error = tr("Cannot restore mouse input to the capture area");
    return false;
  }
  return true;
}

bool VirtualPointer::scrollAt(const QPointF &point, const QRectF &desktop,
                              int steps, QString *error) {
  // Always refresh pointer focus after the capture overlay releases input,
  // including consecutive wheel events at exactly the same position.
  if (steps <= 0 || !queueMove(point + QPointF(1, 0), desktop, error) ||
      !moveTo(point, desktop, error))
    return false;
  const uint32_t now = pointerTime();
  zwlr_virtual_pointer_v1_axis_source(m_pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
  // Keep the physical wheel value small while preserving a discrete step for
  // applications that ignore fractional wheel movement.
  zwlr_virtual_pointer_v1_axis_discrete(
      m_pointer, now, WL_POINTER_AXIS_VERTICAL_SCROLL,
      wl_fixed_from_double(0.5 * steps), steps);
  zwlr_virtual_pointer_v1_frame(m_pointer);
  if (wl_display_roundtrip(m_display) < 0) {
    if (error)
      *error = tr("Cannot send a scroll event");
    return false;
  }
  return true;
}
