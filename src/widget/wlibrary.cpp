#include "widget/wlibrary.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtDebug>

#include "library/libraryview.h"
#include "moc_wlibrary.cpp"
#include "skin/legacy/skincontext.h"
#include "util/math.h"
#include "widget/wtracktableview.h"

WLibrary::WLibrary(QWidget* parent)
        : QWidget(parent),
          WBaseWidget(this),
          m_mutex(QT_RECURSIVE_MUTEX_INIT),
          m_pViewStack(new QStackedWidget(this)),
          m_pMainPaneWidget(new QWidget(this)),
          m_pMainPaneLayout(new QVBoxLayout(m_pMainPaneWidget)),
          m_pMainSplitter(new QSplitter(Qt::Horizontal, this)),
          m_pSearchWidget(nullptr),
          m_pAutoDJQueueWidget(nullptr),
          m_autoDJSplitLeftRatioPermille(667),
          m_showAutoDJSplitEnabled(false),
          m_autoDJSplitActive(false),
          m_ignoreAutoDJSplitterMoved(false),
          m_autoDJSplitRestoreScheduled(false),
          m_embedSearchWidgetInMainPane(false),
          m_trackTableBackgroundColorOpacity(kDefaultTrackTableBackgroundColorOpacity),
          m_bShowButtonText(true) {
    auto* pLayout = new QHBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(m_pMainSplitter);

    m_pMainPaneLayout->setContentsMargins(0, 0, 0, 0);
    m_pMainPaneLayout->setSpacing(0);
    m_pMainPaneLayout->addWidget(m_pViewStack);

    m_pMainSplitter->setChildrenCollapsible(false);
    m_pMainSplitter->addWidget(m_pMainPaneWidget);
    m_pMainSplitter->setStretchFactor(0, 3);
    setFocusProxy(m_pViewStack);

    connect(m_pMainSplitter,
            &QSplitter::splitterMoved,
            this,
            [this](int /*pos*/, int /*index*/) {
                if (m_ignoreAutoDJSplitterMoved) {
                    return;
                }
                persistAutoDJSplitRatioFromCurrentSizes(true);
            });
}

void WLibrary::setup(const QDomNode& node, const SkinContext& context) {
    m_bShowButtonText =
            context.selectBool(
                    node,
                    "ShowButtonText",
                    true);
    m_trackTableBackgroundColorOpacity = math_clamp(
            context.selectDouble(
                    node,
                    "TrackTableBackgroundColorOpacity",
                    kDefaultTrackTableBackgroundColorOpacity),
            kMinTrackTableBackgroundColorOpacity,
            kMaxTrackTableBackgroundColorOpacity);
    m_embedSearchWidgetInMainPane =
            context.selectBool(node, "EmbedSearchBoxInMainPane", false);
    updateSearchWidgetPlacement();

    m_overviewSignalColors.setup(node, context);
}

bool WLibrary::registerView(const QString& name, QWidget* pView) {
    // qDebug() << "WLibrary::registerView" << name;
    const auto lock = lockMutex(&m_mutex);
    if (m_viewMap.contains(name)) {
        return false;
    }
    if (dynamic_cast<LibraryView*>(pView) == nullptr) {
        qDebug() << "WARNING: Attempted to register view" << name << "with WLibrary "
                 << "which does not implement the LibraryView interface. "
                 << "Ignoring.";
        return false;
    }
    m_pViewStack->addWidget(pView);
    m_viewMap[name] = pView;
    return true;
}

QWidget* WLibrary::currentWidget() const {
    return m_pViewStack->currentWidget();
}

void WLibrary::setAutoDJQueueView(QWidget* pView) {
    if (m_pAutoDJQueueWidget == pView) {
        updateAutoDJQueueVisibility();
        return;
    }

    if (m_pAutoDJQueueWidget) {
        m_pAutoDJQueueWidget->hide();
        m_pAutoDJQueueWidget->setParent(nullptr);
    }

    m_pAutoDJQueueWidget = pView;
    if (m_pAutoDJQueueWidget) {
        m_pAutoDJQueueWidget->setParent(m_pMainSplitter);
        m_pMainSplitter->addWidget(m_pAutoDJQueueWidget);
        m_pMainSplitter->setStretchFactor(1, 2);
    }
    updateAutoDJQueueVisibility();
}

void WLibrary::setAutoDJViewName(const QString& name) {
    m_autoDJViewName = name;
    updateAutoDJQueueVisibility();
}

