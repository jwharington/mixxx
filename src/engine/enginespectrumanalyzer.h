#pragma once

#include <array>
#include <cstddef>
#include <memory>

#include "control/controlobject.h"
#include "control/pollingcontrolproxy.h"
#include "util/types.h"

class FFTReal;

class EngineSpectrumAnalyzer {
  public:
    static constexpr int kNumBins = 32;

    EngineSpectrumAnalyzer(const QString& group,
            const QString& legacyGroup = QString(),
            bool createLegacyAliases = false);
    ~EngineSpectrumAnalyzer();

    void process(const CSAMPLE* pIn, std::size_t bufferSize);
    void reset();

  private:
    static constexpr int kFftSize = 1024;

    void updateBands(double sampleRate);

    std::array<std::unique_ptr<ControlObject>, kNumBins> m_binControls;
    std::array<double, kNumBins> m_lastPublishedBins;

    std::array<double, kFftSize> m_ringBuffer;
    std::array<double, kFftSize> m_window;
    std::array<double, kFftSize> m_fftInput;
    std::array<double, kFftSize> m_fftReal;
    std::array<double, kFftSize> m_fftImag;
    std::array<double, kNumBins + 1> m_bandEdgesHz;

    int m_writeIndex;
    int m_ringSamples;
    unsigned int m_samplesSinceUpdate;

    PollingControlProxy m_sampleRate;

    std::unique_ptr<FFTReal> m_pFft;
};
