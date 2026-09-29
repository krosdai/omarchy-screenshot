#include "capturecontroller.h"
#include "../third_party/omasnap/auto-capture.hpp"
#include "../third_party/omasnap/scroll-inject.hpp"
#include "../third_party/omasnap/stitch.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QByteArray run(const QString &program, const QStringList &arguments,
               QString *error) {
  QProcess process;
  process.start(program, arguments);
  if (!process.waitForStarted(3000) || !process.waitForFinished(15000) ||
      process.exitCode() != 0) {
    if (error)
      *error = QStringLiteral("%1: %2").arg(
          program, QString::fromUtf8(process.readAllStandardError()).trimmed());
    return {};
  }
  return process.readAllStandardOutput();
}

bool copyBytes(const QByteArray &bytes, const QString &mimeType,
               QString *error) {
  QProcess process;
  process.start(QStringLiteral("wl-copy"),
                {QStringLiteral("--type"), mimeType});
  if (!process.waitForStarted(3000)) {
    if (error)
      *error =
          QStringLiteral("无法启动 wl-copy：%1").arg(process.errorString());
    return false;
  }
  process.write(bytes);
  process.closeWriteChannel();
  if (!process.waitForFinished(10000) || process.exitCode() != 0) {
    if (error)
      *error = QStringLiteral("复制到剪贴板失败：%1")
                   .arg(QString::fromUtf8(process.readAllStandardError()));
    return false;
  }
  return true;
}

QVariantMap pointMap(const QPointF &p) {
  return {{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}};
}

QPointF fromMap(const QVariant &value) {
  const auto map = value.toMap();
  return {map.value(QStringLiteral("x")).toDouble(),
          map.value(QStringLiteral("y")).toDouble()};
}

QPointF limited(const QPointF &p, const QRectF &rect) {
  return {std::clamp(p.x(), rect.left(), rect.right()),
          std::clamp(p.y(), rect.top(), rect.bottom())};
}

int resizeHandleAt(const QRectF &rect, const QPointF &point) {
  constexpr qreal hitRadius = 15;
  const qreal middleX = rect.center().x();
  const qreal middleY = rect.center().y();
  struct Handle {
    QPointF center;
    int edges;
  };
  const Handle handles[] = {
      {{rect.left(), rect.top()}, 1 | 4},
      {{middleX, rect.top()}, 4},
      {{rect.right(), rect.top()}, 2 | 4},
      {{rect.left(), middleY}, 1},
      {{rect.right(), middleY}, 2},
      {{rect.left(), rect.bottom()}, 1 | 8},
      {{middleX, rect.bottom()}, 8},
      {{rect.right(), rect.bottom()}, 2 | 8},
  };
  qreal closest = std::numeric_limits<qreal>::max();
  int edges = 0;
  for (const auto &handle : handles) {
    const qreal dx = point.x() - handle.center.x();
    const qreal dy = point.y() - handle.center.y();
    if (std::abs(dx) > hitRadius || std::abs(dy) > hitRadius)
      continue;
    const qreal distance = dx * dx + dy * dy;
    if (distance < closest) {
      closest = distance;
      edges = handle.edges;
    }
  }
  return edges;
}

QVector<QPointF> filteredFreehandPoints(const QVariantList &raw) {
  QVector<QPointF> samples;
  samples.reserve(raw.size());
  for (const QVariant &value : raw)
    samples.append(fromMap(value));
  QVector<QPointF> points = samples;
  // A 1-2-1 filter suppresses alternating mouse jitter while keeping both ends
  // at the exact press and release positions.
  for (int i = 1; i + 1 < points.size(); ++i)
    points[i] = (samples[i - 1] + samples[i] * 2 + samples[i + 1]) / 4;
  return points;
}

QVariantMap translatedAnnotation(QVariantMap item, const QPointF &delta) {
  for (const QString &key : {QStringLiteral("start"), QStringLiteral("end")}) {
    if (item.contains(key))
      item.insert(key, pointMap(fromMap(item.value(key)) + delta));
  }
  if (item.contains(QStringLiteral("points"))) {
    QVariantList shifted;
    for (const QVariant &point : item.value(QStringLiteral("points")).toList())
      shifted.append(pointMap(fromMap(point) + delta));
    item.insert(QStringLiteral("points"), shifted);
  }
  return item;
}

QPainterPath spotlightHoles(const QVariantList &annotations) {
  QPainterPath holes;
  holes.setFillRule(Qt::WindingFill);
  for (const QVariant &annotation : annotations) {
    const QVariantMap item = annotation.toMap();
    if (item.value(QStringLiteral("type")).toString() ==
        QStringLiteral("spotlight"))
      holes.addEllipse(QRectF(fromMap(item.value(QStringLiteral("start"))),
                              fromMap(item.value(QStringLiteral("end"))))
                           .normalized());
  }
  return holes;
}
} // namespace

CaptureController::CaptureController(QObject *parent) : QObject(parent) {
  m_scrollTimer.setInterval(280);
  connect(&m_scrollTimer, &QTimer::timeout, this,
          &CaptureController::pollScrollFrame);
  const QColor stored(
      QSettings()
          .value(QStringLiteral("drawing/color"), m_annotationColor)
          .toString());
  if (stored.isValid() && stored.alpha() == 255)
    m_annotationColor = stored.name(QColor::HexRgb);
  m_colorSaveTimer.setSingleShot(true);
  m_colorSaveTimer.setInterval(250);
  connect(&m_colorSaveTimer, &QTimer::timeout, this,
          &CaptureController::saveAnnotationColor);
}

CaptureController::~CaptureController() {
  cancelScrollCapture();
  saveAnnotationColor();
}

