#include <dsp/onsets/DetectionFunction.h>
#include <dsp/tempotracking/TempoTrackV2.h>

#include "analyzer/plugins/analyzerlarocheswingbeats.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>

#include "analyzer/constants.h"
#include "util/math.h"

namespace mixxx {
namespace {

constexpr float kStepSecs = 0.01161f;
constexpr int kMaximumBinSizeHz = 50;
constexpr double kTransientThresholdStddev = 0.5;
constexpr double kMinTransientSpacingSecs = 0.03;
constexpr double kEpsilon = 1e-12;
constexpr double kUnknownSwingPercent = -1.0;
constexpr std::size_t kMinTransientsForLaroche = 2;

constexpr double kTempoMinBpm = 70.0;
constexpr double kTempoMaxBpm = 140.0;
constexpr double kCoarseBpmStep = 0.5;
constexpr double kFineBpmStep = 0.1;
constexpr double kCoarseSwingStep = 0.1;
constexpr double kFineSwingStep = 0.02;
constexpr double kMinSwing = 0.0;
constexpr double kMaxSwing = 0.4;
constexpr int kCoarsePhaseBins = 32;
constexpr int kFinePhaseBins = 64;
constexpr double kCoarseWindowSeconds = 12.0;
constexpr double kAmbiguityLogLikelihoodMarginPerTransient = 0.12;
constexpr int kTempoLevelPhaseBins = 72;
constexpr double kTempoLevelHitTolerancePeriodRatio = 0.15;
constexpr double kTempoLevelSigmaPeriodRatio = 0.12;

struct TempoLevelDecision {
    double beatBpm = 0.0;
    double beatPhaseSec = 0.0;
    double multiplier = 1.0;
    double score = -1.0e300;
    double runnerUpScore = -1.0e300;
    bool ambiguous = false;
};

struct TempoLevelModelPrediction {
    double multiplier = 1.0;
    double confidence = 0.0;
    bool valid = false;
};

struct TempoLevelModel {
    bool valid = false;
    std::vector<double> classes;
    std::vector<std::vector<double>> weights;
    std::vector<double> bias;
    std::vector<double> mean;
    std::vector<double> stddev;

    static TempoLevelModel fromJsonFile(const QString& path) {
        TempoLevelModel model;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return model;
        }
        const auto doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
            return model;
        }
        const auto obj = doc.object();
        if (obj.value("model_type").toString() != "softmax_linear") {
            return model;
        }

        const auto readVector = [](const QJsonArray& arr) {
            std::vector<double> out;
            out.reserve(arr.size());
            for (const auto& v : arr) {
                out.push_back(v.toDouble());
            }
            return out;
        };

        model.classes = readVector(obj.value("classes").toArray());
        model.bias = readVector(obj.value("bias").toArray());
        model.mean = readVector(obj.value("feature_mean").toArray());
        model.stddev = readVector(obj.value("feature_std").toArray());

        const auto weightsArray = obj.value("weights").toArray();
        model.weights.reserve(weightsArray.size());
        for (const auto& row : weightsArray) {
            model.weights.push_back(readVector(row.toArray()));
        }

        if (model.classes.empty() ||
                model.weights.empty() ||
                model.bias.size() != model.classes.size() ||
                model.weights.size() != model.classes.size() ||
                model.mean.empty() ||
                model.stddev.size() != model.mean.size()) {
            return TempoLevelModel();
        }
        for (const auto& row : model.weights) {
            if (row.size() != model.mean.size()) {
                return TempoLevelModel();
            }
        }

        model.valid = true;
        return model;
    }

