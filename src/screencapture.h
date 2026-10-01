// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QList>
#include <QStringList>

// Copies each named output through ext-image-copy-capture-v1 on a private
// Wayland connection. A null image means the caller must capture that output
// another way (protocol missing, transformed output, failure or timeout).
QList<QImage> captureOutputs(const QStringList &names, int timeoutMs);
