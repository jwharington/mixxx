#pragma once

#include <QObject>
#include <memory>
#include <vector>

#include "analyzer/plugins/analyzerplugin.h"
#include "analyzer/plugins/buffering_utils.h"

class DetectionFunction;

namespace mixxx {

class AnalyzerLarocheSwingBeats : public AnalyzerBeatsPlugin {
  public:
    static AnalyzerPluginInfo pluginInfo() {
        return AnalyzerPluginInfo(
                "mixxxlarocheswingbeats:1",
                "Mixxx",
                QObject::tr("Laroche Tempo/Swing Beat Tracker (Experimental)"),
                true);
    }

    AnalyzerLarocheSwingBeats();
    ~AnalyzerLarocheSwingBeats() override;

    AnalyzerPluginInfo info() const override {
        return pluginInfo();
    }

    bool initialize(mixxx::audio::SampleRate sampleRate) override;
    bool processSamples(const CSAMPLE* pIn, SINT iLen) override;
    bool finalize() override;

    bool supportsBeatTracking() const override {
        return true;
    }

    QVector<mixxx::audio::FramePos> getBeats() const override {
        return m_resultBeats;
    }

    QHash<QString, QString> getExtraVersionInfo() const override;

  private:
    struct SearchResult {
        double bpm = 0.0;
        double swing = 0.0;
        double phaseSec = 0.0;
        double logLikelihood = -1.0e300;
    };

    SearchResult searchParameters(const std::vector<double>& transientTimesSec,
            double minBpm,
            double maxBpm,
            double bpmStep,
            double minSwing,
            double maxSwing,
            double swingStep,
            int phaseBins) const;

    double evaluateLogLikelihood(const std::vector<double>& transientTimesSec,
            double bpm,
            double swing,
            double phaseSec) const;

    std::vector<double> detectTransientTimesSeconds() const;

    std::unique_ptr<DetectionFunction> m_pDetectionFunction;
    DownmixAndOverlapHelper m_helper;
    mixxx::audio::SampleRate m_sampleRate;
    int m_windowSize;
    int m_stepSizeFrames;
    std::vector<double> m_detectionResults;
    QVector<mixxx::audio::FramePos> m_resultBeats;
    mixxx::audio::FrameDiff_t m_processedFrames;
    double m_resultSwingPercent;
    double m_resultSwingPercentStart;
    double m_resultSwingPercentEnd;
    bool m_usedFallback;
    bool m_isHalfDoubleAmbiguous;
    double m_bestModelBpm;
    double m_altModelBpm;
    double m_altLogLikelihoodPerTransient;
    double m_rawBeatBpm;
    double m_selectedBeatBpm;
    double m_selectedTempoMultiplier;
    double m_tempoLevelConfidence;
};

} // namespace mixxx
