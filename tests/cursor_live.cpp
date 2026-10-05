// SPDX-License-Identifier: GPL-3.0-only

// Run manually inside Hyprland: build/cursor-live-test. Each output briefly
// shows a solid fixture while the real pointer crosses three cursor shapes.
#include "capturecontroller.h"
#include "screencapture.h"
#include "virtualpointer.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

static bool uniform(const QImage &image) {
  if (image.isNull() || image.width() < 100 || image.height() < 100)
    return false;
  const QRgb reference = image.pixel(32, 32);
  for (int y = 24; y < image.height() - 24; ++y)
    for (int x = 24; x < image.width() - 24; ++x) {
      const QRgb pixel = image.pixel(x, y);
      if (qAbs(qRed(pixel) - qRed(reference)) > 2 ||
          qAbs(qGreen(pixel) - qGreen(reference)) > 2 ||
          qAbs(qBlue(pixel) - qBlue(reference)) > 2)
        return false;
    }
  return true;
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  QTemporaryDir settings;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     settings.path());
  QQmlEngine engine;
  QQmlComponent fixture(&engine);
  fixture.setData(R"(
import QtQuick
Rectangle {
    width: 640; height: 480; color: "#35634f"
    Repeater {
        model: 3
        MouseArea {
            required property int index
            x: index * parent.width / 3
            width: parent.width / 3; height: parent.height
            hoverEnabled: true
            cursorShape: [Qt.ArrowCursor, Qt.IBeamCursor, Qt.PointingHandCursor][index]
        }
    }
}
)",
                  QUrl(QStringLiteral("cursor-fixture.qml")));
  QRectF desktop;
  std::vector<std::unique_ptr<QQuickView>> views;
  for (QScreen *screen : app.screens()) {
    desktop = desktop.united(screen->geometry());
    auto view = std::make_unique<QQuickView>(&engine, nullptr);
    view->setScreen(screen);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->setFlags(Qt::FramelessWindowHint);
    auto *layer = LayerShellQt::Window::get(view.get());
    layer->setScope(QStringLiteral("omarchy-screenshot-cursor-test"));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(
        LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
        LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight);
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(
        LayerShellQt::Window::KeyboardInteractivityNone);
    view->setContent(fixture.url(), &fixture, fixture.create());
    if (view->status() != QQuickView::Ready)
      return 1;
    view->show();
    views.push_back(std::move(view));
  }
  QTest::qWait(600);
  VirtualPointer pointer;
  if (!pointer.available())
    return 1;
  QString error;
  int moves = 0;
  QTimer motion;
  motion.setInterval(8);
  QObject::connect(&motion, &QTimer::timeout, &app, [&] {
    const QRectF screen =
        views[(moves / 3) % views.size()]->screen()->geometry();
    const QPointF point(screen.left() +
                            screen.width() * ((moves % 3) + 0.5) / 3,
                        screen.top() + screen.height() * 0.45);
    QString pointerError;
    if (!pointer.moveTo(point, desktop, &pointerError))
      qFatal("Cannot move test pointer");
    ++moves;
  });
  motion.start();
  int frames = 0;
  for (int iteration = 0; iteration < 12; ++iteration) {
    CaptureController controller;
    if (!controller.startCapture(&error)) {
      qCritical().noquote() << error;
      return 2;
    }
    // Keep dispatching the fixture's shape changes while the capture worker
    // runs, so this checks the reported race instead of a stationary cursor.
    QTest::qWait(900);
    if (!controller.finishCapture(&error)) {
      qCritical().noquote() << error;
      return 2;
    }
    for (const auto &monitor : controller.monitors()) {
      if (!uniform(monitor.image)) {
        monitor.image.save(QStringLiteral("/tmp/omarchy-cursor-leak.png"));
        qCritical() << "Cursor leaked in monitor capture" << monitor.name
                    << iteration;
        return 3;
      }
      ++frames;
    }
    // Exercise the exact grim region helper used by initial, final and resumed
    // scrolling frames, alternating between transformed and normal outputs.
    const QRect region =
        views[iteration % views.size()]->screen()->geometry().adjusted(
            60, 60, -60, -60);
    const QString geometry = QStringLiteral("%1,%2 %3x%4")
                                 .arg(region.x())
                                 .arg(region.y())
                                 .arg(region.width())
                                 .arg(region.height());
    auto capture = std::async(std::launch::async, [&] {
      return captureRegionWithoutCursor(geometry, &error);
    });
    while (capture.wait_for(std::chrono::milliseconds(0)) !=
           std::future_status::ready)
      QTest::qWait(10);
    const QImage image = capture.get();
    if (!uniform(image)) {
      image.save(QStringLiteral("/tmp/omarchy-cursor-leak.png"));
      qCritical() << "Cursor leaked in scrolling capture" << iteration << error;
      return 4;
    }
    ++frames;
  }
  motion.stop();
  // Check the detector against a deliberately cursor-inclusive capture so
  // an unfocused fixture or missing cursor cannot make this test pass falsely.
  const QRectF controlScreen = views.front()->screen()->geometry();
  if (!pointer.moveTo(controlScreen.center(), desktop, &error))
    return 5;
  QTest::qWait(600);
  QProcess control;
  control.start(QStringLiteral("grim"),
                {QStringLiteral("-c"), QStringLiteral("-t"),
                 QStringLiteral("ppm"), QStringLiteral("-o"),
                 views.front()->screen()->name(), QStringLiteral("-")});
  if (!control.waitForFinished(3000) || control.exitCode() != 0 ||
      uniform(QImage::fromData(control.readAllStandardOutput()))) {
    qCritical()
        << "The test fixture did not detect the positive-control cursor";
    return 5;
  }
  QProcess option;
  option.start(QStringLiteral("hyprctl"),
               {QStringLiteral("-j"), QStringLiteral("getoption"),
                QStringLiteral("cursor:invisible")});
  if (!option.waitForFinished(1000) ||
      QJsonDocument::fromJson(option.readAllStandardOutput())
          .object()
          .value(QStringLiteral("bool"))
          .toBool())
    return 6;
  QTextStream(stdout) << "Cursor-free captures passed: " << frames
                      << " frames, " << moves << " real pointer moves across "
                      << views.size()
                      << " outputs; cursor-inclusive control detected\n";
  return 0;
}
