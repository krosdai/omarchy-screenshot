// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QImage>

class ScrollStitcher {
public:
  enum class Result { Added, Unchanged, NoMatch, TooLarge };

  void reset();
  Result append(const QImage &frame);
  const QImage &image() const { return m_image; }
  const QImage &lastFrame() const { return m_lastFrame; }
  int lastShift() const { return m_lastShift; }

private:
  QImage m_image;
  QImage m_lastFrame;
  int m_footerHeight = 0;
  int m_fixedLeft = 0;
  int m_fixedRight = 0;
  bool m_sidesDetected = false;
  int m_lastShift = 0;
};