bool CaptureController::initialize(QString *error) {
  if (qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland") ||
      qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
    if (error)
      *error = QStringLiteral("需要在 Hyprland Wayland 会话中运行。");
    return false;
  }

  QJsonParseError parseError;
  auto monitorData = QJsonDocument::fromJson(
      run(QStringLiteral("hyprctl"),
          {QStringLiteral("-j"), QStringLiteral("monitors")}, error),
      &parseError);
  if (!monitorData.isArray() || monitorData.array().isEmpty()) {
    if (error && error->isEmpty())
      *error = QStringLiteral("无法读取显示器信息：%1")
                   .arg(parseError.errorString());
    return false;
  }

  const auto screens = QGuiApplication::screens();
  for (const auto &entry : monitorData.array()) {
    const auto data = entry.toObject();
    CaptureMonitor monitor;
    monitor.name = data.value(QStringLiteral("name")).toString();
    monitor.id = data.value(QStringLiteral("id")).toInt(-1);
    monitor.workspace = data.value(QStringLiteral("activeWorkspace"))
                            .toObject()
                            .value(QStringLiteral("id"))
                            .toInt(-1);
    monitor.screen = nullptr;
    for (QScreen *screen : screens) {
      if (screen->name() == monitor.name) {
        monitor.screen = screen;
        break;
      }
    }
    if (!monitor.screen) {
      if (error)
        *error = QStringLiteral("Qt 找不到显示器 %1。").arg(monitor.name);
      return false;
    }
    const qreal scale = data.value(QStringLiteral("scale")).toDouble(1.0);
    if (scale <= 0) {
      if (error)
        *error = QStringLiteral("显示器 %1 的缩放比例无效。").arg(monitor.name);
      return false;
    }
    qreal width = data.value(QStringLiteral("width")).toDouble() / scale;
    qreal height = data.value(QStringLiteral("height")).toDouble() / scale;
    const int transform = data.value(QStringLiteral("transform")).toInt();
    if (transform == 1 || transform == 3 || transform == 5 || transform == 7)
      std::swap(width, height);
    monitor.geometry =
        QRectF(data.value(QStringLiteral("x")).toDouble(),
               data.value(QStringLiteral("y")).toDouble(), width, height);

    const QByteArray bytes =
        run(QStringLiteral("grim"),
            {QStringLiteral("-o"), monitor.name, QStringLiteral("-")}, error);
    monitor.image = QImage::fromData(bytes);
    if (monitor.image.isNull()) {
      if (error && error->isEmpty())
        *error = QStringLiteral("无法截取显示器 %1。").arg(monitor.name);
      return false;
    }
    const QImage coarse =
        monitor.image.scaled(qMax(1, monitor.image.width() / 12),
                             qMax(1, monitor.image.height() / 12),
                             Qt::IgnoreAspectRatio, Qt::FastTransformation);
    monitor.mosaicImage = coarse.scaled(
        monitor.image.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    m_candidates.push_back({monitor.geometry, false});
    m_monitors.push_back(std::move(monitor));
  }

  const auto clients = QJsonDocument::fromJson(
      run(QStringLiteral("hyprctl"),
          {QStringLiteral("-j"), QStringLiteral("clients")}, nullptr));
  for (const auto &entry : clients.array()) {
    const auto data = entry.toObject();
    if (!data.value(QStringLiteral("mapped")).toBool() ||
        data.value(QStringLiteral("hidden")).toBool() ||
        !data.value(QStringLiteral("visible")).toBool(true))
      continue;
    const int monitorId = data.value(QStringLiteral("monitor")).toInt(-1);
    int monitorIndex = -1;
    for (int i = 0; i < m_monitors.size(); ++i) {
      if (m_monitors[i].id == monitorId) {
        monitorIndex = i;
        break;
      }
    }
    if (monitorIndex < 0)
      continue;
    const int workspace = data.value(QStringLiteral("workspace"))
                              .toObject()
                              .value(QStringLiteral("id"))
                              .toInt(-1);
    if (workspace != m_monitors[monitorIndex].workspace &&
        !data.value(QStringLiteral("pinned")).toBool())
      continue;
    const auto at = data.value(QStringLiteral("at")).toArray();
    const auto size = data.value(QStringLiteral("size")).toArray();
    if (at.size() < 2 || size.size() < 2)
      continue;
    const QRectF rect(at[0].toDouble(), at[1].toDouble(), size[0].toDouble(),
                      size[1].toDouble());
    if (rect.isValid())
      m_candidates.push_back({rect, true});
  }
  return true;
}

QPointF CaptureController::globalPoint(int screenIndex, qreal x,
                                       qreal y) const {
  if (screenIndex < 0 || screenIndex >= m_monitors.size())
    return {};
  return m_monitors[screenIndex].geometry.topLeft() + QPointF(x, y);
}

QRectF CaptureController::candidateAt(const QPointF &point) const {
  QRectF best;
  qreal bestArea = std::numeric_limits<qreal>::max();
  for (const auto &candidate : m_candidates) {
    if (!candidate.geometry.contains(point))
      continue;
    const qreal area = candidate.geometry.width() * candidate.geometry.height();
    if (area < bestArea || (area == bestArea && candidate.window)) {
      best = candidate.geometry;
      bestArea = area;
    }
  }
  return best;
}

void CaptureController::setSelection(const QRectF &rect) {
  if (m_selection == rect)
    return;
  m_selection = rect;
  emit selectionChanged();
}

void CaptureController::setHovered(const QRectF &rect) {
  if (m_hovered == rect)
    return;
  m_hovered = rect;
  emit hoveredChanged();
}

void CaptureController::setStatus(const QString &message) {
  m_status = message;
  emit statusChanged();
}

int CaptureController::toolbarScreen() const {
  int best = -1;
  qreal area = 0;
  for (int i = 0; i < m_monitors.size(); ++i) {
    const auto shared = m_selection.intersected(m_monitors[i].geometry);
    const qreal sharedArea = shared.width() * shared.height();
    if (sharedArea > area) {
      best = i;
      area = sharedArea;
    }
  }
  return best;
}

void CaptureController::setTool(const QString &tool) {
  if (tool != QStringLiteral("select") && tool != QStringLiteral("rect") &&
      tool != QStringLiteral("ellipse") && tool != QStringLiteral("arrow") &&
      tool != QStringLiteral("pen") && tool != QStringLiteral("text") &&
      tool != QStringLiteral("mosaic") && tool != QStringLiteral("line") &&
      tool != QStringLiteral("highlighter") &&
      tool != QStringLiteral("spotlight") && tool != QStringLiteral("marker") &&
      tool != QStringLiteral("roundrect") &&
      tool != QStringLiteral("fillrect") &&
      tool != QStringLiteral("fillellipse") &&
      tool != QStringLiteral("curvedarrow") &&
      tool != QStringLiteral("doublearrow"))
    return;
  if (m_tool == tool)
    return;
  m_tool = tool;
  emit toolChanged();
}

void CaptureController::setAnnotationColor(const QString &color) {
  const QColor parsed(color);
  if (!parsed.isValid() || parsed.alpha() != 255)
    return;
  const QString normalized = parsed.name(QColor::HexRgb);
  if (m_annotationColor == normalized)
    return;
  m_annotationColor = normalized;
  m_colorDirty = true;
  m_colorSaveTimer.start();
  emit annotationColorChanged();
}

void CaptureController::setAnnotationColorFromHsv(qreal hue, qreal saturation,
                                                  qreal value) {
  setAnnotationColor(
      QColor::fromHsvF(std::clamp(hue, qreal(0), qreal(1)),
                       std::clamp(saturation, qreal(0), qreal(1)),
                       std::clamp(value, qreal(0), qreal(1)))
          .name(QColor::HexRgb));
}

void CaptureController::saveAnnotationColor() {
  if (!m_colorDirty)
    return;
  m_colorSaveTimer.stop();
  QSettings settings;
  settings.setValue(QStringLiteral("drawing/color"), m_annotationColor);
  settings.sync();
  m_colorDirty = false;
}

void CaptureController::adjustSelectionEdge(int key, bool shrink) {
  if (!m_selected || m_tool != QStringLiteral("select") ||
      m_selection.isEmpty() || m_monitors.isEmpty() || hasScrollImage())
    return;

  QRectF desktop = m_monitors.first().geometry;
  for (const auto &monitor : m_monitors)
    desktop = desktop.united(monitor.geometry);

  qreal left = m_selection.left();
  qreal right = m_selection.right();
  qreal top = m_selection.top();
  qreal bottom = m_selection.bottom();
  switch (key) {
  case Qt::Key_Up:
    top = shrink ? std::min(top + 1, bottom - 2)
                 : std::max(top - 1, desktop.top());
    break;
  case Qt::Key_Down:
    bottom = shrink ? std::max(bottom - 1, top + 2)
                    : std::min(bottom + 1, desktop.bottom());
    break;
  case Qt::Key_Left:
    left = shrink ? std::min(left + 1, right - 2)
                  : std::max(left - 1, desktop.left());
    break;
  case Qt::Key_Right:
    right = shrink ? std::max(right - 1, left + 2)
                   : std::min(right + 1, desktop.right());
    break;
  default:
    return;
  }
  const QRectF next(QPointF(left, top), QPointF(right, bottom));
  if (next != m_selection) {
    m_redoAnnotations.clear();
    setSelection(next);
  }
}

void CaptureController::pointerMove(int screenIndex, qreal x, qreal y) {
  const QPointF point = globalPoint(screenIndex, x, y);
  if (m_drag == Drag::None) {
    if (!m_selected)
      setHovered(candidateAt(point));
    return;
  }
  if (QLineF(m_press, point).length() > 4)
    m_moved = true;
  if (m_drag == Drag::Pan) {
    scrollPreviewBy(m_initialScrollOffset + m_press.y() - point.y() -
                    m_scrollOffset);
  } else if (m_drag == Drag::Select) {
    if (m_moved)
      setSelection(QRectF(m_press, point).normalized());
  } else if (m_drag == Drag::Move) {
    const QPointF delta = point - m_press;
    setSelection(m_initialSelection.translated(delta));
    if (!m_initialAnnotations.isEmpty()) {
      m_annotations.clear();
      for (const QVariant &item : m_initialAnnotations)
        m_annotations.append(translatedAnnotation(item.toMap(), delta));
      emit annotationsChanged();
    }
  } else if (m_drag == Drag::Resize) {
    qreal left = m_initialSelection.left(), right = m_initialSelection.right();
    qreal top = m_initialSelection.top(), bottom = m_initialSelection.bottom();
    if (m_resizeEdges & 1)
      left = std::min(point.x(), right - 2);
    if (m_resizeEdges & 2)
      right = std::max(point.x(), left + 2);
    if (m_resizeEdges & 4)
      top = std::min(point.y(), bottom - 2);
    if (m_resizeEdges & 8)
      bottom = std::max(point.y(), top + 2);
    setSelection(QRectF(QPointF(left, top), QPointF(right, bottom)));
  } else if (m_drag == Drag::Draw) {
    const QPointF virtualPoint =
        point + QPointF(0, hasScrollImage() ? m_scrollOffset : 0);
    const QPointF inside = limited(
        virtualPoint,
        m_selection.translated(0, hasScrollImage() ? m_scrollOffset : 0));
    if (m_tool == QStringLiteral("pen") ||
        m_tool == QStringLiteral("highlighter")) {
      auto points = m_draft.value(QStringLiteral("points")).toList();
      if (points.isEmpty() ||
          QLineF(fromMap(points.last()), inside).length() >= 2) {
        points.append(pointMap(inside));
        m_draft.insert(QStringLiteral("points"), points);
      }
    }
    m_draft.insert(QStringLiteral("end"), pointMap(inside));
    emit draftChanged();
  }
}

void CaptureController::pointerPress(int screenIndex, qreal x, qreal y) {
  if (m_scrolling)
    return;
  const QPointF point = globalPoint(screenIndex, x, y);
  m_press = point;
  m_moved = false;
  if (hasScrollImage() && m_selected && m_tool == QStringLiteral("select") &&
      m_selection.contains(point)) {
    m_initialScrollOffset = m_scrollOffset;
    m_drag = Drag::Pan;
    return;
  }
  if (m_selected && m_tool != QStringLiteral("select")) {
    if (!m_selection.contains(point) || m_tool == QStringLiteral("text"))
      return;
    m_drag = Drag::Draw;
    const QPointF drawPoint =
        point + QPointF(0, hasScrollImage() ? m_scrollOffset : 0);
    m_draft = {{QStringLiteral("type"), m_tool},
               {QStringLiteral("color"), m_annotationColor},
               {QStringLiteral("start"), pointMap(drawPoint)},
               {QStringLiteral("end"), pointMap(drawPoint)}};
    if (m_tool == QStringLiteral("pen") ||
        m_tool == QStringLiteral("highlighter"))
      m_draft.insert(QStringLiteral("points"),
                     QVariantList{pointMap(drawPoint)});
    emit draftChanged();
    return;
  }
  if (m_selected && m_tool == QStringLiteral("select")) {
    m_resizeEdges = resizeHandleAt(m_selection, point);
    if (m_resizeEdges) {
      m_initialSelection = m_selection;
      m_drag = Drag::Resize;
      return;
    }
  }
  if (m_selected && m_selection.adjusted(-7, -7, 7, 7).contains(point)) {
    m_initialSelection = m_selection;
    m_resizeEdges = 0;
    if (std::abs(point.x() - m_selection.left()) <= 7)
      m_resizeEdges |= 1;
    if (std::abs(point.x() - m_selection.right()) <= 7)
      m_resizeEdges |= 2;
    if (std::abs(point.y() - m_selection.top()) <= 7)
      m_resizeEdges |= 4;
    if (std::abs(point.y() - m_selection.bottom()) <= 7)
      m_resizeEdges |= 8;
    m_drag = m_resizeEdges ? Drag::Resize : Drag::Move;
    if (m_drag == Drag::Move)
      m_initialAnnotations = m_annotations;
    return;
  }
  if (!m_annotations.isEmpty()) {
    m_annotations.clear();
    emit annotationsChanged();
  }
  m_redoAnnotations.clear();
  if (!m_scrollImage.isNull()) {
    m_scrollImage = {};
    m_scrollMosaicImage = {};
    m_scrollOffset = 0;
    emit scrollOffsetChanged();
    emit scrollImageChanged();
  }
  m_selected = false;
  emit selectedChanged();
  setSelection({});
  setHovered(candidateAt(point));
  m_drag = Drag::Select;
}

void CaptureController::pointerRelease(int screenIndex, qreal x, qreal y) {
  if (m_drag == Drag::None)
    return;
  pointerMove(screenIndex, x, y);
  if (m_drag == Drag::Draw && (m_tool == QStringLiteral("pen") ||
                               m_tool == QStringLiteral("highlighter"))) {
    const QPointF release = limited(
        globalPoint(screenIndex, x, y) +
            QPointF(0, hasScrollImage() ? m_scrollOffset : 0),
        m_selection.translated(0, hasScrollImage() ? m_scrollOffset : 0));
    auto points = m_draft.value(QStringLiteral("points")).toList();
    if (!points.isEmpty() && fromMap(points.last()) != release) {
      points.append(pointMap(release));
      m_draft.insert(QStringLiteral("points"), points);
      emit draftChanged();
    }
  }
  if (m_drag == Drag::Select) {
    if (!m_moved)
      setSelection(candidateAt(m_press));
    if (m_selection.width() >= 2 && m_selection.height() >= 2) {
      m_selected = true;
      emit selectedChanged();
      setHovered({});
    } else
      setSelection({});
  } else if (m_drag == Drag::Draw &&
             (m_moved || m_tool == QStringLiteral("marker"))) {
    const QRectF area = QRectF(fromMap(m_draft.value(QStringLiteral("start"))),
                               fromMap(m_draft.value(QStringLiteral("end"))))
                            .normalized();
    if (m_tool != QStringLiteral("mosaic") ||
        (area.width() >= 2 && area.height() >= 2)) {
      if (m_tool == QStringLiteral("marker")) {
        int number = 1;
        for (const QVariant &mark : m_annotations)
          if (mark.toMap().value(QStringLiteral("type")).toString() ==
              QStringLiteral("marker"))
            number = std::max(
                number,
                mark.toMap().value(QStringLiteral("number")).toInt() + 1);
        m_draft.insert(QStringLiteral("number"), number);
      }
      appendAnnotation(m_draft);
    }
  }
  if (m_drag == Drag::Draw) {
    m_draft.clear();
    emit draftChanged();
  }
  if (m_moved && (m_drag == Drag::Move || m_drag == Drag::Resize))
    m_redoAnnotations.clear();
  m_drag = Drag::None;
  m_initialAnnotations.clear();
}

void CaptureController::addText(qreal x, qreal y, const QString &text) {
  if (!m_selected || text.trimmed().isEmpty())
    return;
  const QPointF point =
      limited(QPointF(x, y) + QPointF(0, hasScrollImage() ? m_scrollOffset : 0),
              m_selection.translated(0, hasScrollImage() ? m_scrollOffset : 0));
  appendAnnotation(QVariantMap{{QStringLiteral("type"), QStringLiteral("text")},
                               {QStringLiteral("start"), pointMap(point)},
                               {QStringLiteral("color"), m_annotationColor},
                               {QStringLiteral("text"), text}});
}

void CaptureController::appendAnnotation(const QVariantMap &item) {
  m_annotations.append(item);
  m_redoAnnotations.clear();
  emit annotationsChanged();
}

void CaptureController::undo() {
  if (m_annotations.isEmpty())
    return;
  m_redoAnnotations.append(m_annotations.last());
  m_annotations.removeLast();
  emit annotationsChanged();
}

void CaptureController::redo() {
  if (m_redoAnnotations.isEmpty())
    return;
  m_annotations.append(m_redoAnnotations.takeLast());
  emit annotationsChanged();
}

qreal CaptureController::scrollDocumentHeight() const {
  if (m_scrollImage.isNull() || m_scrollImage.width() <= 0)
    return m_selection.height();
  return m_scrollImage.height() * m_selection.width() / m_scrollImage.width();
}

void CaptureController::scrollPreviewBy(qreal distance) {
  if (m_scrollImage.isNull())
    return;
  const qreal maxOffset =
      std::max<qreal>(0, scrollDocumentHeight() - m_selection.height());
  const qreal next = std::clamp(m_scrollOffset + distance, qreal(0), maxOffset);
  if (qFuzzyCompare(next + 1, m_scrollOffset + 1))
    return;
  m_scrollOffset = next;
  emit scrollOffsetChanged();
}

void CaptureController::startScrollCapture(bool horizontal) {
  beginScrollCapture(horizontal, false);
}

void CaptureController::startAutoScrollCapture(bool horizontal) {
  beginScrollCapture(horizontal, true);
}

void CaptureController::beginScrollCapture(bool horizontal, bool automatic) {
  if (!m_selected || m_scrolling)
    return;
  if (hasScrollImage()) {
    setStatus(QStringLiteral("请重新选择区域后开始新的滚动截图"));
    return;
  }
  int monitorIndex = -1;
  for (int i = 0; i < m_monitors.size(); ++i) {
    if (m_monitors[i].geometry.contains(m_selection)) {
      monitorIndex = i;
      break;
    }
  }
  if (monitorIndex < 0 || m_selection.width() < 64 ||
      m_selection.height() < 64) {
    setStatus(
        QStringLiteral("滚动截图需要在单个显示器内选择至少 64×64 的区域"));
    return;
  }
  const QRectF display = m_monitors[monitorIndex].geometry;
  if (m_selection.top() - display.top() < 56 &&
      display.bottom() - m_selection.bottom() < 56) {
    setStatus(QStringLiteral("请在选区上方或下方为完成按钮留出 56px 空间"));
    return;
  }
  m_scrollMonitor = monitorIndex;
  const quint64 generation = ++m_scrollGeneration;
  const auto axis =
      horizontal ? stitch::Axis::Horizontal : stitch::Axis::Vertical;
  m_autoScroll = automatic;
  m_lastAutoCycle = 0;
  if (automatic) {
    m_autoSession = std::make_unique<stitch::AutoCapture>(axis);
    m_scrollHandshake = std::make_shared<stitch::CaptureHandshake>();
    m_injectStop = std::make_shared<std::atomic<bool>>(false);
    m_scrollTimer.setInterval(450);
  } else {
    m_scrollSession = std::make_unique<stitch::ManualCapture>(axis);
    m_scrollTimer.setInterval(280);
  }
  m_scrolling = true;
  emit scrollingChanged();
  setStatus(automatic
                ? QStringLiteral("自动滚动准备中…")
                : QStringLiteral("在选区内滚动页面，完成后点击“完成长截图”"));
  // Wait for the live input hole and hidden frozen frame to reach the display.
  QTimer::singleShot(350, this, [this, axis, generation] {
    if (m_scrolling && m_scrollGeneration == generation) {
      if (m_autoScroll) {
        const auto &monitor = m_monitors[m_scrollMonitor];
        const qreal sx = monitor.image.width() / monitor.geometry.width();
        const qreal sy = monitor.image.height() / monitor.geometry.height();
        const QPointF local = m_selection.center() - monitor.geometry.topLeft();
        const int parkX = qRound(local.x() * sx);
        const int parkY = qRound(local.y() * sy);
        const auto stop = m_injectStop;
        const auto handshake = m_scrollHandshake;
        const QString output = monitor.name;
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this,
                [this, watcher, stop, axis] {
                  const QString error = watcher->result();
                  watcher->deleteLater();
                  if (!m_scrolling || m_injectStop != stop)
                    return;
                  if (!error.isEmpty()) {
                    stop->store(true, std::memory_order_release);
                    m_autoSession.reset();
                    m_autoScroll = false;
                    m_scrollSession =
                        std::make_unique<stitch::ManualCapture>(axis);
                    setStatus(
                        QStringLiteral("自动滚动不可用：%1；可手动滚动继续截图")
                            .arg(error));
                  }
                });
        watcher->setFuture(
            QtConcurrent::run([stop, handshake, parkX, parkY, axis, output] {
              QString error;
              if (!spawnScrollInjector(stop, handshake, parkX, parkY, axis,
                                       output, error) &&
                  error.isEmpty())
                error = QStringLiteral("滚动注入已取消");
              return error;
            }));
      }
      pollScrollFrame();
      m_scrollTimer.start();
    }
  });
}

