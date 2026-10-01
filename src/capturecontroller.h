// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <future>

class QScreen;
class QPainter;
class QThread;

struct CaptureMonitor {
  QString name;
  QRectF geometry;
  QImage image;
  // One pixel per mosaic block; painting it unsmoothed at full size pixelates.
  QImage mosaicImage;
  QScreen *screen = nullptr;
  int id = -1;
  int workspace = -1;
};

class CaptureController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QRectF selection READ selection NOTIFY selectionChanged)
  Q_PROPERTY(QRectF hovered READ hovered NOTIFY hoveredChanged)
  Q_PROPERTY(bool selected READ selected NOTIFY selectedChanged)
  Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
  Q_PROPERTY(QVariantMap toolVariants READ toolVariants NOTIFY toolVariantsChanged)
  Q_PROPERTY(QString annotationColor READ annotationColor WRITE
                 setAnnotationColor NOTIFY annotationColorChanged)
  Q_PROPERTY(bool darkToolbar READ darkToolbar WRITE setDarkToolbar NOTIFY
                 darkToolbarChanged)
  Q_PROPERTY(
      QVariantList annotations READ annotations NOTIFY annotationsChanged)
  Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
  Q_PROPERTY(int toolbarScreen READ toolbarScreen NOTIFY selectionChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool imagesReady READ imagesReady NOTIFY imagesReadyChanged)
  Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

public:
  explicit CaptureController(QObject *parent = nullptr);
  ~CaptureController() override;

  bool initialize(QString *error);
  // Split form of initialize(): the capture runs on a worker thread between
  // the two calls so the caller can load the UI meanwhile.
  bool startCapture(QString *error);
  bool finishCapture(QString *error);
  const QVector<CaptureMonitor> &monitors() const { return m_monitors; }
  QRectF selection() const { return m_selection; }
  QRectF hovered() const { return m_hovered; }
  bool selected() const { return m_selected; }
  QString tool() const { return m_tool; }
  QVariantMap toolVariants() const { return m_toolVariants; }
  QString annotationColor() const { return m_annotationColor; }
  bool darkToolbar() const { return m_darkToolbar; }
  QVariantList annotations() const { return m_annotations; }
  QVariantMap draft() const { return m_draft; }
  int toolbarScreen() const;
  QString status() const { return m_status; }
  bool imagesReady() const { return m_imagesReady; }
  bool exporting() const { return m_exporting; }
  QImage renderedImage() const;
  static QPainterPath freehandPath(const QVariantList &points);
  static QPainterPath mosaicPath(const QVariantMap &item);

  Q_INVOKABLE void pointerMove(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void pointerPress(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void pointerRelease(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void addText(qreal x, qreal y, const QString &text);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void copy();
  Q_INVOKABLE void ocr();
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void setTool(const QString &tool);
  Q_INVOKABLE void restoreToolBeforeText();
  Q_INVOKABLE void activateToolGroup(const QString &group);
  Q_INVOKABLE void cancelPointerAction();
  Q_INVOKABLE void setAnnotationColor(const QString &color);
  Q_INVOKABLE void setAnnotationColorFromHsv(qreal hue, qreal saturation,
                                             qreal value);
  Q_INVOKABLE void saveAnnotationColor();
  Q_INVOKABLE void setDarkToolbar(bool dark);
  Q_INVOKABLE void adjustSelectionEdge(int key, bool shrink);
  Q_INVOKABLE QVariantList
  smoothedFreehandPoints(const QVariantList &points) const;

signals:
  void selectionChanged();
  void hoveredChanged();
  void selectedChanged();
  void toolChanged();
  void toolVariantsChanged();
  void annotationColorChanged();
  void darkToolbarChanged();
  void annotationsChanged();
  void draftChanged();
  void statusChanged();
  void imagesReadyChanged();
  void exportingChanged();
  void done();

private:
  struct Candidate {
    QRectF geometry;
    bool window = false;
  };
  enum class Drag { None, Select, Move, Resize, Draw };
  struct CaptureResult {
    QList<QImage> images;
    QList<QImage> mosaics;
    QString error;
  };

  QPointF globalPoint(int screenIndex, qreal x, qreal y) const;
  QRectF candidateAt(const QPointF &point) const;
  void setSelection(const QRectF &rect);
  void setHovered(const QRectF &rect);
  void setStatus(const QString &message);
  void paintAnnotation(QPainter &painter, const QVariantMap &item) const;
  void appendAnnotation(const QVariantMap &item);
  // Runs job off the GUI thread; job returns an error message or nothing.
  void exportInBackground(std::function<QString()> job);
  void finishExport(const QString &error);
  void addWindowCandidates(const QByteArray &json);
  static QString recognizeText(const QImage &image);
  static CaptureResult captureMonitors(const QStringList &names,
                                       const QList<bool> &direct);

  QVector<CaptureMonitor> m_monitors;
  QVector<Candidate> m_candidates;
  QRectF m_selection;
  QRectF m_hovered;
  QRectF m_initialSelection;
  QPointF m_press;
  Drag m_drag = Drag::None;
  bool m_selected = false;
  bool m_moved = false;
  int m_resizeEdges = 0;
  QString m_tool = QStringLiteral("select");
  QString m_toolBeforeText = QStringLiteral("select");
  QVariantMap m_toolVariants = {
      {QStringLiteral("rect"), QStringLiteral("rect")},
      {QStringLiteral("ellipse"), QStringLiteral("ellipse")},
      {QStringLiteral("arrow"), QStringLiteral("arrow")},
      {QStringLiteral("pen"), QStringLiteral("pen")}};
  QString m_annotationColor = QStringLiteral("#ff4b55");
  bool m_darkToolbar = false;
  QTimer m_colorSaveTimer;
  bool m_colorDirty = false;
  QVariantList m_annotations;
  QVariantList m_redoAnnotations;
  QVariantList m_initialAnnotations;
  QVariantMap m_draft;
  QString m_status;
  std::future<CaptureResult> m_capture;
  std::future<QByteArray> m_clients;
  bool m_imagesReady = false;
  bool m_exporting = false;
  QPointer<QThread> m_exportThread;
};
