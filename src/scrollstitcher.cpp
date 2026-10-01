// SPDX-FileCopyrightText: 2026 Andy Stewart
// SPDX-License-Identifier: GPL-3.0-only

#include "scrollstitcher.h"

#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <limits>

namespace {
constexpr qsizetype maxPixels = 48'000'000;

int pixelDifference(QRgb a, QRgb b) {
  return (std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
          std::abs(qBlue(a) - qBlue(b))) /
         3;
}

int edgeDifference(QRgb a, QRgb aRight, QRgb aBelow, QRgb b,
                   QRgb bRight, QRgb bBelow) {
  const auto distance = [](int c1, int n1, int c2, int n2) {
    return std::abs((c1 - n1) - (c2 - n2));
  };
  return (distance(qRed(a), qRed(aRight), qRed(b), qRed(bRight)) +
          distance(qGreen(a), qGreen(aRight), qGreen(b), qGreen(bRight)) +
          distance(qBlue(a), qBlue(aRight), qBlue(b), qBlue(bRight)) +
          distance(qRed(a), qRed(aBelow), qRed(b), qRed(bBelow)) +
          distance(qGreen(a), qGreen(aBelow), qGreen(b), qGreen(bBelow)) +
          distance(qBlue(a), qBlue(aBelow), qBlue(b), qBlue(bBelow))) /
         6;
}

double samePositionDifference(const QImage &a, const QImage &b) {
  qint64 difference = 0;
  int samples = 0;
  const int xStep = std::max(1, a.width() / 48);
  const int yStep = std::max(1, a.height() / 48);
  for (int row = 0, baseY = 0; baseY < a.height(); ++row, baseY += yStep)
    for (int column = 0, baseX = 0; baseX < a.width(); ++column, baseX += xStep) {
      // Vary the position within each cell so regular line spacing cannot
      // make every sample land in the blank gap between text lines.
      const int y = std::min(baseY + (row * 7 + column * 3) % yStep,
                             a.height() - 1);
      const int x = std::min(baseX + (row * 11 + column * 5) % xStep,
                             a.width() - 1);
      const int right = std::min(x + 3, a.width() - 1);
      const int below = std::min(y + 3, a.height() - 1);
      difference += edgeDifference(a.pixel(x, y), a.pixel(right, y),
                                   a.pixel(x, below), b.pixel(x, y),
                                   b.pixel(right, y), b.pixel(x, below));
      ++samples;
    }
  return double(difference) / std::max(1, samples);
}

int fixedEdge(const QImage &a, const QImage &b, bool top) {
  const int limit = a.height() / 4;
  const int xStep = std::max(1, a.width() / 256);
  int edge = 0;
  for (int i = 0; i < limit; i += 4) {
    int informative = 0;
    int changed = 0;
    int stable = 0;
    int total = 0;
    for (int yy = i; yy < std::min(i + 4, limit); ++yy) {
      const int y = top ? yy : a.height() - 1 - yy;
      for (int x = 0; x < a.width(); x += xStep) {
        const int neighbor = std::min(x + 3, a.width() - 1);
        const QRgb pa = a.pixel(x, y);
        const QRgb pb = b.pixel(x, y);
        const QRgb paRight = a.pixel(neighbor, y);
        const QRgb pbRight = b.pixel(neighbor, y);
        const int difference = edgeDifference(pa, paRight, pa,
                                              pb, pbRight, pb);
        ++total;
        if (difference < 3)
          ++stable;
        if (pixelDifference(pa, paRight) < 15 &&
            pixelDifference(pb, pbRight) < 15)
          continue;
        ++informative;
        if (difference > 12)
          ++changed;
      }
    }
    if ((informative < 8 && stable * 100 < total * 98) ||
        (informative >= 8 && changed * 100 >= informative * 2))
      break;
    edge = std::min(i + 4, limit);
  }
  return edge;
}

double overlapDifference(const QImage &previous, const QImage &current,
                         int top, int bottom, int left, int right, int shift) {
  const int overlap = bottom - top - shift;
  if (overlap < 32)
    return std::numeric_limits<double>::infinity();
  const int xStep = std::max(1, (right - left) / 96);
  const int yStep = std::max(1, overlap / 64);
  qint64 difference = 0;
  int samples = 0;
  for (int y = top; y < bottom - shift; y += yStep) {
    const auto *prevRow = reinterpret_cast<const QRgb *>(
        previous.constScanLine(y + shift));
    const auto *currRow = reinterpret_cast<const QRgb *>(
        current.constScanLine(y));
    const auto *prevNext = reinterpret_cast<const QRgb *>(
        previous.constScanLine(std::min(y + shift + 3, previous.height() - 1)));
    const auto *currNext = reinterpret_cast<const QRgb *>(
        current.constScanLine(std::min(y + 3, current.height() - 1)));
    for (int x = left; x < right; x += xStep) {
      const int neighbor = std::min(x + 3, right - 1);
      const QRgb pa = prevRow[x];
      const QRgb pb = currRow[x];
      const QRgb paRight = prevRow[neighbor];
      const QRgb pbRight = currRow[neighbor];
      const QRgb paBelow = prevNext[x];
      const QRgb pbBelow = currNext[x];
      if (pixelDifference(pa, paRight) < 15 &&
          pixelDifference(pb, pbRight) < 15 &&
          pixelDifference(pa, paBelow) < 15 &&
          pixelDifference(pb, pbBelow) < 15)
        continue;
      difference += edgeDifference(pa, paRight, paBelow,
                                   pb, pbRight, pbBelow);
      ++samples;
    }
  }
  if (samples < 24)
    return std::numeric_limits<double>::infinity();
  return double(difference) / samples;
}

int fixedSide(const QImage &previous, const QImage &current, int top,
              int bottom, bool left) {
  const int limit = previous.width() / 4;
  const int yStep = std::max(1, (bottom - top) / 96);
  int edge = 0;
  for (int i = 0; i < limit; i += 4) {
    int changed = 0;
    int informative = 0;
    int stable = 0;
    int total = 0;
    for (int offset = i; offset < std::min(i + 4, limit); ++offset) {
      const int x = left ? offset : previous.width() - 1 - offset;
      const int neighbor = std::clamp(x + (left ? 3 : -3), 0,
                                      previous.width() - 1);
      for (int y = top; y < bottom; y += yStep) {
        const int below = std::min(y + 3, previous.height() - 1);
        const QRgb pa = previous.pixel(x, y);
        const QRgb pb = current.pixel(x, y);
        const QRgb paNeighbor = previous.pixel(neighbor, y);
        const QRgb pbNeighbor = current.pixel(neighbor, y);
        const QRgb paBelow = previous.pixel(x, below);
        const QRgb pbBelow = current.pixel(x, below);
        const int difference = edgeDifference(pa, paNeighbor, paBelow,
                                              pb, pbNeighbor, pbBelow);
        ++total;
        if (difference < 3)
          ++stable;
        if (pixelDifference(pa, paNeighbor) < 15 &&
            pixelDifference(pb, pbNeighbor) < 15 &&
            pixelDifference(pa, paBelow) < 15 &&
            pixelDifference(pb, pbBelow) < 15)
          continue;
        ++informative;
        if (difference > 12)
          ++changed;
      }
    }
    if ((informative < 8 && stable * 100 < total * 98) ||
        (informative >= 8 && changed * 100 >= informative * 2))
      break;
    edge = std::min(i + 4, limit);
  }
  return edge;
}
} // namespace

