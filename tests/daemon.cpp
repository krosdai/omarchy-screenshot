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

#include <chrono>
#include <cstring>
#include <future>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

// forwardCaptureRequest() waits for the daemon's answer, so the client runs
// on its own thread while the test plays the daemon.
static std::future<bool> forwardLater(const std::string &path) {
  return std::async(std::launch::async,
                    [path] { return forwardCaptureRequest(path); });
}

// Drains until the expected number of requests arrived or two seconds pass.
static int takeWithin(int listener, int expected) {
  int taken = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (taken < expected && std::chrono::steady_clock::now() < deadline) {
    pollfd ready{listener, POLLIN, 0};
    poll(&ready, 1, 50);
    taken += takeCaptureRequests(listener);
  }
  return taken;
}

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
    int lock = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::Listening);
    struct stat info;
    QCOMPARE(stat(path.c_str(), &info), 0);
    QCOMPARE(info.st_mode & 0777, mode_t(0600));

    int second = -1;
    int secondLock = -1;
    QCOMPARE(listenForCaptureRequests(path, &second, &secondLock, &error),
             ListenResult::AlreadyRunning);
    QCOMPARE(second, -1);
    QCOMPARE(secondLock, -1);

    auto firstRequest = forwardLater(path);
    auto secondRequest = forwardLater(path);
    QCOMPARE(takeWithin(listener, 2), 2);
    QVERIFY(firstRequest.get());
    QVERIFY(secondRequest.get());
    QCOMPARE(takeCaptureRequests(listener), 0);
    close(listener);
    close(lock);
  }

  // A daemon that exits before accepting leaves the request unanswered, and
  // the client must then capture itself rather than report success.
  void unansweredRequestFallsBack() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    int listener = -1;
    int lock = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::Listening);
    auto pending = forwardLater(path);
    pollfd queued{listener, POLLIN, 0};
    QCOMPARE(poll(&queued, 1, 2000), 1);
    close(listener);
    close(lock);
    QVERIFY(!pending.get());
  }

  // A client that gave up waiting has captured by itself, so the daemon must
  // not serve its request when it finally reads it.
  void withdrawnRequestIsNotServed() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    int listener = -1;
    int lock = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::Listening);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, path.c_str());
    const int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    QCOMPARE(::connect(client, reinterpret_cast<const sockaddr *>(&address), sizeof address), 0);
    QCOMPARE(send(client, "capture\n", 8, MSG_NOSIGNAL), ssize_t(8));
    close(client);
    QCOMPARE(takeCaptureRequests(listener), 0);
    close(listener);
    close(lock);
  }

  // systemd can hold the socket with no daemon behind it; a daemon started
  // by hand must leave it alone without its probe counting as a capture.
  void probeOfAHeldSocketIsNoRequest() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    const int held = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strcpy(address.sun_path, path.c_str());
    QCOMPARE(bind(held, reinterpret_cast<const sockaddr *>(&address), sizeof address), 0);
    QCOMPARE(listen(held, 8), 0);

    int listener = -1;
    int lock = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::AlreadyRunning);
    QCOMPARE(takeCaptureRequests(held), 0);

    // Neither a wrong message nor a client that stalls counts.
    const int other = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    QCOMPARE(::connect(other, reinterpret_cast<const sockaddr *>(&address), sizeof address), 0);
    QCOMPARE(send(other, "probe\n", 6, MSG_NOSIGNAL), ssize_t(6));
    const int stalled = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    QCOMPARE(::connect(stalled, reinterpret_cast<const sockaddr *>(&address), sizeof address), 0);
    QCOMPARE(send(stalled, "cap", 3, MSG_NOSIGNAL), ssize_t(3));
    auto request = forwardLater(path);
    QCOMPARE(takeWithin(held, 1), 1);
    QVERIFY(request.get());
    close(other);
    close(stalled);
    close(held);
  }

  void replacesStaleSocketButNotOtherFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const std::string path = dir.filePath(QStringLiteral("daemon.sock")).toStdString();
    int listener = -1;
    int lock = -1;
    std::string error;
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::Listening);
    // A crashed daemon leaves its socket file behind; the kernel drops its lock.
    close(listener);
    close(lock);
    QVERIFY(!forwardCaptureRequest(path));
    QCOMPARE(listenForCaptureRequests(path, &listener, &lock, &error),
             ListenResult::Listening);
    auto request = forwardLater(path);
    QCOMPARE(takeWithin(listener, 1), 1);
    QVERIFY(request.get());
    close(listener);
    close(lock);

    const QString other = dir.filePath(QStringLiteral("notes.txt"));
    QFile file(other);
    QVERIFY(file.open(QIODevice::WriteOnly) && file.write("keep") == 4);
    file.close();
    QCOMPARE(listenForCaptureRequests(other.toStdString(), &listener, &lock, &error),
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
