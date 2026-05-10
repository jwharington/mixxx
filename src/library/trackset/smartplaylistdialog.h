#pragma once

#include <QDialog>

#include "library/dao/playlistdao.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QTableWidget;
class QWidget;

class SmartPlaylistDialog final : public QDialog {
  public:
    SmartPlaylistDialog(PlaylistDAO& playlistDao,
            int playlistId,
            QWidget* parent = nullptr);

    int createdPlaylistId() const {
        return m_createdPlaylistId;
    }

  protected:
    void accept() override;

  private:
    void addRuleRow();
    void removeSelectedRuleRows();
    void setupUi();
    void loadPlaylist();
    void addRuleRow(const PlaylistDAO::SmartPlaylistRule* pRule);
    QList<PlaylistDAO::SmartPlaylistRule> readRules() const;
    bool save();

    PlaylistDAO& m_playlistDao;
    const int m_playlistId;
    int m_createdPlaylistId;

    QLineEdit* m_pNameEdit;
    QComboBox* m_pMatchModeCombo;
    QCheckBox* m_pAutoRefreshCheck;
    QTableWidget* m_pRulesTable;
    QDialogButtonBox* m_pButtonBox;
};