void ScrollStitcher::reset() {
  m_image = {};
  m_lastFrame = {};
  m_footerHeight = 0;
  m_fixedLeft = 0;
  m_fixedRight = 0;
  m_sidesDetected = false;
  m_lastShift = 0;
}

ScrollStitcher::Result ScrollStitcher::append(const QImage &input) {
  if (input.isNull() || input.width() < 32 || input.height() < 64)
    return Result::NoMatch;
  const QImage frame = input.convertToFormat(QImage::Format_RGB32);
  if (m_lastFrame.isNull()) {
    if (qsizetype(frame.width()) * frame.height() > maxPixels)
      return Result::TooLarge;
    m_image = frame;
    m_lastFrame = frame;
    return Result::Added;
  }
  if (frame.size() != m_lastFrame.size())
    return Result::NoMatch;
  const double stationaryScore = samePositionDifference(m_lastFrame, frame);
  if (stationaryScore < 1.5)
    return Result::Unchanged;

  const int header = fixedEdge(m_lastFrame, frame, true);
  const int detectedFooter = fixedEdge(m_lastFrame, frame, false);
  // A few blank rows between paragraphs are not evidence of a fixed footer.
  const int footer = detectedFooter >= 16 ? detectedFooter : 0;
  const int bodyEnd = frame.height() - footer;
  // Browser chrome, sticky headers and sidebars can change independently of
  // the page. Match the central scrolling content instead of letting these
  // fixed regions dominate the score or produce a false small shift.
  const int matchTop = std::max(header, frame.height() / 4);
  const int matchBottom = std::min(bodyEnd, frame.height() * 7 / 8);
  const int matchLeft = frame.width() / 4;
  const int matchRight = frame.width() - frame.width() / 12;
  const double coreStationaryScore =
      overlapDifference(m_lastFrame, frame, matchTop, matchBottom,
                        matchLeft, matchRight, 0);
  if (coreStationaryScore < 1.5)
    return Result::Unchanged;
  const int maxShift = matchBottom - matchTop - 32;
  if (maxShift < 8)
    return Result::NoMatch;

  int bestShift = 0;
  double best = std::numeric_limits<double>::infinity();
  // Exact offsets matter: text rows can match perfectly at one pixel offset
  // while the neighboring offsets have a much worse score.
  for (int shift = 1; shift <= maxShift; ++shift) {
    const double score =
        overlapDifference(m_lastFrame, frame, matchTop, matchBottom,
                          matchLeft, matchRight, shift);
    if (score < best) {
      best = score;
      bestShift = shift;
    }
  }
  if (qEnvironmentVariableIsSet("OMARCHY_SCROLL_DIAGNOSTICS"))
    QTextStream(stderr) << "scroll match stationary=" << stationaryScore
                        << " core stationary=" << coreStationaryScore
                        << " header=" << header << " footer=" << footer
                        << " core=" << matchTop << ',' << matchBottom
                        << " best shift=" << bestShift << " score=" << best
                        << '\n';
  if (bestShift <= 0 || best > 18.0)
    return Result::NoMatch;

  if (!m_sidesDetected) {
    m_fixedLeft = fixedSide(m_lastFrame, frame, header, bodyEnd, true);
    m_fixedRight = fixedSide(m_lastFrame, frame, header, bodyEnd, false);
    m_sidesDetected = true;
  }

  const int oldFooter = m_footerHeight ? m_footerHeight : footer;
  const int nextHeight = m_image.height() - oldFooter + bestShift + footer;
  if (qsizetype(frame.width()) * nextHeight > maxPixels)
    return Result::TooLarge;
  QImage combined(frame.width(), nextHeight, QImage::Format_RGB32);
  if (combined.isNull())
    return Result::TooLarge;
  {
    QPainter painter(&combined);
    painter.drawImage(QPoint(0, 0), m_image,
                      QRect(0, 0, m_image.width(),
                            m_image.height() - oldFooter));
    painter.drawImage(QPoint(0, m_image.height() - oldFooter), frame,
                      QRect(0, bodyEnd - bestShift, frame.width(), bestShift));
    if (m_fixedLeft)
      painter.fillRect(QRect(0, m_image.height() - oldFooter,
                             m_fixedLeft, bestShift),
                       QColor::fromRgb(frame.pixel(0, header)));
    if (m_fixedRight)
      painter.fillRect(QRect(frame.width() - m_fixedRight,
                             m_image.height() - oldFooter,
                             m_fixedRight, bestShift),
                       QColor::fromRgb(frame.pixel(frame.width() - 1, header)));
    if (footer)
      painter.drawImage(QPoint(0, nextHeight - footer), frame,
                        QRect(0, bodyEnd, frame.width(), footer));
  }
  m_image = std::move(combined);
  m_lastFrame = frame;
  m_footerHeight = footer;
  m_lastShift = bestShift;
  return Result::Added;
}
