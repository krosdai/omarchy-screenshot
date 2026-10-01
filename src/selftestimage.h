// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>
#include <QSizeF>

// Qt Quick grabs contain device pixels. Self-test sample points use the
// item's logical coordinates, so normalize before inspecting those points.
inline QImage selfTestLogicalImage(const QImage &image, const QSizeF &itemSize) {
  if (image.isNull() || itemSize.isEmpty())
    return {};
  QImage result = image.scaled(itemSize.toSize(), Qt::IgnoreAspectRatio,
                               Qt::FastTransformation);
  result.setDevicePixelRatio(1);
  return result;
}