    TempoLevelModelPrediction predict(const std::vector<double>& features) const {
        TempoLevelModelPrediction prediction;
        if (!valid || features.size() != mean.size()) {
            return prediction;
        }

        std::vector<double> logits(classes.size(), 0.0);
        for (std::size_t c = 0; c < classes.size(); ++c) {
            logits[c] = bias[c];
            for (std::size_t i = 0; i < features.size(); ++i) {
                const double sigma = std::max(1e-9, stddev[i]);
                const double normX = (features[i] - mean[i]) / sigma;
                logits[c] += weights[c][i] * normX;
            }
        }

        const double maxLogit = *std::max_element(logits.begin(), logits.end());
        double denom = 0.0;
        for (double& logit : logits) {
            logit = std::exp(logit - maxLogit);
            denom += logit;
        }
        if (denom <= 0.0) {
            return prediction;
        }
        for (double& p : logits) {
            p /= denom;
        }

        const auto bestIt = std::max_element(logits.begin(), logits.end());
        const std::size_t idx = static_cast<std::size_t>(std::distance(logits.begin(), bestIt));
        prediction.multiplier = classes[idx];
        prediction.confidence = *bestIt;
        prediction.valid = true;
        return prediction;
    }
};

const TempoLevelModel& getTempoLevelModel() {
    static std::once_flag once;
    static TempoLevelModel model;
    std::call_once(once, [] {
        QStringList candidatePaths;

        const QString envPath = qEnvironmentVariable("MIXXX_LAROCHE_TEMPO_MODEL_JSON").trimmed();
        if (!envPath.isEmpty()) {
            candidatePaths.append(envPath);
        }

        // Default project/runtime locations when env var is not set.
        candidatePaths.append(
                QDir::cleanPath(QDir::currentPath() + "/res/models/tempo_level_model.json"));

        const QString appDir = QCoreApplication::applicationDirPath();
        if (!appDir.isEmpty()) {
            candidatePaths.append(QDir::cleanPath(appDir + "/../res/models/tempo_level_model.json"));
            candidatePaths.append(QDir::cleanPath(appDir + "/res/models/tempo_level_model.json"));
        }

        for (const QString& path : candidatePaths) {
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                continue;
            }
            model = TempoLevelModel::fromJsonFile(path);
            if (model.valid) {
                break;
            }
        }
    });
    return model;
}

std::vector<double> buildTempoModelFeatures(
        double rawBeatBpm,
        double swing,
        double levelConfidence,
        bool ambiguous) {
    return {
            rawBeatBpm,
            swing * 100.0,
            levelConfidence,
            ambiguous ? 1.0 : 0.0,
    };
}

