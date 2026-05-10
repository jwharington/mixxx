#include "library/trackset/smartplaylistdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "util/assert.h"

namespace {
constexpr int kRuleColumnField = 0;
constexpr int kRuleColumnOperator = 1;
constexpr int kRuleColumnValue = 2;
constexpr int kRuleColumnSecondValue = 3;
constexpr int kRuleColumnNegate = 4;
constexpr int kRuleColumnCount = 5;

QString matchModeToLabel(PlaylistDAO::SmartPlaylistMatchMode matchMode) {
    switch (matchMode) {
    case PlaylistDAO::SmartPlaylistMatchMode::MatchAll:
        return QObject::tr("Match all rules");
    case PlaylistDAO::SmartPlaylistMatchMode::MatchAny:
        return QObject::tr("Match any rule");
    }
    return QObject::tr("Match all rules");
}

} // namespace

SmartPlaylistDialog::SmartPlaylistDialog(PlaylistDAO& playlistDao,
        int playlistId,
        QWidget* parent)
        : QDialog(parent),
          m_playlistDao(playlistDao),
          m_playlistId(playlistId),
          m_createdPlaylistId(kInvalidPlaylistId),
          m_pNameEdit(nullptr),
          m_pMatchModeCombo(nullptr),
          m_pAutoRefreshCheck(nullptr),
          m_pRulesTable(nullptr),
          m_pButtonBox(nullptr) {
    setupUi();
    loadPlaylist();
}

void SmartPlaylistDialog::setupUi() {
    setWindowTitle(m_playlistId == kInvalidPlaylistId
                    ? tr("Create Smart Playlist")
                    : tr("Edit Smart Playlist"));

    auto* pLayout = new QVBoxLayout(this);

    if (m_playlistId == kInvalidPlaylistId) {
        auto* pFormLayout = new QFormLayout;
        m_pNameEdit = new QLineEdit(this);
        pFormLayout->addRow(tr("Name"), m_pNameEdit);
        pLayout->addLayout(pFormLayout);
    }

    auto* pOptionsLayout = new QFormLayout;
    m_pMatchModeCombo = new QComboBox(this);
    m_pMatchModeCombo->addItem(matchModeToLabel(PlaylistDAO::SmartPlaylistMatchMode::MatchAll),
            QVariant::fromValue(static_cast<int>(PlaylistDAO::SmartPlaylistMatchMode::MatchAll)));
    m_pMatchModeCombo->addItem(matchModeToLabel(PlaylistDAO::SmartPlaylistMatchMode::MatchAny),
            QVariant::fromValue(static_cast<int>(PlaylistDAO::SmartPlaylistMatchMode::MatchAny)));
    pOptionsLayout->addRow(tr("Rule match mode"), m_pMatchModeCombo);

    m_pAutoRefreshCheck = new QCheckBox(tr("Auto-refresh when tracks change"), this);
    pOptionsLayout->addRow(QString(), m_pAutoRefreshCheck);

    pLayout->addLayout(pOptionsLayout);

    auto* pHelpLabel = new QLabel(this);
    pHelpLabel->setWordWrap(true);
    pHelpLabel->setText(tr("Enter one rule per row. Use Mixxx search field names such as title, artist, genre, bpm, rating, or crate."));
    pLayout->addWidget(pHelpLabel);

    m_pRulesTable = new QTableWidget(this);
    m_pRulesTable->setColumnCount(kRuleColumnCount);
    m_pRulesTable->setHorizontalHeaderLabels({tr("Field"),
            tr("Operator"),
            tr("Value"),
            tr("Second value"),
            tr("Negate")});
    m_pRulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pRulesTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pRulesTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_pRulesTable->horizontalHeader()->setStretchLastSection(false);
    m_pRulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_pRulesTable->horizontalHeader()->setSectionResizeMode(kRuleColumnNegate,
            QHeaderView::ResizeToContents);
    m_pRulesTable->verticalHeader()->setVisible(false);
    m_pRulesTable->setAlternatingRowColors(true);
    pLayout->addWidget(m_pRulesTable);

    auto* pRuleButtonLayout = new QHBoxLayout;
    auto* pAddRuleButton = new QPushButton(tr("Add Rule"), this);
    auto* pRemoveRuleButton = new QPushButton(tr("Remove Selected"), this);
    connect(pAddRuleButton, &QPushButton::clicked, this, [this](bool) {
        addRuleRow();
    });
    connect(pRemoveRuleButton, &QPushButton::clicked, this, [this](bool) {
        removeSelectedRuleRows();
    });
    pRuleButtonLayout->addWidget(pAddRuleButton);
    pRuleButtonLayout->addWidget(pRemoveRuleButton);
    pRuleButtonLayout->addStretch();
    pLayout->addLayout(pRuleButtonLayout);

    m_pButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_pButtonBox, &QDialogButtonBox::accepted, this, &SmartPlaylistDialog::accept);
    connect(m_pButtonBox, &QDialogButtonBox::rejected, this, &SmartPlaylistDialog::reject);
    pLayout->addWidget(m_pButtonBox);

    resize(760, 480);
}

