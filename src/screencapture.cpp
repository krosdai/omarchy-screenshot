// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "screencapture.h"

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <QDeadlineTimer>
#include <QtGlobal>

#include <wayland-client.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
struct Output {
  wl_output *output = nullptr;
  QString name;
};

struct Capture {
  wl_shm *shm = nullptr;
  ext_image_capture_source_v1 *source = nullptr;
  ext_image_copy_capture_session_v1 *session = nullptr;
  ext_image_copy_capture_frame_v1 *frame = nullptr;
  wl_buffer *buffer = nullptr;
  void *pixels = MAP_FAILED;
  size_t size = 0;
  int width = 0;
  int height = 0;
  bool xrgb = false;
  bool argb = false;
  bool ready = false;
  bool finished = false;
  uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

struct Connection {
  wl_display *display = nullptr;
  wl_registry *registry = nullptr;
  wl_shm *shm = nullptr;
  ext_output_image_capture_source_manager_v1 *sources = nullptr;
  ext_image_copy_capture_manager_v1 *copier = nullptr;
  std::vector<std::unique_ptr<Output>> outputs;
  std::vector<std::unique_ptr<Capture>> captures;

  ~Connection() {
    for (const auto &capture : captures) {
      if (!capture)
        continue;
      if (capture->frame)
        ext_image_copy_capture_frame_v1_destroy(capture->frame);
      if (capture->session)
        ext_image_copy_capture_session_v1_destroy(capture->session);
      if (capture->source)
        ext_image_capture_source_v1_destroy(capture->source);
      if (capture->buffer)
        wl_buffer_destroy(capture->buffer);
      if (capture->pixels != MAP_FAILED)
        munmap(capture->pixels, capture->size);
    }
    for (const auto &output : outputs)
      wl_output_release(output->output);
    if (copier)
      ext_image_copy_capture_manager_v1_destroy(copier);
    if (sources)
      ext_output_image_capture_source_manager_v1_destroy(sources);
    if (shm)
      wl_shm_destroy(shm);
    if (registry)
      wl_registry_destroy(registry);
    if (display)
      wl_display_disconnect(display);
  }
};

void outputGeometry(void *, wl_output *, int32_t, int32_t, int32_t, int32_t,
                    int32_t, const char *, const char *, int32_t) {}
void outputMode(void *, wl_output *, uint32_t, int32_t, int32_t, int32_t) {}
void outputDone(void *, wl_output *) {}
void outputScale(void *, wl_output *, int32_t) {}
void outputName(void *data, wl_output *, const char *name) {
  static_cast<Output *>(data)->name = QString::fromUtf8(name);
}
void outputDescription(void *, wl_output *, const char *) {}
const wl_output_listener outputListener = {
    outputGeometry, outputMode, outputDone,
    outputScale,    outputName, outputDescription};

void registryGlobal(void *data, wl_registry *registry, uint32_t name,
                    const char *interface, uint32_t version) {
  auto *connection = static_cast<Connection *>(data);
  if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    connection->shm = static_cast<wl_shm *>(
        wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, wl_output_interface.name) == 0 &&
             version >= 4) {
    // Version 4 is the first to announce the connector name hyprctl reports.
    auto output = std::make_unique<Output>();
    output->output = static_cast<wl_output *>(
        wl_registry_bind(registry, name, &wl_output_interface, 4));
    wl_output_add_listener(output->output, &outputListener, output.get());
    connection->outputs.push_back(std::move(output));
  } else if (std::strcmp(interface,
                         ext_output_image_capture_source_manager_v1_interface
                             .name) == 0) {
    connection->sources =
        static_cast<ext_output_image_capture_source_manager_v1 *>(
            wl_registry_bind(
                registry, name,
                &ext_output_image_capture_source_manager_v1_interface, 1));
  } else if (std::strcmp(interface,
                         ext_image_copy_capture_manager_v1_interface.name) ==
             0) {
    connection->copier = static_cast<ext_image_copy_capture_manager_v1 *>(
        wl_registry_bind(registry, name,
                         &ext_image_copy_capture_manager_v1_interface, 1));
  }
}
void registryGlobalRemove(void *, wl_registry *, uint32_t) {}
const wl_registry_listener registryListener = {registryGlobal,
                                               registryGlobalRemove};

void frameTransform(void *data, ext_image_copy_capture_frame_v1 *,
                    uint32_t transform) {
  static_cast<Capture *>(data)->transform = transform;
}
void frameDamage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t,
                 int32_t, int32_t) {}
void framePresentationTime(void *, ext_image_copy_capture_frame_v1 *,
                           uint32_t, uint32_t, uint32_t) {}
void frameReady(void *data, ext_image_copy_capture_frame_v1 *) {
  auto *capture = static_cast<Capture *>(data);
  capture->ready = true;
  capture->finished = true;
}
void frameFailed(void *data, ext_image_copy_capture_frame_v1 *, uint32_t) {
  static_cast<Capture *>(data)->finished = true;
}
const ext_image_copy_capture_frame_v1_listener frameListener = {
    frameTransform, frameDamage, framePresentationTime, frameReady,
    frameFailed};

void sessionBufferSize(void *data, ext_image_copy_capture_session_v1 *,
                       uint32_t width, uint32_t height) {
  auto *capture = static_cast<Capture *>(data);
  capture->width = int(width);
  capture->height = int(height);
}
void sessionShmFormat(void *data, ext_image_copy_capture_session_v1 *,
                      uint32_t format) {
  auto *capture = static_cast<Capture *>(data);
  if (format == WL_SHM_FORMAT_XRGB8888)
    capture->xrgb = true;
  else if (format == WL_SHM_FORMAT_ARGB8888)
    capture->argb = true;
}
void sessionDmabufDevice(void *, ext_image_copy_capture_session_v1 *,
                         wl_array *) {}
