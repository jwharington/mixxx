#include "waveform/renderers/allshader/waveformrenderbeat.h"

#include <QDomNode>

#include <algorithm>
#include <vector>

#include "moc_waveformrenderbeat.cpp"
#include "rendergraph/geometry.h"
#include "rendergraph/material/unicolormaterial.h"
#include "rendergraph/vertexupdaters/vertexupdater.h"
#include "skin/legacy/skincontext.h"
#include "track/beatfactory.h"
#include "track/track.h"
#include "waveform/renderers/waveformwidgetrenderer.h"
#include "widget/wskincolor.h"

using namespace rendergraph;

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

namespace allshader {

WaveformRenderBeat::WaveformRenderBeat(WaveformWidgetRenderer* waveformWidget,
        ::WaveformRendererAbstract::PositionSource type)
        : ::WaveformRendererAbstract(waveformWidget),
          m_isSlipRenderer(type == ::WaveformRendererAbstract::Slip) {
    initForRectangles<UniColorMaterial>(0);
    setUsePreprocess(true);
}

void WaveformRenderBeat::setup(const QDomNode& node, const SkinContext& skinContext) {
    m_color = QColor(skinContext.selectString(node, QStringLiteral("BeatColor")));
    m_color = WSkinColor::getCorrectColor(m_color).toRgb();
}

void WaveformRenderBeat::draw(QPainter* painter, QPaintEvent* event) {
    Q_UNUSED(painter);
    Q_UNUSED(event);
    DEBUG_ASSERT(false);
}

void WaveformRenderBeat::preprocess() {
    if (!preprocessInner()) {
        geometry().allocate(0);
        markDirtyGeometry();
    }
}

bool WaveformRenderBeat::preprocessInner() {
    const TrackPointer trackInfo = m_waveformRenderer->getTrackInfo();

    if (!trackInfo || (m_isSlipRenderer && !m_waveformRenderer->isSlipActive())) {
        return false;
    }

    auto positionType = m_isSlipRenderer ? ::WaveformRendererAbstract::Slip
                                         : ::WaveformRendererAbstract::Play;

    mixxx::BeatsPointer trackBeats = trackInfo->getBeats();
    if (!trackBeats) {
        return false;
    }

#ifndef __SCENEGRAPH__
    int alpha = m_waveformRenderer->getBeatGridAlpha();
    if (alpha == 0) {
        return false;
    }
    m_color.setAlphaF(alpha / 100.0f);
#endif

    if (!m_color.alpha()) {
        // Don't render the beatgrid lines is there are fully transparent
        return true;
    }

    const float devicePixelRatio = m_waveformRenderer->getDevicePixelRatio();

    const double trackSamples = m_waveformRenderer->getTrackSamples();
    if (trackSamples <= 0.0) {
        return false;
    }

    const double firstDisplayedPosition =
            m_waveformRenderer->getFirstDisplayedPosition(positionType);
    const double lastDisplayedPosition =
            m_waveformRenderer->getLastDisplayedPosition(positionType);

    const auto startPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            firstDisplayedPosition * trackSamples);
    const auto endPosition = mixxx::audio::FramePos::fromEngineSamplePos(
            lastDisplayedPosition * trackSamples);

    if (!startPosition.isValid() || !endPosition.isValid()) {
        return false;
    }

    const float rendererBreadth = m_waveformRenderer->getBreadth();

    const int numVerticesPerLine = 6; // 2 triangles

    const auto swing = extractSwingEstimate(trackBeats);
    const double visibleStartSample = firstDisplayedPosition * trackSamples;
    const double visibleEndSample = lastDisplayedPosition * trackSamples;

    std::vector<float> beatLinePositions;
    beatLinePositions.reserve(256);

    auto it = trackBeats->iteratorFrom(startPosition);
    auto nextIt = it;
    if (nextIt != trackBeats->cend()) {
        ++nextIt;
    }

    for (; it != trackBeats->cend() && *it <= endPosition; ++it) {
        const double beatPosition = it->toEngineSamplePos();
        double xBeatPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                beatPosition,
                positionType);
        xBeatPoint = qRound(xBeatPoint * devicePixelRatio) / devicePixelRatio;
        beatLinePositions.push_back(static_cast<float>(xBeatPoint));

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
                m_waveformRenderer->transformSamplePositionInRendererWorld(
                        nextBeatPosition,
                        positionType);
        if (std::abs(xNextBeatPoint - xBeatPoint) < kMinPixelsBetweenBeatsForSubdivisions) {
            ++nextIt;
            continue;
        }

        const double offBeatFraction = 0.5 * (1.0 + swingFraction);
        const double subBeatPosition = beatPosition + beatInterval * offBeatFraction;
        if (subBeatPosition >= visibleStartSample && subBeatPosition <= visibleEndSample) {
            double xSubBeatPoint = m_waveformRenderer->transformSamplePositionInRendererWorld(
                    subBeatPosition,
                    positionType);
            xSubBeatPoint = qRound(xSubBeatPoint * devicePixelRatio) / devicePixelRatio;
            beatLinePositions.push_back(static_cast<float>(xSubBeatPoint));
        }

        ++nextIt;
    }

    const int reserved = static_cast<int>(beatLinePositions.size()) * numVerticesPerLine;
    geometry().allocate(reserved);

    VertexUpdater vertexUpdater{geometry().vertexDataAs<Geometry::Point2D>()};
    for (float x : beatLinePositions) {
        const float x2 = x + 1.f;
        vertexUpdater.addRectangle({x, 0.f},
                {x2, m_isSlipRenderer ? rendererBreadth / 2 : rendererBreadth});
    }
    markDirtyGeometry();

    DEBUG_ASSERT(reserved == vertexUpdater.index());

    material().setUniform(1, m_color);
    markDirtyMaterial();

    return true;
}

} // namespace allshader
