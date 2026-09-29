#include "capturecontroller.h"
#include "mosaicoverlay.h"

#include <LayerShellQt/window.h>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class CaptureImageProvider final : public QQuickImageProvider {
public:
  explicit CaptureImageProvider(const CaptureController *controller)
      : QQuickImageProvider(QQuickImageProvider::Image),
        m_controller(controller) {}

  QImage requestImage(const QString &id, QSize *size,
                      const QSize &requestedSize) override {
    Q_UNUSED(requestedSize)
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() != 2)
      return {};
    bool valid = false;
    const int index = parts[1].toInt(&valid);
    if (!valid || index < 0 || index >= m_controller->monitors().size())
      return {};
    const QImage *image = nullptr;
    if (parts[0] == QStringLiteral("screen"))
      image = &m_controller->monitors()[index].image;
    else if (parts[0] == QStringLiteral("mosaic"))
      image = &m_controller->monitors()[index].mosaicImage;
    if (!image)
      return {};
    if (size)
      *size = image->size();
    return *image;
  }

private:
  const CaptureController *m_controller;
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("omarchy-screenshot"));
  app.setOrganizationName(QStringLiteral("Omarchy"));

  std::unique_ptr<QTemporaryDir> testSettings;
  if (app.arguments().contains(QStringLiteral("--self-test")) ||
      app.arguments().contains(QStringLiteral("--ui-self-test"))) {
    testSettings = std::make_unique<QTemporaryDir>();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       testSettings->path());
  }

  CaptureController controller;
  QString error;
  if (!controller.initialize(&error)) {
    qCritical().noquote() << error;
    return 1;
  }

  if (app.arguments().contains(QStringLiteral("--self-test"))) {
    if (controller.monitors().size() < 2) {
      qCritical() << "Cross-monitor self-test needs two displays";
      return 2;
    }
    const auto &first = controller.monitors()[0].geometry;
    const auto &second = controller.monitors()[1].geometry;
    controller.pointerPress(0, first.width() * .2, first.height() * .2);
    controller.pointerMove(1, second.width() * .7, second.height() * .7);
    controller.pointerRelease(1, second.width() * .7, second.height() * .7);
    const auto image = controller.renderedImage();
    if (!controller.selected() || image.isNull() || image.width() < 2 ||
        image.height() < 2) {
      qCritical() << "Cross-monitor selection or composition failed";
      return 2;
    }
    for (int i = 0; i < 2; ++i) {
      const QRectF part =
          controller.selection().intersected(controller.monitors()[i].geometry);
      if (part.isEmpty()) {
        qCritical() << "Selection missed monitor" << i;
        return 2;
      }
      const QPointF center = part.center() - controller.selection().topLeft();
      const int sampleX = qBound(
          0,
          qRound(center.x() * image.width() / controller.selection().width()),
          image.width() - 1);
      const int sampleY = qBound(
          0,
          qRound(center.y() * image.height() / controller.selection().height()),
          image.height() - 1);
      if (image.pixelColor(sampleX, sampleY).alpha() == 0) {
        qCritical() << "Transparent pixels in monitor" << i;
        return 2;
      }
    }
    controller.setTool(QStringLiteral("mosaic"));
    controller.pointerPress(0, first.width() * .25, first.height() * .25);
    controller.pointerMove(0, first.width() * .5, first.height() * .45);
    controller.pointerRelease(0, first.width() * .5, first.height() * .45);
    if (controller.annotations().size() != 1 ||
        controller.renderedImage().size() != image.size()) {
      qCritical() << "Mosaic composition failed";
      return 2;
    }
    QVariantList jitter;
    for (const QPointF &point : {QPointF(0, 0), QPointF(10, 10),
                                 QPointF(20, -10), QPointF(30, 10),
                                 QPointF(40, 0)})
      jitter.append(QVariantMap{{QStringLiteral("x"), point.x()},
                                {QStringLiteral("y"), point.y()}});
    const auto smoothed = controller.smoothedFreehandPoints(jitter);
    const auto path = CaptureController::freehandPath(jitter);
    if (smoothed.size() != jitter.size() ||
        smoothed[1].toMap().value(QStringLiteral("y")).toDouble() >= 10 ||
        smoothed.first().toMap() != jitter.first().toMap() ||
        smoothed.last().toMap() != jitter.last().toMap() ||
        path.elementCount() <= jitter.size() ||
        path.elementAt(1).type != QPainterPath::CurveToElement) {
      qCritical() << "Freehand smoothing failed";
      return 2;
    }
    QTextStream(stdout) << "Cross-monitor capture OK: " << image.width() << "x"
                        << image.height() << ", curve smoothing OK\n";
    return 0;
  }

  qmlRegisterType<MosaicOverlay>("ScreenshotInternals", 1, 0,
                                 "MosaicOverlay");
  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings, &app,
                   [](const QList<QQmlError> &warnings) {
                     for (const auto &warning : warnings)
                       QTextStream(stderr) << warning.toString() << '\n';
                   });
  engine.rootContext()->setContextProperty(QStringLiteral("captureController"),
                                           &controller);
  engine.addImageProvider(QStringLiteral("captures"),
                          new CaptureImageProvider(&controller));
  QObject::connect(&controller, &CaptureController::done, &app,
                   &QCoreApplication::quit);

  std::vector<std::unique_ptr<QQuickView>> views;
  for (int i = 0; i < controller.monitors().size(); ++i) {
    const auto &monitor = controller.monitors()[i];
    auto view = std::make_unique<QQuickView>(&engine, nullptr);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->setColor(Qt::transparent);
    view->setFlags(Qt::FramelessWindowHint);
    view->setScreen(monitor.screen);
    view->setInitialProperties(
        {{QStringLiteral("screenIndex"), i},
         {QStringLiteral("screenRect"), monitor.geometry}});

    auto *layer = LayerShellQt::Window::get(view.get());
    layer->setScope(QStringLiteral("omarchy-screenshot"));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(
        LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
        LayerShellQt::Window::AnchorBottom | LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight);
    // Ignore the bar's reserved area: the frozen image uses the full output.
    layer->setExclusiveZone(-1);
    const bool uiTest = app.arguments().contains(QStringLiteral("--ui-self-test"));
    layer->setKeyboardInteractivity(
        uiTest ? (i == 0 ? LayerShellQt::Window::KeyboardInteractivityExclusive
                         : LayerShellQt::Window::KeyboardInteractivityNone)
               : LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setScreen(monitor.screen);

    view->setSource(
        QUrl(QStringLiteral("qrc:/qt/qml/OmarchyScreenshot/Overlay.qml")));
    if (view->status() != QQuickView::Ready) {
      qCritical() << "Cannot load Overlay.qml for" << monitor.name;
      return 1;
    }
    const auto *root = view->rootObject();
    const auto *image =
        root->findChild<QObject *>(QStringLiteral("screenCaptureImage"));
    const QString expectedSource =
        QStringLiteral("image://captures/screen/%1").arg(i);
    if (root->property("screenIndex").toInt() != i || !image ||
        image->property("source").toUrl().toString() != expectedSource) {
      qCritical() << "Wrong image source for" << monitor.name;
      return 1;
    }
    view->show();
    views.push_back(std::move(view));
  }
  if (!views.empty())
    (app.arguments().contains(QStringLiteral("--ui-self-test"))
         ? views.front()
         : views.back())->requestActivate();

  auto sendMouse = [&](QEvent::Type type, const QPointF &local,
                       Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF global =
        controller.monitors()[0].geometry.topLeft() + local;
    QMouseEvent event(type, local, global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(views[0].get(), &event);
  };
  auto toolbarCenter = [&](int index) {
    QVariant position;
    QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarButtonCenter",
                              Q_RETURN_ARG(QVariant, position),
                              Q_ARG(QVariant, index));
    const auto point = position.toMap();
    return QPointF(point.value(QStringLiteral("x")).toDouble(),
                   point.value(QStringLiteral("y")).toDouble());
  };
  auto visibleHandleCount = [&] {
    QVariant count;
    QMetaObject::invokeMethod(views[0]->rootObject(),
                              "visibleResizeHandleCount",
                              Q_RETURN_ARG(QVariant, count));
    return count.toInt();
  };
  QImage baseline;
  bool toolbarWorked = false;
  bool colorPickerWorked = false;
  bool colorPersistenceWorked = false;
  bool toolbarPaddingWorked = false;
  QString selectedTestColor;
  bool annotationColorsKept = false;
  bool coloredTextExported = false;
  bool coloredPenExported = false;
  bool ocrButtonRemoved = false;
  bool cursorIconRendered = false;
  bool handlesInitiallyVisible = false;
  bool handlesHiddenForMosaic = false;
  bool handleResizeWorked = false;
  bool arrowResizeWorked = false;
  bool repeatingArrowResizeWorked = false;
  bool leftHandHotkeysWorked = false;
  bool hoverDescriptionWorked = false;
  bool dragPreviewWorked = false;
  bool mosaicExportChanged = false;
  bool rectangleShapeWorked = false;
  bool mosaicPixelsVisible = false;
  bool mosaicMaskTransparent = false;
  bool dragBorderPixelsVisible = false;
  bool dragBorderGone = false;
  bool penDraftWorked = false;
  bool penExportChanged = false;
  bool penPreviewVisible = false;
  bool editorFocused = false;
  bool multilineEditing = false;
  bool editorBorderDashed = false;
  bool editorBackgroundTransparent = false;
  bool exportedTextRows = false;
  if (app.arguments().contains(QStringLiteral("--ui-self-test"))) {
    controller.pointerPress(0, 100, 100);
    controller.pointerMove(0, 900, 600);
    controller.pointerRelease(0, 900, 600);
    baseline = controller.renderedImage();
    QTimer::singleShot(100, &app, [&] {
      views[0]->requestActivate();
      QVariant hasOcrButton;
      QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarHasAction",
                                Q_RETURN_ARG(QVariant, hasOcrButton),
                                Q_ARG(QVariant, QStringLiteral("ocr")));
      ocrButtonRemoved =
          views[0]->rootObject()->property("toolbarButtonCount").toInt() == 11 &&
          !hasOcrButton.toBool();
      handlesInitiallyVisible = visibleHandleCount() == 8;
      std::function<QQuickItem *(QQuickItem *, const QString &)> findVisualItem =
          [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name)
          return item;
        for (QQuickItem *child : item->childItems())
          if (auto *found = findVisualItem(child, name))
            return found;
        return nullptr;
      };
      auto *cursorIcon = findVisualItem(
          views[0]->rootObject(), QStringLiteral("selectCursorGlyph"));
      if (!cursorIcon)
        QTextStream(stdout) << "Cursor diagnostic: visual item missing\n";
      if (cursorIcon) {
        auto grab = cursorIcon->grabToImage();
        if (!grab)
          QTextStream(stdout) << "Cursor diagnostic: grab unavailable\n";
        if (grab)
          QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &app,
                           [&, grab] {
                             const QImage image = grab->image();
                             cursorIconRendered =
                                 image.width() >= 16 && image.height() >= 20 &&
                                 image.pixelColor(2, 3).alpha() > 0 &&
                                 image.pixelColor(15, 19).alpha() == 0;
                             if (!cursorIconRendered)
                               QTextStream(stdout)
                                   << "Cursor diagnostic: " << image.width()
                                   << "x" << image.height() << " alpha="
                                   << image.pixelColor(2, 3).alpha() << ","
                                   << image.pixelColor(15, 19).alpha() << "\n";
                           });
      }
      auto *colorButton = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("colorButton"));
      auto *colorPanel = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("colorPanel"));
      auto *saturationValueField = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("saturationValueField"));
      auto *hueField = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("hueField"));
      auto *highlight = findVisualItem(
          views[0]->rootObject(), QStringLiteral("selectHighlight"));
      auto *content = findVisualItem(
          views[0]->rootObject(), QStringLiteral("selectContent"));
      toolbarPaddingWorked = highlight && content &&
          highlight->width() - content->width() >= 8 &&
          highlight->height() - content->height() >= 8;
      if (colorButton && colorPanel && saturationValueField && hueField) {
        const QPointF buttonCenter = colorButton->mapToItem(
            views[0]->rootObject(),
            QPointF(colorButton->width() / 2, colorButton->height() / 2));
        sendMouse(QEvent::MouseButtonPress, buttonCenter, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, buttonCenter, Qt::LeftButton,
                  Qt::NoButton);
        const bool opened = colorPanel->isVisible();
        const bool colorButtonUnselected =
            colorButton->property("color").value<QColor>().alpha() == 0;
        const QPointF huePoint = hueField->mapToItem(
            views[0]->rootObject(),
            QPointF(hueField->width() / 2, hueField->height() * .42));
        sendMouse(QEvent::MouseButtonPress, huePoint, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, huePoint, Qt::LeftButton,
                  Qt::NoButton);
        const QPointF palettePoint = saturationValueField->mapToItem(
            views[0]->rootObject(),
            QPointF(saturationValueField->width() * .72,
                    saturationValueField->height() * .22));
        sendMouse(QEvent::MouseButtonPress, palettePoint, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, palettePoint, Qt::LeftButton,
                  Qt::NoButton);
        selectedTestColor = controller.annotationColor();
        colorPickerWorked = opened && colorButtonUnselected &&
            colorPanel->isVisible() && selectedTestColor !=
                QStringLiteral("#ff4b55") &&
            !views[0]->rootObject()->findChild<QQuickItem *>(
                QStringLiteral("colorHexInput")) &&
            !views[0]->rootObject()->findChild<QQuickItem *>(
                QStringLiteral("colorApplyButton"));
        QKeyEvent colorKey(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier,
                           QStringLiteral("q"));
        QCoreApplication::sendEvent(views[0].get(), &colorKey);
        const bool shortcutClosed = !colorPanel->isVisible();
        controller.saveAnnotationColor();
        CaptureController restored;
        colorPersistenceWorked =
            restored.annotationColor() == selectedTestColor;
        QCoreApplication::sendEvent(views[0].get(), &colorKey);
        colorPickerWorked &= shortcutClosed && colorPanel->isVisible() &&
                             controller.selected();
        QKeyEvent closePanel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(views[0].get(), &closePanel);
        colorPickerWorked &= !colorPanel->isVisible();
        sendMouse(QEvent::MouseButtonPress, buttonCenter, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, buttonCenter, Qt::LeftButton,
                  Qt::NoButton);
        QVariant swatchPosition;
        QMetaObject::invokeMethod(views[0]->rootObject(), "colorPresetCenter",
                                  Q_RETURN_ARG(QVariant, swatchPosition),
                                  Q_ARG(QVariant, 1));
        const auto swatchPoint = swatchPosition.toMap();
        const QPointF swatchCenter(
            swatchPoint.value(QStringLiteral("x"), -1).toDouble(),
            swatchPoint.value(QStringLiteral("y"), -1).toDouble());
        if (swatchCenter.x() >= 0 && colorPanel->isVisible()) {
          sendMouse(QEvent::MouseButtonPress, swatchCenter, Qt::LeftButton,
                    Qt::LeftButton);
          sendMouse(QEvent::MouseButtonRelease, swatchCenter, Qt::LeftButton,
                    Qt::NoButton);
        }
        colorPickerWorked &= swatchCenter.x() >= 0 &&
                             !colorPanel->isVisible() &&
                             controller.annotationColor() ==
                                 QStringLiteral("#ff9d42");
        controller.setAnnotationColor(selectedTestColor);
      }
      const QPointF mosaicButton = toolbarCenter(6);
      sendMouse(QEvent::MouseButtonPress, mosaicButton, Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, mosaicButton, Qt::LeftButton,
                Qt::NoButton);
      toolbarWorked = controller.tool() == QStringLiteral("mosaic");
      handlesHiddenForMosaic = visibleHandleCount() == 0;
      sendMouse(QEvent::MouseButtonPress, QPointF(250, 250), Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseMove, QPointF(700, 500), Qt::NoButton,
                Qt::LeftButton);
      const auto draft = controller.draft();
      const auto start = draft.value(QStringLiteral("start")).toMap();
      const auto end = draft.value(QStringLiteral("end")).toMap();
      dragPreviewWorked =
          draft.value(QStringLiteral("type")).toString() ==
              QStringLiteral("mosaic") &&
          draft.value(QStringLiteral("points")).toList().isEmpty() &&
          end.value(QStringLiteral("x")).toDouble() >
              start.value(QStringLiteral("x")).toDouble() &&
          end.value(QStringLiteral("y")).toDouble() >
              start.value(QStringLiteral("y")).toDouble() &&
          views[0]->rootObject()
              ->property("mosaicDraftBorderVisible").toBool();
      auto *border = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("mosaicDraftBorder"));
      if (border) {
        auto grab = border->grabToImage();
        if (grab)
          QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &app,
                           [&, grab] {
                             const QImage image = grab->image();
                             dragBorderPixelsVisible =
                                 image.width() > 10 && image.height() > 10 &&
                                 image.pixelColor(1, 1).alpha() > 0 &&
                                 image.pixelColor(image.width() / 2,
                                                  image.height() / 2)
                                         .alpha() == 0;
                           });
      }
      QTimer::singleShot(100, &app, [&] {
        sendMouse(QEvent::MouseButtonRelease, QPointF(700, 500),
                  Qt::LeftButton, Qt::NoButton);
        dragBorderGone = !views[0]->rootObject()
                              ->property("mosaicDraftBorderVisible").toBool();
        const QImage withMosaic = controller.renderedImage();
        if (withMosaic.size() == baseline.size()) {
          for (int y = 0; y < baseline.height() && !mosaicExportChanged; ++y)
            for (int x = 0; x < baseline.width(); ++x)
              if (baseline.pixel(x, y) != withMosaic.pixel(x, y)) {
                mosaicExportChanged = true;
                break;
              }
        }
        if (!controller.annotations().isEmpty()) {
          const auto &monitor = controller.monitors()[0];
          const auto item = controller.annotations().first().toMap();
          const auto shape = CaptureController::mosaicPath(item);
          rectangleShapeWorked =
              item.value(QStringLiteral("points")).toList().isEmpty() &&
              shape.contains(monitor.geometry.topLeft() + QPointF(475, 375)) &&
              !shape.contains(monitor.geometry.topLeft() + QPointF(710, 260));
        }
        QTimer::singleShot(50, &app, [&] {
          auto *overlay = views[0]->rootObject()->findChild<QQuickItem *>(
              QStringLiteral("mosaicOverlay"));
          if (!overlay)
            return;
          auto grab = overlay->grabToImage();
          if (!grab)
            return;
          QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &app,
                           [&, grab] {
                             const QImage image = grab->image();
                             if (image.width() > 710 && image.height() > 500) {
                               mosaicPixelsVisible =
                                   image.pixelColor(475, 375).alpha() > 0;
                               mosaicMaskTransparent =
                                   image.pixelColor(710, 260).alpha() == 0;
                             }
                           });
        });
      });
      QTimer::singleShot(300, &app, [&] {
        controller.setTool(QStringLiteral("text"));
        const QImage beforeText = controller.renderedImage();
        views[0]->requestActivate();
        const QPointF local(750, 550);
        const QPointF global =
            controller.monitors()[0].geometry.topLeft() + local;
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(views[0].get(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(views[0].get(), &release);
        editorFocused =
            views[0]->rootObject()->property("editorFocused").toBool();
        if (!editorFocused)
          QTextStream(stdout) << "Text focus diagnostic: active="
                              << views[0]->isActive()
                              << " focusWindow="
                              << (QGuiApplication::focusWindow() == views[0].get())
                              << "\n";
        QKeyEvent letters(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier,
                          QStringLiteral("HI"));
        QCoreApplication::sendEvent(views[0].get(), &letters);
        QKeyEvent newline(QEvent::KeyPress, Qt::Key_Return,
                          Qt::ShiftModifier, QStringLiteral("\r"));
        QCoreApplication::sendEvent(views[0].get(), &newline);
        QKeyEvent secondLine(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                             QStringLiteral("BY"));
        QCoreApplication::sendEvent(views[0].get(), &secondLine);
        multilineEditing =
            views[0]->rootObject()->property("editorText").toString() ==
                QStringLiteral("HI\nBY") &&
            views[0]->rootObject()->property("editorRows").toInt() == 2;
        QTimer::singleShot(35, &app, [&] {
          auto *editor = views[0]->rootObject()->findChild<QQuickItem *>(
              QStringLiteral("textEditor"));
          if (!editor)
            return;
          auto grab = editor->grabToImage();
          if (!grab)
            return;
          QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &app,
                           [&, grab] {
                             const QImage image = grab->image();
                             if (image.width() < 40 || image.height() < 40)
                               return;
                             int painted = 0, clear = 0;
                             for (int x = 2; x < image.width() - 2; ++x)
                               image.pixelColor(x, 0).alpha() > 16 ? ++painted
                                                                    : ++clear;
                             editorBorderDashed = painted > 5 && clear > 5;
                             editorBackgroundTransparent =
                                 image.pixelColor(image.width() - 10,
                                                  image.height() / 2)
                                     .alpha() == 0;
                           });
        });
        QTimer::singleShot(120, &app, [&, beforeText] {
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                          QStringLiteral("\r"));
          QCoreApplication::sendEvent(views[0].get(), &enter);
          if (controller.annotations().size() < 2)
            return;
          const auto annotation = controller.annotations()[1].toMap();
          const auto start = annotation.value(QStringLiteral("start")).toMap();
          const QImage afterText = controller.renderedImage();
          if (afterText.size() != beforeText.size())
            return;
          const qreal scale = afterText.width() / controller.selection().width();
          const int textX = qRound((start.value(QStringLiteral("x")).toDouble() -
                                    controller.selection().x()) * scale);
          const qreal firstY =
              (start.value(QStringLiteral("y")).toDouble() -
               controller.selection().y()) * scale;
          const qreal lineHeight =
              views[0]->rootObject()->property("textLineHeight").toDouble() *
              scale;
          bool rowsChanged[2] = {false, false};
          bool customColorPainted = false;
          for (int row = 0; row < 2; ++row)
            for (int y = qMax(0, qFloor(firstY + row * lineHeight - lineHeight / 2));
                 y < qMin(afterText.height(),
                          qCeil(firstY + row * lineHeight + lineHeight / 2));
                 ++y)
              for (int x = qMax(0, textX);
                   x < qMin(afterText.width(), textX + 80); ++x)
                if (afterText.pixel(x, y) != beforeText.pixel(x, y)) {
                  rowsChanged[row] = true;
                  customColorPainted |=
                      afterText.pixelColor(x, y).name() ==
                      selectedTestColor;
                }
          exportedTextRows = rowsChanged[0] && rowsChanged[1];
          coloredTextExported = customColorPainted;
        });
      });
      QTimer::singleShot(530, &app, [&] {
        const QImage beforePen = controller.renderedImage();
        controller.setAnnotationColor(QStringLiteral("#4d94ff"));
        const bool oldImageUnchanged = beforePen == controller.renderedImage();
        controller.setTool(QStringLiteral("pen"));
        controller.pointerPress(0, 150, 200);
        controller.pointerMove(0, 200, 208);
        controller.pointerMove(0, 250, 192);
        controller.pointerMove(0, 300, 208);
        controller.pointerMove(0, 350, 200);
        penDraftWorked =
            controller.draft().value(QStringLiteral("points")).toList().size() >= 5;
        controller.pointerRelease(0, 350, 200);
        const QImage afterPen = controller.renderedImage();
        penExportChanged = beforePen != afterPen;
        if (beforePen.size() == afterPen.size())
          for (int y = 0; y < afterPen.height() && !coloredPenExported; ++y)
            for (int x = 0; x < afterPen.width(); ++x)
              if (beforePen.pixel(x, y) != afterPen.pixel(x, y) &&
                  afterPen.pixelColor(x, y).name() ==
                      QStringLiteral("#4d94ff")) {
                coloredPenExported = true;
                break;
              }
        annotationColorsKept = oldImageUnchanged &&
            controller.annotations().size() == 3 &&
            controller.annotations()[1].toMap().value(QStringLiteral("color")) ==
                selectedTestColor &&
            controller.annotations()[2].toMap().value(QStringLiteral("color")) ==
                QStringLiteral("#4d94ff");
        QTimer::singleShot(50, &app, [&] {
          auto *canvas = views[0]->rootObject()->findChild<QQuickItem *>(
              QStringLiteral("marksCanvas"));
          if (!canvas)
            return;
          auto grab = canvas->grabToImage();
          if (!grab)
            return;
          QObject::connect(grab.get(), &QQuickItemGrabResult::ready, &app,
                           [&, grab] {
                             const QImage image = grab->image();
                             const QColor stroke = image.width() > 200 &&
                                                       image.height() > 202
                                                       ? image.pixelColor(200, 202)
                                                       : QColor();
                             penPreviewVisible =
                                 image.width() > 200 && image.height() > 208 &&
                                 stroke.alpha() > 0 &&
                                 stroke.blue() > stroke.red() &&
                                 image.pixelColor(200, 208).alpha() == 0;
                           });
        });
      });
      QTimer::singleShot(800, &app, [&] {
        views[0]->requestActivate();
        views[0]->rootObject()->forceActiveFocus();
        QKeyEvent selectKey(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier,
                            QStringLiteral("v"));
        QCoreApplication::sendEvent(views[0].get(), &selectKey);
        const bool selectKeyWorked =
            controller.tool() == QStringLiteral("select") &&
            visibleHandleCount() == 8;
        QKeyEvent mosaicKey(QEvent::KeyPress, Qt::Key_G, Qt::NoModifier,
                            QStringLiteral("g"));
        QCoreApplication::sendEvent(views[0].get(), &mosaicKey);
        const bool mosaicKeyWorked =
            controller.tool() == QStringLiteral("mosaic") &&
            visibleHandleCount() == 0;
        QKeyEvent penKey(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
                         QStringLiteral("d"));
        QCoreApplication::sendEvent(views[0].get(), &penKey);
        leftHandHotkeysWorked =
            selectKeyWorked && mosaicKeyWorked &&
            controller.tool() == QStringLiteral("pen") &&
            visibleHandleCount() == 0;
        sendMouse(QEvent::MouseMove, toolbarCenter(6), Qt::NoButton,
                  Qt::NoButton);
        hoverDescriptionWorked =
            views[0]->rootObject()->property("toolbarTooltipText").toString() ==
            QStringLiteral("马赛克：拖动选择矩形区域");
      });
    });
    QTimer::singleShot(1600, &app, [&] {
      const auto *root = views[0]->rootObject();
      QVariant preview;
      QMetaObject::invokeMethod(const_cast<QQuickItem *>(root), "previewState",
                                Q_RETURN_ARG(QVariant, preview));
      const auto state = preview.toMap();
      const bool mosaicVisible =
          state.value(QStringLiteral("mosaicVisible")).toBool() &&
          state.value(QStringLiteral("mosaicWidth")).toDouble() > 0 &&
          state.value(QStringLiteral("mosaicImageReady")).toBool();
      const bool textVisible =
          state.value(QStringLiteral("textVisible")).toBool() &&
          state.value(QStringLiteral("textValue")).toString() ==
              QStringLiteral("HI\nBY");
      const qreal textLineHeight =
          state.value(QStringLiteral("textLineHeight")).toDouble();
      const bool previewRowsCentered =
          state.value(QStringLiteral("textRows")).toInt() == 2 &&
          qAbs(state.value(QStringLiteral("firstLineCenter")).toDouble() -
               textLineHeight / 2) < 0.1 &&
          qAbs(state.value(QStringLiteral("secondLineCenter")).toDouble() -
               textLineHeight * 1.5) < 0.1;
      QTextStream(stdout)
          << "qmlCount=" << root->property("annotationCount").toInt()
          << " mosaicExists="
          << state.value(QStringLiteral("mosaicExists")).toBool()
          << " textExists="
          << state.value(QStringLiteral("textExists")).toBool() << "\n";
      bool capturesReady = true;
      for (const auto &view : views)
        capturesReady &= view->rootObject()->property("captureReady").toBool();
      const QRectF beforeResize = controller.selection();
      QKeyEvent selectKey(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier,
                          QStringLiteral("v"));
      views[0]->rootObject()->forceActiveFocus();
      QCoreApplication::sendEvent(views[0].get(), &selectKey);
      sendMouse(QEvent::MouseButtonPress, QPointF(88, 88), Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseMove, QPointF(70, 70), Qt::NoButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, QPointF(70, 70), Qt::LeftButton,
                Qt::NoButton);
      const QRectF afterResize = controller.selection();
      const bool cornerResizeWorked =
          qAbs(afterResize.left() - (beforeResize.left() - 30)) < 0.1 &&
          qAbs(afterResize.top() - (beforeResize.top() - 30)) < 0.1 &&
          qAbs(afterResize.right() - beforeResize.right()) < 0.1 &&
          qAbs(afterResize.bottom() - beforeResize.bottom()) < 0.1;
      const auto &monitorGeometry = controller.monitors()[0].geometry;
      const qreal middleX = afterResize.center().x() - monitorGeometry.x();
      const qreal topY = afterResize.top() - monitorGeometry.y();
      sendMouse(QEvent::MouseButtonPress, QPointF(middleX, topY - 12),
                Qt::LeftButton, Qt::LeftButton);
      sendMouse(QEvent::MouseMove, QPointF(middleX, topY - 30),
                Qt::NoButton, Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, QPointF(middleX, topY - 30),
                Qt::LeftButton, Qt::NoButton);
      const QRectF afterTopResize = controller.selection();
      handleResizeWorked =
          cornerResizeWorked &&
          qAbs(afterTopResize.top() - (afterResize.top() - 30)) < 0.1 &&
          qAbs(afterTopResize.left() - afterResize.left()) < 0.1 &&
          qAbs(afterTopResize.right() - afterResize.right()) < 0.1 &&
          qAbs(afterTopResize.bottom() - afterResize.bottom()) < 0.1;
      const_cast<QQuickItem *>(root)->forceActiveFocus();
      auto pressArrow = [&](Qt::Key key, Qt::KeyboardModifiers modifiers,
                            bool repeated = false) {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, QString(), repeated);
        QCoreApplication::sendEvent(views[0].get(), &event);
      };
      pressArrow(Qt::Key_Up, Qt::NoModifier);
      pressArrow(Qt::Key_Down, Qt::NoModifier);
      pressArrow(Qt::Key_Left, Qt::NoModifier);
      pressArrow(Qt::Key_Right, Qt::NoModifier);
      const QRectF expanded = controller.selection();
      arrowResizeWorked =
          expanded == afterTopResize.adjusted(-1, -1, 1, 1);
      pressArrow(Qt::Key_Up, Qt::ShiftModifier);
      pressArrow(Qt::Key_Down, Qt::ShiftModifier);
      pressArrow(Qt::Key_Left, Qt::ShiftModifier);
      pressArrow(Qt::Key_Right, Qt::ShiftModifier);
      arrowResizeWorked &= controller.selection() == afterTopResize;
      pressArrow(Qt::Key_Right, Qt::NoModifier);
      for (int i = 0; i < 3; ++i)
        pressArrow(Qt::Key_Right, Qt::NoModifier, true);
      repeatingArrowResizeWorked =
          controller.selection() == afterTopResize.adjusted(0, 0, 4, 0);
      pressArrow(Qt::Key_Right, Qt::ShiftModifier);
      for (int i = 0; i < 3; ++i)
        pressArrow(Qt::Key_Right, Qt::ShiftModifier, true);
      repeatingArrowResizeWorked &= controller.selection() == afterTopResize;
      QTextStream(stdout) << "Mosaic preview: " << mosaicVisible
                          << ", toolbar: " << toolbarWorked
                          << ", color picker: " << colorPickerWorked
                          << ", saved color: " << colorPersistenceWorked
                          << ", button padding: " << toolbarPaddingWorked
                          << ", old/new colors: " << annotationColorsKept
                          << ", colored text export: " << coloredTextExported
                          << ", colored pen export: " << coloredPenExported
                          << ", F icon removed: " << ocrButtonRemoved
                          << ", cursor icon: " << cursorIconRendered
                          << ", eight handles: " << handlesInitiallyVisible
                          << ", handles hidden: " << handlesHiddenForMosaic
                          << ", enlarged hit: " << handleResizeWorked
                          << ", arrow resize: " << arrowResizeWorked
                          << ", held arrow resize: " << repeatingArrowResizeWorked
                          << ", left-hand keys: " << leftHandHotkeysWorked
                          << ", hover text: " << hoverDescriptionWorked
                          << ", drag: " << dragPreviewWorked
                          << ", export changed: " << mosaicExportChanged
                          << ", rectangle shape: " << rectangleShapeWorked
                          << ", visible pixels: " << mosaicPixelsVisible
                          << ", transparent outside: " << mosaicMaskTransparent
                          << ", drag border: " << dragBorderPixelsVisible
                          << ", border gone: " << dragBorderGone
                          << ", pen draft: " << penDraftWorked
                          << ", pen export: " << penExportChanged
                          << ", pen preview: " << penPreviewVisible
                          << ", text preview: " << textVisible
                          << ", multiline edit: " << multilineEditing
                          << ", dashed border: " << editorBorderDashed
                          << ", transparent editor: " << editorBackgroundTransparent
                          << ", centered lines: " << previewRowsCentered
                          << ", exported rows: " << exportedTextRows
                          << ", editor focused: " << editorFocused
                          << ", captures ready: " << capturesReady << "\n";
      app.exit(mosaicVisible && toolbarWorked && colorPickerWorked &&
                       colorPersistenceWorked && toolbarPaddingWorked &&
                       annotationColorsKept && coloredTextExported &&
                       coloredPenExported &&
                       ocrButtonRemoved &&
                       cursorIconRendered && handlesInitiallyVisible &&
                       handlesHiddenForMosaic && handleResizeWorked &&
                       arrowResizeWorked && repeatingArrowResizeWorked &&
                       leftHandHotkeysWorked &&
                       hoverDescriptionWorked && dragPreviewWorked &&
                       mosaicExportChanged && rectangleShapeWorked &&
                       mosaicPixelsVisible && mosaicMaskTransparent &&
                       dragBorderPixelsVisible && dragBorderGone &&
                       penDraftWorked && penExportChanged &&
                       penPreviewVisible && textVisible && multilineEditing &&
                       editorBorderDashed && editorBackgroundTransparent &&
                       previewRowsCentered && exportedTextRows &&
                       editorFocused && capturesReady
                   ? 0
                   : 2);
    });
  }
  return app.exec();
}
