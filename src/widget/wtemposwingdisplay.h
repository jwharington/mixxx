#pragma once

#include "control/controlproxy.h"
#include "widget/wlabel.h"

class WTempoSwingDisplay : public WLabel {
    Q_OBJECT
  public:
    explicit WTempoSwingDisplay(const QString& group, QWidget* pParent = nullptr);

    void setup(const QDomNode& node, const SkinContext& context) override;

  private slots:
    void refreshDisplay();

  private:
    QString formatValueOrPlaceholder(double value, int decimals, const QString& suffix) const;

    ControlProxy m_engineBpm;
    ControlProxy m_fileBpm;
    ControlProxy m_visualBpm;
    ControlProxy m_visualBpmSelected;
    ControlProxy m_visualTempoLevelConfidence;
    ControlProxy m_visualSwing;
    ControlProxy m_trackLoaded;

    int m_bpmDecimals;
    int m_swingDecimals;
    bool m_compact;
    double m_minComputedBpmConfidence;
    QString m_noTrackText;
};