void CaptureController::pollScrollFrame() {
  if (!m_scrolling || m_scrollProcess || m_scrollMonitor < 0)
    return;
  if (m_autoScroll) {
    if (!m_scrollHandshake ||
        m_scrollHandshake->readyCycle() <= m_lastAutoCycle) {
      if (m_injectStop && m_injectStop->load(std::memory_order_acquire) &&
          m_scrollHandshake && m_scrollHandshake->readyCycle() > 0) {
        m_scrollTimer.stop();
        setStatus(QStringLiteral("自动滚动已停止，可点击完成保存已拼接部分"));
      }
      return;
    }
    m_captureAutoCycle = m_scrollHandshake->readyCycle();
  }
  const auto &monitor = m_monitors[m_scrollMonitor];
  const quint64 generation = m_scrollGeneration;
  auto *process = new QProcess(this);
  m_scrollProcess = process;
  connect(
      process, &QProcess::errorOccurred, this,
      [this, process, generation](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_scrolling ||
            m_scrollGeneration != generation)
          return;
        if (m_scrollProcess == process)
          m_scrollProcess = nullptr;
        m_scrollTimer.stop();
        setStatus(
            QStringLiteral("无法启动 grim：%1").arg(process->errorString()));
        process->deleteLater();
      });
  connect(
      process, &QProcess::finished, this,
      [this, process, generation](int exitCode,
                                  QProcess::ExitStatus exitStatus) {
        if (m_scrollProcess == process)
          m_scrollProcess = nullptr;
        if (m_scrolling && m_scrollGeneration == generation &&
            exitStatus == QProcess::NormalExit && exitCode == 0) {
          const auto &monitor = m_monitors[m_scrollMonitor];
          const QImage screen =
              QImage::fromData(process->readAllStandardOutput());
          if (!screen.isNull()) {
            const qreal sx = screen.width() / monitor.geometry.width();
            const qreal sy = screen.height() / monitor.geometry.height();
            const QRectF local =
                m_selection.translated(-monitor.geometry.topLeft());
            const QRect crop(qRound(local.x() * sx), qRound(local.y() * sy),
                             qRound(local.width() * sx),
                             qRound(local.height() * sy));
            const QImage frame = screen.copy(crop.intersected(screen.rect()))
                                     .convertToFormat(QImage::Format_RGBA8888);
            if (!frame.isNull() && m_autoScroll && m_autoSession) {
              const auto result = m_autoSession->feed(frame);
              using Event = stitch::AutoCapture::Event;
              using Ack = stitch::AutoCapture::Ack;
              if (result.event != Event::Blank)
                m_lastAutoCycle = m_captureAutoCycle;
              if (result.event == Event::ReachedEnd ||
                  result.event == Event::ReachedEndAtSeam) {
                finishScrollCapture();
                process->deleteLater();
                return;
              }
              if (result.event == Event::Halted ||
                  result.event == Event::Paused) {
                m_scrollTimer.stop();
                if (m_injectStop)
                  m_injectStop->store(true, std::memory_order_release);
                setStatus(result.error.isEmpty()
                              ? QStringLiteral("自动滚动已暂停：画面无法可靠匹"
                                               "配，可点击完成保存已拼接部分")
                              : result.error);
              } else if (result.ack == Ack::Normal) {
                m_scrollHandshake->acknowledge(m_captureAutoCycle);
                m_scrollTimer.start();
                setStatus(QStringLiteral("自动滚动中，已拼接 %1 帧")
                              .arg(m_autoSession->keptFrames()));
              } else if (result.ack == Ack::Probe) {
                m_scrollHandshake->acknowledgeWithNotches(m_captureAutoCycle,
                                                          1);
                m_scrollTimer.start();
              }
            } else if (!frame.isNull() && m_scrollSession) {
              const auto result = m_scrollSession->feed(frame);
              using Event = stitch::ManualCapture::Event;
              if (result.event == Event::Kept)
                setStatus(
                    QStringLiteral("长截图已拼接 %1 帧，继续滚动或点击完成")
                        .arg(result.keptFrames));
              else if (result.event == Event::Unmatchable ||
                       result.event == Event::Ambiguous)
                setStatus(QStringLiteral(
                    "当前画面无法可靠匹配，请缓慢滚动并保持相邻画面重叠"));
              else if (result.event == Event::Full ||
                       result.event == Event::Error)
                setStatus(
                    result.error.isEmpty()
                        ? QStringLiteral("长截图已达到大小上限，请点击完成")
                        : result.error);
            }
          }
        } else if (m_scrolling && m_scrollGeneration == generation) {
          m_scrollTimer.stop();
          setStatus(QStringLiteral("抓取滚动画面失败：%1")
                        .arg(QString::fromUtf8(process->readAllStandardError())
                                 .trimmed()));
        }
        process->deleteLater();
      });
  process->start(QStringLiteral("grim"),
                 {QStringLiteral("-o"), monitor.name, QStringLiteral("-")});
}

