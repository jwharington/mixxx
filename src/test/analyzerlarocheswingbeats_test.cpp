#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <QFileInfo>

#include "analyzer/constants.h"
#include "analyzer/plugins/analyzerlarocheswingbeats.h"
#include "sources/audiosourcestereoproxy.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxtest.h"
#include "test/soundsourceproviderregistration.h"
#include "track/track.h"
#include "util/samplebuffer.h"

namespace {

constexpr SINT kReadChunkFrames = 4096;

class AnalyzerLarocheSwingBeatsFileTest
        : public MixxxTest,
          public SoundSourceProviderRegistration {
  protected:
    static mixxx::AudioSourcePointer openStereoAudioSource(const QString& filePath) {
        auto pTrack = Track::newTemporary(filePath);
        SoundSourceProxy proxy(pTrack);

        mixxx::AudioSource::OpenParams openParams;
        openParams.setChannelCount(mixxx::audio::ChannelCount::stereo());

        auto pAudioSource = proxy.openAudioSource(openParams);
        if (pAudioSource &&
                pAudioSource->getSignalInfo().getChannelCount() !=
                        mixxx::audio::ChannelCount::stereo()) {
            pAudioSource = mixxx::AudioSourceStereoProxy::create(
                    pAudioSource,
                    kReadChunkFrames);
        }
        return pAudioSource;
    }
};

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t mid = values.size() / 2;
    if (values.size() % 2 == 0) {
        return 0.5 * (values[mid - 1] + values[mid]);
    }
    return values[mid];
}

TEST(AnalyzerLarocheSwingBeatsSyntheticTest, EstimatesTempoAndSwingOnSyntheticClicks) {
    const QByteArray oldModelEnv = qgetenv("MIXXX_LAROCHE_TEMPO_MODEL_JSON");
    qputenv("MIXXX_LAROCHE_TEMPO_MODEL_JSON", QByteArray("__disable_tempo_model_for_test__"));

    constexpr auto sampleRate = mixxx::audio::SampleRate(44100);
    constexpr double durationSec = 12.0;
    constexpr double bpm = 120.0;
    constexpr double swing = 0.2; // 20%
    constexpr double periodSec = 60.0 / bpm;
    constexpr double quarterSec = periodSec / 4.0;
    constexpr double startSec = 0.2;

    const int totalFrames = static_cast<int>(durationSec * sampleRate.value());
    std::vector<CSAMPLE> audio(totalFrames * mixxx::kAnalysisChannels, 0.0f);

    auto addClick = [&](double timeSec, float amplitude) {
        const int center = static_cast<int>(std::round(timeSec * sampleRate.value()));
        for (int n = 0; n < 180; ++n) {
            const int frame = center + n;
            if (frame < 0 || frame >= totalFrames) {
                continue;
            }
            const float env = amplitude * (1.0f - static_cast<float>(n) / 180.0f);
            const int sampleIndex = frame * mixxx::kAnalysisChannels;
            audio[sampleIndex] += env;
            audio[sampleIndex + 1] += env;
        }
    };

    for (double beat = startSec; beat < durationSec - periodSec; beat += periodSec) {
        addClick(beat + 0.0, 0.9f);
        addClick(beat + quarterSec * (1.0 + swing), 0.6f);
        addClick(beat + 2.0 * quarterSec, 0.8f);
        addClick(beat + quarterSec * (3.0 + swing), 0.6f);
    }

    mixxx::AnalyzerLarocheSwingBeats analyzer;
    ASSERT_TRUE(analyzer.initialize(sampleRate));

    const int chunkSamples = mixxx::kAnalysisFramesPerChunk * mixxx::kAnalysisChannels;
    for (int i = 0; i < static_cast<int>(audio.size()); i += chunkSamples) {
        const int remaining = static_cast<int>(audio.size()) - i;
        const int count = std::min(chunkSamples, remaining);
        ASSERT_EQ(count % mixxx::kAnalysisChannels, 0);
        ASSERT_TRUE(analyzer.processSamples(audio.data() + i, count));
    }

    ASSERT_TRUE(analyzer.finalize());
    const auto beats = analyzer.getBeats();
    ASSERT_GE(beats.size(), 8);

    std::vector<double> beatDiffs;
    beatDiffs.reserve(beats.size() - 1);
    for (int i = 1; i < beats.size(); ++i) {
        const double diff = (beats[i] - beats[i - 1]) / sampleRate.toDouble();
        if (diff > 0.0) {
            beatDiffs.push_back(diff);
        }
    }
    const double beatPeriodEstimated = median(beatDiffs);
    ASSERT_GT(beatPeriodEstimated, 0.0);
    const double bpmEstimated = 60.0 / beatPeriodEstimated;
    // Synthetic pattern encodes two quarter-note beats per cycle.
    EXPECT_NEAR(bpmEstimated, bpm * 2.0, 4.0);

    const auto extraInfo = analyzer.getExtraVersionInfo();
    ASSERT_TRUE(extraInfo.contains("swing_pct"));
    bool ok = false;
    const double swingEstimatedPercent = extraInfo.value("swing_pct").toDouble(&ok);
    ASSERT_TRUE(ok);
    EXPECT_NEAR(swingEstimatedPercent, swing * 100.0, 10.0);

    if (oldModelEnv.isEmpty()) {
        qunsetenv("MIXXX_LAROCHE_TEMPO_MODEL_JSON");
    } else {
        qputenv("MIXXX_LAROCHE_TEMPO_MODEL_JSON", oldModelEnv);
    }
}

