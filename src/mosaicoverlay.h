#pragma once

#include <QQuickPaintedItem>

class CaptureController;
class QPainter;

class MosaicOverlay : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(CaptureController *controller READ controller WRITE setController NOTIFY controllerChanged)
  Q_PROPERTY(int screenIndex READ screenIndex WRITE setScreenIndex NOTIFY screenIndexChanged)
  Q_PROPERTY(bool hasMosaic READ hasMosaic NOTIFY hasMosaicChanged)
  Q_PROPERTY(bool imageReady READ imageReady NOTIFY screenIndexChanged)

public:
  explicit MosaicOverlay(QQuickItem *parent = nullptr);
  CaptureController *controller() const { return m_controller; }
  void setController(CaptureController *controller);
  int screenIndex() const { return m_screenIndex; }
  void setScreenIndex(int index);
  bool hasMosaic() const;
  bool imageReady() const;
  void paint(QPainter *painter) override;

signals:
  void controllerChanged();
  void screenIndexChanged();
  void hasMosaicChanged();

private:
  CaptureController *m_controller = nullptr;
  int m_screenIndex = -1;
};
