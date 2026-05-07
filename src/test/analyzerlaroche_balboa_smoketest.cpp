#include <gtest/gtest.h>

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

#include "analyzer/plugins/analyzerlarocheswingbeats.h"
#include "sources/audiosourcestereoproxy.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxtest.h"
#include "test/soundsourceproviderregistration.h"
#include "track/track.h"
#include "util/samplebuffer.h"

namespace {

constexpr SINT kReadChunkFrames = 4096;
constexpr int kDisplayNameMax = 36;

struct ResultRow {
    QString path;
    QString artist;
    QString title;
    double bpm = 0.0;
    double bpmRaw = 0.0;
    double bpmSelected = 0.0;
    double levelMultiplier = 1.0;
    double levelConfidence = 0.0;
    QString swing;
    QString confidence;
    QString ambiguity;
    qint64 elapsedMs = 0;
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
constexpr int kMaxFilesToAnalyzeDefault = 50;
constexpr int kMaxSecondsPerFile = 90;

class AnalyzerLarocheBalboaSmokeTest
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

TEST_F(AnalyzerLarocheBalboaSmokeTest, AnalyzeBalboaMp3Collection) {
    const QString balboaPath = QStringLiteral("/home/jmw/Movies/m/Music/Balboa");
    const QString musicPath = QStringLiteral("/home/jmw/Movies/m/Music");

    const QString rootOverride = QString::fromLocal8Bit(qgetenv("MIXXX_LAROCHE_ROOT")).trimmed();
    const QStringList roots = !rootOverride.isEmpty()
            ? QStringList{rootOverride}
            : QStringList{balboaPath, musicPath};

    bool anyRootExists = false;
    for (const auto& root : roots) {
        if (QFileInfo::exists(root)) {
            anyRootExists = true;
            break;
        }
    }
    if (!anyRootExists) {
        GTEST_SKIP() << "No input root directory exists.";
    }

    bool okMax = false;
    const int maxFilesToAnalyze = QString::fromLocal8Bit(qgetenv("MIXXX_LAROCHE_MAX_FILES")).toInt(&okMax);
    const int maxFiles = (okMax && maxFilesToAnalyze > 0)
            ? maxFilesToAnalyze
            : kMaxFilesToAnalyzeDefault;

    QStringList candidatePaths;
    QSet<QString> seenPaths;
    for (const auto& root : roots) {
        if (!QFileInfo::exists(root)) {
            continue;
        }
        QDirIterator it(
                root,
                QStringList() << QStringLiteral("*.mp3") << QStringLiteral("*.MP3"),
                QDir::Files,
                QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            if (seenPaths.contains(filePath)) {
                continue;
            }
            seenPaths.insert(filePath);
            candidatePaths.push_back(filePath);
        }
    }
    std::sort(candidatePaths.begin(), candidatePaths.end());

    int filesSeen = 0;
    int analyzed = 0;
    int withSwing = 0;
    int fallbackCount = 0;
    int ambiguousCount = 0;
    std::vector<ResultRow> rows;

    for (const auto& filePath : candidatePaths) {
        if (filesSeen >= maxFiles) {
            break;
        }
        ++filesSeen;

        QElapsedTimer timer;
        timer.start();

        auto pAudioSource = openStereoAudioSource(filePath);
        if (!pAudioSource) {
            continue;
        }

        const auto signalInfo = pAudioSource->getSignalInfo();
        mixxx::AnalyzerLarocheSwingBeats analyzer;
        ASSERT_TRUE(analyzer.initialize(signalInfo.getSampleRate()));

        mixxx::SampleBuffer sampleBuffer(signalInfo.frames2samples(kReadChunkFrames));
        const SINT maxFrames = static_cast<SINT>(signalInfo.secs2frames(kMaxSecondsPerFile));
        SINT frameIndex = pAudioSource->frameIndexMin();
        const SINT frameStop = std::min(pAudioSource->frameIndexMax(), frameIndex + maxFrames);

        while (frameIndex < frameStop && pAudioSource->frameIndexRange().containsIndex(frameIndex)) {
            const SINT readFrameCount = std::min(kReadChunkFrames, frameStop - frameIndex);
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

        if (!analyzer.finalize()) {
            continue;
        }

        ++analyzed;
        const auto beats = analyzer.getBeats();
        const auto extra = analyzer.getExtraVersionInfo();
        if (extra.contains(QStringLiteral("swing_pct"))) {
            ++withSwing;
        }
        if (extra.contains(QStringLiteral("fallback"))) {
            ++fallbackCount;
        }

        std::vector<double> beatDiffs;
        if (beats.size() > 1) {
            beatDiffs.reserve(static_cast<std::size_t>(beats.size() - 1));
        }
        for (int i = 1; i < beats.size(); ++i) {
            const double diff = (beats[i] - beats[i - 1]) / signalInfo.getSampleRate().toDouble();
            if (diff > 0.0) {
                beatDiffs.push_back(diff);
            }
        }
        const double beatPeriod = median(beatDiffs);
        const double bpm = beatPeriod > 0.0 ? (60.0 / beatPeriod) : 0.0;

        const QString swingValue = extra.value(QStringLiteral("swing_pct"), QStringLiteral("(none)"));
        const bool isAmbiguous = extra.value(QStringLiteral("tempo_halfdouble_ambiguous")) == QStringLiteral("1");

        bool okRaw = false;
        const double bpmRaw = extra.value(QStringLiteral("bpm_raw")).toDouble(&okRaw);
        bool okSelected = false;
        const double bpmSelected = extra.value(QStringLiteral("bpm_selected")).toDouble(&okSelected);
        bool okMult = false;
        const double levelMultiplier = extra.value(QStringLiteral("tempo_level_multiplier")).toDouble(&okMult);
        bool okLevelConf = false;
        const double levelConfidence = extra.value(QStringLiteral("tempo_level_confidence")).toDouble(&okLevelConf);
        if (isAmbiguous) {
            ++ambiguousCount;
        }

        QString confidence;
        if (extra.contains(QStringLiteral("fallback"))) {
            confidence = QStringLiteral("low");
        } else if (isAmbiguous) {
            confidence = QStringLiteral("medium");
        } else if (extra.contains(QStringLiteral("swing_pct"))) {
            confidence = QStringLiteral("high");
        } else {
            confidence = QStringLiteral("medium");
        }

        const QString ambiguityText = isAmbiguous
                ? QStringLiteral("yes")
                : QStringLiteral("no");

        const QString fileName = QFileInfo(filePath).completeBaseName();
        QString artist;
        QString title = fileName;
        const QString separator = QStringLiteral(" - ");
        const int sepIndex = fileName.indexOf(separator);
        if (sepIndex > 0) {
            artist = fileName.left(sepIndex).trimmed();
            title = fileName.mid(sepIndex + separator.size()).trimmed();
        }

        rows.push_back(ResultRow{
                filePath,
                artist,
                title,
                bpm,
                okRaw ? bpmRaw : bpm,
                okSelected ? bpmSelected : bpm,
                okMult ? levelMultiplier : 1.0,
                okLevelConf ? levelConfidence : 0.0,
                swingValue,
                confidence,
                ambiguityText,
                timer.elapsed()});
    }

    ASSERT_GT(filesSeen, 0) << "No MP3 files found in selected roots.";
    ASSERT_GT(analyzed, 0) << "No tracks analyzed successfully.";

    std::sort(rows.begin(), rows.end(), [](const ResultRow& a, const ResultRow& b) {
        const int artistCmp = QString::compare(a.artist, b.artist, Qt::CaseInsensitive);
        if (artistCmp != 0) {
            return artistCmp < 0;
        }
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });

    const bool emitCsv = qEnvironmentVariableIntValue("MIXXX_LAROCHE_EMIT_CSV") != 0;
    if (emitCsv) {
        std::cout << "CSV|path\tartist\ttitle\tbpm\tbpm_raw\tbpm_selected\tlevel_multiplier\tlevel_confidence\tswing_pct\tconfidence\tambiguous\ttime_ms\n";
        for (const auto& row : rows) {
            std::cout << "CSV|"
                      << row.path.toStdString() << "\t"
                      << row.artist.toStdString() << "\t"
                      << row.title.toStdString() << "\t"
                      << std::fixed << std::setprecision(2) << row.bpm << "\t"
                      << std::fixed << std::setprecision(2) << row.bpmRaw << "\t"
                      << std::fixed << std::setprecision(2) << row.bpmSelected << "\t"
                      << std::fixed << std::setprecision(2) << row.levelMultiplier << "\t"
                      << std::fixed << std::setprecision(3) << row.levelConfidence << "\t"
                      << row.swing.toStdString() << "\t"
                      << row.confidence.toStdString() << "\t"
                      << row.ambiguity.toStdString() << "\t"
                      << row.elapsedMs
                      << "\n";
        }
    }

    std::cout << "\nMusic Analyzer Results\n";
    std::cout << "| Artist | Title | BPM | BPM Raw | BPM Selected | Level x | Level Conf | Swing % | Confidence | Half/Double Ambiguous | Time (ms) |\n";
    std::cout << "|---|---|---:|---:|---:|---:|---:|---:|---|---|---:|\n";
    for (const auto& row : rows) {
        QString artist = row.artist;
        QString title = row.title;
        if (artist.length() > kDisplayNameMax) {
            artist = artist.left(kDisplayNameMax - 1) + QStringLiteral("…");
        }
        if (title.length() > kDisplayNameMax) {
            title = title.left(kDisplayNameMax - 1) + QStringLiteral("…");
        }
        std::cout << "| " << artist.toStdString()
                  << " | " << title.toStdString()
                  << " | " << std::fixed << std::setprecision(2) << row.bpm
                  << " | " << std::fixed << std::setprecision(2) << row.bpmRaw
                  << " | " << std::fixed << std::setprecision(2) << row.bpmSelected
                  << " | " << std::fixed << std::setprecision(2) << row.levelMultiplier
                  << " | " << std::fixed << std::setprecision(2) << row.levelConfidence
                  << " | " << row.swing.toStdString()
                  << " | " << row.confidence.toStdString()
                  << " | " << row.ambiguity.toStdString()
                  << " | " << row.elapsedMs
                  << " |\n";
    }
    std::cout << "Summary: filesSeen=" << filesSeen
              << " analyzed=" << analyzed
              << " withSwing=" << withSwing
              << " fallback=" << fallbackCount
              << " ambiguous=" << ambiguousCount << "\n";

    EXPECT_GE(withSwing, 1) << "No swing estimates were produced on Balboa sample set.";
}

} // namespace
