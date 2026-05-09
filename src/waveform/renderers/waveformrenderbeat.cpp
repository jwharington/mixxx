#include "waveform/renderers/waveformrenderbeat.h"

#include <QPainter>

#include <algorithm>

#include "track/beatfactory.h"
#include "track/track.h"
#include "util/painterscope.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

class QPaintEvent;

namespace {
constexpr double kUnknownSwingPercent = -1.0;
constexpr double kMaxSwingFraction = 0.95;
constexpr double kMinPixelsBetweenBeatsForSubdivisions = 12.0;

struct SwingEstimate {
    double now = kUnknownSwingPercent;
    double start = kUnknownSwingPercent;
    double end = kUnknownSwingPercent;
};

SwingEstimate extractSwingEstimate(const mixxx::BeatsPointer& pBeats) {
    SwingEstimate estimate;
    if (!pBeats) {
        return estimate;
    }

    const auto info = BeatFactory::parseSubVersion(pBeats->getSubVersion());
    bool ok = false;

    estimate.now = info.value(QStringLiteral("swing_pct")).toDouble(&ok);
    if (!ok) {
        estimate.now = kUnknownSwingPercent;
    }

    estimate.start = info.value(QStringLiteral("swing_start_pct")).toDouble(&ok);
    if (!ok) {
        estimate.start = kUnknownSwingPercent;
    }

    estimate.end = info.value(QStringLiteral("swing_end_pct")).toDouble(&ok);
    if (!ok) {
        estimate.end = kUnknownSwingPercent;
    }

    return estimate;
}

double swingFractionAtPosition(const SwingEstimate& estimate,
        double beatPosition,
        double trackSamples) {
    double swingPercent = estimate.now;
    if (estimate.start >= 0.0 && estimate.end >= 0.0 && trackSamples > 0.0) {
        const double normalizedPosition = std::clamp(beatPosition / trackSamples, 0.0, 1.0);
        swingPercent =
                estimate.start + (estimate.end - estimate.start) * normalizedPosition;
    }

    if (swingPercent < 0.0) {
        return kUnknownSwingPercent;
    }

    return std::clamp(swingPercent / 100.0, 0.0, kMaxSwingFraction);
}
} // namespace

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidgetRenderer)
        : WaveformRendererAbstract(waveformWidgetRenderer) {
    m_beats.resize(128);
}

WaveformRenderBeat::~WaveformRenderBeat() {
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& context) {
    m_beatColor = QColor(context.selectString(node, "BeatColor"));
    m_beatColor = WSkinColor::getCorrectColor(m_beatColor).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* /*event*/) {
    TrackPointer pTrackInfo = m_waveformRenderer->getTrackInfo();

    if (!pTrackInfo) {
        return;
    }

    mixxx::BeatsPointer trackBeats = pTrackInfo->getBeats();
    if (!trackBeats) {
        return;
    }

    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return;
    }
#ifdef MIXXX_USE_QOPENGL
    // Using alpha transparency with drawLines causes a graphical issue when
    // drawing with QPainter on the QOpenGLWindow: instead of individual lines
    // a large rectangle encompassing all beatlines is drawn.
    m_beatColor.setAlphaF(1.f);
#else
    m_beatColor.setAlphaF(alpha/100.0);
#endif

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0) {
        return;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition();
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition();

    // qDebug() << "trackSamples" << trackSamples
    //          << "firstDisplayedPosition" << firstDisplayedPosition
    //          << "lastDisplayedPosition" << lastDisplayedPosition;

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);
    auto it = trackBeats->iteratorFrom(startPosition);

    // if no beat do not waste time saving/restoring painter
    if (it == trackBeats->cend() || *it > endPosition) {
        return;
    }

    PainterScope PainterScope(painter);

    painter->setRenderHint(QPainter::Antialiasing);

    QPen beatPen(m_beatColor);
    beatPen.setWidthF(std::max(1.0, scaleFactor()));
    painter->setPen(beatPen);

    const Qt::Orientation orientation = m_waveformRenderer->getOrientation();
    const float rendererWidth = m_waveformRenderer->getWidth();
    const float rendererHeight = m_waveformRenderer->getHeight();

    const auto swing = extractSwingEstimate(trackBeats);

    int beatCount = 0;
    auto nextIt = it;
    if (nextIt != trackBeats->cend()) {
        ++nextIt;
    }

    const auto appendBeatLine = [&](double xBeatPoint) {
        // If we don't have enough space, double the size.
        if (beatCount >= m_beats.size()) {
            m_beats.resize(m_beats.size() * 2);
        }

        if (orientation == Qt::Horizontal) {
            m_beats[beatCount++].setLine(xBeatPoint, 0.0f, xBeatPoint, rendererHeight);
        } else {
            m_beats[beatCount++].setLine(0.0f, xBeatPoint, rendererWidth, xBeatPoint);
        }
    };

    for (; it != trackBeats->cend() && *it <= endPosition; ++it) {
        const double beatPosition = it->toEngineSamplePos();
        double xBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(beatPosition);

        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;
        appendBeatLine(xBeatPoint);

        if (nextIt == trackBeats->cend()) {
            continue;
        }

        const double nextBeatPosition = nextIt->toEngineSamplePos();
        const double beatInterval = nextBeatPosition - beatPosition;
        if (beatInterval <= 0.0) {
            ++nextIt;
            continue;
        }

        const double swingFraction = swingFractionAtPosition(swing, beatPosition, trackSamples);
        if (swingFraction < 0.0) {
            ++nextIt;
            continue;
        }

        const double xNextBeatPoint =
                m_waveformRenderer->transformSamplePositionInRendererWorld(nextBeatPosition);
        if (std::abs(xNextBeatPoint - xBeatPoint) < kMinPixelsBetweenBeatsForSubdivisions) {
            ++nextIt;
            continue;
        }

        const double fractions[] = {
                0.25 * (1.0 + swingFraction),
                0.5,
                0.75 + 0.25 * swingFraction,
        };
        for (double fraction : fractions) {
            const double subBeatPosition = beatPosition + beatInterval * fraction;
            if (subBeatPosition < firstDisplayedPosition * trackSamples ||
                    subBeatPosition > lastDisplayedPosition * trackSamples) {
                continue;
            }
            double xSubBeatPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                    subBeatPosition);
            xSubBeatPoint = qRound(xSubBeatPoint * devicePixelRatio) / devicePixelRatio;
            appendBeatLine(xSubBeatPoint);
        }

        ++nextIt;
    }

    // Make sure to use constData to prevent detaches!
    painter->drawLines(m_beats.constData(), beatCount);
}
