// SPDX-License-Identifier: GPL-3.0-only

#include "capturecontroller.h"
#include "daemonsocket.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

class DaemonTest : public QObject {
  Q_OBJECT
private slots:
  void socketPathFollowsRuntimeDir() {
    qputenv("XDG_RUNTIME_DIR", "/run/user/4242");
    QCOMPARE(daemonSocketPath(), std::string("/run/user/4242/omarchy-screenshot.sock"));
    qunsetenv("XDG_RUNTIME_DIR");
    QVERIFY(daemonSocketPath().empty());
  }

  void activationNeedsMatchingPid() {
    QCOMPARE(activatedSocket(42, "42", "1"), 3);
    QCOMPARE(activatedSocket(42, "43", "1"), -1);
    QCOMPARE(activatedSocket(42, "42", "0"), -1);
    QCOMPARE(activatedSocket(42, "42x", "1"), -1);
    QCOMPARE(activatedSocket(42, "", "1"), -1);
    QCOMPARE(activatedSocket(42, nullptr, "1"), -1);
    QCOMPARE(activatedSocket(42, "42", nullptr), -1);
  }

  void listensForwardsAndRefusesSecondDaemon() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    QVERIFY(!forwardCaptureRequest(path));

    int listener = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &error), ListenResult::Listening);
    struct stat info;
    QCOMPARE(stat(path.c_str(), &info), 0);
    QCOMPARE(info.st_mode & 0777, mode_t(0600));

    int second = -1;
    QCOMPARE(listenForCaptureRequests(path, &second, &error), ListenResult::AlreadyRunning);
    QCOMPARE(second, -1);

    QVERIFY(forwardCaptureRequest(path));
    // The probe from the refused second daemon is queued ahead of the request.
    int accepted = 0;
    for (int client; (client = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC)) >= 0;) {
      close(client);
      ++accepted;
    }
    QCOMPARE(accepted, 2);
    close(listener);
  }

  void replacesStaleSocketButNotOtherFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    int listener = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &error), ListenResult::Listening);
    close(listener);  // A crashed daemon leaves its socket file behind.
    QVERIFY(!forwardCaptureRequest(path));
    QCOMPARE(listenForCaptureRequests(path, &listener, &error), ListenResult::Listening);
    QVERIFY(forwardCaptureRequest(path));
    close(listener);

    const QString other = dir.filePath(QStringLiteral("notes.txt"));
    QFile file(other);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write("keep") == 4);
    file.close();
    QCOMPARE(listenForCaptureRequests(other.toStdString(), &listener, &error),
             ListenResult::Failed);
    QVERIFY(QFile::exists(other));
  }

  void resetPreparesTheNextCapture() {
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
                              {"width", 400}, {"height", 300}, {"scale", 1},
                              {"x", 0}, {"y", 0}};
    QVERIFY(write(QStringLiteral("monitors.json"),
                  QJsonDocument(QJsonArray{monitor}).toJson()));
    QImage screen(400, 300, QImage::Format_RGB32);
    screen.fill(Qt::darkGreen);
    QVERIFY(screen.save(temp.filePath(QStringLiteral("screen.png"))));
    QVERIFY(write(QStringLiteral("hyprctl"),
                  "#!/bin/sh\nif [ \"$2\" = monitors ]; then /usr/bin/cat \"$DAEMON_TEST_DIR/monitors.json\"; else printf '[]'; fi\n", true));
    QVERIFY(write(QStringLiteral("grim"),
                  "#!/bin/sh\n/usr/bin/cat \"$DAEMON_TEST_DIR/screen.png\"\n", true));
    qputenv("DAEMON_TEST_DIR", QFile::encodeName(temp.path()));
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(temp.path()));
    qputenv("XDG_SESSION_TYPE", "wayland");
    qputenv("HYPRLAND_INSTANCE_SIGNATURE", "test");
    // Keep the in-process capture away from any real compositor.
    qunsetenv("WAYLAND_DISPLAY");

    CaptureController controller;
    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));
    const int firstGeneration = controller.captureGeneration();
    controller.pointerPress(0, 10, 10);
    controller.pointerMove(0, 120, 90);
    controller.pointerRelease(0, 120, 90);
    controller.setTool(QStringLiteral("fillrect"));
    controller.pointerPress(0, 20, 20);
    controller.pointerMove(0, 60, 60);
    controller.pointerRelease(0, 60, 60);
    QVERIFY(controller.selected());
    QCOMPARE(controller.annotations().size(), 1);
    QCOMPARE(controller.toolVariants().value(QStringLiteral("rect")).toString(),
             QStringLiteral("fillrect"));

    controller.reset();
    QVERIFY(!controller.selected());
    QVERIFY(!controller.imagesReady());
    QVERIFY(controller.selection().isEmpty());
    QVERIFY(controller.annotations().isEmpty());
    QVERIFY(controller.monitors().isEmpty());
    QCOMPARE(controller.tool(), QStringLiteral("select"));
    QCOMPARE(controller.toolVariants().value(QStringLiteral("rect")).toString(),
             QStringLiteral("rect"));
    QCOMPARE(controller.scrollState(), int(CaptureController::ScrollState::Idle));

    QVERIFY2(controller.initialize(&error), qPrintable(error));
    QVERIFY(controller.imagesReady());
    QCOMPARE(controller.monitors().size(), 1);
    QVERIFY(controller.captureGeneration() > firstGeneration);
    qputenv("PATH", path);
  }
};

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("OmarchyTest"));
  app.setApplicationName(QStringLiteral("daemon-test"));
  DaemonTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "daemon.moc"
