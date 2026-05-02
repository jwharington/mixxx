#pragma once

#include <array>
#include <memory>

#include "control/pollingcontrolproxy.h"
#include "engine/enginespectrumanalyzer.h"
#include "widget/wwidget.h"

class QDomNode;
class SkinContext;

class WSpectrum : public WWidget {
    Q_OBJECT
  public:
    explicit WSpectrum(QWidget* pParent = nullptr);

    void setup(const QString& group, const QDomNode& node, const SkinContext& context);

  public slots:
    void maybeUpdate();

  protected:
    void paintEvent(QPaintEvent* /*unused*/) override;

  private:
    QString m_group;
    int m_numBins;
    int m_barGap;
    double m_decayPerTick;

    std::array<std::unique_ptr<PollingControlProxy>, EngineSpectrumAnalyzer::kNumBins>
            m_binControls;
    std::array<double, EngineSpectrumAnalyzer::kNumBins> m_binValues;
    std::array<double, EngineSpectrumAnalyzer::kNumBins> m_lastPaintedValues;
};
