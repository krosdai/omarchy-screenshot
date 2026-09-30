#pragma once

#include <QImage>
#include <QObject>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class QScreen;
class QPainter;

struct CaptureMonitor {
  QString name;
  QRectF geometry;
  QImage image;
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

public:
  explicit CaptureController(QObject *parent = nullptr);
  ~CaptureController() override;

  bool initialize(QString *error);
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
  void done();

private:
  struct Candidate {
    QRectF geometry;
    bool window = false;
  };
  enum class Drag { None, Select, Move, Resize, Draw };

  QPointF globalPoint(int screenIndex, qreal x, qreal y) const;
  QRectF candidateAt(const QPointF &point) const;
  void setSelection(const QRectF &rect);
  void setHovered(const QRectF &rect);
  void setStatus(const QString &message);
  void paintAnnotation(QPainter &painter, const QVariantMap &item) const;
  void appendAnnotation(const QVariantMap &item);

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
};
