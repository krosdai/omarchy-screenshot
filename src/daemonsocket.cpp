// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "daemonsocket.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
bool fillAddress(const std::string &path, sockaddr_un *address) {
  if (path.empty() || path.size() >= sizeof address->sun_path)
    return false;
  std::memset(address, 0, sizeof *address);
  address->sun_family = AF_UNIX;
  std::memcpy(address->sun_path, path.c_str(), path.size() + 1);
  return true;
}

// Connects without waiting: a local connect either succeeds at once or, with
// the listener's backlog full, fails with EAGAIN, which errno keeps. The
// returned socket blocks again so callers can rely on their timeouts.
int connectTo(const std::string &path) {
  sockaddr_un address;
  if (!fillAddress(path, &address))
    return -1;
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return -1;
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address),
              sizeof address) != 0) {
    const int failure = errno;
    close(fd);
    errno = failure;
    return -1;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
  return fd;
}

constexpr char captureRequest[] = "capture\n";
constexpr char captureAccepted[] = "ok\n";

std::string systemError(const char *what) {
  return std::string(what) + ": " + std::strerror(errno);
}
} // namespace

std::string daemonSocketPath() {
  const char *runtime = std::getenv("XDG_RUNTIME_DIR");
  if (!runtime || !*runtime)
    return {};
  const std::string path = std::string(runtime) + "/omarchy-screenshot.sock";
  return path.size() < sizeof(sockaddr_un::sun_path) ? path : std::string();
}

bool forwardCaptureRequest(const std::string &path) {
  const int fd = connectTo(path);
  if (fd < 0)
    return false;
  if (send(fd, captureRequest, sizeof captureRequest - 1, MSG_NOSIGNAL) !=
      ssize_t(sizeof captureRequest - 1)) {
    close(fd);
    return false;
  }
  // A daemon that exits before accepting closes the connection unanswered,
  // and the caller then captures itself. Under systemd the request instead
  // waits for the restarted daemon, which takes well under the timeout.
  // Giving up closes the connection, which withdraws the request: a daemon
  // that reads it later can no longer answer, so it will not capture too.
  const timeval timeout{5, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  char reply[sizeof captureAccepted - 1];
  ssize_t received = recv(fd, reply, sizeof reply, MSG_WAITALL);
  if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // Withdraw atomically: after shutdown() the daemon's answer fails, and
    // one it sent just before is still read here, so exactly one side
    // captures.
    shutdown(fd, SHUT_RDWR);
    received = recv(fd, reply, sizeof reply, MSG_DONTWAIT);
  }
  const bool accepted =
      received == ssize_t(sizeof reply) &&
      std::memcmp(reply, captureAccepted, sizeof reply) == 0;
  close(fd);
  return accepted;
}

int activatedSocket(pid_t self, const char *listenPid, const char *listenFds) {
  if (!listenPid || !listenFds)
    return -1;
  char *end = nullptr;
  const long pid = std::strtol(listenPid, &end, 10);
  if (*listenPid == '\0' || *end != '\0' || pid != long(self))
    return -1;
  const long count = std::strtol(listenFds, &end, 10);
  if (*listenFds == '\0' || *end != '\0' || count < 1)
    return -1;
  // systemd hands its sockets over starting at SD_LISTEN_FDS_START.
  return 3;
}

int activatedSocket() {
  const int fd = activatedSocket(getpid(), std::getenv("LISTEN_PID"),
                                 std::getenv("LISTEN_FDS"));
  if (fd < 0)
    return -1;
  // Children must not mistake these variables for their own activation.
  unsetenv("LISTEN_PID");
  unsetenv("LISTEN_FDS");
  unsetenv("LISTEN_FDNAMES");
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  return fd;
}

ListenResult listenForCaptureRequests(const std::string &path, int *fd,
                                      int *lock, std::string *error) {
  sockaddr_un address;
  if (!fillAddress(path, &address)) {
    if (error)
      *error = "XDG_RUNTIME_DIR is not set or too long";
    return ListenResult::Failed;
  }
  // The lock decides which daemon owns the path, so two starting together
  // can never both take it or remove each other's socket.
  const std::string lockPath = path + ".lock";
  const int held = open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (held < 0) {
    if (error)
      *error = systemError(lockPath.c_str());
    return ListenResult::Failed;
  }
  if (flock(held, LOCK_EX | LOCK_NB) != 0) {
    const bool busy = errno == EWOULDBLOCK;
    if (error && !busy)
      *error = systemError("flock");
    close(held);
    return busy ? ListenResult::AlreadyRunning : ListenResult::Failed;
  }
  // systemd can hold the socket without a daemon behind it. This probe
  // sends nothing, so it never counts as a capture request.
  const int probe = connectTo(path);
  // A full backlog still means someone listens there.
  if (probe >= 0 || errno == EAGAIN) {
    if (probe >= 0)
      close(probe);
    close(held);
    return ListenResult::AlreadyRunning;
  }
  // Nobody answered, so a socket here was left by a daemon that died.
  struct stat existing;
  if (lstat(path.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode)) {
      if (error)
        *error = path + " exists and is not a socket";
      close(held);
      return ListenResult::Failed;
    }
    unlink(path.c_str());
  }
  const int listener =
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listener < 0) {
    if (error)
      *error = systemError("socket");
    close(held);
    return ListenResult::Failed;
  }
  // Only this user may ask for captures of this user's screen.
  const mode_t previous = umask(0177);
  const bool bound =
      bind(listener, reinterpret_cast<const sockaddr *>(&address),
           sizeof address) == 0;
  umask(previous);
  if (!bound || listen(listener, 8) != 0) {
    if (error)
      *error = systemError(bound ? "listen" : "bind");
    // A bound but unused node would block the systemd socket later.
    if (bound)
      unlink(path.c_str());
    close(listener);
    close(held);
    return ListenResult::Failed;
  }
  *fd = listener;
  *lock = held;
  return ListenResult::Listening;
}

int takeCaptureRequests(int listener) {
  int requests = 0;
  for (int client; (client = accept4(listener, nullptr, nullptr,
                                     SOCK_CLOEXEC)) >= 0;) {
    // A client writes its request right after connecting; a probe closes
    // without writing, which reads as end of file.
    // The timeout keeps a stalled client from blocking the daemon.
    const timeval timeout{0, 100000};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    char buffer[sizeof captureRequest - 1];
    const bool request =
        recv(client, buffer, sizeof buffer, MSG_WAITALL) ==
            ssize_t(sizeof buffer) &&
        std::memcmp(buffer, captureRequest, sizeof buffer) == 0;
    // Only a request whose client still waits for the answer counts; one
    // that gave up has captured by itself.
    const bool answered =
        request && send(client, captureAccepted, sizeof captureAccepted - 1,
                        MSG_NOSIGNAL) == ssize_t(sizeof captureAccepted - 1);
    close(client);
    requests += answered;
  }
  return requests;
}
