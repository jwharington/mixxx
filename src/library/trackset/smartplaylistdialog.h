#pragma once

#include <QDialog>

#include "library/dao/playlistdao.h"

class QCheckBox;
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
    void setupUi();
    void loadPlaylist();
    void addRuleRow(QTableWidget* pRulesTable,
            PlaylistDAO::SmartPlaylistRuleBlock block,
            const PlaylistDAO::SmartPlaylistRule* pRule = nullptr);
    void removeSelectedRuleRows(QTableWidget* pRulesTable);
    QList<PlaylistDAO::SmartPlaylistRule> readRules(
            QTableWidget* pRulesTable,
            PlaylistDAO::SmartPlaylistRuleBlock block,
            int* pNextPosition) const;
    bool save();

    PlaylistDAO& m_playlistDao;
    const int m_playlistId;
    int m_createdPlaylistId;

    QLineEdit* m_pNameEdit;
    QCheckBox* m_pAutoRefreshCheck;
    QTableWidget* m_pMatchAllRulesTable;
    QTableWidget* m_pMatchAnyRulesTable;
    QDialogButtonBox* m_pButtonBox;
};