TempoLevelDecision chooseTempoLevel(const std::vector<double>& transientTimesSec,
        double durationSec,
        double rawBeatBpm,
        double rawBeatPhaseSec) {
    const auto wrap = [](double value, double period) {
        if (period <= 0.0) {
            return 0.0;
        }
        const double wrapped = std::fmod(value, period);
        return wrapped >= 0.0 ? wrapped : wrapped + period;
    };
    const auto gauss = [](double distance, double sigma) {
        if (sigma <= 0.0) {
            return 0.0;
        }
        const double norm = distance / sigma;
        return std::exp(-0.5 * norm * norm);
    };

    TempoLevelDecision best;
    if (transientTimesSec.empty() || durationSec <= 0.0 || rawBeatBpm <= 0.0) {
        return best;
    }

    const std::array<double, 3> multipliers{0.5, 1.0, 2.0};
    for (double mult : multipliers) {
        const double beatBpm = rawBeatBpm * mult;
        if (beatBpm < 60.0 || beatBpm > 320.0) {
            continue;
        }

        const double period = 60.0 / beatBpm;
        const double sigma = std::max(0.008, kTempoLevelSigmaPeriodRatio * period);
        const double hitTolerance = kTempoLevelHitTolerancePeriodRatio * period;

        double candidateBestScore = -1.0e300;
        double candidateBestPhase = 0.0;
        for (int phaseIdx = 0; phaseIdx < kTempoLevelPhaseBins; ++phaseIdx) {
            const double phase = rawBeatPhaseSec + (period * phaseIdx) / kTempoLevelPhaseBins;

            int beatCount = 0;
            int hitCount = 0;
            double pulseScoreSum = 0.0;
            double evenPulseScoreSum = 0.0;
            double oddPulseScoreSum = 0.0;
            int evenCount = 0;
            int oddCount = 0;

            for (double t = wrap(phase, period); t <= durationSec; t += period) {
                const double nearestDistance = [&]() {
                    auto it = std::lower_bound(transientTimesSec.begin(), transientTimesSec.end(), t);
                    double d = std::numeric_limits<double>::max();
                    if (it != transientTimesSec.end()) {
                        d = std::min(d, std::fabs(*it - t));
                    }
                    if (it != transientTimesSec.begin()) {
                        --it;
                        d = std::min(d, std::fabs(*it - t));
                    }
                    return d;
                }();

                const double pulseScore = gauss(nearestDistance, sigma);
                pulseScoreSum += pulseScore;
                if (nearestDistance <= hitTolerance) {
                    ++hitCount;
                }
                if ((beatCount % 2) == 0) {
                    evenPulseScoreSum += pulseScore;
                    ++evenCount;
                } else {
                    oddPulseScoreSum += pulseScore;
                    ++oddCount;
                }
                ++beatCount;
            }

            if (beatCount == 0) {
                continue;
            }

            int transientCovered = 0;
            for (double t : transientTimesSec) {
                const double residue = wrap(t - phase, period);
                const double dist = std::min(residue, period - residue);
                if (dist <= hitTolerance) {
                    ++transientCovered;
                }
            }

            const double hitRate = static_cast<double>(hitCount) / beatCount;
            const double pulseMean = pulseScoreSum / beatCount;
            const double evenMean = evenCount > 0 ? (evenPulseScoreSum / evenCount) : pulseMean;
            const double oddMean = oddCount > 0 ? (oddPulseScoreSum / oddCount) : pulseMean;
            const double oddEvenContrast =
                    std::fabs(evenMean - oddMean) / (evenMean + oddMean + kEpsilon);
            const double coverage = static_cast<double>(transientCovered) /
                    static_cast<double>(transientTimesSec.size());

            const double transientsPerBeat =
                    static_cast<double>(transientTimesSec.size()) / beatCount;
            const double densityNorm = (transientsPerBeat - 1.6) / 0.9;
            const double densityScore = std::exp(-0.5 * densityNorm * densityNorm);

            double tempoBandPrior = 0.0;
            if (beatBpm >= 180.0 && beatBpm <= 260.0) {
                tempoBandPrior = 1.0;
            } else if (beatBpm >= 70.0 && beatBpm <= 140.0) {
                tempoBandPrior = 0.7;
            } else {
                tempoBandPrior = 0.2;
            }

            const double score =
                    0.40 * hitRate +
                    0.20 * pulseMean +
                    0.15 * coverage +
                    0.15 * densityScore +
                    0.10 * tempoBandPrior -
                    0.05 * oddEvenContrast;

            if (score > candidateBestScore) {
                candidateBestScore = score;
                candidateBestPhase = phase;
            }
        }

        if (candidateBestScore > best.score) {
            best.runnerUpScore = best.score;
            best.score = candidateBestScore;
            best.beatBpm = beatBpm;
            best.beatPhaseSec = candidateBestPhase;
            best.multiplier = mult;
        } else if (candidateBestScore > best.runnerUpScore) {
            best.runnerUpScore = candidateBestScore;
        }
    }

    if (best.score > -1.0e200 && best.runnerUpScore > -1.0e200) {
        best.ambiguous = (best.score - best.runnerUpScore) < 0.04;
    }
    return best;
}

DFConfig makeDetectionFunctionConfig(int stepSizeFrames, int windowSize) {
    DFConfig config;
    config.DFType = DF_COMPLEXSD;
    config.stepSize = stepSizeFrames;
    config.frameLength = windowSize;
    config.dbRise = 3;
    config.adaptiveWhitening = false;
    config.whiteningRelaxCoeff = -1;
    config.whiteningFloor = -1;
    return config;
}

double wrapToPeriod(double value, double period) {
    if (period <= 0.0) {
        return 0.0;
    }
    const double wrapped = std::fmod(value, period);
    return wrapped >= 0.0 ? wrapped : wrapped + period;
}

double circularDistance(double value, double center, double period) {
    double d = std::fabs(wrapToPeriod(value - center, period));
    if (d > period * 0.5) {
        d = period - d;
    }
    return d;
}

