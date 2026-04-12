#pragma once

#include <QPolygonF>
#include <QSize>

#include "track/trackrecord.h"

QT_FORWARD_DECLARE_CLASS(QPainter);
QT_FORWARD_DECLARE_CLASS(QRect);

/// The StarRating class represents a rating as a number of stars.
/// In addition to holding the data, it is also capable of painting the stars
/// on a QPaintDevice, which in this example is either a view or an editor.
/// The m_starCount member variable stores the current rating, and
/// m_maxStarCount stores the highest possible rating (typically 5).
class StarRating {
  public:
    enum EditMode { Editable,
        ReadOnly };

    static constexpr int kMinStarCount = 0;
    static constexpr int kInvalidStarCount = -1;
    static constexpr int kDefaultPaintingScaleFactor = 15;
    static constexpr int kMinPaintingScaleFactor = 8;
    static constexpr int kMaxPaintingScaleFactor = 24;

    explicit StarRating(
            int starCount = kMinStarCount,
            int maxStarCount = mixxx::TrackRecord::kMaxRating - mixxx::TrackRecord::kMinRating,
            int paintingScaleFactor = kDefaultPaintingScaleFactor);

    void paint(QPainter* painter, const QRect& rect) const;
    QSize sizeHint() const;

    int starCount() const {
        return m_starCount;
    }
    int maxStarCount() const {
        return m_maxStarCount;
    }

    /// x is the x-position inside the parent rectangle rect
    int starAtPosition(int x, const QRect& rect) const;

    bool verifyStarCount(int starCount) {
        return starCount >= kMinStarCount && starCount <= m_maxStarCount;
    }

    void setStarCount(int starCount) {
        VERIFY_OR_DEBUG_ASSERT(verifyStarCount(starCount)) {
            return;
        }
        m_starCount = starCount;
    }

    bool verifyPaintingScaleFactor(int paintingScaleFactor) const {
        return paintingScaleFactor >= kMinPaintingScaleFactor &&
                paintingScaleFactor <= kMaxPaintingScaleFactor;
    }

    int paintingScaleFactor() const {
        return m_paintingScaleFactor;
    }

    void setPaintingScaleFactor(int paintingScaleFactor) {
        VERIFY_OR_DEBUG_ASSERT(verifyPaintingScaleFactor(paintingScaleFactor)) {
            return;
        }
        m_paintingScaleFactor = paintingScaleFactor;
    }

  private:
    QPolygonF m_starPolygon;
    QPolygonF m_diamondPolygon;
    int m_starCount;
    int m_maxStarCount;
    int m_paintingScaleFactor;
};

Q_DECLARE_METATYPE(StarRating)
