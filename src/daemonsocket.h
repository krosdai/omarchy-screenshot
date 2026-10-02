// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <string>

#include <sys/types.h>

// The resident daemon's request socket. Plain C++ on purpose: the client side
// runs before Qt starts, so forwarding a capture costs no toolkit start-up.

// $XDG_RUNTIME_DIR/omarchy-screenshot.sock, or empty without a runtime dir.
std::string daemonSocketPath();

// Asks a listening daemon (or the systemd socket holding its place) for a
// capture and waits for it to accept. False means no daemon took the request
// and the caller should capture itself.
bool forwardCaptureRequest(const std::string &path);

// The listening socket systemd passed in, or -1 when not socket-activated.
// The parts are explicit so tests need not touch the process environment.
int activatedSocket(pid_t self, const char *listenPid, const char *listenFds);
int activatedSocket();

enum class ListenResult { Listening, AlreadyRunning, Failed };

// Binds path for a daemon started by hand and locks path + ".lock", which
// the daemon keeps until it exits. A socket left by a dead daemon is
// replaced; one a live daemon or systemd still answers is left alone.
ListenResult listenForCaptureRequests(const std::string &path, int *fd,
                                      int *lock, std::string *error);

// Accepts every queued connection, acknowledges each capture request, and
// returns how many were acknowledged; a client that already gave up is not.
int takeCaptureRequests(int listener);
