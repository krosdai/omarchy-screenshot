// SPDX-License-Identifier: GPL-3.0-only

#include "cursorcaptureguard.h"

#include <QCoreApplication>
#include <QFile>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <chrono>
#include <future>

class CursorCaptureTest : public QObject {
  Q_OBJECT
  QTemporaryDir m_dir;
  QProcessEnvironment m_original;

  bool write(const QString &name, const QByteArray &bytes) {
    QFile file(m_dir.filePath(name));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
  }
  QByteArray state() const {
    QFile file(m_dir.filePath(QStringLiteral("state")));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
  }

private slots:
  void initTestCase() {
    QVERIFY(m_dir.isValid());
    m_original = QProcessEnvironment::systemEnvironment();
    // A shape update uses the current compositor visibility value, just as
    // Hyprland does. The capture's protocol option alone cannot stop it.
    QVERIFY(write(QStringLiteral("hyprctl"), R"(#!/bin/sh
case "$1:$2" in
  -j:getoption)
    [ -z "$CURSOR_TEST_BAD_OPTION" ] || { printf '{}'; exit; }
    value=$(/usr/bin/cat "$CURSOR_TEST_DIR/state")
    if [ -n "$CURSOR_TEST_LEGACY" ]; then printf '{"int":%s}' "$value"
    elif [ "$value" = 1 ]; then printf '{"bool":true}'
    else printf '{"bool":false}'; fi;;
  eval:*)
    [ -z "$CURSOR_TEST_LEGACY" ] || { printf 'eval is only supported with the lua config manager'; exit; }
    case "$2" in
      *true*) [ -z "$CURSOR_TEST_FAIL_HIDE" ] || { printf 'failed'; exit; }
              printf 1 > "$CURSOR_TEST_DIR/state";;
      *false*) printf 0 > "$CURSOR_TEST_DIR/state";;
    esac
    printf ok;;
  keyword:cursor:invisible)
    [ -z "$CURSOR_TEST_FAIL_HIDE" ] || { printf 'failed'; exit; }
    printf '%s' "$3" > "$CURSOR_TEST_DIR/state"; printf ok;;
esac
)"));
    QFile command(m_dir.filePath(QStringLiteral("hyprctl")));
    QVERIFY(command.setPermissions(QFile::ReadOwner | QFile::WriteOwner |
                                   QFile::ExeOwner));
    qputenv("PATH", QFile::encodeName(m_dir.path()) + ':' +
                        m_original.value("PATH").toUtf8());
    qputenv("CURSOR_TEST_DIR", QFile::encodeName(m_dir.path()));
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(m_dir.path()));
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", "cursor-test");
  }
  void init() {
    QVERIFY(write(QStringLiteral("state"), "0"));
    qunsetenv("CURSOR_TEST_BAD_OPTION");
    qunsetenv("CURSOR_TEST_FAIL_HIDE");
    qunsetenv("CURSOR_TEST_LEGACY");
  }
  void cleanupTestCase() {
    for (const auto &key :
         {"PATH", "XDG_RUNTIME_DIR", "HYPRLAND_INSTANCE_SIGNATURE"}) {
      if (m_original.contains(key))
        qputenv(key, m_original.value(key).toUtf8());
      else
        qunsetenv(key);
    }
    qunsetenv("CURSOR_TEST_DIR");
    qunsetenv("CURSOR_TEST_BAD_OPTION");
    qunsetenv("CURSOR_TEST_FAIL_HIDE");
    qunsetenv("CURSOR_TEST_LEGACY");
  }
  void excludesShapeUpdatesUntilAllPixelsAreCopied() {
    QString error;
    {
      CursorCaptureGuard cursor(&error);
      QVERIFY2(cursor.ready(), qPrintable(error));
      // Multiple outputs, or multiple cursor shapes during one copy, must
      // all observe suppression until the capture leaves this scope.
      for (int output = 0; output < 4; ++output)
        QCOMPARE(state(), QByteArray("1"));
    }
    QCOMPARE(state(), QByteArray("0"));
  }
  void restoresOnCaptureFailure() {
    const auto failedCapture = [&] {
      QString error;
      CursorCaptureGuard cursor(&error);
      if (!cursor.ready())
        return error;
      return QStringLiteral("grim failed");
    };
    QCOMPARE(failedCapture(), QStringLiteral("grim failed"));
    QCOMPARE(state(), QByteArray("0"));
  }
  void preservesAlreadyInvisibleCursor() {
    QVERIFY(write(QStringLiteral("state"), "1"));
    {
      QString error;
      CursorCaptureGuard cursor(&error);
      QVERIFY2(cursor.ready(), qPrintable(error));
    }
    QCOMPARE(state(), QByteArray("1"));
  }
  void supportsLegacyHyprland() {
    qputenv("CURSOR_TEST_LEGACY", "1");
    {
      QString error;
      CursorCaptureGuard cursor(&error);
      QVERIFY2(cursor.ready(), qPrintable(error));
      QCOMPARE(state(), QByteArray("1"));
    }
    QCOMPARE(state(), QByteArray("0"));
  }
  void refusesCaptureWhenSuppressionFails() {
    qputenv("CURSOR_TEST_FAIL_HIDE", "1");
    QString error;
    {
      CursorCaptureGuard cursor(&error);
      QVERIFY(!cursor.ready());
      QVERIFY(!error.isEmpty());
    }
    QCOMPARE(state(), QByteArray("0"));
  }
  void refusesCaptureWithUnknownCursorState() {
    qputenv("CURSOR_TEST_BAD_OPTION", "1");
    QString error;
    CursorCaptureGuard cursor(&error);
    QVERIFY(!cursor.ready());
    QVERIFY(!error.isEmpty());
    QCOMPARE(state(), QByteArray("0"));
  }
  void serializesOverlappingCaptures() {
    std::future<QByteArray> second;
    {
      QString error;
      CursorCaptureGuard cursor(&error);
      QVERIFY2(cursor.ready(), qPrintable(error));
      second = std::async(std::launch::async, [this] {
        QString error;
        CursorCaptureGuard cursor(&error);
        return cursor.ready() ? state() : error.toUtf8();
      });
      QCOMPARE(second.wait_for(std::chrono::milliseconds(100)),
               std::future_status::timeout);
      QCOMPARE(state(), QByteArray("1"));
    }
    QCOMPARE(second.get(), QByteArray("1"));
    QCOMPARE(state(), QByteArray("0"));
  }
  void restoresWhenScreenshotProcessIsKilled() {
    QProcess capture;
    capture.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--hold-cursor")});
    QVERIFY(capture.waitForStarted());
    QVERIFY(capture.waitForReadyRead(5000));
    QCOMPARE(capture.readAllStandardOutput().trimmed(), QByteArray("ready"));
    QCOMPARE(state(), QByteArray("1"));
    capture.kill();
    QVERIFY(capture.waitForFinished());
    QTRY_COMPARE_WITH_TIMEOUT(state(), QByteArray("0"), 3000);
  }
};

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains(QStringLiteral("--hold-cursor"))) {
    QString error;
    CursorCaptureGuard cursor(&error);
    if (!cursor.ready())
      return 1;
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
      return 1;
    output.write("ready\n");
    output.flush();
    QThread::sleep(30);
    return 0;
  }
  CursorCaptureTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "cursorcapture.moc"