void sessionDmabufFormat(void *, ext_image_copy_capture_session_v1 *,
                         uint32_t, wl_array *) {}
void sessionDone(void *data, ext_image_copy_capture_session_v1 *session) {
  auto *capture = static_cast<Capture *>(data);
  // Constraints may be re-sent; one frame is all a screenshot needs.
  if (capture->frame || capture->finished)
    return;
  if ((!capture->xrgb && !capture->argb) || capture->width <= 0 ||
      capture->height <= 0) {
    capture->finished = true;
    return;
  }
  const int stride = capture->width * 4;
  capture->size = size_t(stride) * size_t(capture->height);
  const int fd = memfd_create("omarchy-screenshot", MFD_CLOEXEC);
  if (fd < 0 || ftruncate(fd, off_t(capture->size)) != 0) {
    if (fd >= 0)
      close(fd);
    capture->finished = true;
    return;
  }
  capture->pixels = mmap(nullptr, capture->size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
  if (capture->pixels == MAP_FAILED) {
    close(fd);
    capture->finished = true;
    return;
  }
  wl_shm_pool *pool = wl_shm_create_pool(capture->shm, fd, int(capture->size));
  capture->buffer = wl_shm_pool_create_buffer(
      pool, 0, capture->width, capture->height, stride,
      capture->xrgb ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  close(fd);

  capture->frame = ext_image_copy_capture_session_v1_create_frame(session);
  ext_image_copy_capture_frame_v1_add_listener(capture->frame, &frameListener,
                                               capture);
  ext_image_copy_capture_frame_v1_attach_buffer(capture->frame,
                                                capture->buffer);
  ext_image_copy_capture_frame_v1_damage_buffer(
      capture->frame, 0, 0, capture->width, capture->height);
  ext_image_copy_capture_frame_v1_capture(capture->frame);
}
void sessionStopped(void *data, ext_image_copy_capture_session_v1 *) {
  static_cast<Capture *>(data)->finished = true;
}
const ext_image_copy_capture_session_v1_listener sessionListener = {
    sessionBufferSize, sessionShmFormat, sessionDmabufDevice,
    sessionDmabufFormat, sessionDone, sessionStopped};

bool allFinished(const Connection &connection) {
  for (const auto &capture : connection.captures)
    if (capture && !capture->finished)
      return false;
  return true;
}

void dispatchUntilFinished(Connection &connection, int timeoutMs) {
  const QDeadlineTimer deadline(timeoutMs);
  wl_display *display = connection.display;
  while (!allFinished(connection)) {
    while (wl_display_prepare_read(display) != 0)
      if (wl_display_dispatch_pending(display) < 0)
        return;
    wl_display_flush(display);
    pollfd descriptor{wl_display_get_fd(display), POLLIN, 0};
    const int ready =
        poll(&descriptor, 1, int(qMax<qint64>(0, deadline.remainingTime())));
    if (ready <= 0) {
      wl_display_cancel_read(display);
      if (ready < 0 && errno == EINTR)
        continue;
      return;
    }
    if (wl_display_read_events(display) < 0 ||
        wl_display_dispatch_pending(display) < 0)
      return;
  }
}
} // namespace

QList<QImage> captureOutputs(const QStringList &names, int timeoutMs) {
  QList<QImage> images(names.size());
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
  // XRGB8888 shares QImage::Format_RGB32's layout only on little-endian hosts.
  Connection connection;
  connection.display = wl_display_connect(nullptr);
  if (!connection.display)
    return images;
  connection.registry = wl_display_get_registry(connection.display);
  wl_registry_add_listener(connection.registry, &registryListener,
                           &connection);
  // The first roundtrip binds globals; the second delivers output names.
  if (wl_display_roundtrip(connection.display) < 0 || !connection.shm ||
      !connection.sources || !connection.copier ||
      wl_display_roundtrip(connection.display) < 0)
    return images;

  connection.captures.resize(names.size());
  for (int i = 0; i < names.size(); ++i) {
    for (const auto &output : connection.outputs) {
      if (output->name != names[i])
        continue;
      auto capture = std::make_unique<Capture>();
      capture->shm = connection.shm;
      capture->source = ext_output_image_capture_source_manager_v1_create_source(
          connection.sources, output->output);
      capture->session = ext_image_copy_capture_manager_v1_create_session(
          connection.copier, capture->source, 0);
      ext_image_copy_capture_session_v1_add_listener(
          capture->session, &sessionListener, capture.get());
      connection.captures[i] = std::move(capture);
      break;
    }
  }
  dispatchUntilFinished(connection, timeoutMs);

  for (int i = 0; i < names.size(); ++i) {
    Capture *capture = connection.captures[i].get();
    // Rotated or flipped buffers are left to grim, which already handles them.
    if (!capture || !capture->ready ||
        capture->transform != WL_OUTPUT_TRANSFORM_NORMAL)
      continue;
    // Hand the mapping to QImage so the pixels are never copied or converted.
    void *pixels = capture->pixels;
    const size_t size = capture->size;
    capture->pixels = MAP_FAILED;
    images[i] = QImage(
        static_cast<uchar *>(pixels), capture->width, capture->height,
        capture->width * 4, QImage::Format_RGB32,
        [](void *info) {
          auto *mapping = static_cast<std::pair<void *, size_t> *>(info);
          munmap(mapping->first, mapping->second);
          delete mapping;
        },
        new std::pair<void *, size_t>(pixels, size));
  }
#else
  Q_UNUSED(names)
  Q_UNUSED(timeoutMs)
#endif
  return images;
}