double gaussianScore(double distance, double sigma) {
    if (sigma <= 0.0) {
        return 0.0;
    }
    const double norm = distance / sigma;
    return std::exp(-0.5 * norm * norm);
}

} // namespace

AnalyzerLarocheSwingBeats::AnalyzerLarocheSwingBeats()
        : m_sampleRate(0),
          m_windowSize(0),
          m_stepSizeFrames(0),
          m_processedFrames(0),
          m_resultSwingPercent(kUnknownSwingPercent),
          m_resultSwingPercentStart(kUnknownSwingPercent),
          m_resultSwingPercentEnd(kUnknownSwingPercent),
          m_usedFallback(false),
          m_isHalfDoubleAmbiguous(false),
          m_bestModelBpm(0.0),
          m_altModelBpm(0.0),
          m_altLogLikelihoodPerTransient(-1.0e300),
          m_rawBeatBpm(0.0),
          m_selectedBeatBpm(0.0),
          m_selectedTempoMultiplier(1.0),
          m_tempoLevelConfidence(0.0) {
}

AnalyzerLarocheSwingBeats::~AnalyzerLarocheSwingBeats() {
}

bool AnalyzerLarocheSwingBeats::initialize(mixxx::audio::SampleRate sampleRate) {
    m_sampleRate = sampleRate;
    m_stepSizeFrames = static_cast<int>(m_sampleRate * kStepSecs);
    m_windowSize = MathUtilities::nextPowerOfTwo(m_sampleRate / kMaximumBinSizeHz);
    m_detectionResults.clear();
    m_resultBeats.clear();
    m_resultSwingPercent = kUnknownSwingPercent;
    m_resultSwingPercentStart = kUnknownSwingPercent;
    m_resultSwingPercentEnd = kUnknownSwingPercent;
    m_processedFrames = 0;
    m_usedFallback = false;
    m_isHalfDoubleAmbiguous = false;
    m_bestModelBpm = 0.0;
    m_altModelBpm = 0.0;
    m_altLogLikelihoodPerTransient = -1.0e300;
    m_rawBeatBpm = 0.0;
    m_selectedBeatBpm = 0.0;
    m_selectedTempoMultiplier = 1.0;
    m_tempoLevelConfidence = 0.0;

    m_pDetectionFunction = std::make_unique<DetectionFunction>(
            makeDetectionFunctionConfig(m_stepSizeFrames, m_windowSize));

    const bool ok = m_helper.initialize(
            m_windowSize, m_stepSizeFrames, [this](double* pWindow, size_t) {
                m_detectionResults.push_back(
                        m_pDetectionFunction->processTimeDomain(pWindow));
                return true;
            });

    return ok;
}

bool AnalyzerLarocheSwingBeats::processSamples(const CSAMPLE* pIn, SINT iLen) {
    DEBUG_ASSERT(iLen % kAnalysisChannels == 0);
    if (!m_pDetectionFunction) {
        return false;
    }

    const SINT numFrames = iLen / kAnalysisChannels;
    m_processedFrames += numFrames;
    return m_helper.processStereoSamples(pIn, iLen);
}

std::vector<double> AnalyzerLarocheSwingBeats::detectTransientTimesSeconds() const {
    std::vector<double> transientTimesSec;
    if (m_detectionResults.size() < 3 || m_sampleRate <= 0) {
        return transientTimesSec;
    }

    std::vector<double> novelty;
    novelty.reserve(m_detectionResults.size() - 1);
    for (size_t i = 1; i < m_detectionResults.size(); ++i) {
        novelty.push_back(std::max(0.0, m_detectionResults[i] - m_detectionResults[i - 1]));
    }

    double mean = 0.0;
    for (double v : novelty) {
        mean += v;
    }
    mean /= novelty.size();

    double variance = 0.0;
    for (double v : novelty) {
        const double d = v - mean;
        variance += d * d;
    }
    variance /= novelty.size();

    const double stddev = std::sqrt(variance);
    const double threshold = mean + kTransientThresholdStddev * stddev;

    const double minSpacingFrames =
            kMinTransientSpacingSecs * static_cast<double>(m_sampleRate) / m_stepSizeFrames;
    constexpr double kVeryNegative = -1.0e300;
    double lastAcceptedFrame = kVeryNegative;

    for (size_t i = 1; i + 1 < novelty.size(); ++i) {
        const double v = novelty[i];
        if (v < threshold) {
            continue;
        }
        if (v < novelty[i - 1] || v < novelty[i + 1]) {
            continue;
        }

        const double frameIndex = static_cast<double>(i + 1);
        if (frameIndex - lastAcceptedFrame < minSpacingFrames) {
            continue;
        }

        const double timeSec =
                (frameIndex * m_stepSizeFrames + m_stepSizeFrames * 0.5) /
                static_cast<double>(m_sampleRate);
        transientTimesSec.push_back(timeSec);
        lastAcceptedFrame = frameIndex;
    }

    return transientTimesSec;
}

