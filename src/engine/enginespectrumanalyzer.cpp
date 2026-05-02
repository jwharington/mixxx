#include "engine/enginespectrumanalyzer.h"

#include <cmath>

#include "dsp/transforms/FFT.h"
#include "util/math.h"

namespace {

constexpr unsigned int kSpectrumUpdateRate = 30;
constexpr double kMinFrequencyHz = 40.0;
constexpr double kMaxFrequencyHz = 16000.0;
constexpr double kMinDb = -90.0;
// Raise upper display limit to reduce saturation.
constexpr double kMaxDb = 0.0;
constexpr double kPublishEpsilon = 0.001;

} // namespace

EngineSpectrumAnalyzer::EngineSpectrumAnalyzer(const QString& group,
        const QString& legacyGroup,
        bool createLegacyAliases)
        : m_lastPublishedBins{},
          m_ringBuffer{},
          m_window{},
          m_fftInput{},
          m_fftReal{},
          m_fftImag{},
          m_bandEdgesHz{},
          m_writeIndex(0),
          m_ringSamples(0),
          m_samplesSinceUpdate(0),
          m_sampleRate(QStringLiteral("[App]"), QStringLiteral("samplerate")),
          m_pFft(std::make_unique<FFTReal>(kFftSize)) {
    const double pi = std::acos(-1.0);
    for (int i = 0; i < kFftSize; ++i) {
        m_window[i] = 0.5 * (1.0 - std::cos((2.0 * pi * i) / (kFftSize - 1)));
    }

    const double logMin = std::log10(kMinFrequencyHz);
    const double logMax = std::log10(kMaxFrequencyHz);
    for (int i = 0; i <= kNumBins; ++i) {
        const double fraction = static_cast<double>(i) / kNumBins;
        m_bandEdgesHz[i] = std::pow(10.0, logMin + fraction * (logMax - logMin));
    }

    const QString aliasGroup = legacyGroup.isEmpty() ? group : legacyGroup;
    for (int i = 0; i < kNumBins; ++i) {
        const QString item = QStringLiteral("spectrum_bin_%1")
                                     .arg(i + 1, 2, 10, QLatin1Char('0'));
        m_binControls[i] = std::make_unique<ControlObject>(ConfigKey(group, item));
        if (createLegacyAliases) {
            const QString aliasItem = QStringLiteral("SpectrumBin%1")
                                              .arg(i + 1, 2, 10, QLatin1Char('0'));
            m_binControls[i]->addAlias(ConfigKey(aliasGroup, aliasItem));
        }
    }

    reset();
}

EngineSpectrumAnalyzer::~EngineSpectrumAnalyzer() = default;

void EngineSpectrumAnalyzer::process(const CSAMPLE* pIn, std::size_t bufferSize) {
    if (pIn == nullptr || bufferSize < 2) {
        return;
    }

    for (std::size_t i = 0; i + 1 < bufferSize; i += 2) {
        const double mono = static_cast<double>(0.5 * (pIn[i] + pIn[i + 1]));
        m_ringBuffer[m_writeIndex] = mono;
        m_writeIndex = (m_writeIndex + 1) % kFftSize;
        if (m_ringSamples < kFftSize) {
            ++m_ringSamples;
        }
        ++m_samplesSinceUpdate;
    }

    const double sampleRate = m_sampleRate.get();
    if (sampleRate <= 0.0) {
        return;
    }

    const unsigned int updateSamples =
            static_cast<unsigned int>(sampleRate / kSpectrumUpdateRate);
    if (updateSamples == 0 || m_samplesSinceUpdate < updateSamples) {
        return;
    }
    m_samplesSinceUpdate = 0;

    if (m_ringSamples < kFftSize) {
        return;
    }

    for (int i = 0; i < kFftSize; ++i) {
        const int ringIndex = (m_writeIndex + i) % kFftSize;
        m_fftInput[i] = m_ringBuffer[ringIndex] * m_window[i];
    }

    m_pFft->forward(m_fftInput.data(), m_fftReal.data(), m_fftImag.data());
    updateBands(sampleRate);
}

void EngineSpectrumAnalyzer::updateBands(double sampleRate) {
    const int nyquistBin = kFftSize / 2;
    const double nyquistFrequency = sampleRate / 2.0;
    const double fftMagnitudeScale = 2.0 / static_cast<double>(kFftSize);

    for (int band = 0; band < kNumBins; ++band) {
        const double lowHz = math_clamp(m_bandEdgesHz[band], kMinFrequencyHz, nyquistFrequency);
        const double highHz =
                math_clamp(m_bandEdgesHz[band + 1], lowHz + 1.0, nyquistFrequency);

        int lowBin = static_cast<int>(std::floor(lowHz * kFftSize / sampleRate));
        int highBin = static_cast<int>(std::ceil(highHz * kFftSize / sampleRate));

        lowBin = math_clamp(lowBin, 1, nyquistBin - 1);
        highBin = math_clamp(highBin, lowBin + 1, nyquistBin);

        double sumMagnitude = 0.0;
        int count = 0;
        for (int bin = lowBin; bin < highBin; ++bin) {
            const double real = m_fftReal[bin];
            const double imag = m_fftImag[bin];
            sumMagnitude += std::sqrt(real * real + imag * imag);
            ++count;
        }

        const double avgMagnitude =
                (count > 0) ? (sumMagnitude / count) * fftMagnitudeScale : 0.0;
        const double db = 20.0 * std::log10(avgMagnitude + 1e-12);
        const double normalized = math_clamp((db - kMinDb) / (kMaxDb - kMinDb), 0.0, 1.0);

        if (std::fabs(normalized - m_lastPublishedBins[band]) > kPublishEpsilon) {
            m_binControls[band]->set(normalized);
            m_lastPublishedBins[band] = normalized;
        }
    }
}

void EngineSpectrumAnalyzer::reset() {
    for (int i = 0; i < kNumBins; ++i) {
        m_binControls[i]->set(0.0);
        m_lastPublishedBins[i] = 0.0;
    }

    m_ringBuffer.fill(0.0);
    m_fftInput.fill(0.0);
    m_fftReal.fill(0.0);
    m_fftImag.fill(0.0);
    m_writeIndex = 0;
    m_ringSamples = 0;
    m_samplesSinceUpdate = 0;
}
