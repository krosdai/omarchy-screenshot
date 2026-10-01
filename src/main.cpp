// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "capturecontroller.h"

#include <LayerShellQt/window.h>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>
#include <QTranslator>

#include <functional>
#include <memory>
#include <tuple>
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
    if (!valid || index < 0)
      return {};
    if (index >= m_controller->monitors().size())
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

  // Parse once before translation so even --help uses the requested language.
  QList<QCommandLineOption> options = {
      {QStringLiteral("language"), QString(), QStringLiteral("locale")},
      {QStringLiteral("self-test"), QString()},
      {QStringLiteral("ui-self-test"), QString()}};
  options[1].setFlags(QCommandLineOption::HiddenFromHelp);
  options[2].setFlags(QCommandLineOption::HiddenFromHelp);
  QCommandLineParser languageParser;
  languageParser.addHelpOption();
  languageParser.addOptions(options);
  languageParser.parse(app.arguments());
  const QString requestedLanguage = languageParser.isSet(QStringLiteral("language"))
      ? languageParser.value(QStringLiteral("language"))
      : qEnvironmentVariable("OMARCHY_SCREENSHOT_LANGUAGE");
  const QLocale requestedLocale = requestedLanguage.isEmpty()
      ? QLocale::system() : QLocale(requestedLanguage);
  QTranslator translator;
  bool translated = false;
  for (const QString &language : requestedLocale.uiLanguages()) {
    const QLocale locale(language);
    // English is the source language, not a missing catalog to skip over.
    if (locale.language() == QLocale::English || locale.language() == QLocale::C)
      break;
    if (translator.load(locale, QStringLiteral("omarchy-screenshot"),
                        QStringLiteral("_"), QStringLiteral(":/i18n"))) {
      translated = true;
      break;
    }
  }
  if (translated)
    app.installTranslator(&translator);
  const QLocale uiLocale = translated ? QLocale(translator.language())
                                     : QLocale(QLocale::English);
  QLocale::setDefault(uiLocale);
  QGuiApplication::setLayoutDirection(uiLocale.textDirection());

  options[0].setDescription(QCoreApplication::translate(
      "main", "Interface language (for example de, pt_BR or zh_TW)."));
  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(
      "main", "Capture and annotate screenshots on Hyprland."));
  parser.addHelpOption();
  parser.addOptions(options);
  parser.process(app);

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
    for (const QPointF &point :
         {QPointF(0, 0), QPointF(10, 10), QPointF(20, -10), QPointF(30, 10),
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
    controller.undo();
    if (!controller.annotations().isEmpty()) {
      qCritical() << "Undo failed";
      return 2;
    }
    controller.redo();
    if (controller.annotations().size() != 1) {
      qCritical() << "Redo failed";
      return 2;
    }
    controller.undo();
    controller.setTool(QStringLiteral("line"));
    controller.pointerPress(0, first.width() * .25, first.height() * .25);
    controller.pointerMove(0, first.width() * .5, first.height() * .4);
    controller.pointerRelease(0, first.width() * .5, first.height() * .4);
    controller.redo();
    if (controller.annotations().size() != 1 ||
        controller.annotations().first().toMap().value(
            QStringLiteral("type")) != QStringLiteral("line")) {
      qCritical() << "Redo history was not cleared after drawing";
      return 2;
    }
    controller.setTool(QStringLiteral("marker"));
    controller.pointerPress(0, first.width() * .3, first.height() * .3);
    controller.pointerRelease(0, first.width() * .3, first.height() * .3);
    if (controller.annotations().size() != 2 ||
        controller.annotations()
                .last()
                .toMap()
                .value(QStringLiteral("number"))
                .toInt() != 1) {
      qCritical() << "Numbered marker failed";
      return 2;
    }
    for (const auto &variant :
         {QPair(QStringLiteral("rect"), QStringLiteral("fillrect")),
          QPair(QStringLiteral("ellipse"), QStringLiteral("fillellipse")),
          QPair(QStringLiteral("ellipse"), QStringLiteral("spotlight")),
          QPair(QStringLiteral("arrow"), QStringLiteral("doublearrow")),
          QPair(QStringLiteral("arrow"), QStringLiteral("line")),
          QPair(QStringLiteral("pen"), QStringLiteral("highlighter"))}) {
      controller.setTool(variant.second);
      controller.setTool(QStringLiteral("text"));
      controller.activateToolGroup(variant.first);
      if (controller.tool() != variant.second ||
          controller.toolVariants().value(variant.first) != variant.second) {
        qCritical() << "Grouped tool selection failed" << variant.first;
        return 2;
      }
    }
    QTextStream(stdout)
        << "Cross-monitor capture OK: " << image.width() << "x"
        << image.height()
        << ", curve smoothing, grouped tools, redo and marker OK\n";
    return 0;
  }

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
  if (!app.arguments().contains(QStringLiteral("--ui-self-test")))
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
    const bool uiTest =
        app.arguments().contains(QStringLiteral("--ui-self-test"));
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
    (app.arguments().contains(QStringLiteral("--ui-self-test")) ? views.front()
                                                                : views.back())
        ->requestActivate();

  auto sendMouse = [&](QEvent::Type type, const QPointF &local,
                       Qt::MouseButton button, Qt::MouseButtons buttons) {
    const QPointF global = controller.monitors()[0].geometry.topLeft() + local;
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
  bool toolbarThemeWorked = false;
  bool toolbarPaddingWorked = false;
  bool toolbarOrderWorked = false;
  bool groupedToolbarWorked = false;
  bool redoShortcutWorked = false;
  bool escapeCloses = false;
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
  bool variantFirstDragWorked = false;
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
  bool doubleClickCopied = false;
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
          views[0]->rootObject()->property("toolbarButtonCount").toInt() >=
              11 &&
          !hasOcrButton.toBool();
      auto displayedAction = [&](int index) {
        QVariant action;
        QMetaObject::invokeMethod(views[0]->rootObject(),
                                  "toolbarDisplayedTool",
                                  Q_RETURN_ARG(QVariant, action),
                                  Q_ARG(QVariant, index));
        return action.toString();
      };
      QVariant closeButton;
      QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarHasAction",
                                Q_RETURN_ARG(QVariant, closeButton),
                                Q_ARG(QVariant, QStringLiteral("cancel")));
      toolbarOrderWorked =
          displayedAction(7) == QStringLiteral("marker") &&
          displayedAction(8) == QStringLiteral("undo") &&
          displayedAction(9) == QStringLiteral("redo") &&
          !closeButton.toBool();
      handlesInitiallyVisible = visibleHandleCount() == 8;
      std::function<QQuickItem *(QQuickItem *, const QString &)>
          findVisualItem =
              [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name)
          return item;
        for (QQuickItem *child : item->childItems())
          if (auto *found = findVisualItem(child, name))
            return found;
        return nullptr;
      };
      auto *cursorIcon = findVisualItem(views[0]->rootObject(),
                                        QStringLiteral("selectCursorGlyph"));
      if (!cursorIcon)
        QTextStream(stdout) << "Cursor diagnostic: visual item missing\n";
      if (cursorIcon) {
        auto grab = cursorIcon->grabToImage();
        if (!grab)
          QTextStream(stdout) << "Cursor diagnostic: grab unavailable\n";
        if (grab)
          QObject::connect(
              grab.get(), &QQuickItemGrabResult::ready, &app, [&, grab] {
                const QImage image = grab->image();
                cursorIconRendered = image.width() >= 16 &&
                                     image.height() >= 20 &&
                                     image.pixelColor(2, 3).alpha() > 0 &&
                                     image.pixelColor(15, 19).alpha() == 0;
                if (!cursorIconRendered)
                  QTextStream(stdout)
                      << "Cursor diagnostic: " << image.width() << "x"
                      << image.height()
                      << " alpha=" << image.pixelColor(2, 3).alpha() << ","
                      << image.pixelColor(15, 19).alpha() << "\n";
              });
      }
      auto *colorButton = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("colorButton"));
      auto *colorPanel = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("colorPanel"));
      auto *toolbarItem = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("toolbar"));
      auto *saturationValueField =
          views[0]->rootObject()->findChild<QQuickItem *>(
              QStringLiteral("saturationValueField"));
      auto *hueField = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("hueField"));
      auto *highlight = findVisualItem(views[0]->rootObject(),
                                       QStringLiteral("selectHighlight"));
      auto *content = findVisualItem(views[0]->rootObject(),
                                     QStringLiteral("selectContent"));
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
        auto themeCenter = [&](int index) {
          QVariant position;
          QMetaObject::invokeMethod(views[0]->rootObject(), "themeButtonCenter",
                                    Q_RETURN_ARG(QVariant, position),
                                    Q_ARG(QVariant, index));
          const auto point = position.toMap();
          return QPointF(point.value(QStringLiteral("x"), -1).toDouble(),
                         point.value(QStringLiteral("y"), -1).toDouble());
        };
        const QPointF lightCenter = themeCenter(0);
        const QPointF darkCenter = themeCenter(1);
        if (opened && toolbarItem && darkCenter.x() >= 0 &&
            lightCenter.x() >= 0) {
          const QColor lightSurface = toolbarItem->property("color").value<QColor>();
          sendMouse(QEvent::MouseButtonPress, darkCenter, Qt::LeftButton,
                    Qt::LeftButton);
          sendMouse(QEvent::MouseButtonRelease, darkCenter, Qt::LeftButton,
                    Qt::NoButton);
          const bool darkApplied = controller.darkToolbar() &&
                                   toolbarItem->property("color").value<QColor>() !=
                                       lightSurface;
          CaptureController restoredTheme;
          const bool darkPersisted = restoredTheme.darkToolbar();
          sendMouse(QEvent::MouseButtonPress, lightCenter, Qt::LeftButton,
                    Qt::LeftButton);
          sendMouse(QEvent::MouseButtonRelease, lightCenter, Qt::LeftButton,
                    Qt::NoButton);
          toolbarThemeWorked = darkApplied && darkPersisted &&
                               !controller.darkToolbar() &&
                               toolbarItem->property("color").value<QColor>() ==
                                   lightSurface &&
                               colorPanel->isVisible();
        }
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
                            colorPanel->isVisible() &&
                            selectedTestColor != QStringLiteral("#ff4b55") &&
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
        colorPickerWorked &=
            shortcutClosed && colorPanel->isVisible() && controller.selected();
        QKeyEvent closePanel(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier,
                             QStringLiteral("q"));
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
        colorPickerWorked &=
            swatchCenter.x() >= 0 && !colorPanel->isVisible() &&
            controller.annotationColor() == QStringLiteral("#ff9d42");
        controller.setAnnotationColor(selectedTestColor);
      }
      const QPointF rectGroupButton = toolbarCenter(1);
      sendMouse(QEvent::MouseButtonPress, rectGroupButton, Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, rectGroupButton, Qt::LeftButton,
                Qt::NoButton);
      QCoreApplication::processEvents();
      auto *variantPanel = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("variantPanel"));
      QVariant optionPosition;
      QMetaObject::invokeMethod(views[0]->rootObject(), "variantOptionCenter",
                                Q_RETURN_ARG(QVariant, optionPosition),
                                Q_ARG(QVariant, 1));
      const auto optionPoint = optionPosition.toMap();
      const QPointF roundRectOption(
          optionPoint.value(QStringLiteral("x"), -1).toDouble(),
          optionPoint.value(QStringLiteral("y"), -1).toDouble());
      const bool groupOpened = variantPanel && variantPanel->isVisible() &&
                               roundRectOption.x() >= 0;
      if (groupOpened) {
        sendMouse(QEvent::MouseButtonPress, roundRectOption, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, roundRectOption, Qt::LeftButton,
                  Qt::NoButton);
      }
      QVariant displayedTool;
      QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarDisplayedTool",
                                Q_RETURN_ARG(QVariant, displayedTool),
                                Q_ARG(QVariant, 1));
      groupedToolbarWorked =
          groupOpened && !variantPanel->isVisible() &&
          controller.tool() == QStringLiteral("roundrect") &&
          controller.toolVariants().value(QStringLiteral("rect")) ==
              QStringLiteral("roundrect") &&
          displayedTool.toString() == QStringLiteral("roundrect");
      controller.setTool(QStringLiteral("line"));
      QKeyEvent groupShortcut(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier,
                              QStringLiteral("r"));
      QCoreApplication::sendEvent(views[0].get(), &groupShortcut);
      groupedToolbarWorked &=
          controller.tool() == QStringLiteral("roundrect");
      QVariant standaloneLine;
      QVariant standaloneSpotlight;
      QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarHasAction",
                                Q_RETURN_ARG(QVariant, standaloneLine),
                                Q_ARG(QVariant, QStringLiteral("line")));
      QMetaObject::invokeMethod(views[0]->rootObject(), "toolbarHasAction",
                                Q_RETURN_ARG(QVariant, standaloneSpotlight),
                                Q_ARG(QVariant, QStringLiteral("spotlight")));
      groupedToolbarWorked &=
          !standaloneLine.toBool() && !standaloneSpotlight.toBool();
      for (const auto &variant :
           {std::tuple(3, 3, QStringLiteral("arrow"), QStringLiteral("line")),
            std::tuple(2, 2, QStringLiteral("ellipse"),
                       QStringLiteral("spotlight"))}) {
        const int buttonIndex = std::get<0>(variant);
        const QPointF groupButton = toolbarCenter(buttonIndex);
        sendMouse(QEvent::MouseButtonPress, groupButton, Qt::LeftButton,
                  Qt::LeftButton);
        sendMouse(QEvent::MouseButtonRelease, groupButton, Qt::LeftButton,
                  Qt::NoButton);
        QCoreApplication::processEvents();
        QVariant variantPosition;
        QMetaObject::invokeMethod(views[0]->rootObject(), "variantOptionCenter",
                                  Q_RETURN_ARG(QVariant, variantPosition),
                                  Q_ARG(QVariant, std::get<1>(variant)));
        const auto point = variantPosition.toMap();
        const QPointF option(point.value(QStringLiteral("x"), -1).toDouble(),
                             point.value(QStringLiteral("y"), -1).toDouble());
        const bool opened = variantPanel->isVisible() && option.x() >= 0;
        if (opened) {
          sendMouse(QEvent::MouseButtonPress, option, Qt::LeftButton,
                    Qt::LeftButton);
          sendMouse(QEvent::MouseButtonRelease, option, Qt::LeftButton,
                    Qt::NoButton);
        }
        QVariant shown;
        QMetaObject::invokeMethod(views[0]->rootObject(),
                                  "toolbarDisplayedTool",
                                  Q_RETURN_ARG(QVariant, shown),
                                  Q_ARG(QVariant, buttonIndex));
        groupedToolbarWorked &=
            opened && !variantPanel->isVisible() &&
            controller.tool() == std::get<3>(variant) &&
            controller.toolVariants().value(std::get<2>(variant)) ==
                std::get<3>(variant) &&
            shown.toString() == std::get<3>(variant);
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
          views[0]->rootObject()->property("mosaicDraftBorderVisible").toBool();
      auto *border = views[0]->rootObject()->findChild<QQuickItem *>(
          QStringLiteral("mosaicDraftBorder"));
      if (border) {
        auto grab = border->grabToImage();
        if (grab)
          QObject::connect(
              grab.get(), &QQuickItemGrabResult::ready, &app, [&, grab] {
                const QImage image = grab->image();
                dragBorderPixelsVisible =
                    image.width() > 10 && image.height() > 10 &&
                    image.pixelColor(1, 1).alpha() > 0 &&
                    image.pixelColor(1, 1).name() == selectedTestColor &&
                    image.pixelColor(image.width() / 2, image.height() / 2)
                            .alpha() == 0;
              });
      }
      QTimer::singleShot(100, &app, [&] {
        sendMouse(QEvent::MouseButtonRelease, QPointF(700, 500), Qt::LeftButton,
                  Qt::NoButton);
        dragBorderGone = !views[0]
                              ->rootObject()
                              ->property("mosaicDraftBorderVisible")
                              .toBool();
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
          QObject::connect(
              grab.get(), &QQuickItemGrabResult::ready, &app, [&, grab] {
                const QImage image = grab->image();
                if (image.width() > 710 && image.height() > 500) {
                  mosaicPixelsVisible = image.pixelColor(475, 375).alpha() > 0;
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
          QTextStream(stdout)
              << "Text focus diagnostic: active=" << views[0]->isActive()
              << " focusWindow="
              << (QGuiApplication::focusWindow() == views[0].get()) << "\n";
        QKeyEvent letters(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier,
                          QStringLiteral("HI"));
        QCoreApplication::sendEvent(views[0].get(), &letters);
        QKeyEvent newline(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier,
                          QStringLiteral("\r"));
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
          QObject::connect(
              grab.get(), &QQuickItemGrabResult::ready, &app, [&, grab] {
                const QImage image = grab->image();
                if (image.width() < 40 || image.height() < 40)
                  return;
                int painted = 0, clear = 0;
                for (int x = 2; x < image.width() - 2; ++x)
                  image.pixelColor(x, 0).alpha() > 16 ? ++painted : ++clear;
                editorBorderDashed = painted > 5 && clear > 5;
                editorBackgroundTransparent =
                    image.pixelColor(image.width() - 10, image.height() / 2)
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
          const qreal scale =
              afterText.width() / controller.selection().width();
          const int textX =
              qRound((start.value(QStringLiteral("x")).toDouble() -
                      controller.selection().x()) *
                     scale);
          const qreal firstY = (start.value(QStringLiteral("y")).toDouble() -
                                controller.selection().y()) *
                               scale;
          const qreal lineHeight =
              views[0]->rootObject()->property("textLineHeight").toDouble() *
              scale;
          bool rowsChanged[2] = {false, false};
          bool customColorPainted = false;
          for (int row = 0; row < 2; ++row)
            for (int y = qMax(
                     0, qFloor(firstY + row * lineHeight - lineHeight / 2));
                 y < qMin(afterText.height(),
                          qCeil(firstY + row * lineHeight + lineHeight / 2));
                 ++y)
              for (int x = qMax(0, textX);
                   x < qMin(afterText.width(), textX + 80); ++x)
                if (afterText.pixel(x, y) != beforeText.pixel(x, y)) {
                  rowsChanged[row] = true;
                  customColorPainted |=
                      afterText.pixelColor(x, y).name() == selectedTestColor;
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
        penDraftWorked = controller.draft()
                             .value(QStringLiteral("points"))
                             .toList()
                             .size() >= 5;
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
        annotationColorsKept =
            oldImageUnchanged && controller.annotations().size() == 3 &&
            controller.annotations()[1].toMap().value(
                QStringLiteral("color")) == selectedTestColor &&
            controller.annotations()[2].toMap().value(
                QStringLiteral("color")) == QStringLiteral("#4d94ff");
        QTimer::singleShot(50, &app, [&] {
          auto *canvas = views[0]->rootObject()->findChild<QQuickItem *>(
              QStringLiteral("marksCanvas"));
          if (!canvas)
            return;
          auto grab = canvas->grabToImage();
          if (!grab)
            return;
          QObject::connect(
              grab.get(), &QQuickItemGrabResult::ready, &app, [&, grab] {
                const QImage image = grab->image();
                const QColor stroke =
                    image.width() > 200 && image.height() > 202
                        ? image.pixelColor(200, 202)
                        : QColor();
                penPreviewVisible =
                    image.width() > 200 && image.height() > 208 &&
                    stroke.alpha() > 0 && stroke.blue() > stroke.red() &&
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
        QKeyEvent markerKey(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                            QStringLiteral("b"));
        QCoreApplication::sendEvent(views[0].get(), &markerKey);
        const bool markerKeyWorked =
            controller.tool() == QStringLiteral("marker") &&
            visibleHandleCount() == 0;
        QKeyEvent penKey(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
                         QStringLiteral("d"));
        QCoreApplication::sendEvent(views[0].get(), &penKey);
        leftHandHotkeysWorked = selectKeyWorked && mosaicKeyWorked &&
                                markerKeyWorked &&
                                controller.tool() == QStringLiteral("pen") &&
                                visibleHandleCount() == 0;
        const int annotationCount = controller.annotations().size();
        QKeyEvent undoKey(QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier,
                           QStringLiteral("z"));
        QCoreApplication::sendEvent(views[0].get(), &undoKey);
        const bool undone = controller.annotations().size() + 1 == annotationCount;
        QKeyEvent redoKey(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
                           QStringLiteral("x"));
        QCoreApplication::sendEvent(views[0].get(), &redoKey);
        redoShortcutWorked = undone &&
                             controller.annotations().size() == annotationCount;
        sendMouse(QEvent::MouseMove, toolbarCenter(6), Qt::NoButton,
                  Qt::NoButton);
        const QString mosaicTooltip =
            views[0]->rootObject()->property("toolbarTooltipText").toString();
        sendMouse(QEvent::MouseMove, toolbarCenter(5), Qt::NoButton,
                  Qt::NoButton);
        const QString textTooltip =
            views[0]->rootObject()->property("toolbarTooltipText").toString();
        hoverDescriptionWorked =
            mosaicTooltip == QCoreApplication::translate(
                "Overlay", "Mosaic · Drag to redact") &&
            textTooltip == QCoreApplication::translate(
                "Overlay", "Text · Click to type");
        if (!hoverDescriptionWorked)
          QTextStream(stdout) << "Tooltip diagnostic: text=" << textTooltip
                              << " mosaic=" << mosaicTooltip << "\n";
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
      sendMouse(QEvent::MouseMove, QPointF(middleX, topY - 30), Qt::NoButton,
                Qt::LeftButton);
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
      arrowResizeWorked = expanded == afterTopResize.adjusted(-1, -1, 1, 1);
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

      const QPointF rectGroupButton = toolbarCenter(1);
      sendMouse(QEvent::MouseButtonPress, rectGroupButton, Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, rectGroupButton, Qt::LeftButton,
                Qt::NoButton);
      auto *activeVariantPanel =
          root->findChild<QQuickItem *>(QStringLiteral("variantPanel"));
      const bool variantOpened =
          activeVariantPanel && activeVariantPanel->isVisible();
      const int beforeFirstDrag = controller.annotations().size();
      sendMouse(QEvent::MouseButtonPress, QPointF(500, 350), Qt::LeftButton,
                Qt::LeftButton);
      const bool firstPressStartedDrawing =
          variantOpened && !activeVariantPanel->isVisible() &&
          controller.draft().value(QStringLiteral("type")).toString() ==
              QStringLiteral("roundrect");
      sendMouse(QEvent::MouseMove, QPointF(560, 400), Qt::NoButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, QPointF(560, 400), Qt::LeftButton,
                Qt::NoButton);
      variantFirstDragWorked =
          firstPressStartedDrawing &&
          controller.annotations().size() == beforeFirstDrag + 1 &&
          controller.annotations()
                  .last()
                  .toMap()
                  .value(QStringLiteral("type"))
                  .toString() == QStringLiteral("roundrect");

      controller.setTool(QStringLiteral("pen"));
      controller.setTool(QStringLiteral("text"));
      const QPointF altTextSpot(450, 350);
      QTest::mouseClick(views[0].get(), Qt::LeftButton, Qt::NoModifier,
                        altTextSpot.toPoint());
      auto *altTextEditor =
          root->findChild<QQuickItem *>(QStringLiteral("textEditor"));
      const bool altEditorOpened = altTextEditor && altTextEditor->isVisible();
      const int beforeAltAnnotations = controller.annotations().size();
      QKeyEvent altText(QEvent::KeyPress, Qt::Key_H, Qt::NoModifier,
                        QStringLiteral("ALT"));
      QCoreApplication::sendEvent(views[0].get(), &altText);
      QKeyEvent altFromEditor(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
      QCoreApplication::sendEvent(views[0].get(), &altFromEditor);
      const bool altFromEditorWorked =
          altEditorOpened && !altTextEditor->isVisible() &&
          controller.tool() == QStringLiteral("pen") &&
          controller.annotations().size() == beforeAltAnnotations + 1 &&
          controller.annotations()
                  .last()
                  .toMap()
                  .value(QStringLiteral("text"))
                  .toString() == QStringLiteral("ALT");
      controller.setTool(QStringLiteral("text"));
      QKeyEvent altFromTool(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
      QCoreApplication::sendEvent(views[0].get(), &altFromTool);
      const bool altFromToolWorked =
          controller.tool() == QStringLiteral("pen") &&
          controller.annotations().size() == beforeAltAnnotations + 1;

      // Replace wl-copy only for this event test so the user's clipboard stays intact.
      QTemporaryDir clipboardTest;
      const QString fakeCopy = clipboardTest.filePath(QStringLiteral("wl-copy"));
      const QString clipboardFile =
          clipboardTest.filePath(QStringLiteral("copied.png"));
      QFile copyScript(fakeCopy);
      const bool stubReady =
          clipboardTest.isValid() && copyScript.open(QIODevice::WriteOnly) &&
          copyScript.write("#!/bin/sh\ncat > \"$OMARCHY_SCREENSHOT_TEST_CLIPBOARD\"\n") > 0;
      copyScript.close();
      if (stubReady) {
        QFile::setPermissions(fakeCopy, QFile::ReadOwner | QFile::WriteOwner |
                                           QFile::ExeOwner | QFile::ReadGroup |
                                           QFile::ExeGroup | QFile::ReadOther |
                                           QFile::ExeOther);
        const QByteArray originalPath = qgetenv("PATH");
        const bool hadTestClipboard =
            qEnvironmentVariableIsSet("OMARCHY_SCREENSHOT_TEST_CLIPBOARD");
        const QByteArray originalTestClipboard =
            qgetenv("OMARCHY_SCREENSHOT_TEST_CLIPBOARD");
        qputenv("PATH", QFile::encodeName(clipboardTest.path()) + ':' +
                            originalPath);
        qputenv("OMARCHY_SCREENSHOT_TEST_CLIPBOARD",
                QFile::encodeName(clipboardFile));
        bool closedAfterCopy = false;
        const auto doneConnection =
            QObject::connect(&controller, &CaptureController::done, &app,
                             [&] { closedAfterCopy = true; });
        auto *textEditor =
            root->findChild<QQuickItem *>(QStringLiteral("textEditor"));
        if (textEditor)
          textEditor->setVisible(false);
        const_cast<QQuickItem *>(root)->forceActiveFocus();
        const QPointF blankSpot(450, 350);
        auto verifyDoubleClick = [&](const QString &tool) {
          controller.setTool(tool);
          const QImage expectedCopy = controller.renderedImage();
          const int annotationCount = controller.annotations().size();
          closedAfterCopy = false;
          QFile::remove(clipboardFile);
          QTest::mouseDClick(views[0].get(), Qt::LeftButton, Qt::NoModifier,
                             blankSpot.toPoint());
          QFile captured(clipboardFile);
          const QImage clipboardImage = captured.open(QIODevice::ReadOnly)
                                            ? QImage::fromData(captured.readAll())
                                            : QImage();
          const bool passed =
              closedAfterCopy && !clipboardImage.isNull() &&
              clipboardImage.convertToFormat(QImage::Format_ARGB32) ==
                  expectedCopy.convertToFormat(QImage::Format_ARGB32) &&
              controller.annotations().size() == annotationCount;
          return passed;
        };
        doubleClickCopied = verifyDoubleClick(QStringLiteral("select")) &&
                            verifyDoubleClick(QStringLiteral("marker")) &&
                            verifyDoubleClick(QStringLiteral("text"));
        QObject::disconnect(doneConnection);
        qputenv("PATH", originalPath);
        if (hadTestClipboard)
          qputenv("OMARCHY_SCREENSHOT_TEST_CLIPBOARD",
                  originalTestClipboard);
        else
          qunsetenv("OMARCHY_SCREENSHOT_TEST_CLIPBOARD");
      }
      int escapeSignals = 0;
      const auto escapeConnection =
          QObject::connect(&controller, &CaptureController::done, &app,
                           [&] { ++escapeSignals; });
      const QPointF blankSpot(450, 350);
      controller.setTool(QStringLiteral("text"));
      QTest::mouseClick(views[0].get(), Qt::LeftButton, Qt::NoModifier,
                        blankSpot.toPoint());
      auto *textEditor =
          root->findChild<QQuickItem *>(QStringLiteral("textEditor"));
      const bool editorOpened = textEditor && textEditor->isVisible();
      QKeyEvent escapeText(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(views[0].get(), &escapeText);
      const bool editorEscapeClosed = editorOpened && escapeSignals == 1;
      if (textEditor)
        textEditor->setVisible(false);
      controller.setTool(QStringLiteral("select"));
      const_cast<QQuickItem *>(root)->forceActiveFocus();
      const QPointF rectButton = toolbarCenter(1);
      sendMouse(QEvent::MouseButtonPress, rectButton, Qt::LeftButton,
                Qt::LeftButton);
      sendMouse(QEvent::MouseButtonRelease, rectButton, Qt::LeftButton,
                Qt::NoButton);
      auto *variantPanel =
          root->findChild<QQuickItem *>(QStringLiteral("variantPanel"));
      const bool panelOpened = variantPanel && variantPanel->isVisible();
      QKeyEvent escapePanel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(views[0].get(), &escapePanel);
      escapeCloses = editorEscapeClosed && panelOpened && escapeSignals == 2;
      QObject::disconnect(escapeConnection);

      // Check long translations on a narrow output without changing desktop settings.
      auto *previewRoot = const_cast<QQuickItem *>(root);
      previewRoot->setWidth(380);
      variantPanel->setVisible(false);
      auto *toolbar = root->findChild<QQuickItem *>(QStringLiteral("toolbar"));
      auto *tooltip = root->findChild<QQuickItem *>(QStringLiteral("toolbarTooltip"));
      auto *label = root->findChild<QQuickItem *>(QStringLiteral("tooltipLabel"));
      bool translationLayoutWorked = toolbar && tooltip && label;
      bool contextualTextHintWorked = false;
      if (translationLayoutWorked) {
        previewRoot->setProperty("toolbarTooltipText", QCoreApplication::translate(
            "Overlay", "Selection · Arrow keys expand, Shift+arrows shrink (1 px)"));
        previewRoot->setProperty("toolbarTooltipX", 190);
        previewRoot->setProperty("toolbarTooltipY", toolbar->y());
        previewRoot->setProperty("toolbarTooltipVisible", true);
        QTest::qWait(50);
        translationLayoutWorked = tooltip->isVisible() && tooltip->x() >= 0 &&
            tooltip->x() + tooltip->width() <= previewRoot->width() &&
            label->property("contentWidth").toReal() <= label->width() + 1 &&
            label->height() <= tooltip->height() - 12 &&
            label->property("horizontalAlignment").toInt() ==
                (uiLocale.textDirection() == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft);

        QFile blockedDirectory(testSettings->filePath(QStringLiteral("not-a-directory")));
        const bool blocked = blockedDirectory.open(QIODevice::WriteOnly);
        blockedDirectory.close();
        const QByteArray oldSaveDirectory = qgetenv("OMARCHY_SCREENSHOT_DIR");
        const bool hadSaveDirectory = qEnvironmentVariableIsSet("OMARCHY_SCREENSHOT_DIR");
        const QString errorPath = blockedDirectory.fileName() +
            QStringLiteral("/<test-folder>/a-long-directory-name-for-wrapping");
        qputenv("OMARCHY_SCREENSHOT_DIR", QFile::encodeName(errorPath));
        controller.save();
        if (hadSaveDirectory)
          qputenv("OMARCHY_SCREENSHOT_DIR", oldSaveDirectory);
        else
          qunsetenv("OMARCHY_SCREENSHOT_DIR");
        auto *status = root->findChild<QQuickItem *>(QStringLiteral("statusPanel"));
        auto *statusLabel = root->findChild<QQuickItem *>(QStringLiteral("statusText"));
        QTest::qWait(50);
        translationLayoutWorked &= blocked && status && statusLabel &&
            status->isVisible() && status->width() <= previewRoot->width() - 16 &&
            statusLabel->property("text").toString() ==
                CaptureController::tr("Cannot create directory: %1").arg(errorPath) &&
            statusLabel->property("textFormat").toInt() == 0 &&
            statusLabel->property("contentWidth").toReal() <= statusLabel->width() + 1 &&
            statusLabel->height() <= status->height() - 16;

        // Optional review crops contain only this app's UI, never captured desktop pixels.
        const QString artifacts = qEnvironmentVariable("OMARCHY_SCREENSHOT_TEST_ARTIFACT_DIR");
        if (!artifacts.isEmpty()) {
          root->findChild<QQuickItem *>(QStringLiteral("screenCaptureImage"))->setVisible(false);
          root->findChild<QQuickItem *>(QStringLiteral("mosaicOverlay"))->setVisible(false);
          root->findChild<QQuickItem *>(QStringLiteral("selectionBorder"))->setVisible(false);
          root->findChild<QQuickItem *>(QStringLiteral("selectionDimensions"))->setVisible(false);
          translationLayoutWorked &= QDir().mkpath(artifacts);
          for (bool dark : {false, true}) {
            controller.setDarkToolbar(dark);
            auto grab = previewRoot->grabToImage();
            if (!grab) {
              translationLayoutWorked = false;
              continue;
            }
            QSignalSpy ready(grab.get(), &QQuickItemGrabResult::ready);
            if (!ready.wait(1000)) {
              translationLayoutWorked = false;
              continue;
            }
            const QRectF crop = QRectF(tooltip->x(), tooltip->y(), tooltip->width(),
                                       tooltip->height()).united(
                QRectF(toolbar->x(), toolbar->y(), toolbar->width(), toolbar->height()))
                                   .adjusted(-6, -6, 6, 6);
            const qreal scale = grab->image().width() / previewRoot->width();
            const QRect pixels = QRectF(crop.topLeft() * scale, crop.size() * scale)
                                     .toAlignedRect();
            const QString path = QDir(artifacts).filePath(
                uiLocale.name() + (dark ? QStringLiteral("-dark.png")
                                       : QStringLiteral("-light.png")));
            translationLayoutWorked &= grab->image().copy(pixels).save(path);
            const QRect statusPixels = QRectF(status->x() * scale, status->y() * scale,
                status->width() * scale, status->height() * scale).toAlignedRect();
            translationLayoutWorked &= grab->image().copy(statusPixels).save(
                QDir(artifacts).filePath(uiLocale.name() + QStringLiteral("-status.png")));
          }
        }

        // The editing hint must replace hover text, wrap at the edges and disappear
        // on both confirmation paths. Enter stays in text mode; Alt returns to pen.
        contextualTextHintWorked = textEditor != nullptr;
        if (textEditor) {
          controller.setTool(QStringLiteral("pen"));
          controller.setTool(QStringLiteral("text"));
          previewRoot->setProperty("toolbarTooltipVisible", false);
          const int originalScreenIndex = root->property("screenIndex").toInt();
          for (const QPointF position : {QPointF(12, 20), QPointF(370, 350)}) {
            // Simulate editing on an output that does not own the toolbar.
            previewRoot->setProperty("screenIndex", position.x() == 12 ? originalScreenIndex : -1);
            previewRoot->setProperty("textX", position.x());
            previewRoot->setProperty("textY", position.y());
            textEditor->setVisible(true);
            QTest::qWait(50);
            contextualTextHintWorked &= tooltip->isVisible() &&
                toolbar->isVisible() == (position.x() == 12) &&
                label->property("text").toString() == QCoreApplication::translate(
                    "Overlay", "Enter: confirm · Shift+Enter: new line · Alt: confirm & return to previous tool") &&
                tooltip->x() >= 8 && tooltip->x() + tooltip->width() <= previewRoot->width() - 8 &&
                tooltip->y() >= 8 && tooltip->y() + tooltip->height() <= previewRoot->height() - 8 &&
                label->property("contentWidth").toReal() <= label->width() + 1 &&
                !QRectF(tooltip->position(), tooltip->size()).intersects(
                    QRectF(textEditor->position(), textEditor->size()));
            if (!artifacts.isEmpty() && position.x() == 370) {
              auto grab = previewRoot->grabToImage();
              QSignalSpy ready(grab.get(), &QQuickItemGrabResult::ready);
              if (ready.wait(1000)) {
                const qreal scale = grab->image().width() / previewRoot->width();
                const QRectF crop = QRectF(tooltip->position(), tooltip->size()).united(
                    QRectF(textEditor->position(), textEditor->size())).adjusted(-6, -6, 6, 6);
                contextualTextHintWorked &= grab->image().copy(
                    QRectF(crop.topLeft() * scale, crop.size() * scale).toAlignedRect()).save(
                        QDir(artifacts).filePath(uiLocale.name() + QStringLiteral("-editing.png")));
              } else {
                contextualTextHintWorked = false;
              }
            }
          }
          previewRoot->setProperty("screenIndex", originalScreenIndex);
          auto *input = root->findChild<QQuickItem *>(QStringLiteral("textEditorInput"));
          input->forceActiveFocus();
          QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          QCoreApplication::sendEvent(views[0].get(), &enter);
          contextualTextHintWorked &= !textEditor->isVisible() && !tooltip->isVisible() &&
              controller.tool() == QStringLiteral("text");
          textEditor->setVisible(true);
          input->forceActiveFocus();
          QKeyEvent alt(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
          QCoreApplication::sendEvent(views[0].get(), &alt);
          contextualTextHintWorked &= !textEditor->isVisible() && !tooltip->isVisible() &&
              controller.tool() == QStringLiteral("pen");
        }
      }
      QTextStream(stdout) << "Mosaic preview: " << mosaicVisible
                          << ", toolbar: " << toolbarWorked
                          << ", toolbar order: " << toolbarOrderWorked
                          << ", grouped tools: " << groupedToolbarWorked
                          << ", color picker: " << colorPickerWorked
                          << ", saved color: " << colorPersistenceWorked
                          << ", toolbar theme: " << toolbarThemeWorked
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
                          << ", held arrow resize: "
                          << repeatingArrowResizeWorked
                          << ", left-hand keys: " << leftHandHotkeysWorked
                          << ", variant first drag: " << variantFirstDragWorked
                          << ", redo X: " << redoShortcutWorked
                          << ", escape closes: " << escapeCloses
                          << ", hover text: " << hoverDescriptionWorked
                          << ", text Alt: " << altFromEditorWorked
                          << ", tool Alt: " << altFromToolWorked
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
                          << ", transparent editor: "
                          << editorBackgroundTransparent
                          << ", centered lines: " << previewRowsCentered
                          << ", exported rows: " << exportedTextRows
                          << ", editor focused: " << editorFocused
                          << ", captures ready: " << capturesReady
                          << ", double-click copy: " << doubleClickCopied
                          << ", translation layout: " << translationLayoutWorked
                          << ", contextual text hint: " << contextualTextHintWorked << "\n";
      app.exit(mosaicVisible && toolbarWorked && toolbarOrderWorked &&
                       groupedToolbarWorked && redoShortcutWorked &&
                       escapeCloses &&
                       colorPickerWorked &&
                       colorPersistenceWorked && toolbarThemeWorked &&
                       toolbarPaddingWorked &&
                       annotationColorsKept && coloredTextExported &&
                       coloredPenExported && ocrButtonRemoved &&
                       cursorIconRendered && handlesInitiallyVisible &&
                       handlesHiddenForMosaic && handleResizeWorked &&
                       arrowResizeWorked && repeatingArrowResizeWorked &&
                       leftHandHotkeysWorked && variantFirstDragWorked &&
                       hoverDescriptionWorked &&
                       altFromEditorWorked && altFromToolWorked &&
                       dragPreviewWorked && mosaicExportChanged &&
                       rectangleShapeWorked && mosaicPixelsVisible &&
                       mosaicMaskTransparent && dragBorderPixelsVisible &&
                       dragBorderGone && penDraftWorked && penExportChanged &&
                       penPreviewVisible && textVisible && multilineEditing &&
                       editorBorderDashed && editorBackgroundTransparent &&
                       previewRowsCentered && exportedTextRows &&
                       editorFocused && capturesReady && doubleClickCopied &&
                       translationLayoutWorked && contextualTextHintWorked
                   ? 0
                   : 2);
    });
  }
  return app.exec();
}