double AnalyzerLarocheSwingBeats::evaluateLogLikelihood(
        const std::vector<double>& transientTimesSec,
        double bpm,
        double swing,
        double phaseSec) const {
    if (transientTimesSec.empty() || bpm <= 0.0) {
        return -1.0e300;
    }

    const double period = 60.0 / bpm;
    const double quarter = period * 0.25;
    const double sigma = std::max(0.005, 0.05 * quarter);

    const double b0 = 0.0;
    const double b1 = quarter * (1.0 + swing);
    const double b2 = 2.0 * quarter;
    const double b3 = quarter * (3.0 + swing);

    constexpr double p0 = 0.4;
    constexpr double p1 = 0.15;
    constexpr double p2 = 0.3;
    constexpr double p3 = 0.15;

    double logLikelihood = 0.0;
    for (double t : transientTimesSec) {
        const double phase = wrapToPeriod(t - phaseSec, period);
        const double score =
                p0 * gaussianScore(circularDistance(phase, b0, period), sigma) +
                p1 * gaussianScore(circularDistance(phase, b1, period), sigma) +
                p2 * gaussianScore(circularDistance(phase, b2, period), sigma) +
                p3 * gaussianScore(circularDistance(phase, b3, period), sigma);
        logLikelihood += std::log(score + kEpsilon);
    }

    return logLikelihood;
}

AnalyzerLarocheSwingBeats::SearchResult AnalyzerLarocheSwingBeats::searchParameters(
        const std::vector<double>& transientTimesSec,
        double minBpm,
        double maxBpm,
        double bpmStep,
        double minSwing,
        double maxSwing,
        double swingStep,
        int phaseBins) const {
    SearchResult best;

    for (double bpm = minBpm; bpm <= maxBpm + 0.5 * bpmStep; bpm += bpmStep) {
        const double period = 60.0 / bpm;
        for (double swing = minSwing; swing <= maxSwing + 0.5 * swingStep; swing += swingStep) {
            for (int phaseIndex = 0; phaseIndex < phaseBins; ++phaseIndex) {
                const double phase = (period * phaseIndex) / phaseBins;
                const double ll = evaluateLogLikelihood(transientTimesSec, bpm, swing, phase);
                if (ll > best.logLikelihood) {
                    best.bpm = bpm;
                    best.swing = swing;
                    best.phaseSec = phase;
                    best.logLikelihood = ll;
                }
            }
        }
    }

    return best;
}

