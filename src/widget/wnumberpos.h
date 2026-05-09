#pragma once


#include "wnumber.h"
#include "preferences/dialog/dlgprefdeck.h"

class ControlProxy;

class WNumberPos : public WNumber {
    Q_OBJECT
    // Custom property to style elapsed/remaining track time by deck play state.
    // Usage in css: WNumberPos[playing="true"/"false"] { /* styles */ }
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingStateChanged)

  public:
    explicit WNumberPos(const QString& group, QWidget* parent = nullptr);

    bool isPlaying() const {
        return m_bPlaying;
    }

  signals:
    void playingStateChanged(bool state);

  protected:
    void mousePressEvent(QMouseEvent* pEvent) override;

  private slots:
    void setValue(double dValue) override;
    void slotSetTimeElapsed(double);
    void slotTimeRemainingUpdated(double);
    void slotSetDisplayMode(double);
    void slotSetTimeFormat(double);
    void slotPlayStateChanged(double playState);
    void slotTrackLoadedChanged(double loaded);

  private:

    TrackTime::DisplayMode m_displayMode;
    TrackTime::DisplayFormat m_displayFormat;

    double m_dOldTimeElapsed;
    bool m_bPlaying;
    ControlProxy* m_pTimeElapsed;
    ControlProxy* m_pTimeRemaining;
    ControlProxy* m_pShowTrackTimeRemaining;
    ControlProxy* m_pTimeFormat;
    ControlProxy* m_pPlayIndicator;
    ControlProxy* m_pPlayPosition;
    ControlProxy* m_pTrackLoaded;
};