void SmartPlaylistDialog::loadPlaylist() {
    if (m_playlistId == kInvalidPlaylistId) {
        m_pMatchModeCombo->setCurrentIndex(0);
        m_pAutoRefreshCheck->setChecked(true);
        addRuleRow(nullptr);
        return;
    }

    PlaylistDAO::SmartPlaylistMatchMode matchMode = PlaylistDAO::SmartPlaylistMatchMode::MatchAll;
    bool autoRefresh = true;
    if (!m_playlistDao.readSmartPlaylistProperties(m_playlistId, &matchMode, &autoRefresh)) {
        qWarning() << "Failed to read smart playlist properties for" << m_playlistId;
        return;
    }

    m_pMatchModeCombo->setCurrentIndex(
            matchMode == PlaylistDAO::SmartPlaylistMatchMode::MatchAny ? 1 : 0);
    m_pAutoRefreshCheck->setChecked(autoRefresh);

    const QList<PlaylistDAO::SmartPlaylistRule> rules = m_playlistDao.readSmartPlaylistRules(m_playlistId);
    if (rules.isEmpty()) {
        addRuleRow(nullptr);
    } else {
        for (const auto& rule : rules) {
            addRuleRow(&rule);
        }
    }
}

void SmartPlaylistDialog::addRuleRow() {
    addRuleRow(nullptr);
}

void SmartPlaylistDialog::addRuleRow(const PlaylistDAO::SmartPlaylistRule* pRule) {
    const int row = m_pRulesTable->rowCount();
    m_pRulesTable->insertRow(row);

    auto* pFieldEdit = new QLineEdit(this);
    auto* pOperatorEdit = new QLineEdit(this);
    auto* pValueEdit = new QLineEdit(this);
    auto* pSecondValueEdit = new QLineEdit(this);
    auto* pNegateCheck = new QCheckBox(this);

    pFieldEdit->setPlaceholderText(tr("genre"));
    pOperatorEdit->setPlaceholderText(tr("contains"));
    pValueEdit->setPlaceholderText(tr("house"));
    pSecondValueEdit->setPlaceholderText(tr("128"));

    if (pRule != nullptr) {
        pFieldEdit->setText(pRule->field);
        pOperatorEdit->setText(pRule->op);
        pValueEdit->setText(pRule->value);
        pSecondValueEdit->setText(pRule->secondValue);
        pNegateCheck->setChecked(pRule->negate);
    }

    m_pRulesTable->setCellWidget(row, kRuleColumnField, pFieldEdit);
    m_pRulesTable->setCellWidget(row, kRuleColumnOperator, pOperatorEdit);
    m_pRulesTable->setCellWidget(row, kRuleColumnValue, pValueEdit);
    m_pRulesTable->setCellWidget(row, kRuleColumnSecondValue, pSecondValueEdit);
    m_pRulesTable->setCellWidget(row, kRuleColumnNegate, pNegateCheck);
}

void SmartPlaylistDialog::removeSelectedRuleRows() {
    const auto selectedRows = m_pRulesTable->selectionModel()->selectedRows();
    QList<int> rowsToRemove;
    rowsToRemove.reserve(selectedRows.size());
    for (const auto& index : selectedRows) {
        rowsToRemove.append(index.row());
    }
    std::sort(rowsToRemove.begin(), rowsToRemove.end(), std::greater<int>());
    for (const int row : rowsToRemove) {
        m_pRulesTable->removeRow(row);
    }
    if (m_pRulesTable->rowCount() == 0) {
        addRuleRow(nullptr);
    }
}