bool AnalyzerLarocheSwingBeats::finalize() {
    m_helper.finalize();

    const auto finalizeWithQueenMaryFallback = [this]() {
        std::size_t nonZeroCount = m_detectionResults.size();
        while (nonZeroCount > 0 && m_detectionResults.at(nonZeroCount - 1) <= 0.0) {
            --nonZeroCount;
        }

        const std::size_t requiredSize =
                std::max(static_cast<std::size_t>(2), nonZeroCount) - 2;
        if (requiredSize < 2) {
            return false;
        }

        std::vector<double> df;
        df.reserve(requiredSize);
        auto beatPeriod = std::vector<int>(requiredSize / 128 + 1);
        for (std::size_t i = 2; i < nonZeroCount; ++i) {
            df.push_back(m_detectionResults.at(i));
        }

        TempoTrackV2 tt(m_sampleRate, m_stepSizeFrames);
        tt.calculateBeatPeriod(df, beatPeriod);

        std::vector<double> beats;
        tt.calculateBeats(df, beatPeriod, beats);

        m_resultBeats.clear();
        m_resultBeats.reserve(static_cast<int>(beats.size()));
        for (double beat : beats) {
            const auto frame = mixxx::audio::FramePos(
                    (beat * m_stepSizeFrames) + m_stepSizeFrames / 2);
            m_resultBeats.push_back(frame);
        }

        m_usedFallback = true;
        m_resultSwingPercent = kUnknownSwingPercent;
        m_resultSwingPercentStart = kUnknownSwingPercent;
        m_resultSwingPercentEnd = kUnknownSwingPercent;
        m_isHalfDoubleAmbiguous = false;
        m_bestModelBpm = 0.0;
        m_altModelBpm = 0.0;
        m_altLogLikelihoodPerTransient = -1.0e300;
        m_rawBeatBpm = 0.0;
        m_selectedBeatBpm = 0.0;
        m_selectedTempoMultiplier = 1.0;
        m_tempoLevelConfidence = 0.0;
        return m_resultBeats.size() >= 2;
    };

    const auto transientTimesSec = detectTransientTimesSeconds();
    if (transientTimesSec.size() < kMinTransientsForLaroche) {
        m_pDetectionFunction.reset();
        return finalizeWithQueenMaryFallback();
    }

    std::vector<double> transientTimesCoarse;
    const double coarseEndTime = std::min(
            kCoarseWindowSeconds,
            static_cast<double>(m_processedFrames) / m_sampleRate.value());
    transientTimesCoarse.reserve(transientTimesSec.size());
    for (double t : transientTimesSec) {
        if (t <= coarseEndTime) {
            transientTimesCoarse.push_back(t);
        }
    }
    if (transientTimesCoarse.size() < 6) {
        transientTimesCoarse = transientTimesSec;
    }

    const SearchResult coarse = searchParameters(
            transientTimesCoarse,
            kTempoMinBpm,
            kTempoMaxBpm,
            kCoarseBpmStep,
            kMinSwing,
            kMaxSwing,
            kCoarseSwingStep,
            kCoarsePhaseBins);

    const double fineMinBpm = std::max(kTempoMinBpm, coarse.bpm - 2.0);
    const double fineMaxBpm = std::min(kTempoMaxBpm, coarse.bpm + 2.0);
    const double fineMinSwing = std::max(kMinSwing, coarse.swing - 0.08);
    const double fineMaxSwing = std::min(kMaxSwing, coarse.swing + 0.08);

    const SearchResult fine = searchParameters(
            transientTimesSec,
            fineMinBpm,
            fineMaxBpm,
            kFineBpmStep,
            fineMinSwing,
            fineMaxSwing,
            kFineSwingStep,
            kFinePhaseBins);

    const double modelBpm = fine.bpm;
    const double swing = fine.swing;
    const double phaseSec = fine.phaseSec;
    if (modelBpm <= 0.0) {
        m_pDetectionFunction.reset();
        return finalizeWithQueenMaryFallback();
    }

    m_bestModelBpm = modelBpm;
    m_altModelBpm = 0.0;
    m_altLogLikelihoodPerTransient = -1.0e300;
    m_isHalfDoubleAmbiguous = false;

    const double bestLlPerTransient = fine.logLikelihood /
            static_cast<double>(std::max<std::size_t>(1, transientTimesSec.size()));

    const auto evaluateAlternativeTempo = [&](double altBpm) {
        if (altBpm < kTempoMinBpm || altBpm > kTempoMaxBpm) {
            return;
        }
        const auto alt = searchParameters(
                transientTimesSec,
                std::max(kTempoMinBpm, altBpm - 1.0),
                std::min(kTempoMaxBpm, altBpm + 1.0),
                kFineBpmStep,
                std::max(kMinSwing, swing - 0.08),
                std::min(kMaxSwing, swing + 0.08),
                kFineSwingStep,
                kFinePhaseBins);
        const double altLlPerTransient = alt.logLikelihood /
                static_cast<double>(std::max<std::size_t>(1, transientTimesSec.size()));
        if (altLlPerTransient > m_altLogLikelihoodPerTransient) {
            m_altLogLikelihoodPerTransient = altLlPerTransient;
            m_altModelBpm = alt.bpm;
        }
    };

    evaluateAlternativeTempo(modelBpm * 0.5);
    evaluateAlternativeTempo(modelBpm * 2.0);
    if (m_altModelBpm > 0.0) {
        const double llGap = bestLlPerTransient - m_altLogLikelihoodPerTransient;
        m_isHalfDoubleAmbiguous = (llGap >= 0.0) &&
                (llGap <= kAmbiguityLogLikelihoodMarginPerTransient);
    }

    // The Laroche template encodes two quarter-note beats per model period
    // (b0 and b2). Convert to beat period/BPM for Mixxx beatgrid output.
    const double modelPeriod = 60.0 / modelBpm;
    const double rawBeatBpm = 120.0 / modelPeriod;
    const double rawBeatPhaseSec = wrapToPeriod(phaseSec, modelPeriod * 0.5);

    const double durationSec = static_cast<double>(m_processedFrames) /
            static_cast<double>(m_sampleRate);

    const auto tempoLevel = chooseTempoLevel(
            transientTimesSec,
            durationSec,
            rawBeatBpm,
            rawBeatPhaseSec);

    m_rawBeatBpm = rawBeatBpm;
    m_selectedBeatBpm = tempoLevel.beatBpm > 0.0 ? tempoLevel.beatBpm : rawBeatBpm;
    double selectedBeatPhaseSec = tempoLevel.beatBpm > 0.0
            ? tempoLevel.beatPhaseSec
            : rawBeatPhaseSec;

    const double levelGap = tempoLevel.score - tempoLevel.runnerUpScore;
    const bool rawInFastBand = (m_rawBeatBpm >= 180.0 && m_rawBeatBpm <= 260.0);
    const bool halfInSlowBand =
            (m_rawBeatBpm * 0.5 >= 70.0 && m_rawBeatBpm * 0.5 <= 140.0);
    // Prefer keeping the raw beat level for typical fast balboa tempos unless
    // the half-time evidence is very strong.
    if (rawInFastBand &&
            halfInSlowBand &&
            m_selectedBeatBpm < m_rawBeatBpm &&
            levelGap < 0.18) {
        m_selectedBeatBpm = m_rawBeatBpm;
        selectedBeatPhaseSec = rawBeatPhaseSec;
    }

    m_selectedTempoMultiplier = m_selectedBeatBpm / std::max(1e-9, m_rawBeatBpm);
    m_tempoLevelConfidence = std::clamp(levelGap / 0.25, 0.0, 1.0);
    m_isHalfDoubleAmbiguous = m_isHalfDoubleAmbiguous || tempoLevel.ambiguous;

    const auto& tempoModel = getTempoLevelModel();
    if (tempoModel.valid) {
        const auto prediction = tempoModel.predict(buildTempoModelFeatures(
                m_rawBeatBpm,
                swing,
                m_tempoLevelConfidence,
                m_isHalfDoubleAmbiguous));
        if (prediction.valid && prediction.confidence >= 0.45) {
            const double modelSelectedBpm = m_rawBeatBpm * prediction.multiplier;
            if (modelSelectedBpm >= 60.0 && modelSelectedBpm <= 320.0) {
                m_selectedBeatBpm = modelSelectedBpm;
                m_selectedTempoMultiplier = prediction.multiplier;
                if (std::abs(prediction.multiplier - 1.0) < 0.01) {
                    selectedBeatPhaseSec = rawBeatPhaseSec;
                }
                m_tempoLevelConfidence = prediction.confidence;
            }
        }
    }

    const double beatPeriod = 60.0 / m_selectedBeatBpm;
    const double beatPhaseSec = wrapToPeriod(selectedBeatPhaseSec, beatPeriod);

    m_resultBeats.clear();
    for (double t = beatPhaseSec; t <= durationSec + beatPeriod; t += beatPeriod) {
        if (t < 0.0) {
            continue;
        }
        m_resultBeats.push_back(mixxx::audio::FramePos(
                std::round(t * static_cast<double>(m_sampleRate))));
    }

    m_resultSwingPercent = 100.0 * swing;

    // Compute start/end swing estimates for dynamic display while playing.
    m_resultSwingPercentStart = m_resultSwingPercent;
    m_resultSwingPercentEnd = m_resultSwingPercent;
    if (transientTimesSec.size() >= 2 * kMinTransientsForLaroche) {
        const double splitTime = 0.5 * durationSec;
        std::vector<double> transientStart;
        std::vector<double> transientEnd;
        transientStart.reserve(transientTimesSec.size());
        transientEnd.reserve(transientTimesSec.size());
        for (double t : transientTimesSec) {
            if (t < splitTime) {
                transientStart.push_back(t);
            } else {
                transientEnd.push_back(t);
            }
        }
        if (transientStart.size() >= kMinTransientsForLaroche) {
            const auto startEstimate = searchParameters(
                    transientStart,
                    std::max(kTempoMinBpm, modelBpm - 1.0),
                    std::min(kTempoMaxBpm, modelBpm + 1.0),
                    kFineBpmStep,
                    kMinSwing,
                    kMaxSwing,
                    kFineSwingStep,
                    kFinePhaseBins);
            if (startEstimate.logLikelihood > -1.0e200) {
                m_resultSwingPercentStart = 100.0 * startEstimate.swing;
            }
        }
        if (transientEnd.size() >= kMinTransientsForLaroche) {
            const auto endEstimate = searchParameters(
                    transientEnd,
                    std::max(kTempoMinBpm, modelBpm - 1.0),
                    std::min(kTempoMaxBpm, modelBpm + 1.0),
                    kFineBpmStep,
                    kMinSwing,
                    kMaxSwing,
                    kFineSwingStep,
                    kFinePhaseBins);
            if (endEstimate.logLikelihood > -1.0e200) {
                m_resultSwingPercentEnd = 100.0 * endEstimate.swing;
            }
        }
    }

    m_pDetectionFunction.reset();

    if (m_resultBeats.size() < 2) {
        return finalizeWithQueenMaryFallback();
    }
    return true;
}

