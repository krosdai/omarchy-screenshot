// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>
#include "selftestimage.h"

class PreviewScalingTest : public QObject {
  Q_OBJECT

private slots:
  void cursorPreviewUsesLogicalCoordinates() {
    QQuickView view;
    view.setSource(QUrl::fromLocalFile(QStringLiteral(PREVIEW_FIXTURE)));
    QCOMPARE(view.status(), QQuickView::Ready);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    auto *glyph = view.rootObject()->findChild<QQuickItem *>("cursorGlyph");
    QVERIFY(glyph);
    const auto grab = glyph->grabToImage();
    QVERIFY(grab);
    QSignalSpy ready(grab.get(), &QQuickItemGrabResult::ready);
    QVERIFY(!ready.isEmpty() || ready.wait());
    const QImage rawImage = grab->image();
    const QImage image = selfTestLogicalImage(
        rawImage, QSizeF(glyph->width(), glyph->height()));
    QVERIFY(!image.isNull());
    qInfo() << "Device pixel ratio:" << view.devicePixelRatio()
            << "grab size:" << rawImage.size();
    QCOMPARE(rawImage.width(), qCeil(glyph->width() * view.devicePixelRatio()));
    QCOMPARE(image.size(), QSize(22, 22));
    QCOMPARE(image.devicePixelRatio(), 1.0);
    // The cursor covers (2, 3), while (15, 19) is outside its path,
    // regardless of the display's pixel density.
    QVERIFY(image.pixelColor(2, 3).alpha() > 0);
    QCOMPARE(image.pixelColor(15, 19).alpha(), 0);
  }
};

QTEST_MAIN(PreviewScalingTest)
#include "tst_preview_scaling.moc"
