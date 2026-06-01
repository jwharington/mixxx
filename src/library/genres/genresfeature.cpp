#include "library/genres/genresfeature.h"

#include <QSqlQuery>

#include "library/dao/trackschema.h"
#include "library/library.h"
#include "library/librarytablemodel.h"
#include "library/queryutil.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "library/treeitemmodel.h"
#include "util/assert.h"
#include "moc_genresfeature.cpp"

GenresFeature::GenresFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("tracks")),
          m_pSidebarModel(new TreeItemModel(this)) {
    connect(m_pLibrary->trackCollectionManager(),
            &TrackCollectionManager::libraryScanFinished,
            this,
            &GenresFeature::rebuildSidebarModel);

    rebuildSidebarModel();
}

QVariant GenresFeature::title() {
    return QVariant(tr("Genres"));
}

TreeItemModel* GenresFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void GenresFeature::activate() {
    applyGenreFilter(QString());
}

void GenresFeature::activateChild(const QModelIndex& index) {
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    if (!pItem) {
        return;
    }
    applyGenreFilter(pItem->getData().toString());
}

void GenresFeature::rebuildSidebarModel() {
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);

    auto* pTrackCollection = m_pLibrary->trackCollectionManager()->internalCollection();
    VERIFY_OR_DEBUG_ASSERT(pTrackCollection) {
        m_pSidebarModel->setRootItem(std::move(pRootItem));
        return;
    }

    QSqlQuery query(pTrackCollection->database());
    const QString queryString = QStringLiteral(
            "SELECT DISTINCT %1 "
            "FROM library "
            "WHERE trim(%1) <> '' "
            "ORDER BY %1 COLLATE NOCASE ASC")
                                        .arg(LIBRARYTABLE_GENRE);
    query.prepare(queryString);

    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        m_pSidebarModel->setRootItem(std::move(pRootItem));
        return;
    }

    while (query.next()) {
        const QString genre = query.value(0).toString().trimmed();
        if (genre.isEmpty()) {
            continue;
        }
        pRootItem->appendChild(genre, genre);
    }

    m_pSidebarModel->setRootItem(std::move(pRootItem));
}

void GenresFeature::applyGenreFilter(const QString& genre) {
    auto* pTrackTableModel = m_pLibrary->trackTableModel();
    VERIFY_OR_DEBUG_ASSERT(pTrackTableModel) {
        return;
    }

    const bool autoDjSplitWasActive = m_pLibrary->isAutoDJSplitActive();

    pTrackTableModel->search(toGenreSearchQuery(genre));

    emit saveModelState();
    emit showTrackModel(pTrackTableModel);
    emit enableCoverArtDisplay(true);

    if (autoDjSplitWasActive) {
        m_pLibrary->setAutoDJSplitEnabled(true);
    }
}

QString GenresFeature::toGenreSearchQuery(const QString& genre) const {
    const QString trimmed = genre.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QString escaped = trimmed;
    escaped.replace(QLatin1Char('"'), QLatin1Char(' '));
    return QStringLiteral("genre:=\"%1\"").arg(escaped);
}