QHash<QString, QString> AnalyzerLarocheSwingBeats::getExtraVersionInfo() const {
    QHash<QString, QString> result;
    if (m_resultSwingPercent >= 0.0) {
        result.insert("swing_pct", QString::number(m_resultSwingPercent, 'f', 2));
        result.insert("swing_start_pct", QString::number(m_resultSwingPercentStart, 'f', 2));
        result.insert("swing_end_pct", QString::number(m_resultSwingPercentEnd, 'f', 2));
    }
    if (m_bestModelBpm > 0.0) {
        result.insert("model_bpm", QString::number(m_bestModelBpm, 'f', 2));
    }
    if (m_rawBeatBpm > 0.0) {
        result.insert("bpm_raw", QString::number(m_rawBeatBpm, 'f', 2));
    }
    if (m_selectedBeatBpm > 0.0) {
        result.insert("bpm_selected", QString::number(m_selectedBeatBpm, 'f', 2));
        result.insert("tempo_level_multiplier",
                QString::number(m_selectedTempoMultiplier, 'f', 2));
        result.insert("tempo_level_confidence",
                QString::number(m_tempoLevelConfidence, 'f', 3));
    }
    if (m_altModelBpm > 0.0) {
        result.insert("model_alt_bpm", QString::number(m_altModelBpm, 'f', 2));
        result.insert("model_alt_ll_per_transient",
                QString::number(m_altLogLikelihoodPerTransient, 'f', 4));
    }
    result.insert("tempo_halfdouble_ambiguous", m_isHalfDoubleAmbiguous ? "1" : "0");
    if (m_usedFallback) {
        result.insert("fallback", "qmtempotrackv2");
    }
    return result;
}

} // namespace mixxx