QList<PlaylistDAO::SmartPlaylistRule> SmartPlaylistDialog::readRules() const {
    QList<PlaylistDAO::SmartPlaylistRule> rules;
    for (int row = 0; row < m_pRulesTable->rowCount(); ++row) {
        auto* pFieldEdit = qobject_cast<QLineEdit*>(m_pRulesTable->cellWidget(row, kRuleColumnField));
        auto* pOperatorEdit = qobject_cast<QLineEdit*>(m_pRulesTable->cellWidget(row, kRuleColumnOperator));
        auto* pValueEdit = qobject_cast<QLineEdit*>(m_pRulesTable->cellWidget(row, kRuleColumnValue));
        auto* pSecondValueEdit = qobject_cast<QLineEdit*>(m_pRulesTable->cellWidget(row, kRuleColumnSecondValue));
        auto* pNegateCheck = qobject_cast<QCheckBox*>(m_pRulesTable->cellWidget(row, kRuleColumnNegate));
        if (pFieldEdit == nullptr || pOperatorEdit == nullptr || pValueEdit == nullptr ||
                pSecondValueEdit == nullptr || pNegateCheck == nullptr) {
            continue;
        }

        PlaylistDAO::SmartPlaylistRule rule;
        rule.position = row + 1;
        rule.field = pFieldEdit->text();
        rule.op = pOperatorEdit->text();
        rule.value = pValueEdit->text();
        rule.secondValue = pSecondValueEdit->text();
        rule.negate = pNegateCheck->isChecked();

        if (rule.field.trimmed().isEmpty() && rule.op.trimmed().isEmpty() &&
                rule.value.trimmed().isEmpty() && rule.secondValue.trimmed().isEmpty() &&
                !rule.negate) {
            continue;
        }
        rules.append(rule);
    }
    return rules;
}

bool SmartPlaylistDialog::save() {
    const PlaylistDAO::SmartPlaylistMatchMode matchMode =
            static_cast<PlaylistDAO::SmartPlaylistMatchMode>(
                    m_pMatchModeCombo->currentData().toInt());
    const bool autoRefresh = m_pAutoRefreshCheck->isChecked();
    const QList<PlaylistDAO::SmartPlaylistRule> rules = readRules();

    if (m_playlistId == kInvalidPlaylistId) {
        const QString name = m_pNameEdit->text().trimmed();
        if (name.isEmpty()) {
            QMessageBox::warning(this,
                    tr("Create Smart Playlist"),
                    tr("A smart playlist cannot have a blank name."));
            return false;
        }
        if (m_playlistDao.getPlaylistIdFromName(name) != kInvalidPlaylistId) {
            QMessageBox::warning(this,
                    tr("Create Smart Playlist"),
                    tr("A playlist by that name already exists."));
            return false;
        }

        const int createdPlaylistId =
                m_playlistDao.createSmartPlaylist(name, matchMode, autoRefresh);
        if (createdPlaylistId == kInvalidPlaylistId) {
            QMessageBox::warning(this,
                    tr("Create Smart Playlist"),
                    tr("An unknown error occurred while creating playlist: ") + name);
            return false;
        }
        if (!m_playlistDao.replaceSmartPlaylistRules(createdPlaylistId, rules)) {
            m_playlistDao.deletePlaylist(createdPlaylistId);
            QMessageBox::warning(this,
                    tr("Create Smart Playlist"),
                    tr("Failed to save smart playlist rules."));
            return false;
        }
        m_createdPlaylistId = createdPlaylistId;
        return true;
    }

    if (!m_playlistDao.replaceSmartPlaylistRules(m_playlistId, rules)) {
        QMessageBox::warning(this,
                tr("Edit Smart Playlist"),
                tr("Failed to save smart playlist rules."));
        return false;
    }
    if (!m_playlistDao.updateSmartPlaylistProperties(m_playlistId, matchMode, autoRefresh)) {
        QMessageBox::warning(this,
                tr("Edit Smart Playlist"),
                tr("Failed to save smart playlist properties."));
        return false;
    }
    return true;
}

void SmartPlaylistDialog::accept() {
    if (save()) {
        QDialog::accept();
    }
}
