#pragma once

#include <QQuickPaintedItem>

class CaptureController;

class ScrollPreview : public QQuickPaintedItem {
  Q_OBJECT
  Q_PROPERTY(CaptureController *controller READ controller WRITE setController
                 NOTIFY controllerChanged)
  Q_PROPERTY(int screenIndex READ screenIndex WRITE setScreenIndex NOTIFY
                 screenIndexChanged)

public:
  explicit ScrollPreview(QQuickItem *parent = nullptr);
  CaptureController *controller() const { return m_controller; }
  void setController(CaptureController *controller);
  int screenIndex() const { return m_screenIndex; }
  void setScreenIndex(int index);
  void paint(QPainter *painter) override;

signals:
  void controllerChanged();
  void screenIndexChanged();

private:
  CaptureController *m_controller = nullptr;
  int m_screenIndex = -1;
};