void WLibrary::setAutoDJSplitEnabled(bool enabled) {
    if (m_showAutoDJSplitEnabled == enabled) {
        return;
    }
    m_showAutoDJSplitEnabled = enabled;
    updateAutoDJQueueVisibility();
}

void WLibrary::setAutoDJSplitLeftRatioPermille(int leftRatioPermille) {
    m_autoDJSplitLeftRatioPermille = qBound(1, leftRatioPermille, 999);
    updateAutoDJQueueVisibility();
}

void WLibrary::setSearchWidget(QWidget* pSearchWidget) {
    m_pSearchWidget = pSearchWidget;
    updateSearchWidgetPlacement();
}

void WLibrary::switchToView(const QString& name) {
    const auto lock = lockMutex(&m_mutex);
    // qDebug() << "WLibrary::switchToView" << name;

    QWidget* pWidget = m_viewMap.value(name, nullptr);
    if (pWidget != nullptr) {
        LibraryView* pLibraryView = dynamic_cast<LibraryView*>(pWidget);
        if (pLibraryView == nullptr) {
            qDebug() << "WARNING: Attempted to switch to view" << name << "with WLibrary "
                     << "which does not implement the LibraryView interface. "
                     << "Ignoring.";
            return;
        }
        m_currentViewName = name;
        updateAutoDJQueueVisibility();
        if (currentWidget() != pWidget) {
            LibraryView* pOldLibraryView = dynamic_cast<LibraryView*>(
                    currentWidget());
            if (pOldLibraryView) {
                pOldLibraryView->saveCurrentViewState();
            }
            // qDebug() << "WLibrary::setCurrentWidget" << name;
            m_pViewStack->setCurrentWidget(pWidget);
            pLibraryView->onShow();
            pLibraryView->restoreCurrentViewState();
        }
    }
}

void WLibrary::pasteFromSidebar() {
    QWidget* pCurrent = currentWidget();
    LibraryView* pView = dynamic_cast<LibraryView*>(pCurrent);
    if (pView) {
        pView->pasteFromSidebar();
    }
}

void WLibrary::search(const QString& name) {
    auto lock = lockMutex(&m_mutex);
    QWidget* pCurrent = currentWidget();
    LibraryView* pView = dynamic_cast<LibraryView*>(pCurrent);
    if (pView == nullptr) {
        qDebug() << "WARNING: Attempted to search in view" << name << "with WLibrary "
                 << "which does not implement the LibraryView interface. Ignoring.";
        return;
    }
    lock.unlock();
    pView->onSearch(name);
}

LibraryView* WLibrary::getActiveView() const {
    return dynamic_cast<LibraryView*>(currentWidget());
}

WTrackTableView* WLibrary::getCurrentTrackTableView() const {
    QWidget* pCurrent = currentWidget();
    WTrackTableView* pTracksView = qobject_cast<WTrackTableView*>(pCurrent);
    if (!pTracksView) {
        // This view is not a tracks view, but possibly a special library view
        // with a controls row and a track view (DlgAutoDJ, DlgRecording etc.)?
        pTracksView = pCurrent->findChild<WTrackTableView*>();
    }
    return pTracksView; // might still be nullptr
}

bool WLibrary::isTrackInCurrentView(const TrackId& trackId) {
    // qDebug() << "WLibrary::isTrackInCurrentView" << trackId;
    VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
        return false;
    }
    WTrackTableView* pTracksView = getCurrentTrackTableView();
    if (!pTracksView) {
        return false;
    }

    return pTracksView->isTrackInCurrentView(trackId);
}

void WLibrary::slotSelectTrackInActiveTrackView(const TrackId& trackId) {
    // qDebug() << "WLibrary::slotSelectTrackInActiveTrackView" << trackId;
    if (!trackId.isValid()) {
        return;
    }
    WTrackTableView* pTracksView = getCurrentTrackTableView();
    if (!pTracksView) {
        return;
    }
    if (pTracksView->isTrackInCurrentView(trackId)) {
        pTracksView->selectTrack(trackId);
        pTracksView->setFocus();
    }
}

void WLibrary::saveCurrentViewState() const {
    WTrackTableView* pTracksView = getCurrentTrackTableView();
    if (!pTracksView) {
        return;
    }
    pTracksView->slotSaveCurrentViewState();
}

void WLibrary::restoreCurrentViewState() const {
    WTrackTableView* pTracksView = getCurrentTrackTableView();
    if (!pTracksView) {
        return;
    }
    pTracksView->slotRestoreCurrentViewState();
}

bool WLibrary::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    }
    return QWidget::event(pEvent);
}

