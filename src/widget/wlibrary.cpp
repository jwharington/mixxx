#include "widget/wlibrary.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QSplitter>
#include <QStackedWidget>
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
          m_pMainSplitter(new QSplitter(Qt::Horizontal, this)),
          m_pAutoDJQueueWidget(nullptr),
          m_autoDJSplitLeftRatioPermille(667),
          m_showAutoDJSplitEnabled(false),
          m_trackTableBackgroundColorOpacity(kDefaultTrackTableBackgroundColorOpacity),
          m_bShowButtonText(true) {
    auto* pLayout = new QHBoxLayout(this);
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->addWidget(m_pMainSplitter);

    m_pMainSplitter->setChildrenCollapsible(false);
    m_pMainSplitter->addWidget(m_pViewStack);
    m_pMainSplitter->setStretchFactor(0, 3);
    setFocusProxy(m_pViewStack);

    connect(m_pMainSplitter,
            &QSplitter::splitterMoved,
            this,
            [this](int /*pos*/, int /*index*/) {
                if (!m_pAutoDJQueueWidget || !m_pAutoDJQueueWidget->isVisible()) {
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
                m_visibleSplitterSizes = sizes;
                const int ratioPermille = (1000 * sizes[0]) / total;
                if (ratioPermille != m_autoDJSplitLeftRatioPermille) {
                    m_autoDJSplitLeftRatioPermille = ratioPermille;
                    emit autoDJSplitLeftRatioPermilleChanged(ratioPermille);
                }
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

void WLibrary::keyPressEvent(QKeyEvent* pEvent) {
    if (pEvent->key() == Qt::Key_Left && pEvent->modifiers() & Qt::ControlModifier) {
        emit setLibraryFocus(FocusWidget::Sidebar);
    }
    QWidget::keyPressEvent(pEvent);
}

void WLibrary::updateAutoDJQueueVisibility() {
    if (!m_pAutoDJQueueWidget) {
        return;
    }

    const bool shouldShow = m_showAutoDJSplitEnabled &&
            !m_autoDJViewName.isEmpty() &&
            m_currentViewName != m_autoDJViewName;
    if (shouldShow) {
        m_pAutoDJQueueWidget->show();
        if (m_visibleSplitterSizes.size() == 2) {
            m_pMainSplitter->setSizes(m_visibleSplitterSizes);
        } else {
            const int totalWidth = qMax(width(), 1);
            const int leftSize = (totalWidth * m_autoDJSplitLeftRatioPermille) / 1000;
            const int rightSize = qMax(1, totalWidth - leftSize);
            m_pMainSplitter->setSizes({qMax(1, leftSize), rightSize});
        }
    } else {
        if (m_pAutoDJQueueWidget->isVisible()) {
            m_visibleSplitterSizes = m_pMainSplitter->sizes();
        }
        m_pAutoDJQueueWidget->hide();
    }
}
