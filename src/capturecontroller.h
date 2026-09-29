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
#include <atomic>
#include <memory>

namespace stitch {
class ManualCapture;
class AutoCapture;
class CaptureHandshake;
} // namespace stitch
class QProcess;

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
  Q_PROPERTY(QString annotationColor READ annotationColor WRITE
                 setAnnotationColor NOTIFY annotationColorChanged)
  Q_PROPERTY(
      QVariantList annotations READ annotations NOTIFY annotationsChanged)
  Q_PROPERTY(QVariantMap draft READ draft NOTIFY draftChanged)
  Q_PROPERTY(int toolbarScreen READ toolbarScreen NOTIFY selectionChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool scrolling READ scrolling NOTIFY scrollingChanged)
  Q_PROPERTY(int scrollMonitor READ scrollMonitor NOTIFY scrollingChanged)
  Q_PROPERTY(bool hasScrollImage READ hasScrollImage NOTIFY scrollImageChanged)
  Q_PROPERTY(qreal scrollOffset READ scrollOffset NOTIFY scrollOffsetChanged)
  Q_PROPERTY(qreal scrollDocumentHeight READ scrollDocumentHeight NOTIFY
                 scrollImageChanged)

public:
  explicit CaptureController(QObject *parent = nullptr);
  ~CaptureController() override;

  bool initialize(QString *error);
  const QVector<CaptureMonitor> &monitors() const { return m_monitors; }
  QRectF selection() const { return m_selection; }
  QRectF hovered() const { return m_hovered; }
  bool selected() const { return m_selected; }
  QString tool() const { return m_tool; }
  QString annotationColor() const { return m_annotationColor; }
  QVariantList annotations() const { return m_annotations; }
  QVariantMap draft() const { return m_draft; }
  int toolbarScreen() const;
  QString status() const { return m_status; }
  bool scrolling() const { return m_scrolling; }
  int scrollMonitor() const { return m_scrollMonitor; }
  bool hasScrollImage() const { return !m_scrollImage.isNull(); }
  qreal scrollOffset() const { return m_scrollOffset; }
  qreal scrollDocumentHeight() const;
  const QImage &scrollImage() const { return m_scrollImage; }
  const QImage &scrollMosaicImage() const { return m_scrollMosaicImage; }
  QImage renderedImage() const;
  static QPainterPath freehandPath(const QVariantList &points);
  static QPainterPath mosaicPath(const QVariantMap &item);

  Q_INVOKABLE void pointerMove(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void pointerPress(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void pointerRelease(int screenIndex, qreal x, qreal y);
  Q_INVOKABLE void addText(qreal x, qreal y, const QString &text);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void startScrollCapture(bool horizontal);
  Q_INVOKABLE void startAutoScrollCapture(bool horizontal);
  Q_INVOKABLE void finishScrollCapture();
  Q_INVOKABLE void cancelScrollCapture();
  Q_INVOKABLE void scrollPreviewBy(qreal distance);
  Q_INVOKABLE void copy();
  Q_INVOKABLE void ocr();
  Q_INVOKABLE void save();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void setTool(const QString &tool);
  Q_INVOKABLE void setAnnotationColor(const QString &color);
  Q_INVOKABLE void setAnnotationColorFromHsv(qreal hue, qreal saturation,
                                             qreal value);
  Q_INVOKABLE void saveAnnotationColor();
  Q_INVOKABLE void adjustSelectionEdge(int key, bool shrink);
  Q_INVOKABLE QVariantList
  smoothedFreehandPoints(const QVariantList &points) const;

signals:
  void selectionChanged();
  void hoveredChanged();
  void selectedChanged();
  void toolChanged();
  void annotationColorChanged();
  void annotationsChanged();
  void draftChanged();
  void statusChanged();
  void scrollingChanged();
  void scrollImageChanged();
  void scrollOffsetChanged();
  void done();

private:
  struct Candidate {
    QRectF geometry;
    bool window = false;
  };
  enum class Drag { None, Select, Move, Resize, Draw, Pan };

  QPointF globalPoint(int screenIndex, qreal x, qreal y) const;
  QRectF candidateAt(const QPointF &point) const;
  void setSelection(const QRectF &rect);
  void setHovered(const QRectF &rect);
  void setStatus(const QString &message);
  void paintAnnotation(QPainter &painter, const QVariantMap &item) const;
  void pollScrollFrame();
  void beginScrollCapture(bool horizontal, bool automatic);
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
  QString m_annotationColor = QStringLiteral("#ff4b55");
  QTimer m_colorSaveTimer;
  bool m_colorDirty = false;
  QVariantList m_annotations;
  QVariantList m_redoAnnotations;
  QVariantList m_initialAnnotations;
  QVariantMap m_draft;
  QString m_status;
  bool m_scrolling = false;
  int m_scrollMonitor = -1;
  QImage m_scrollImage;
  QImage m_scrollMosaicImage;
  qreal m_scrollOffset = 0;
  qreal m_initialScrollOffset = 0;
  std::unique_ptr<stitch::ManualCapture> m_scrollSession;
  std::unique_ptr<stitch::AutoCapture> m_autoSession;
  std::shared_ptr<stitch::CaptureHandshake> m_scrollHandshake;
  std::shared_ptr<std::atomic<bool>> m_injectStop;
  bool m_autoScroll = false;
  quint64 m_lastAutoCycle = 0;
  quint64 m_captureAutoCycle = 0;
  quint64 m_scrollGeneration = 0;
  QProcess *m_scrollProcess = nullptr;
  QTimer m_scrollTimer;
};