void CaptureController::finishScrollCapture() {
  if (!m_scrolling)
    return;
  m_scrollTimer.stop();
  if (m_scrollProcess)
    m_scrollProcess->kill();
  QString error;
  QImage result = m_autoScroll && m_autoSession ? m_autoSession->finish(error)
                  : m_scrollSession             ? m_scrollSession->finish(error)
                                                : QImage();
  if (result.isNull()) {
    setStatus(error.isEmpty() ? QStringLiteral("尚未获得可用的滚动画面")
                              : error);
    if (error == QStringLiteral("no frames were captured"))
      m_scrollTimer.start();
    return;
  }
  m_scrollImage = std::move(result);
  m_redoAnnotations.clear();
  m_scrollOffset = 0;
  emit scrollOffsetChanged();
  const QImage coarse = m_scrollImage.scaled(
      qMax(1, m_scrollImage.width() / 12), qMax(1, m_scrollImage.height() / 12),
      Qt::IgnoreAspectRatio, Qt::FastTransformation);
  m_scrollMosaicImage = coarse;
  m_scrollSession.reset();
  m_autoSession.reset();
  if (m_injectStop)
    m_injectStop->store(true, std::memory_order_release);
  m_scrollHandshake.reset();
  m_injectStop.reset();
  m_autoScroll = false;
  ++m_scrollGeneration;
  m_scrolling = false;
  emit scrollImageChanged();
  emit scrollingChanged();
  setStatus(QStringLiteral("长截图已拼接：%1 × %2")
                .arg(m_scrollImage.width())
                .arg(m_scrollImage.height()));
}

