// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "capturecontroller.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

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
      *error = CaptureController::tr("Cannot start wl-copy: %1")
                   .arg(process.errorString());
    return false;
  }
  process.write(bytes);
  process.closeWriteChannel();
  if (!process.waitForFinished(10000) || process.exitCode() != 0) {
    if (error)
      *error = CaptureController::tr("Failed to copy to the clipboard: %1")
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
  const QColor stored(
      QSettings()
          .value(QStringLiteral("drawing/color"), m_annotationColor)
          .toString());
  if (stored.isValid() && stored.alpha() == 255)
    m_annotationColor = stored.name(QColor::HexRgb);
  m_darkToolbar = QSettings().value(QStringLiteral("toolbar/dark"), false).toBool();
  m_colorSaveTimer.setSingleShot(true);
  m_colorSaveTimer.setInterval(250);
  connect(&m_colorSaveTimer, &QTimer::timeout, this,
          &CaptureController::saveAnnotationColor);
}

CaptureController::~CaptureController() {
  saveAnnotationColor();
}

bool CaptureController::initialize(QString *error) {
  if (qEnvironmentVariable("XDG_SESSION_TYPE") != QStringLiteral("wayland") ||
      qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
    if (error)
      *error = tr("Run this application in a Hyprland Wayland session.");
    return false;
  }

  QJsonParseError parseError;
  auto monitorData = QJsonDocument::fromJson(
      run(QStringLiteral("hyprctl"),
          {QStringLiteral("-j"), QStringLiteral("monitors")}, error),
      &parseError);
  if (!monitorData.isArray() || monitorData.array().isEmpty()) {
    if (error && error->isEmpty())
      *error = tr("Cannot read monitor information: %1")
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
        *error = tr("Qt cannot find monitor %1.").arg(monitor.name);
      return false;
    }
    const qreal scale = data.value(QStringLiteral("scale")).toDouble(1.0);
    if (scale <= 0) {
      if (error)
        *error = tr("Invalid scale for monitor %1.").arg(monitor.name);
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

    // grim's PNG compression dominates startup; PPM transfers the same pixels
    // without compression before QImage decodes them.
    const QByteArray bytes =
        run(QStringLiteral("grim"),
            {QStringLiteral("-t"), QStringLiteral("ppm"), QStringLiteral("-o"),
             monitor.name, QStringLiteral("-")}, error);
    monitor.image = QImage::fromData(bytes);
    if (monitor.image.isNull()) {
      if (error && error->isEmpty())
        *error = tr("Cannot capture monitor %1.").arg(monitor.name);
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
  QString group;
  if (tool == QStringLiteral("rect") || tool == QStringLiteral("roundrect") ||
      tool == QStringLiteral("fillrect"))
    group = QStringLiteral("rect");
  else if (tool == QStringLiteral("ellipse") ||
           tool == QStringLiteral("fillellipse") ||
           tool == QStringLiteral("spotlight"))
    group = QStringLiteral("ellipse");
  else if (tool == QStringLiteral("arrow") ||
           tool == QStringLiteral("curvedarrow") ||
           tool == QStringLiteral("doublearrow") ||
           tool == QStringLiteral("line"))
    group = QStringLiteral("arrow");
  else if (tool == QStringLiteral("pen") ||
           tool == QStringLiteral("highlighter"))
    group = QStringLiteral("pen");
  if (!group.isEmpty() && m_toolVariants.value(group).toString() != tool) {
    m_toolVariants.insert(group, tool);
    emit toolVariantsChanged();
  }
  if (m_tool == tool)
    return;
  if (tool == QStringLiteral("text"))
    m_toolBeforeText = m_tool;
  m_tool = tool;
  emit toolChanged();
}

void CaptureController::restoreToolBeforeText() {
  if (m_tool == QStringLiteral("text"))
    setTool(m_toolBeforeText);
}

void CaptureController::activateToolGroup(const QString &group) {
  if (group != QStringLiteral("rect") && group != QStringLiteral("ellipse") &&
      group != QStringLiteral("arrow") && group != QStringLiteral("pen"))
    return;
  setTool(m_toolVariants.value(group).toString());
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

void CaptureController::setDarkToolbar(bool dark) {
  if (m_darkToolbar == dark)
    return;
  m_darkToolbar = dark;
  QSettings settings;
  settings.setValue(QStringLiteral("toolbar/dark"), dark);
  settings.sync();
  emit darkToolbarChanged();
}

void CaptureController::adjustSelectionEdge(int key, bool shrink) {
  if (!m_selected || m_tool != QStringLiteral("select") ||
      m_selection.isEmpty() || m_monitors.isEmpty())
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
  if (m_drag == Drag::Select) {
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
    const QPointF inside = limited(point, m_selection);
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
  const QPointF point = globalPoint(screenIndex, x, y);
  m_press = point;
  m_moved = false;
  if (m_selected && m_tool != QStringLiteral("select")) {
    if (!m_selection.contains(point) || m_tool == QStringLiteral("text"))
      return;
    m_drag = Drag::Draw;
    m_draft = {{QStringLiteral("type"), m_tool},
               {QStringLiteral("color"), m_annotationColor},
               {QStringLiteral("start"), pointMap(point)},
               {QStringLiteral("end"), pointMap(point)}};
    if (m_tool == QStringLiteral("pen") ||
        m_tool == QStringLiteral("highlighter"))
      m_draft.insert(QStringLiteral("points"), QVariantList{pointMap(point)});
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
    const QPointF release = limited(globalPoint(screenIndex, x, y), m_selection);
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

void CaptureController::cancelPointerAction() {
  m_drag = Drag::None;
  m_resizeEdges = 0;
  m_initialAnnotations.clear();
  if (!m_draft.isEmpty()) {
    m_draft.clear();
    emit draftChanged();
  }
}

void CaptureController::addText(qreal x, qreal y, const QString &text) {
  if (!m_selected || text.trimmed().isEmpty())
    return;
  const QPointF point = limited(QPointF(x, y), m_selection);
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
  const QStringList available = languages.split(QLatin1Char('\n'));
  const QLocale locale;
  QString preferred = QLocale::languageToCode(locale.language(), QLocale::ISO639Part3);
  if (locale.language() == QLocale::Chinese)
    preferred = locale.script() == QLocale::TraditionalHanScript
                    ? QStringLiteral("chi_tra") : QStringLiteral("chi_sim");
  else if (locale.language() == QLocale::NorwegianBokmal)
    preferred = QStringLiteral("nor");
  QStringList selected;
  if (available.contains(preferred))
    selected.append(preferred);
  if (available.contains(QStringLiteral("eng")) && preferred != QStringLiteral("eng"))
    selected.append(QStringLiteral("eng"));
  const QString language = selected.isEmpty() ? QStringLiteral("eng")
                                             : selected.join(QLatin1Char('+'));
  QProcess process;
  process.start(QStringLiteral("tesseract"),
                {QStringLiteral("stdin"), QStringLiteral("stdout"),
                 QStringLiteral("-l"), language});
  if (!process.waitForStarted(3000)) {
    setStatus(tr("Install tesseract to use OCR"));
    return;
  }
  process.write(png);
  process.closeWriteChannel();
  if (!process.waitForFinished(30000) || process.exitCode() != 0) {
    setStatus(
        tr("Text recognition failed: %1")
            .arg(QString::fromUtf8(process.readAllStandardError()).trimmed()));
    return;
  }
  const QByteArray recognized = process.readAllStandardOutput().trimmed();
  if (recognized.isEmpty()) {
    setStatus(tr("No text recognized"));
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
    setStatus(tr("Cannot create directory: %1").arg(directory));
    return;
  }
  const QString filename = QStringLiteral("screenshot-%1.png")
                               .arg(QDateTime::currentDateTime().toString(
                                   QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz")));
  const QString path = QDir(directory).filePath(filename);
  if (!image.save(path, "PNG")) {
    setStatus(tr("Cannot save: %1").arg(path));
    return;
  }
  QTextStream(stdout) << path << '\n';
  emit done();
}

void CaptureController::cancel() { emit done(); }