void WLibrary::hideEvent(QHideEvent* pEvent) {
    if (!m_ignoreAutoDJSplitterMoved) {
        persistAutoDJSplitRatioFromCurrentSizes(true);
    }
    QWidget::hideEvent(pEvent);
}

void WLibrary::keyPressEvent(QKeyEvent* pEvent) {
    if (pEvent->key() == Qt::Key_Left && pEvent->modifiers() & Qt::ControlModifier) {
        emit setLibraryFocus(FocusWidget::Sidebar);
    }
    QWidget::keyPressEvent(pEvent);
}

void WLibrary::updateSearchWidgetPlacement() {
    if (!m_pSearchWidget || !m_embedSearchWidgetInMainPane) {
        return;
    }

    if (m_pSearchWidget->parentWidget() != m_pMainPaneWidget) {
        m_pSearchWidget->setParent(m_pMainPaneWidget);
    }

    if (m_pMainPaneLayout->indexOf(m_pSearchWidget) != 0) {
        m_pMainPaneLayout->insertWidget(0, m_pSearchWidget);
    }
    m_pSearchWidget->show();
}

void WLibrary::updateAutoDJQueueVisibility() {
    if (!m_pAutoDJQueueWidget) {
        return;
    }

    const bool shouldShow = m_showAutoDJSplitEnabled &&
            !m_autoDJViewName.isEmpty() &&
            !m_currentViewName.isEmpty() &&
            m_currentViewName != m_autoDJViewName;
    const bool wasSplitActive = m_autoDJSplitActive;

    if (m_autoDJSplitActive != shouldShow) {
        m_autoDJSplitActive = shouldShow;
        emit autoDJSplitActiveChanged(shouldShow);
    }

    if (shouldShow) {
        m_pAutoDJQueueWidget->show();
        applyAutoDJSplitSizes();
    } else {
        // Only cache sizes when transitioning from visible split -> hidden split.
        // This avoids recording startup/default sizes before the split has ever
        // been shown, which would override the persisted ratio on next startup.
        if (wasSplitActive) {
            persistAutoDJSplitRatioFromCurrentSizes(true);
        }
        m_pAutoDJQueueWidget->hide();
    }
}

void WLibrary::applyAutoDJSplitSizes() {
    const QList<int> currentSizes = m_pMainSplitter->sizes();
    const int currentTotal = currentSizes.size() == 2 ? (currentSizes[0] + currentSizes[1]) : 0;

    if (m_visibleSplitterSizes.size() == 2 && currentTotal > 0) {
        m_ignoreAutoDJSplitterMoved = true;
        m_pMainSplitter->setSizes(m_visibleSplitterSizes);
        m_ignoreAutoDJSplitterMoved = false;
        return;
    }

    if (currentTotal <= 1) {
        scheduleDeferredAutoDJSplitRestore();
        return;
    }

    const int leftSize = (currentTotal * m_autoDJSplitLeftRatioPermille) / 1000;
    const int rightSize = qMax(1, currentTotal - leftSize);
    m_ignoreAutoDJSplitterMoved = true;
    m_pMainSplitter->setSizes({qMax(1, leftSize), rightSize});
    m_ignoreAutoDJSplitterMoved = false;
}

void WLibrary::scheduleDeferredAutoDJSplitRestore() {
    if (m_autoDJSplitRestoreScheduled) {
        return;
    }
    m_autoDJSplitRestoreScheduled = true;
    QMetaObject::invokeMethod(
            this,
            [this]() {
                m_autoDJSplitRestoreScheduled = false;
                if (!m_autoDJSplitActive || !m_pAutoDJQueueWidget) {
                    return;
                }
                applyAutoDJSplitSizes();
            },
            Qt::QueuedConnection);
}

void WLibrary::persistAutoDJSplitRatioFromCurrentSizes(bool cacheVisibleSizes) {
    const bool queueVisible = m_pAutoDJQueueWidget && m_pAutoDJQueueWidget->isVisible();
    if (!queueVisible && !m_autoDJSplitActive) {
        return;
    }
    const QList<int> sizes = m_pMainSplitter->sizes();
    if (sizes.size() != 2) {
        return;
    }
    const int total = sizes[0] + sizes[1];
    if (total <= 0) {
        return;
    }
    if (cacheVisibleSizes) {
        m_visibleSplitterSizes = sizes;
    }
    const int ratioPermille = (1000 * sizes[0]) / total;
    if (ratioPermille != m_autoDJSplitLeftRatioPermille) {
        m_autoDJSplitLeftRatioPermille = ratioPermille;
        emit autoDJSplitLeftRatioPermilleChanged(ratioPermille);
    }
}
