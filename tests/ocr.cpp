// SPDX-License-Identifier: GPL-3.0-only

#include "capturecontroller.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

class OcrTest : public QObject {
  Q_OBJECT
private slots:
  void languages_data() {
    QTest::addColumn<QString>("locale");
    QTest::addColumn<QByteArray>("installed");
    QTest::addColumn<QByteArray>("expected");
    QTest::newRow("not-always-Chinese") << QStringLiteral("de_DE")
        << QByteArray("chi_sim\ndeu\neng\n") << QByteArray("deu+eng");
    QTest::newRow("traditional") << QStringLiteral("zh_TW")
        << QByteArray("chi_sim\nchi_tra\neng\n") << QByteArray("chi_tra+eng");
    QTest::newRow("simplified") << QStringLiteral("zh_CN")
        << QByteArray("chi_sim\neng\n") << QByteArray("chi_sim+eng");
    QTest::newRow("English-optional") << QStringLiteral("ja_JP")
        << QByteArray("jpn\n") << QByteArray("jpn");
    QTest::newRow("missing-language") << QStringLiteral("uk_UA")
        << QByteArray("chi_sim\neng\n") << QByteArray("eng");
    QTest::newRow("exact-data-name") << QStringLiteral("de_DE")
        << QByteArray("deu_frak\neng\n") << QByteArray("eng");
    QTest::newRow("Bokmal-code") << QStringLiteral("nb_NO")
        << QByteArray("nor\neng\n") << QByteArray("nor+eng");
    QTest::newRow("English-once") << QStringLiteral("en_US")
        << QByteArray("chi_sim\neng\n") << QByteArray("eng");
  }

  void languages() {
    QFETCH(QString, locale);
    QFETCH(QByteArray, installed);
    QFETCH(QByteArray, expected);
    QLocale::setDefault(QLocale(locale));
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    auto write = [&](const QString &name, const QByteArray &data, bool executable = false) {
      QFile file(temp.filePath(name));
      if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
      file.close();
      return !executable || file.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                                QFile::ExeOwner);
    };
    const QJsonObject monitor{{"name", QGuiApplication::primaryScreen()->name()},
                              {"width", 1000}, {"height", 800}, {"scale", 1},
                              {"x", 0}, {"y", 0}};
    QVERIFY(write(QStringLiteral("monitors.json"),
                  QJsonDocument(QJsonArray{monitor}).toJson()));
    QImage screen(1000, 800, QImage::Format_RGB32);
    screen.fill(Qt::blue);
    QVERIFY(screen.save(temp.filePath(QStringLiteral("screen.png"))));
    QVERIFY(write(QStringLiteral("hyprctl"),
                  "#!/bin/sh\nif [ \"$2\" = monitors ]; then /usr/bin/cat \"$OCR_TEST_DIR/monitors.json\"; else printf '[]'; fi\n", true));
    QVERIFY(write(QStringLiteral("grim"),
                  "#!/bin/sh\n/usr/bin/cat \"$OCR_TEST_DIR/screen.png\"\n", true));
    QVERIFY(write(QStringLiteral("languages"), installed));
    QVERIFY(write(QStringLiteral("tesseract"),
                  "#!/bin/sh\nif [ \"$1\" = --list-langs ]; then\n"
                  "  printf 'List of available languages (test):\\n'\n"
                  "  /usr/bin/cat \"$OCR_TEST_DIR/languages\"\n"
                  "else\n  printf '%s' \"$4\" > \"$OCR_TEST_DIR/chosen\"\n"
                  "  /usr/bin/cat > \"$OCR_TEST_DIR/input.png\"\nfi\n", true));
    qputenv("OCR_TEST_DIR", QFile::encodeName(temp.path()));
    qputenv("PATH", QFile::encodeName(temp.path()));
    qputenv("XDG_SESSION_TYPE", "wayland");
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", "test");
    CaptureController controller;
    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));
    controller.pointerPress(0, 10, 10);
    controller.pointerMove(0, 80, 50);
    controller.pointerRelease(0, 80, 50);
    controller.ocr();
    QTRY_COMPARE(controller.status(), QStringLiteral("No text recognized"));
    QFile chosen(temp.filePath(QStringLiteral("chosen")));
    QVERIFY(chosen.open(QIODevice::ReadOnly));
    QCOMPARE(chosen.readAll(), expected);
    QCOMPARE(QImage(temp.filePath(QStringLiteral("input.png"))).size(), QSize(70, 40));
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("OmarchyTest"));
  app.setApplicationName(QStringLiteral("ocr-test"));
  OcrTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "ocr.moc"