void CaptureController::cancelScrollCapture() {
  if (!m_scrolling)
    return;
  m_scrollTimer.stop();
  if (m_scrollProcess)
    m_scrollProcess->kill();
  m_scrollSession.reset();
  m_autoSession.reset();
  if (m_injectStop)
    m_injectStop->store(true, std::memory_order_release);
  m_scrollHandshake.reset();
  m_injectStop.reset();
  m_autoScroll = false;
  ++m_scrollGeneration;
  m_scrolling = false;
  emit scrollingChanged();
  setStatus({});
}

void CaptureController::paintAnnotation(QPainter &painter,
                                        const QVariantMap &item) const {
  const QString type = item.value(QStringLiteral("type")).toString();
  const QPointF start = fromMap(item.value(QStringLiteral("start")));
  const QPointF end = fromMap(item.value(QStringLiteral("end")));
  QColor color(item.value(QStringLiteral("color")).toString());
  if (!color.isValid())
    color = QColor(QStringLiteral("#ff4b55"));
  painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  painter.setBrush(Qt::NoBrush);
  if (type == QStringLiteral("rect"))
    painter.drawRect(QRectF(start, end).normalized());
  else if (type == QStringLiteral("roundrect"))
    painter.drawRoundedRect(QRectF(start, end).normalized(), 10, 10);
  else if (type == QStringLiteral("fillrect"))
    painter.fillRect(QRectF(start, end).normalized(), color);
  else if (type == QStringLiteral("fillellipse")) {
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(start, end).normalized());
  } else if (type == QStringLiteral("ellipse"))
    painter.drawEllipse(QRectF(start, end).normalized());
  else if (type == QStringLiteral("line"))
    painter.drawLine(start, end);
  else if (type == QStringLiteral("arrow")) {
    painter.drawLine(start, end);
    const qreal angle = std::atan2(end.y() - start.y(), end.x() - start.x());
    const qreal wing = 12;
    painter.drawLine(end, end - QPointF(std::cos(angle - .55) * wing,
                                        std::sin(angle - .55) * wing));
    painter.drawLine(end, end - QPointF(std::cos(angle + .55) * wing,
                                        std::sin(angle + .55) * wing));
  } else if (type == QStringLiteral("curvedarrow") ||
             type == QStringLiteral("doublearrow")) {
    const QPointF delta = end - start;
    const QPointF control =
        (start + end) / 2 + QPointF(-delta.y(), delta.x()) * .22;
    QPainterPath curve(start);
    curve.quadTo(control, end);
    painter.drawPath(curve);
    const auto head = [&](const QPointF &tip, const QPointF &from) {
      const qreal angle = std::atan2(tip.y() - from.y(), tip.x() - from.x());
      painter.drawLine(tip, tip - QPointF(std::cos(angle - .55) * 12,
                                          std::sin(angle - .55) * 12));
      painter.drawLine(tip, tip - QPointF(std::cos(angle + .55) * 12,
                                          std::sin(angle + .55) * 12));
    };
    head(end, control);
    if (type == QStringLiteral("doublearrow"))
      head(start, control);
  } else if (type == QStringLiteral("pen") ||
             type == QStringLiteral("highlighter")) {
    const auto points = item.value(QStringLiteral("points")).toList();
    if (points.isEmpty())
      return;
    if (type == QStringLiteral("highlighter")) {
      color.setAlpha(88);
      painter.setPen(
          QPen(color, 18, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.drawPath(freehandPath(points));
  } else if (type == QStringLiteral("marker")) {
    painter.setBrush(color);
    painter.drawEllipse(start, 13, 13);
    painter.setPen(QColor(Qt::white));
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(14);
    painter.setFont(font);
    painter.drawText(
        QRectF(start.x() - 13, start.y() - 13, 26, 26), Qt::AlignCenter,
        QString::number(item.value(QStringLiteral("number")).toInt()));
  } else if (type == QStringLiteral("text")) {
    QFont font = painter.font();
    font.setPixelSize(22);
    font.setBold(true);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const qreal lineHeight = metrics.lineSpacing();
    const qreal baseline =
        (lineHeight - metrics.height()) / 2 + metrics.ascent();
    const auto lines = item.value(QStringLiteral("text"))
                           .toString()
                           .split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (int i = 0; i < lines.size(); ++i)
      painter.drawText(QPointF(start.x(), start.y() - lineHeight / 2 +
                                              i * lineHeight + baseline),
                       lines[i]);
  }
}

QVariantList
CaptureController::smoothedFreehandPoints(const QVariantList &raw) const {
  QVariantList smoothed;
  for (const QPointF &point : filteredFreehandPoints(raw))
    smoothed.append(pointMap(point));
  return smoothed;
}

QPainterPath CaptureController::freehandPath(const QVariantList &raw) {
  QPainterPath path;
  const auto points = filteredFreehandPoints(raw);
  if (points.isEmpty())
    return path;
  path.moveTo(points.first());
  if (points.size() == 1)
    return path;
  if (points.size() == 2) {
    path.lineTo(points.last());
    return path;
  }
  for (int i = 1; i + 1 < points.size(); ++i)
    path.quadTo(points[i], (points[i] + points[i + 1]) / 2);
  path.quadTo(points.last(), points.last());
  return path;
}

QPainterPath CaptureController::mosaicPath(const QVariantMap &item) {
  QPainterPath result;
  if (item.value(QStringLiteral("type")).toString() != QStringLiteral("mosaic"))
    return result;
  result.addRect(QRectF(fromMap(item.value(QStringLiteral("start"))),
                        fromMap(item.value(QStringLiteral("end"))))
                     .normalized());
  return result;
}

QImage CaptureController::renderedImage() const {
  if (!m_selected || m_selection.isEmpty())
    return {};
  if (!m_scrollImage.isNull()) {
    QImage result =
        m_scrollImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal scaleX = result.width() / m_selection.width();
    const QRectF document(m_selection.topLeft(),
                          QSizeF(m_selection.width(), scrollDocumentHeight()));
    const QPainterPath holes = spotlightHoles(m_annotations);
    if (!holes.isEmpty()) {
      QPainter painter(&result);
      painter.scale(scaleX, scaleX);
      painter.translate(-m_selection.topLeft());
      QPainterPath outer;
      outer.addRect(document);
      painter.fillPath(outer.subtracted(holes), QColor(0, 0, 0, 150));
    }
    for (const QVariant &annotation : m_annotations) {
      const auto item = annotation.toMap();
      if (item.value(QStringLiteral("type")).toString() ==
          QStringLiteral("spotlight"))
        continue;
      QPainter painter(&result);
      painter.scale(scaleX, scaleX);
      painter.translate(-m_selection.topLeft());
      painter.setClipRect(document);
      if (item.value(QStringLiteral("type")).toString() ==
          QStringLiteral("mosaic")) {
        painter.setClipPath(mosaicPath(item), Qt::IntersectClip);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(document, m_scrollMosaicImage);
      } else {
        painter.setRenderHint(QPainter::Antialiasing);
        paintAnnotation(painter, item);
      }
    }
    return result;
  }
  qreal scale = 1;
  for (const auto &monitor : m_monitors)
    scale = std::max(scale, monitor.image.width() / monitor.geometry.width());
  const QSize size(qMax(1, qRound(m_selection.width() * scale)),
                   qMax(1, qRound(m_selection.height() * scale)));
  QImage result(size, QImage::Format_ARGB32_Premultiplied);
  if (result.isNull())
    return {};
  result.fill(Qt::transparent);
  {
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const auto &monitor : m_monitors) {
      const QRectF shared = m_selection.intersected(monitor.geometry);
      if (shared.isEmpty())
        continue;
      const QRectF source(
          (shared.x() - monitor.geometry.x()) * monitor.image.width() /
              monitor.geometry.width(),
          (shared.y() - monitor.geometry.y()) * monitor.image.height() /
              monitor.geometry.height(),
          shared.width() * monitor.image.width() / monitor.geometry.width(),
          shared.height() * monitor.image.height() / monitor.geometry.height());
      const QRectF target((shared.x() - m_selection.x()) * scale,
                          (shared.y() - m_selection.y()) * scale,
                          shared.width() * scale, shared.height() * scale);
      painter.drawImage(target, monitor.image, source);
    }
  }
  for (const QVariant &annotation : m_annotations) {
    const auto item = annotation.toMap();
    if (item.value(QStringLiteral("type")).toString() !=
        QStringLiteral("mosaic"))
      continue;
    const QPainterPath shape = mosaicPath(item);
    if (shape.isEmpty())
      continue;
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.scale(scale, scale);
    painter.translate(-m_selection.topLeft());
    painter.setClipRect(m_selection);
    painter.setClipPath(shape, Qt::IntersectClip);
    for (const auto &monitor : m_monitors) {
      if (!shape.boundingRect().intersects(monitor.geometry))
        continue;
      painter.drawImage(monitor.geometry, monitor.mosaicImage);
    }
  }
  const QPainterPath holes = spotlightHoles(m_annotations);
  if (!holes.isEmpty()) {
    QPainter painter(&result);
    painter.scale(scale, scale);
    painter.translate(-m_selection.topLeft());
    QPainterPath outer;
    outer.addRect(m_selection);
    painter.fillPath(outer.subtracted(holes), QColor(0, 0, 0, 150));
  }
  for (const QVariant &annotation : m_annotations) {
    const auto item = annotation.toMap();
    if (item.value(QStringLiteral("type")).toString() ==
            QStringLiteral("mosaic") ||
        item.value(QStringLiteral("type")).toString() ==
            QStringLiteral("spotlight"))
      continue;
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    painter.translate(-m_selection.topLeft());
    paintAnnotation(painter, item);
  }
  return result;
}

void CaptureController::copy() {
  const auto image = renderedImage();
  if (image.isNull())
    return;
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  QString error;
  if (!copyBytes(png, QStringLiteral("image/png"), &error)) {
    setStatus(error);
    return;
  }
  emit done();
}

void CaptureController::ocr() {
  const auto image = renderedImage();
  if (image.isNull())
    return;
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");

  const QString languages = QString::fromUtf8(run(
      QStringLiteral("tesseract"), {QStringLiteral("--list-langs")}, nullptr));
  const QString language = languages.contains(QStringLiteral("chi_sim"))
                               ? QStringLiteral("chi_sim+eng")
                               : QStringLiteral("eng");
  QProcess process;
  process.start(QStringLiteral("tesseract"),
                {QStringLiteral("stdin"), QStringLiteral("stdout"),
                 QStringLiteral("-l"), language});
  if (!process.waitForStarted(3000)) {
    setStatus(QStringLiteral("OCR 需要安装 tesseract"));
    return;
  }
  process.write(png);
  process.closeWriteChannel();
  if (!process.waitForFinished(30000) || process.exitCode() != 0) {
    setStatus(
        QStringLiteral("识字失败：%1")
            .arg(QString::fromUtf8(process.readAllStandardError()).trimmed()));
    return;
  }
  const QByteArray recognized = process.readAllStandardOutput().trimmed();
  if (recognized.isEmpty()) {
    setStatus(QStringLiteral("没有识别到文字"));
    return;
  }
  QString error;
  if (!copyBytes(recognized, QStringLiteral("text/plain"), &error)) {
    setStatus(error);
    return;
  }
  emit done();
}

void CaptureController::save() {
  const auto image = renderedImage();
  if (image.isNull())
    return;
  QString directory = qEnvironmentVariable("OMARCHY_SCREENSHOT_DIR");
  if (directory.isEmpty())
    directory =
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (directory.isEmpty())
    directory = QDir::homePath() + QStringLiteral("/Pictures");
  if (!QDir().mkpath(directory)) {
    setStatus(QStringLiteral("无法创建目录：%1").arg(directory));
    return;
  }
  const QString filename = QStringLiteral("screenshot-%1.png")
                               .arg(QDateTime::currentDateTime().toString(
                                   QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz")));
  const QString path = QDir(directory).filePath(filename);
  if (!image.save(path, "PNG")) {
    setStatus(QStringLiteral("无法保存：%1").arg(path));
    return;
  }
  QTextStream(stdout) << path << '\n';
  emit done();
}

void CaptureController::cancel() { emit done(); }
