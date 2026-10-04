// SPDX-License-Identifier: GPL-3.0-only

#include "pinnedimages.h"
#include "pinnedwindows.h"

#include <QAbstractItemModelTester>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

class PinnedImagesTest : public QObject {
  Q_OBJECT
  static QImage sample() {
    QImage image(320, 200, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    return image;
  }

private slots:
  void retainsFinishedImageAndClosesOnlyOnePin() {
    PinnedImages pins;
    QAbstractItemModelTester modelTester(
        &pins, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QImage original = sample();
    const int first = pins.add(original, QRectF(10, 20, 160, 100));
    original.fill(Qt::red);
    const int second = pins.add(original, QRectF(30, 40, 160, 100));
    QCOMPARE(pins.image(first).pixelColor(50, 50), QColor(Qt::darkGreen));
    QCOMPARE(pins.image(second).pixelColor(50, 50), QColor(Qt::red));
    pins.close(first);
    QCOMPARE(pins.count(), 1);
    QVERIFY(pins.image(first).isNull());
    QVERIFY(!pins.image(second).isNull());
    pins.close(first);
    QCOMPARE(pins.count(), 1);
    pins.close(second);
    QCOMPARE(pins.count(), 0);
    QCOMPARE(pins.add(QImage(), QRectF(0, 0, 10, 10)), -1);
  }

  void crossesOutputsWithoutResizingOrRecreatingDelegates() {
    PinnedImages pins;
    QAbstractItemModelTester modelTester(
        &pins, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QRectF left(-800, -200, 800, 600);
    const QRectF right(0, 0, 1200, 800);
    pins.setScreens({left, right});
    const QRectF initial(-200, 60, 160, 100);
    const int id = pins.add(sample(), initial);
    QSignalSpy removed(&pins, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&pins, &QAbstractItemModel::modelReset);
    pins.beginDrag(id, QPointF(-180, 80));
    pins.moveDrag(QPointF(30, 100));
    QCOMPARE(pins.geometry(id), QRectF(10, 80, 160, 100));
    QVERIFY(pins.inputRegion(left).isEmpty());
    QVERIFY(pins.inputRegion(right).contains(QPoint(40, 100)));
    QVERIFY(!pins.inputRegion(right).contains(QPoint(500, 500)));
    pins.endDrag();
    QCOMPARE(pins.geometry(id).size(), initial.size());
    QCOMPARE(removed.count(), 0);
    QCOMPARE(reset.count(), 0);
  }

  void straddlesOutputsAndKeepsTinyCloseButtonClickable() {
    PinnedImages pins;
    const QRectF left(0, 0, 800, 600);
    const QRectF right(800, 0, 800, 600);
    pins.setScreens({left, right});
    pins.add(sample(), QRectF(750, 100, 160, 100));
    QVERIFY(pins.inputRegion(left).contains(QPoint(780, 150)));
    QVERIFY(pins.inputRegion(right).contains(QPoint(50, 150)));
    const int tiny = pins.add(sample(), QRectF(100, 250, 2, 2));
    const QPointF close = PinnedImages::closeRect(pins.geometry(tiny)).center();
    QVERIFY(pins.inputRegion(left).contains(close.toPoint()));
  }

  void keepsCloseButtonReachableAfterDropAndOutputRemoval() {
    PinnedImages pins;
    const QRectF left(0, 0, 800, 600);
    const QRectF right(800, 0, 800, 600);
    pins.setScreens({left, right});
    const int id = pins.add(sample(), QRectF(1000, 100, 160, 100));
    pins.beginDrag(id, QPointF(1020, 120));
    pins.moveDrag(QPointF(2000, -300));
    pins.endDrag();
    QVERIFY(right.contains(PinnedImages::closeRect(pins.geometry(id))));
    pins.setScreens({left});
    QVERIFY(left.contains(PinnedImages::closeRect(pins.geometry(id))));
    QCOMPARE(pins.geometry(id).size(), QSizeF(160, 100));
  }

  void qmlDragDoubleClickAndClose() {
    PinnedImages pins;
    QQuickView view;
    PinnedWindows resources(view.engine(), &pins);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl::fromLocalFile(QStringLiteral(PIN_OVERLAY)));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.resize(640, 480);
    view.rootObject()->setProperty("screenRect", QRectF(0, 0, 640, 480));
    pins.setScreens({QRectF(0, 0, 640, 480)});
    const int id = pins.add(sample(), QRectF(40, 40, 160, 100));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(80, 80));
    QCOMPARE(pins.draggingId(), id);
    QTest::mouseMove(&view, QPoint(200, 160), 30);
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        QPoint(200, 160));
    QCOMPARE(pins.geometry(id), QRectF(160, 120, 160, 100));
    QCOMPARE(pins.draggingId(), -1);
    QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, QPoint(200, 160));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier,
                        QPoint(200, 160));
    QCOMPARE(pins.count(), 1);
    QTest::mouseClick(
        &view, Qt::LeftButton, Qt::NoModifier,
        PinnedImages::closeRect(pins.geometry(id)).center().toPoint());
    QCOMPARE(pins.count(), 0);
  }
};

QTEST_MAIN(PinnedImagesTest)
#include "pinnedimages.moc"
