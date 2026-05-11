#include "widget/wtemposwingdisplay.h"

#include "moc_wtemposwingdisplay.cpp"
#include "skin/legacy/skincontext.h"

namespace {
constexpr double kMissingValue = -1.0;
}

WTempoSwingDisplay::WTempoSwingDisplay(const QString& group, QWidget* pParent)
        : WLabel(pParent),
          m_engineBpm(group,
                  QStringLiteral("bpm"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_fileBpm(group,
                  QStringLiteral("file_bpm"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_visualBpm(group,
                  QStringLiteral("visual_bpm"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_visualBpmSelected(group,
                  QStringLiteral("visual_bpm_selected"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_visualTempoLevelConfidence(group,
                  QStringLiteral("visual_tempo_level_confidence"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_visualSwing(group,
                  QStringLiteral("visual_swing"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_trackLoaded(group,
                  QStringLiteral("track_loaded"),
                  this,
                  ControlFlag::AllowMissingOrInvalid),
          m_bpmDecimals(1),
          m_swingDecimals(0),
          m_compact(false),
          m_minComputedBpmConfidence(0.6),
          m_noTrackText(QStringLiteral("--.-\n--%")) {
    m_engineBpm.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_fileBpm.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_visualBpm.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_visualBpmSelected.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_visualTempoLevelConfidence.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_visualSwing.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    m_trackLoaded.connectValueChanged(this, &WTempoSwingDisplay::refreshDisplay);
    refreshDisplay();
}

void WTempoSwingDisplay::setup(const QDomNode& node, const SkinContext& context) {
    WLabel::setup(node, context);

    context.hasNodeSelectInt(node, "BpmDecimals", &m_bpmDecimals);
    context.hasNodeSelectInt(node, "SwingDecimals", &m_swingDecimals);

    m_compact = context.selectBool(node, "Compact", true);
    context.hasNodeSelectDouble(
            node, "MinComputedBpmConfidence", &m_minComputedBpmConfidence);

    QString noTrackText;
    if (context.hasNodeSelectString(node, "NoTrackText", &noTrackText)) {
        m_noTrackText = noTrackText;
    }

    refreshDisplay();
}

void WTempoSwingDisplay::refreshDisplay() {
    if (!m_trackLoaded.valid() || !m_trackLoaded.toBool()) {
        setText(m_noTrackText);
        return;
    }

    const double computedBpm =
            m_visualBpmSelected.valid() ? m_visualBpmSelected.get() : kMissingValue;
    const double computedConfidence = m_visualTempoLevelConfidence.valid()
            ? m_visualTempoLevelConfidence.get()
            : kMissingValue;
    const double fileBpm = m_fileBpm.valid() ? m_fileBpm.get() : kMissingValue;

    double bpm = kMissingValue;
    if (computedBpm >= 0.0) {
        const bool hasConfidence = computedConfidence >= 0.0;
        const bool lowConfidence =
                hasConfidence && computedConfidence < m_minComputedBpmConfidence;
        bpm = lowConfidence && fileBpm >= 0.0 ? fileBpm : computedBpm;
    } else if (fileBpm >= 0.0) {
        bpm = fileBpm;
    } else if (m_engineBpm.valid()) {
        bpm = m_engineBpm.get();
    } else if (m_visualBpm.valid()) {
        bpm = m_visualBpm.get();
    }

    const double swing = m_visualSwing.valid() ? m_visualSwing.get() : kMissingValue;

    if (m_compact) {
        const QString bpmText = formatValueOrPlaceholder(bpm, m_bpmDecimals, QString());
        const QString swingText = formatValueOrPlaceholder(swing, m_swingDecimals, QStringLiteral("%"));
        setText(QStringLiteral("%1|%2").arg(bpmText, swingText));
    } else {
        const QString bpmLine = formatValueOrPlaceholder(bpm, m_bpmDecimals, QString());
        const QString swingValue = formatValueOrPlaceholder(swing, m_swingDecimals, QStringLiteral("%"));
        setText(QStringLiteral("%1\n%2").arg(bpmLine, swingValue));
    }
}

QString WTempoSwingDisplay::formatValueOrPlaceholder(
        double value, int decimals, const QString& suffix) const {
    if (value < 0.0) {
        return QStringLiteral("--") + suffix;
    }
    return QString::number(value, 'f', decimals) + suffix;
}
