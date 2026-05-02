#include "widget/wspectrum.h"

#include <cmath>

#include <QPainter>
#include <QStyleOption>

#include "moc_wspectrum.cpp"
#include "skin/legacy/skincontext.h"
#include "util/math.h"

namespace {
constexpr int kDefaultBarGap = 1;
constexpr double kDefaultDecayPerTick = 0.04;
constexpr double kDefaultFilteredLineAlpha = 0.35;
constexpr double kUpdateEpsilon = 0.001;
} // namespace

WSpectrum::WSpectrum(QWidget* pParent)
        : WWidget(pParent),
          m_numBins(EngineSpectrumAnalyzer::kNumBins),
          m_barGap(kDefaultBarGap),
          m_decayPerTick(kDefaultDecayPerTick),
          m_showFilteredLine(true),
          m_filteredLineAlpha(kDefaultFilteredLineAlpha),
          m_binValues{},
          m_lineFilteredValues{},
          m_lastPaintedValues{} {
}

void WSpectrum::setup(const QString& group, const QDomNode& node, const SkinContext& context) {
    m_group = group;

    m_numBins = context.selectInt(node, "NumberOfBins");
    if (m_numBins <= 0 || m_numBins > EngineSpectrumAnalyzer::kNumBins) {
        m_numBins = EngineSpectrumAnalyzer::kNumBins;
    }

    m_barGap = context.selectInt(node, "BarGap");
    if (m_barGap < 0 || m_barGap > 20) {
        m_barGap = kDefaultBarGap;
    }

    const int decayPercent = context.selectInt(node, "DecayPerTick");
    if (decayPercent > 0 && decayPercent <= 100) {
        m_decayPerTick = decayPercent / 100.0;
    }

    m_showFilteredLine = context.selectBool(node, "ShowFilteredLine", true);
    const int filteredLineAlphaPercent = context.selectInt(node, "FilteredLineLowPass");
    if (filteredLineAlphaPercent > 0 && filteredLineAlphaPercent < 100) {
        m_filteredLineAlpha = filteredLineAlphaPercent / 100.0;
    }

    for (int i = 0; i < m_numBins; ++i) {
        const QString item = QStringLiteral("spectrum_bin_%1")
                                     .arg(i + 1, 2, 10, QLatin1Char('0'));
        m_binControls[i] = std::make_unique<PollingControlProxy>(
                ConfigKey(group, item),
                ControlFlag::AllowMissingOrInvalid);
    }

    setFocusPolicy(Qt::NoFocus);
}

void WSpectrum::maybeUpdate() {
    bool changed = false;
    for (int i = 0; i < m_numBins; ++i) {
        const double target = math_clamp(m_binControls[i]->get(), 0.0, 1.0);
        double value = m_binValues[i];
        if (target >= value) {
            value = target;
        } else {
            value = math_max(0.0, value - m_decayPerTick);
        }

        if (std::fabs(value - m_binValues[i]) > kUpdateEpsilon) {
            m_binValues[i] = value;
            changed = true;
        }
    }

    if (m_showFilteredLine && m_numBins > 0) {
        std::array<double, EngineSpectrumAnalyzer::kNumBins> forward{};
        forward[0] = m_binValues[0];
        for (int i = 1; i < m_numBins; ++i) {
            forward[i] = m_filteredLineAlpha * m_binValues[i] +
                    (1.0 - m_filteredLineAlpha) * forward[i - 1];
        }

        m_lineFilteredValues[m_numBins - 1] = forward[m_numBins - 1];
        for (int i = m_numBins - 2; i >= 0; --i) {
            m_lineFilteredValues[i] = m_filteredLineAlpha * forward[i] +
                    (1.0 - m_filteredLineAlpha) * m_lineFilteredValues[i + 1];
        }
    }

    if (changed) {
        repaint();
    }
}

void WSpectrum::paintEvent(QPaintEvent* /*unused*/) {
    QStyleOption option;
    option.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &p, this);

    const QRect content = rect().adjusted(1, 1, -1, -1);
    if (content.width() <= 0 || content.height() <= 0 || m_numBins <= 0) {
        return;
    }

    const int totalGap = (m_numBins - 1) * m_barGap;
    const int barWidth = math_max(1, (content.width() - totalGap) / m_numBins);
    const QColor barColor = palette().highlight().color();

    int x = content.left();
    QVector<QPointF> filteredPoints;
    filteredPoints.reserve(m_numBins);
    for (int i = 0; i < m_numBins; ++i) {
        const double value = math_clamp(m_binValues[i], 0.0, 1.0);
        const int height = math_clamp(
                static_cast<int>(std::round(value * content.height())),
                0,
                content.height());

        QRect barRect(x,
                content.bottom() - height + 1,
                barWidth,
                height);
        p.fillRect(barRect, barColor);

        if (m_showFilteredLine) {
            const double filteredValue = math_clamp(m_lineFilteredValues[i], 0.0, 1.0);
            const int filteredHeight = math_clamp(
                    static_cast<int>(std::round(filteredValue * content.height())),
                    0,
                    content.height());
            filteredPoints.append(QPointF(
                    x + barWidth / 2.0,
                    content.bottom() - filteredHeight + 1));
        }

        m_lastPaintedValues[i] = value;
        x += barWidth + m_barGap;
    }

    if (m_showFilteredLine && filteredPoints.size() > 1) {
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor lineColor = palette().highlightedText().color();
        lineColor.setAlpha(220);
        QPen linePen(lineColor);
        linePen.setWidthF(1.5);
        p.setPen(linePen);
        p.drawPolyline(filteredPoints.constData(), filteredPoints.size());
    }
}
