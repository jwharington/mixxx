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
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "util/assert.h"

namespace {
constexpr int kRuleColumnField = 0;
constexpr int kRuleColumnOperator = 1;
constexpr int kRuleColumnValue = 2;
constexpr int kRuleColumnCount = 3;

struct SmartPlaylistFieldOption {
    const char* field;
    const char* label;
    bool isTextField;
};

struct SmartPlaylistOperatorOption {
    const char* op;
    const char* label;
};

const QList<SmartPlaylistFieldOption> kSmartPlaylistFieldOptions = {
        {"artist", QT_TR_NOOP("Artist"), true},
        {"title", QT_TR_NOOP("Title"), true},
        {"album", QT_TR_NOOP("Album"), true},
        {"album_artist", QT_TR_NOOP("Album Artist"), true},
        {"year", QT_TR_NOOP("Year"), false},
        {"genre", QT_TR_NOOP("Genre"), true},
        {"composer", QT_TR_NOOP("Composer"), true},
        {"grouping", QT_TR_NOOP("Grouping"), true},
        {"tracknumber", QT_TR_NOOP("Track Number"), true},
        {"filetype", QT_TR_NOOP("File Type"), true},
        {"location", QT_TR_NOOP("Track Location"), true},
        {"directory", QT_TR_NOOP("Directory"), true},
        {"comment", QT_TR_NOOP("Comment"), true},
        {"duration", QT_TR_NOOP("Duration"), false},
        {"bpm", QT_TR_NOOP("BPM"), false},
        {"datetime_added", QT_TR_NOOP("Date Added"), false},
        {"timesplayed", QT_TR_NOOP("Times Played"), false},
        {"rating", QT_TR_NOOP("Rating"), false},
        {"key", QT_TR_NOOP("Key"), true},
        {"crate", QT_TR_NOOP("Crate"), true},
};

const QList<SmartPlaylistOperatorOption> kTextOperatorOptions = {
        {"contains", QT_TR_NOOP("contains")},
        {"not_contains", QT_TR_NOOP("does not contain")},
        {"equals", QT_TR_NOOP("is")},
        {"not_equals", QT_TR_NOOP("is not")},
        {"is_empty", QT_TR_NOOP("is empty")},
        {"is_not_empty", QT_TR_NOOP("is not empty")},
};

const QList<SmartPlaylistOperatorOption> kNumericOperatorOptions = {
        {"equals", QT_TR_NOOP("equals")},
        {"lt", QT_TR_NOOP("is less than")},
        {"lte", QT_TR_NOOP("is less than or equal to")},
        {"gt", QT_TR_NOOP("is greater than")},
        {"gte", QT_TR_NOOP("is greater than or equal to")},
        {"is_empty", QT_TR_NOOP("is empty")},
        {"is_not_empty", QT_TR_NOOP("is not empty")},
};

QString defaultSmartPlaylistOperatorForField(const QString& field) {
    for (const auto& option : kSmartPlaylistFieldOptions) {
        if (field == QLatin1StringView(option.field)) {
            return option.isTextField ? QStringLiteral("contains") : QStringLiteral("equals");
        }
    }
    return QStringLiteral("contains");
}

bool smartPlaylistFieldIsText(const QString& field) {
    for (const auto& option : kSmartPlaylistFieldOptions) {
        if (field == QLatin1StringView(option.field)) {
            return option.isTextField;
        }
    }
    return true;
}

const QList<SmartPlaylistOperatorOption>& smartPlaylistOperatorsForField(const QString& field) {
    return smartPlaylistFieldIsText(field) ? kTextOperatorOptions : kNumericOperatorOptions;
}

void populateSmartPlaylistOperators(QComboBox* pOperatorCombo,
        const QString& field,
        const QString& selectedOp = QString()) {
    const QSignalBlocker blocker(pOperatorCombo);
    pOperatorCombo->clear();
    const auto& operatorOptions = smartPlaylistOperatorsForField(field);
    for (const auto& option : operatorOptions) {
        pOperatorCombo->addItem(QObject::tr(option.label), QString::fromLatin1(option.op));
    }

    const QString desiredOp = selectedOp.isEmpty()
            ? defaultSmartPlaylistOperatorForField(field)
            : selectedOp;
    int selectedIndex = pOperatorCombo->findData(desiredOp);
    if (selectedIndex < 0) {
        selectedIndex = 0;
    }
    pOperatorCombo->setCurrentIndex(selectedIndex);
}

QString smartPlaylistFieldLabel(const QString& field) {
    for (const auto& option : kSmartPlaylistFieldOptions) {
        if (field == QLatin1StringView(option.field)) {
            return QObject::tr(option.label);
        }
    }
    return field;
}

QTableWidget* createSmartPlaylistRulesTable(QWidget* parent, const QString& objectName) {
    auto* pRulesTable = new QTableWidget(parent);
    pRulesTable->setObjectName(objectName);
    pRulesTable->setColumnCount(kRuleColumnCount);
    pRulesTable->setHorizontalHeaderLabels({QObject::tr("Field"),
            QObject::tr("Operator"),
            QObject::tr("Value")});
    pRulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pRulesTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pRulesTable->setEditTriggers(QAbstractItemView::AllEditTriggers);
    pRulesTable->horizontalHeader()->setStretchLastSection(false);
    pRulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pRulesTable->verticalHeader()->setVisible(false);
    pRulesTable->setAlternatingRowColors(true);
    return pRulesTable;
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
          m_pAutoRefreshCheck(nullptr),
          m_pMatchAllRulesTable(nullptr),
          m_pMatchAnyRulesTable(nullptr),
          m_pButtonBox(nullptr) {
    setupUi();
    loadPlaylist();
}

void SmartPlaylistDialog::setupUi() {
    setWindowTitle(m_playlistId == kInvalidPlaylistId
                    ? tr("Create Smart Playlist")
                    : tr("Edit Smart Playlist"));

    auto* pLayout = new QVBoxLayout(this);

    auto* pNameFormLayout = new QFormLayout;
    m_pNameEdit = new QLineEdit(this);
    m_pNameEdit->setObjectName(QStringLiteral("smartPlaylistName"));
    pNameFormLayout->addRow(tr("Name"), m_pNameEdit);
    pLayout->addLayout(pNameFormLayout);

    auto* pOptionsLayout = new QFormLayout;
    m_pAutoRefreshCheck = new QCheckBox(tr("Auto-refresh when tracks change"), this);
    pOptionsLayout->addRow(QString(), m_pAutoRefreshCheck);

    pLayout->addLayout(pOptionsLayout);

    auto* pMatchAllLabel = new QLabel(tr("Match all of these"), this);
    pLayout->addWidget(pMatchAllLabel);
    m_pMatchAllRulesTable = createSmartPlaylistRulesTable(this, QStringLiteral("smartPlaylistMatchAllRules"));
    pLayout->addWidget(m_pMatchAllRulesTable);

    auto* pMatchAllButtonLayout = new QHBoxLayout;
    auto* pAddMatchAllRuleButton = new QPushButton(tr("Add Rule"), this);
    auto* pRemoveMatchAllRuleButton = new QPushButton(tr("Remove Selected"), this);
    connect(pAddMatchAllRuleButton, &QPushButton::clicked, this, [this](bool) {
        addRuleRow(m_pMatchAllRulesTable, PlaylistDAO::SmartPlaylistRuleBlock::MatchAll);
    });
    connect(pRemoveMatchAllRuleButton, &QPushButton::clicked, this, [this](bool) {
        removeSelectedRuleRows(m_pMatchAllRulesTable);
    });
    pMatchAllButtonLayout->addWidget(pAddMatchAllRuleButton);
    pMatchAllButtonLayout->addWidget(pRemoveMatchAllRuleButton);
    pMatchAllButtonLayout->addStretch();
    pLayout->addLayout(pMatchAllButtonLayout);

    auto* pMatchAnyLabel = new QLabel(tr("Match any of these"), this);
    pLayout->addWidget(pMatchAnyLabel);
    m_pMatchAnyRulesTable = createSmartPlaylistRulesTable(this, QStringLiteral("smartPlaylistMatchAnyRules"));
    pLayout->addWidget(m_pMatchAnyRulesTable);

    auto* pMatchAnyButtonLayout = new QHBoxLayout;
    auto* pAddMatchAnyRuleButton = new QPushButton(tr("Add Rule"), this);
    auto* pRemoveMatchAnyRuleButton = new QPushButton(tr("Remove Selected"), this);
    connect(pAddMatchAnyRuleButton, &QPushButton::clicked, this, [this](bool) {
        addRuleRow(m_pMatchAnyRulesTable, PlaylistDAO::SmartPlaylistRuleBlock::MatchAny);
    });
    connect(pRemoveMatchAnyRuleButton, &QPushButton::clicked, this, [this](bool) {
        removeSelectedRuleRows(m_pMatchAnyRulesTable);
    });
    pMatchAnyButtonLayout->addWidget(pAddMatchAnyRuleButton);
    pMatchAnyButtonLayout->addWidget(pRemoveMatchAnyRuleButton);
    pMatchAnyButtonLayout->addStretch();
    pLayout->addLayout(pMatchAnyButtonLayout);

    m_pButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_pButtonBox, &QDialogButtonBox::accepted, this, &SmartPlaylistDialog::accept);
    connect(m_pButtonBox, &QDialogButtonBox::rejected, this, &SmartPlaylistDialog::reject);
    pLayout->addWidget(m_pButtonBox);

    resize(760, 480);
}

void SmartPlaylistDialog::loadPlaylist() {
    if (m_playlistId == kInvalidPlaylistId) {
        m_pNameEdit->setText(tr("New Smart Playlist"));
        m_pAutoRefreshCheck->setChecked(true);
        addRuleRow(m_pMatchAllRulesTable, PlaylistDAO::SmartPlaylistRuleBlock::MatchAll);
        return;
    }

    m_pNameEdit->setText(m_playlistDao.getPlaylistName(m_playlistId));

    PlaylistDAO::SmartPlaylistMatchMode matchMode = PlaylistDAO::SmartPlaylistMatchMode::MatchAll;
    bool autoRefresh = true;
    if (!m_playlistDao.readSmartPlaylistProperties(m_playlistId, &matchMode, &autoRefresh)) {
        qWarning() << "Failed to read smart playlist properties for" << m_playlistId;
        return;
    }

    m_pAutoRefreshCheck->setChecked(autoRefresh);

    const QList<PlaylistDAO::SmartPlaylistRule> rules = m_playlistDao.readSmartPlaylistRules(m_playlistId);
    bool hasMatchAnyRules = false;
    for (const auto& rule : rules) {
        if (rule.block == PlaylistDAO::SmartPlaylistRuleBlock::MatchAny) {
            hasMatchAnyRules = true;
            break;
        }
    }
    if (rules.isEmpty()) {
        addRuleRow(m_pMatchAllRulesTable, PlaylistDAO::SmartPlaylistRuleBlock::MatchAll);
    } else {
        for (const auto& rule : rules) {
            const auto block = (!hasMatchAnyRules &&
                                       matchMode == PlaylistDAO::SmartPlaylistMatchMode::MatchAny)
                    ? PlaylistDAO::SmartPlaylistRuleBlock::MatchAny
                    : rule.block;
            if (block == PlaylistDAO::SmartPlaylistRuleBlock::MatchAny) {
                addRuleRow(m_pMatchAnyRulesTable, block, &rule);
            } else {
                addRuleRow(m_pMatchAllRulesTable, block, &rule);
            }
        }
    }
}

void SmartPlaylistDialog::addRuleRow(QTableWidget* pRulesTable,
        PlaylistDAO::SmartPlaylistRuleBlock block,
        const PlaylistDAO::SmartPlaylistRule* pRule) {
    const int row = pRulesTable->rowCount();
    pRulesTable->insertRow(row);

    auto* pFieldCombo = new QComboBox(this);
    auto* pOperatorCombo = new QComboBox(this);
    auto* pValueEdit = new QLineEdit(this);

    pFieldCombo->setObjectName(QStringLiteral("smartPlaylistField"));
    pOperatorCombo->setObjectName(QStringLiteral("smartPlaylistOperator"));
    pValueEdit->setObjectName(QStringLiteral("smartPlaylistValue"));

    pFieldCombo->setEditable(false);
    for (const auto& option : kSmartPlaylistFieldOptions) {
        pFieldCombo->addItem(QObject::tr(option.label), QString::fromLatin1(option.field));
    }

    pValueEdit->setPlaceholderText(tr("house"));

    if (pRule != nullptr) {
        const int fieldIndex = pFieldCombo->findData(pRule->field);
        if (fieldIndex >= 0) {
            pFieldCombo->setCurrentIndex(fieldIndex);
        } else if (!pRule->field.trimmed().isEmpty()) {
            pFieldCombo->addItem(smartPlaylistFieldLabel(pRule->field), pRule->field);
            pFieldCombo->setCurrentIndex(pFieldCombo->count() - 1);
        }
        pValueEdit->setText(pRule->value);
    }

    populateSmartPlaylistOperators(
            pOperatorCombo, pFieldCombo->currentData().toString(), pRule ? pRule->op : QString());
    connect(pFieldCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            [pFieldCombo, pOperatorCombo](int) {
                populateSmartPlaylistOperators(
                        pOperatorCombo, pFieldCombo->currentData().toString());
            });

    pRulesTable->setCellWidget(row, kRuleColumnField, pFieldCombo);
    pRulesTable->setCellWidget(row, kRuleColumnOperator, pOperatorCombo);
    pRulesTable->setCellWidget(row, kRuleColumnValue, pValueEdit);
    Q_UNUSED(block);
}

void SmartPlaylistDialog::removeSelectedRuleRows(QTableWidget* pRulesTable) {
    const auto selectedRows = pRulesTable->selectionModel()->selectedRows();
    QList<int> rowsToRemove;
    rowsToRemove.reserve(selectedRows.size());
    for (const auto& index : selectedRows) {
        rowsToRemove.append(index.row());
    }
    std::sort(rowsToRemove.begin(), rowsToRemove.end(), std::greater<int>());
    for (const int row : rowsToRemove) {
        pRulesTable->removeRow(row);
    }
}

QList<PlaylistDAO::SmartPlaylistRule> SmartPlaylistDialog::readRules(
        QTableWidget* pRulesTable,
        PlaylistDAO::SmartPlaylistRuleBlock block,
        int* pNextPosition) const {
    QList<PlaylistDAO::SmartPlaylistRule> rules;
    for (int row = 0; row < pRulesTable->rowCount(); ++row) {
        auto* pFieldCombo = qobject_cast<QComboBox*>(pRulesTable->cellWidget(row, kRuleColumnField));
        auto* pOperatorCombo = qobject_cast<QComboBox*>(pRulesTable->cellWidget(row, kRuleColumnOperator));
        auto* pValueEdit = qobject_cast<QLineEdit*>(pRulesTable->cellWidget(row, kRuleColumnValue));
        if (pFieldCombo == nullptr || pOperatorCombo == nullptr || pValueEdit == nullptr) {
            continue;
        }

        PlaylistDAO::SmartPlaylistRule rule;
        rule.position = *pNextPosition;
        rule.field = pFieldCombo->currentData().toString();
        rule.op = pOperatorCombo->currentData().toString();
        rule.value = pValueEdit->text();
        rule.negate = false;
        rule.block = block;

        if (rule.field.trimmed().isEmpty() && rule.op.trimmed().isEmpty() &&
                rule.value.trimmed().isEmpty()) {
            continue;
        }
        rules.append(rule);
        *pNextPosition += 1;
    }
    return rules;
}

bool SmartPlaylistDialog::save() {
    const QString name = m_pNameEdit->text().trimmed();
    const bool autoRefresh = m_pAutoRefreshCheck->isChecked();
    int nextPosition = 1;
    QList<PlaylistDAO::SmartPlaylistRule> rules = readRules(
            m_pMatchAllRulesTable,
            PlaylistDAO::SmartPlaylistRuleBlock::MatchAll,
            &nextPosition);
    rules.append(readRules(
            m_pMatchAnyRulesTable,
            PlaylistDAO::SmartPlaylistRuleBlock::MatchAny,
            &nextPosition));

    if (name.isEmpty()) {
        QMessageBox::warning(this,
                m_playlistId == kInvalidPlaylistId ? tr("Create Smart Playlist")
                                                   : tr("Edit Smart Playlist"),
                tr("A smart playlist cannot have a blank name."));
        return false;
    }

    if (m_playlistId == kInvalidPlaylistId) {
        if (m_playlistDao.getPlaylistIdFromName(name) != kInvalidPlaylistId) {
            QMessageBox::warning(this,
                    tr("Create Smart Playlist"),
                    tr("A playlist by that name already exists."));
            return false;
        }

        const int createdPlaylistId =
                m_playlistDao.createSmartPlaylist(
                        name,
                        PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
                        autoRefresh);
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

    const QString oldName = m_playlistDao.getPlaylistName(m_playlistId);
    if (name != oldName) {
        if (m_playlistDao.getPlaylistIdFromName(name) != kInvalidPlaylistId) {
            QMessageBox::warning(this,
                    tr("Edit Smart Playlist"),
                    tr("A playlist by that name already exists."));
            return false;
        }
        m_playlistDao.renamePlaylist(m_playlistId, name);
    }

    if (!m_playlistDao.replaceSmartPlaylistRules(m_playlistId, rules)) {
        QMessageBox::warning(this,
                tr("Edit Smart Playlist"),
                tr("Failed to save smart playlist rules."));
        return false;
    }
    if (!m_playlistDao.updateSmartPlaylistProperties(
                m_playlistId,
                PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
                autoRefresh)) {
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
