// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "daemonsocket.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

int connectTo(const std::string &path) {
  sockaddr_un address;
  if (!fillAddress(path, &address))
    return -1;
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  if (connect(fd, reinterpret_cast<const sockaddr *>(&address),
              sizeof address) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

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
  static constexpr char request[] = "capture\n";
  const ssize_t written =
      send(fd, request, sizeof request - 1, MSG_NOSIGNAL);
  close(fd);
  return written == ssize_t(sizeof request - 1);
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
                                      std::string *error) {
  sockaddr_un address;
  if (!fillAddress(path, &address)) {
    if (error)
      *error = "XDG_RUNTIME_DIR is not set or too long";
    return ListenResult::Failed;
  }
  const int probe = connectTo(path);
  if (probe >= 0) {
    close(probe);
    return ListenResult::AlreadyRunning;
  }
  // Nobody answered, so a socket here was left by a daemon that died.
  struct stat existing;
  if (lstat(path.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode)) {
      if (error)
        *error = path + " exists and is not a socket";
      return ListenResult::Failed;
    }
    unlink(path.c_str());
  }
  const int listener =
      socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (listener < 0) {
    if (error)
      *error = systemError("socket");
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
    close(listener);
    return ListenResult::Failed;
  }
  *fd = listener;
  return ListenResult::Listening;
}