TEST_F(AnalyzerLarocheSwingBeatsFileTest, AnalyzesMainmixWavFromTestData) {
    const QString filePath = getTestDir().filePath(QStringLiteral("stems/mainmix.wav"));
    if (!QFileInfo::exists(filePath)) {
        GTEST_SKIP() << "Missing test file: " << filePath.toStdString();
    }

    auto pAudioSource = openStereoAudioSource(filePath);
    ASSERT_TRUE(pAudioSource) << "Unable to open audio source for " << filePath.toStdString();

    const auto signalInfo = pAudioSource->getSignalInfo();
    ASSERT_EQ(signalInfo.getChannelCount(), mixxx::audio::ChannelCount::stereo());

    mixxx::AnalyzerLarocheSwingBeats analyzer;
    ASSERT_TRUE(analyzer.initialize(signalInfo.getSampleRate()));

    mixxx::SampleBuffer sampleBuffer(signalInfo.frames2samples(kReadChunkFrames));
    SINT frameIndex = pAudioSource->frameIndexMin();

    while (pAudioSource->frameIndexRange().containsIndex(frameIndex)) {
        const SINT readFrameCount = std::min(
                kReadChunkFrames,
                pAudioSource->frameIndexMax() - frameIndex);
        const auto requestedFrameRange = mixxx::IndexRange::forward(frameIndex, readFrameCount);
        auto sampleFrames = pAudioSource->readSampleFrames(
                mixxx::WritableSampleFrames(
                        requestedFrameRange,
                        mixxx::SampleBuffer::WritableSlice(
                                sampleBuffer,
                                0,
                                signalInfo.frames2samples(readFrameCount))));

        if (sampleFrames.frameIndexRange().empty()) {
            break;
        }

        const SINT readSamples = signalInfo.frames2samples(sampleFrames.frameLength());
        ASSERT_TRUE(analyzer.processSamples(sampleFrames.readableData(), readSamples));
        frameIndex = sampleFrames.frameIndexRange().end();
    }

    ASSERT_TRUE(analyzer.finalize());
    const auto beats = analyzer.getBeats();
    ASSERT_GE(beats.size(), 8);

    std::vector<double> beatDiffs;
    beatDiffs.reserve(beats.size() - 1);
    for (int i = 1; i < beats.size(); ++i) {
        const double diff =
                (beats[i] - beats[i - 1]) / signalInfo.getSampleRate().toDouble();
        if (diff > 0.0) {
            beatDiffs.push_back(diff);
        }
    }

    const double beatPeriodEstimated = median(beatDiffs);
    ASSERT_GT(beatPeriodEstimated, 0.0);

    const double bpmEstimated = 60.0 / beatPeriodEstimated;
    EXPECT_GT(bpmEstimated, 70.0);
    EXPECT_LT(bpmEstimated, 300.0);
}

} // namespace
