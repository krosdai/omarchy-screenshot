// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "pinnedwindows.h"

#include <LayerShellQt/window.h>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <algorithm>

namespace {
class PinImageProvider final : public QQuickImageProvider {
public:
  explicit PinImageProvider(PinnedImages *images)
      : QQuickImageProvider(QQuickImageProvider::Image), m_images(images) {}
  QImage requestImage(const QString &id, QSize *size, const QSize &) override {
    const QImage image = m_images->image(id.toInt());
    if (size)
      *size = image.size();
    return image;
  }

private:
  PinnedImages *m_images;
};
} // namespace

PinnedWindows::PinnedWindows(QQmlEngine *engine, PinnedImages *images,
                             QObject *parent)
    : QObject(parent), m_engine(engine), m_images(images) {
  engine->rootContext()->setContextProperty(QStringLiteral("pinnedImages"),
                                            images);
  engine->addImageProvider(QStringLiteral("pins"),
                           new PinImageProvider(images));
  connect(images, &PinnedImages::contentChanged, this, &PinnedWindows::update);
  connect(images, &PinnedImages::draggingIdChanged, this,
          &PinnedWindows::update);
  // Output removal must not leave a pin's close button on an absent screen.
  connect(
      qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        std::erase_if(m_views, [screen](const auto &view) {
          return view->property("outputName").toString() == screen->name();
        });
        QList<QRectF> screens;
        for (const auto &view : m_views)
          screens.append(view->rootObject()->property("screenRect").toRectF());
        m_images->endDrag();
        m_images->setScreens(screens);
        update();
      });
}

bool PinnedWindows::setMonitors(const QVector<CaptureMonitor> &monitors) {
  bool sameScreens = m_views.size() == size_t(monitors.size());
  for (int i = 0; sameScreens && i < monitors.size(); ++i)
    sameScreens = m_views[i]->screen() == monitors[i].screen;
  if (!sameScreens) {
    m_views.clear();
    for (const auto &monitor : monitors) {
      auto view = std::make_unique<QQuickView>(m_engine, nullptr);
      view->setResizeMode(QQuickView::SizeRootObjectToView);
      view->setColor(Qt::transparent);
      view->setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
      view->setScreen(monitor.screen);
      view->setProperty("outputName", monitor.name);
      view->setInitialProperties(
          {{QStringLiteral("screenRect"), monitor.geometry}});
      auto *layer = LayerShellQt::Window::get(view.get());
      layer->setScope(QStringLiteral("omarchy-screenshot-pin"));
      layer->setLayer(LayerShellQt::Window::LayerOverlay);
      layer->setAnchors(
          LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
          LayerShellQt::Window::AnchorBottom |
          LayerShellQt::Window::AnchorLeft | LayerShellQt::Window::AnchorRight);
      layer->setExclusiveZone(-1);
      layer->setKeyboardInteractivity(
          LayerShellQt::Window::KeyboardInteractivityNone);
      layer->setScreen(monitor.screen);
      view->setSource(
          QUrl(QStringLiteral("qrc:/qt/qml/OmarchyScreenshot/PinOverlay.qml")));
      if (view->status() != QQuickView::Ready) {
        m_views.clear();
        return false;
      }
      m_views.push_back(std::move(view));
    }
  }
  QList<QRectF> screens;
  for (int i = 0; i < monitors.size(); ++i) {
    m_views[i]->rootObject()->setProperty("screenRect", monitors[i].geometry);
    screens.append(monitors[i].geometry);
  }
  m_images->setScreens(screens);
  update();
  return !m_views.empty();
}

void PinnedWindows::setSuspended(bool suspended) {
  m_suspended = suspended;
  update();
}

void PinnedWindows::update() {
  for (const auto &view : m_views) {
    const QRectF screen = view->rootObject()->property("screenRect").toRectF();
    const QRegion mask = m_images->inputRegion(screen);
    // Keep the source surface mapped during a cross-output pointer grab,
    // even after the image has moved entirely onto another output.
    if (m_suspended || (mask.isEmpty() && m_images->draggingId() < 0)) {
      view->hide();
    } else {
      // An empty QWindow mask restores the full input area. Use a region
      // outside the surface instead when it only holds a pointer grab.
      view->setMask(mask.isEmpty() ? QRegion(-1, -1, 1, 1) : mask);
      view->show();
      view->update();
    }
  }
}
