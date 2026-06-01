#pragma once

#include "library/libraryfeature.h"

class TreeItemModel;

class GenresFeature final : public LibraryFeature {
    Q_OBJECT
  public:
    GenresFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~GenresFeature() override = default;

    QVariant title() override;
    TreeItemModel* sidebarModel() const override;

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;

  private slots:
    void rebuildSidebarModel();

  private:
    void applyGenreFilter(const QString& genre);
    QString toGenreSearchQuery(const QString& genre) const;

    TreeItemModel* m_pSidebarModel;
};
