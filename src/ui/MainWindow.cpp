#include "MainWindow.h"
#include "AboutDialog.h"
#include "AddEditDialog.h"
#include "BepInEx.h"
#include "Common.h"
#include "GameDetailsDialog.h"
#include "IconFactory.h"
#include "LaunchLogs.h"
#include "LaunchPlan.h"
#include "LaunchProgressDialog.h"
#include "LibraryView.h"
#include "LogsDialog.h"
#include "ModManagerDialog.h"
#include "OnlineFixDialog.h"
#include "RunnerInstallDialog.h"
#include "SettingsDialog.h"
#include "Sidebar.h"
#include "SteamPrecondition.h"
#include "SteamPromptDialog.h"
#include "CoverFetcher.h"
#include "CoverPickerDialog.h"
#include "GameSelectionBar.h"
#include "LibraryImportDialog.h"
#include "TutorialOverlay.h"
#include "UpdateBanner.h"
#include "Version.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kSidebarWidth = 232;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_library(m_storage)
    , m_scanner(m_storage, m_library)
    , m_launcher(this)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Vanta"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/vanta.svg")));
    setMinimumSize(940, 620);
    resize(1240, 800);

    buildUi();
    buildMenus();
    connectSignals();
    restoreState();

    m_coverFetcher = new CoverFetcher(this);
    connect(m_coverFetcher, &CoverFetcher::resolved, this,
            [this](const QString &gameId, const QString &matchedName, int score) {
                const Game game = m_library.gameById(gameId);
                if (!game.id.isEmpty()) {
                    setStatusMessage(QStringLiteral("SteamGridDB matched “%1” to “%2” (%3%).")
                                         .arg(game.name, matchedName)
                                         .arg(score),
                                     9000);
                }
            });
    connect(m_coverFetcher, &CoverFetcher::finished, this,
            [this](const QString &gameId, const QString &coverPath) {
                bool found = false;
                Game game = m_library.gameById(gameId, &found);
                if (!found)
                    return;
                game.coverPath = coverPath;
                game.modifiedAt = QDateTime::currentDateTime();
                if (m_library.updateGame(game)) {
                    refreshLibrary();
                    setStatusMessage(QStringLiteral("Fetched cover art for %1.").arg(game.name));
                }
            });
    connect(m_coverFetcher, &CoverFetcher::failed, this,
            [this](const QString &gameId, const QString &message) {
                const Game game = m_library.gameById(gameId);
                if (!game.id.isEmpty())
                    setStatusMessage(QStringLiteral("No cover art for %1: %2").arg(game.name, message), 9000);
            });
    connect(m_coverFetcher, &CoverFetcher::artworksListed, this,
            [this](const QString &gameId, const QString &matchedName, const QList<CoverArt> &artworks) {
                if (m_pendingPickGameId != gameId)
                    return;
                const QString gameName = m_pendingPickName;
                m_pendingPickGameId.clear();
                m_pendingPickName.clear();

                CoverPickerDialog picker(gameName, matchedName, artworks, &m_theme, this);
                const auto thumbConnection = connect(m_coverFetcher, &CoverFetcher::thumbnailReady,
                                                     &picker, &CoverPickerDialog::applyThumbnail);
                for (const CoverArt &art : artworks)
                    m_coverFetcher->downloadThumbnail(art.gridId, art.thumbUrl);

                const int result = picker.exec();
                disconnect(thumbConnection);
                if (result != QDialog::Accepted) {
                    // The picker suspends the automatic queue while it is open;
                    // covers waiting behind it have to resume either way.
                    m_coverFetcher->resumeQueue();
                    return;
                }
                const CoverArt chosen = picker.selectedArt();
                if (chosen.fullUrl.isEmpty())
                    return;
                setStatusMessage(QStringLiteral("Downloading the selected artwork…"));
                m_coverFetcher->downloadCoverArt(gameId, chosen);
            });

    m_scanner.loadCache();
    refreshLibrary();
    refreshIcons();
    updateRunningState();

    if (!m_library.loadWarnings().isEmpty())
        setStatusMessage(m_library.loadWarnings().join(QStringLiteral(" ")), 12000);

    QTimer::singleShot(400, this, [this] {
        if (!m_library.settings().tutorialCompleted)
            showTutorial(0);
        scheduleAutoScan();
    });

    // Check once per run, a moment after start-up so the window is painted and
    // the status line is not fighting the first paint.
    QTimer::singleShot(2500, this, [this] { checkForUpdates(false); });
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
    if (m_steamPrecondition) {
        m_steamPrecondition->cancel();
        m_steamPrecondition->deleteLater();
        m_steamPrecondition = nullptr;
    }
    m_launcher.shutdown();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("mainSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(0);

    buildSidebar();
    m_sidebar->setParent(splitter);
    splitter->addWidget(m_sidebar);
    splitter->setStretchFactor(0, 0);

    auto *contentArea = new QWidget(splitter);
    contentArea->setObjectName(QStringLiteral("contentArea"));
    auto *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    buildHeader();
    contentLayout->addWidget(m_header);

    m_updateBanner = new UpdateBanner(&m_theme, contentArea);
    m_updateBanner->setVisible(false);
    contentLayout->addWidget(m_updateBanner);

    buildContent();
    contentLayout->addWidget(m_libraryView, 1);

    m_selectionBar = new GameSelectionBar(&m_theme, contentArea);
    contentLayout->addWidget(m_selectionBar);
    connect(m_selectionBar, &GameSelectionBar::launchRequested, this,
            [this](const QString &gameId, bool applyOnlineFix) {
                requestLaunch(gameId, applyOnlineFix);
            });
    connect(m_selectionBar, &GameSelectionBar::stopRequested, this, &MainWindow::requestStop);
    connect(m_selectionBar, &GameSelectionBar::menuRequested, this, &MainWindow::showContextMenu);

    auto *statusBarArea = new QWidget(contentArea);
    statusBarArea->setObjectName(QStringLiteral("statusBarArea"));
    auto *statusLayout = new QHBoxLayout(statusBarArea);
    statusLayout->setContentsMargins(22, 6, 22, 10);
    statusLayout->setSpacing(10);
    m_statusLabel = new QLabel(statusBarArea);
    m_statusLabel->setProperty("class", QStringLiteral("muted"));
    m_statusLabel->setText(QStringLiteral("Ready."));
    statusLayout->addWidget(m_statusLabel, 1);
    m_runningLabel = new QLabel(statusBarArea);
    m_runningLabel->setProperty("class", QStringLiteral("muted"));
    statusLayout->addWidget(m_runningLabel, 0, Qt::AlignRight);
    contentLayout->addWidget(statusBarArea);

    splitter->addWidget(contentArea);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({kSidebarWidth, 1000});
    root->addWidget(splitter, 1);
    setCentralWidget(central);

    m_tutorial = new TutorialOverlay(&m_theme, central);
    m_tutorial->setGeometry(central->rect());
}

void MainWindow::buildSidebar()
{
    m_sidebar = new Sidebar(&m_theme);
    m_sidebar->setObjectName(QStringLiteral("sidebarSurface"));
    m_sidebar->setMinimumWidth(206);
    m_sidebar->setMaximumWidth(300);
}

void MainWindow::buildHeader()
{
    m_header = new QFrame;
    m_header->setObjectName(QStringLiteral("contentHeader"));
    auto *layout = new QVBoxLayout(m_header);
    layout->setContentsMargins(22, 18, 22, 12);
    layout->setSpacing(12);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(12);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(1);
    m_titleLabel = new QLabel(QStringLiteral("All games"), m_header);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(22);
    titleFont.setWeight(QFont::Black);
    m_titleLabel->setFont(titleFont);
    titles->addWidget(m_titleLabel);
    m_subtitleLabel = new QLabel(m_header);
    m_subtitleLabel->setProperty("class", QStringLiteral("muted"));
    titles->addWidget(m_subtitleLabel);
    topRow->addLayout(titles, 1);

    m_searchContainer = new QFrame(m_header);
    m_searchContainer->setObjectName(QStringLiteral("searchContainer"));
    m_searchContainer->setFixedHeight(38);
    m_searchContainer->setMinimumWidth(250);
    m_searchContainer->setMaximumWidth(360);
    auto *searchLayout = new QHBoxLayout(m_searchContainer);
    searchLayout->setContentsMargins(12, 0, 3, 0);
    searchLayout->setSpacing(6);

    auto *searchIcon = new QLabel(m_searchContainer);
    searchIcon->setObjectName(QStringLiteral("searchIcon"));
    searchIcon->setFixedSize(16, 16);
    searchLayout->addWidget(searchIcon);

    m_searchField = new QLineEdit(m_searchContainer);
    m_searchField->setObjectName(QStringLiteral("searchField"));
    m_searchField->setPlaceholderText(QStringLiteral("Search your library…"));
    searchLayout->addWidget(m_searchField, 1);

    m_searchClearButton = new QToolButton(m_searchContainer);
    m_searchClearButton->setObjectName(QStringLiteral("searchClearButton"));
    m_searchClearButton->setCursor(Qt::PointingHandCursor);
    m_searchClearButton->setToolTip(QStringLiteral("Clear the search"));
    m_searchClearButton->setIconSize(QSize(13, 13));
    m_searchClearButton->setAutoRaise(true);
    m_searchClearButton->setVisible(false);
    connect(m_searchClearButton, &QToolButton::clicked, this, [this] {
        m_searchField->clear();
        m_searchField->setFocus(Qt::OtherFocusReason);
    });
    searchLayout->addWidget(m_searchClearButton);

    m_filterButton = new QToolButton(m_searchContainer);
    m_filterButton->setObjectName(QStringLiteral("filterButton"));
    m_filterButton->setPopupMode(QToolButton::InstantPopup);
    m_filterButton->setCursor(Qt::PointingHandCursor);
    m_filterButton->setToolTip(QStringLiteral("Filters"));
    m_filterButton->setIconSize(QSize(17, 17));
    searchLayout->addWidget(m_filterButton);
    topRow->addWidget(m_searchContainer, 0, Qt::AlignVCenter);

    m_addButton = new QToolButton(m_header);
    m_addButton->setObjectName(QStringLiteral("addGameButton"));
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setToolTip(QStringLiteral("Add game (Ctrl+N)"));
    topRow->addWidget(m_addButton, 0, Qt::AlignVCenter);

    auto *viewGroup = new QWidget(m_header);
    auto *viewLayout = new QHBoxLayout(viewGroup);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(0);
    m_viewOptionsButton = new QToolButton(viewGroup);
    m_viewOptionsButton->setObjectName(QStringLiteral("viewOptionsButton"));
    m_viewOptionsButton->setCursor(Qt::PointingHandCursor);
    m_viewOptionsButton->setToolTip(QStringLiteral("Change view"));
    m_viewOptionsButton->setIconSize(QSize(17, 17));
    viewLayout->addWidget(m_viewOptionsButton);
    m_viewMenuButton = new QToolButton(viewGroup);
    m_viewMenuButton->setObjectName(QStringLiteral("viewMenuButton"));
    m_viewMenuButton->setCursor(Qt::PointingHandCursor);
    m_viewMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_viewMenuButton->setIconSize(QSize(13, 13));
    viewLayout->addWidget(m_viewMenuButton);
    topRow->addWidget(viewGroup, 0, Qt::AlignVCenter);

    m_mainMenuButton = new QToolButton(m_header);
    m_mainMenuButton->setObjectName(QStringLiteral("mainMenuButton"));
    m_mainMenuButton->setCursor(Qt::PointingHandCursor);
    m_mainMenuButton->setPopupMode(QToolButton::InstantPopup);
    m_mainMenuButton->setIconSize(QSize(17, 17));
    m_mainMenuButton->setToolTip(QStringLiteral("More actions"));
    topRow->addWidget(m_mainMenuButton, 0, Qt::AlignVCenter);

    layout->addLayout(topRow);
}

void MainWindow::buildContent()
{
    m_libraryView = new LibraryView(&m_theme);
}

void MainWindow::buildMenus()
{
    m_filterMenu = new QMenu(this);
    m_filterMenu->setObjectName(QStringLiteral("filterPopup"));
    m_filterButton->setMenu(m_filterMenu);

    m_viewMenu = new QMenu(this);
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));
    m_viewMenuButton->setMenu(m_viewMenu);

    m_mainMenu = new QMenu(this);
    m_mainMenu->setObjectName(QStringLiteral("mainMenu"));
    m_mainMenuButton->setMenu(m_mainMenu);
}

void MainWindow::connectSignals()
{
    connect(m_sidebar, &Sidebar::filterSelected, this, &MainWindow::selectFilter);
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::openSettingsDialog);
    connect(m_sidebar, &Sidebar::tutorialRequested, this, [this] { showTutorial(0); });
    connect(m_sidebar, &Sidebar::aboutRequested, this, &MainWindow::openAboutDialog);
    connect(m_sidebar, &Sidebar::collapsedGroupsChanged, this, [this](const QJsonObject &collapsed) {
        AppSettings settings = m_library.settings();
        if (!settings.rememberCollapsedGroups)
            return;
        settings.collapsedGroups = collapsed;
        m_library.updateSettings(settings);
    });

    connect(m_libraryView, &LibraryView::gameActivated, this,
            [this](const QString &gameId) { requestLaunch(gameId); });
    connect(m_libraryView, &LibraryView::gameSelected, this, [this](const QString &gameId) {
        m_selectedGameId = gameId;
        m_sidebar->setCurrentItem(m_filterId);
        refreshSelectionBar();
    });
    connect(m_libraryView, &LibraryView::contextMenuRequested, this, &MainWindow::showContextMenu);
    connect(m_libraryView, &LibraryView::addRequested, this, &MainWindow::openAddDialog);
    connect(m_libraryView, &LibraryView::clearFiltersRequested, this, [this] {
        m_searchField->clear();
        m_compatibilityFilter = CompatibilityFilter::All;
        m_installFilter = InstallFilter::All;
        m_favoritesOnly = false;
        selectFilter(QStringLiteral("library.games"));
    });

    connect(m_addButton, &QToolButton::clicked, this, &MainWindow::openAddDialog);
    connect(m_searchField, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_searchText = text;
        if (m_searchClearButton)
            m_searchClearButton->setVisible(!text.isEmpty());
        applyFilter();
    });
    connect(m_viewOptionsButton, &QToolButton::clicked, this, [this] {
        AppSettings settings = m_library.settings();
        settings.viewMode = settings.viewMode == ViewMode::Grid ? ViewMode::List : ViewMode::Grid;
        m_library.updateSettings(settings);
        m_libraryView->setViewMode(settings.viewMode);
        refreshIcons();
    });

    connect(&m_library, &GameLibrary::gamesChanged, this, &MainWindow::refreshLibrary);
    connect(&m_library, &GameLibrary::gameChanged, this, [this](const QString &) { refreshLibrary(); });
    connect(&m_library, &GameLibrary::settingsChanged, this, [this] {
        m_libraryView->setViewMode(m_library.settings().viewMode);
        m_libraryView->setCoverSize(m_library.settings().coverSize);
        m_libraryView->setShowBadges(m_library.settings().showBadges);
        m_libraryView->setAnimationsEnabled(m_library.settings().animations);
        refreshSidebar();
        applyFilter();
    });
    connect(&m_library, &GameLibrary::saveError, this, [this](const QString &message) {
        warn(message);
    });

    connect(&m_scanner, &ScanController::scanStarted, this, [this](const QString &gameId) {
        Q_UNUSED(gameId);
        refreshStatus();
    });
    connect(&m_scanner, &ScanController::scanFinished, this, [this](const QString &gameId, const ScanResult &result) {
        bool found = false;
        Game game = m_library.gameById(gameId, &found);
        if (found) {
            GameScanner::applyResultToGame(game, result);
            game.modifiedAt = QDateTime::currentDateTime();
            m_library.updateGame(game);
        }
        refreshStatus();
        if (m_libraryView->selectedGameId() == gameId)
            applyFilter();
    });
    connect(&m_scanner, &ScanController::warning, this, [this](const QString &message) {
        setStatusMessage(message, 12000);
    });

    connect(&m_theme, &ThemeManager::themeChanged, this, &MainWindow::refreshIcons);
    connect(&m_theme, &ThemeManager::themeChanged, this, [this] {
        m_sidebar->refreshTheme();
        m_libraryView->refreshTheme();
        refreshStatus();
    });
    connect(&m_theme, &ThemeManager::animationsChanged, this, [this](bool enabled) {
        m_libraryView->setAnimationsEnabled(enabled);
    });

    connect(&m_launcher, &GameLauncher::gameStarted, this, [this](const QString &gameId, const QDateTime &) {
        Q_UNUSED(gameId);
        closeLaunchDialog();
        updateRunningState();
        setStatusMessage(QStringLiteral("Game started."));
    });
    connect(&m_launcher, &GameLauncher::launchFailed, this,
            [this](const QString &gameId, const QString &message, const QString &diagnostic,
                   const QString &logPath) {
        updateRunningState();
        if (m_launchDialog) {
            m_launchDialog->setLogPath(logPath);
            m_launchDialog->showFailure(message);
            return;
        }
        if (logPath.isEmpty()) {
            showDiagnostics(QStringLiteral("Launch failed"), message, diagnostic);
            return;
        }
        // The launch dialog is already gone, so offer the log directly instead
        // of a diagnostic box that duplicates what the log records.
        const auto reply = QMessageBox::question(
            this, QStringLiteral("Launch failed"),
            QStringLiteral("%1\n\nThe launch log has the full command and any output from the process. "
                           "Open it now?").arg(message),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply == QMessageBox::Yes)
            openLogs(gameId, logPath);
    });
    connect(&m_launcher, &GameLauncher::gameStopping, this, [this](const QString &gameId) {
        Q_UNUSED(gameId);
        setStatusMessage(QStringLiteral("Stopping the game…"), 15000);
        updateRunningState();
    });
    connect(&m_launcher, &GameLauncher::gameFinished, this, &MainWindow::handleGameFinished);

    connect(&m_updateChecker, &UpdateChecker::updateAvailable, this, &MainWindow::handleUpdateAvailable);
    connect(&m_updateChecker, &UpdateChecker::upToDate, this, [this](const QString &) {
        if (m_updateCheckManual)
            setStatusMessage(QStringLiteral("Vanta is up to date."));
    });
    connect(&m_updateChecker, &UpdateChecker::checkSkipped, this, [this](const QString &) {
        // No request was made, so saying "up to date" would be a claim Vanta
        // cannot support. A development build has no release to compare against.
        if (m_updateCheckManual)
            setStatusMessage(QStringLiteral("This build has no release version, so updates cannot be checked."));
    });
    connect(&m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString &message) {
        // A failed check is not something the user has to fix, so it stays a
        // status line instead of a dialog, and only when the check was asked for.
        if (m_updateCheckManual)
            setStatusMessage(QStringLiteral("The update check failed. %1").arg(message), 9000);
    });
    connect(m_updateBanner, &UpdateBanner::openReleaseRequested, this, &MainWindow::openUpdatePage);
    connect(m_updateBanner, &UpdateBanner::dismissed, this, [this] { m_updateDismissed = true; });
}

void MainWindow::restoreState()
{
    const AppSettings &settings = m_library.settings();
    m_theme.setAnimationsEnabled(settings.animations);
    m_theme.setChoice(settings.theme);
    m_theme.apply();

    m_libraryView->setViewMode(settings.viewMode);
    m_libraryView->setCoverSize(settings.coverSize);
    m_libraryView->setShowBadges(settings.showBadges);
    m_libraryView->setAnimationsEnabled(settings.animations);
    m_sidebar->setCollapsedGroups(settings.rememberCollapsedGroups ? settings.collapsedGroups : QJsonObject());

    if (settings.rememberSidebarSelection && !settings.lastSidebarItem.isEmpty())
        m_filterId = settings.lastSidebarItem;
    if (m_sidebar->targetFor(m_filterId) == nullptr)
        m_filterId = QStringLiteral("library.games");
    m_sidebar->setCurrentItem(m_filterId);

    if (!settings.windowGeometry.isEmpty())
        restoreGeometry(settings.windowGeometry);
    if (settings.windowMaximized)
        showMaximized();
}

void MainWindow::saveWindowState()
{
    AppSettings settings = m_library.settings();
    settings.windowGeometry = saveGeometry();
    settings.windowMaximized = isMaximized();
    settings.lastSidebarItem = m_filterId;
    m_library.updateSettings(settings);
}

void MainWindow::refreshLibrary()
{
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(40);
        connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshLibraryImmediately);
    }
    if (!m_refreshTimer->isActive())
        m_refreshTimer->start();
}

void MainWindow::refreshLibraryImmediately()
{
    if (m_refreshTimer && m_refreshTimer->isActive())
        m_refreshTimer->stop();
    refreshSidebar();
    applyFilter();
    refreshStatus();
}

void MainWindow::refreshSidebar()
{
    if (!m_sidebar)
        return;
    m_sidebar->setGames(m_library.games());
    m_sidebar->setCurrentItem(m_filterId);
}

// Keeps the bottom detail strip in sync with the current selection.
void MainWindow::refreshSelectionBar()
{
    if (!m_selectionBar)
        return;
    if (m_selectedGameId.isEmpty()) {
        m_selectionBar->setGame(Game());
        return;
    }
    bool found = false;
    const Game game = m_library.gameById(m_selectedGameId, &found);
    m_selectionBar->setGame(found ? game : Game());
    m_selectionBar->setRunning(found && m_launcher.isRunning(game.id));
}

void MainWindow::refreshStatus()
{
    if (!m_statusLabel)
        return;
    const int scanning = m_scanner.activeScanCount();
    if (scanning > 0) {
        m_statusLabel->setText(QStringLiteral("Scanning %1 game%2…")
                                   .arg(scanning)
                                   .arg(scanning == 1 ? QString() : QStringLiteral("s")));
    } else if (!m_statusMessage.isEmpty()) {
        m_statusLabel->setText(m_statusMessage);
    } else {
        m_statusLabel->setText(QStringLiteral("Ready."));
    }
    if (!m_runningLabel)
        return;
    const int running = m_running.size();
    m_runningLabel->setText(running == 0 ? QString()
                                         : QStringLiteral("%1 running").arg(running));
}

void MainWindow::setStatusMessage(const QString &message, int timeoutMs)
{
    if (!m_statusLabel)
        return;
    m_statusMessage = message;
    refreshStatus();
    const int serial = ++m_statusMessageSerial;
    QTimer::singleShot(timeoutMs, this, [this, serial] {
        if (serial != m_statusMessageSerial)
            return;
        m_statusMessage.clear();
        refreshStatus();
    });
}

void MainWindow::warn(const QString &message)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Vanta"));
    box.setText(message);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::refreshIcons()
{
    const ThemeColors &colors = m_theme.colors();
    Icons::setButtonIcon(m_addButton, Glyph::Plus, QColor(Qt::white), 20);
    Icons::setButtonIcon(m_filterButton, Glyph::Filter, colors.muted, 17);
    Icons::setButtonIcon(m_mainMenuButton, Glyph::More, colors.text, 17);
    Icons::setButtonIcon(m_viewMenuButton, Glyph::ChevronDown, colors.muted, 13);
    Icons::setButtonIcon(m_viewOptionsButton,
                         m_library.settings().viewMode == ViewMode::Grid ? Glyph::List : Glyph::Grid,
                         colors.text, 17);
    if (QWidget *searchIcon = m_searchContainer->findChild<QLabel *>(QStringLiteral("searchIcon")))
        static_cast<QLabel *>(searchIcon)->setPixmap(Icons::icon(Glyph::Search, colors.muted, 16).pixmap(16, 16));
    Icons::setButtonIcon(m_searchClearButton, Glyph::Close, colors.muted, 13);
    m_searchClearButton->setVisible(!m_searchText.isEmpty());
    if (m_selectionBar)
        m_selectionBar->refreshTheme();

    m_viewMenu->clear();
    auto *gridAction = m_viewMenu->addAction(QStringLiteral("Grid view"));
    gridAction->setCheckable(true);
    gridAction->setChecked(m_library.settings().viewMode == ViewMode::Grid);
    connect(gridAction, &QAction::triggered, this, [this] {
        AppSettings settings = m_library.settings();
        settings.viewMode = ViewMode::Grid;
        m_library.updateSettings(settings);
        m_libraryView->setViewMode(ViewMode::Grid);
        refreshIcons();
    });
    auto *listAction = m_viewMenu->addAction(QStringLiteral("List view"));
    listAction->setCheckable(true);
    listAction->setChecked(m_library.settings().viewMode == ViewMode::List);
    connect(listAction, &QAction::triggered, this, [this] {
        AppSettings settings = m_library.settings();
        settings.viewMode = ViewMode::List;
        m_library.updateSettings(settings);
        m_libraryView->setViewMode(ViewMode::List);
        refreshIcons();
    });
    m_viewMenu->addSeparator();
    auto *badges = m_viewMenu->addAction(QStringLiteral("Show badges"));
    badges->setCheckable(true);
    badges->setChecked(m_library.settings().showBadges);
    connect(badges, &QAction::triggered, this, [this](bool checked) {
        AppSettings settings = m_library.settings();
        settings.showBadges = checked;
        m_library.updateSettings(settings);
    });
    auto *animations = m_viewMenu->addAction(QStringLiteral("Subtle animations"));
    animations->setCheckable(true);
    animations->setChecked(m_library.settings().animations);
    connect(animations, &QAction::triggered, this, [this](bool checked) {
        AppSettings settings = m_library.settings();
        settings.animations = checked;
        m_library.updateSettings(settings);
        m_theme.setAnimationsEnabled(checked);
    });
    m_viewMenu->addSeparator();
    auto *sizes = m_viewMenu->addMenu(QStringLiteral("Cover size"));
    for (CoverSize size : {CoverSize::Small, CoverSize::Medium, CoverSize::Large}) {
        auto *action = sizes->addAction(coverSizeName(size));
        action->setCheckable(true);
        action->setChecked(m_library.settings().coverSize == size);
        connect(action, &QAction::triggered, this, [this, size] {
            AppSettings settings = m_library.settings();
            settings.coverSize = size;
            m_library.updateSettings(settings);
            refreshIcons();
        });
    }

    m_mainMenu->clear();
    auto *addAction = m_mainMenu->addAction(QStringLiteral("Add game…"));
    addAction->setShortcut(QKeySequence::New);
    connect(addAction, &QAction::triggered, this, &MainWindow::openAddDialog);
    auto *scanAction = m_mainMenu->addAction(QStringLiteral("Scan library"));
    connect(scanAction, &QAction::triggered, this, [this] { scanLibrary(false); });
    auto *rescanAction = m_mainMenu->addAction(QStringLiteral("Rescan all games"));
    connect(rescanAction, &QAction::triggered, this, [this] { scanLibrary(true); });
    auto *coversAction = m_mainMenu->addAction(QStringLiteral("Fetch missing covers"));
    connect(coversAction, &QAction::triggered, this, &MainWindow::fetchMissingCovers);
    auto *logsAction = m_mainMenu->addAction(QStringLiteral("Logs…"));
    connect(logsAction, &QAction::triggered, this, &MainWindow::openLogsForSelection);
    m_mainMenu->addSeparator();
    auto *runnersAction = m_mainMenu->addAction(QStringLiteral("Download runners…"));
    connect(runnersAction, &QAction::triggered, this, &MainWindow::openRunnerInstallDialog);
    auto *portAction = m_mainMenu->addAction(QStringLiteral("Port external libraries…"));
    connect(portAction, &QAction::triggered, this, &MainWindow::openLibraryImportDialog);
    auto *settingsAction = m_mainMenu->addAction(QStringLiteral("Settings…"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    auto *tutorialAction = m_mainMenu->addAction(QStringLiteral("Show tutorial"));
    connect(tutorialAction, &QAction::triggered, this, [this] { showTutorial(0); });
    m_mainMenu->addSeparator();
    auto *updateAction = m_mainMenu->addAction(QStringLiteral("Check for updates…"));
    connect(updateAction, &QAction::triggered, this, [this] { checkForUpdates(true); });
    auto *aboutAction = m_mainMenu->addAction(QStringLiteral("About Vanta"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::openAboutDialog);
    auto *quitAction = m_mainMenu->addAction(QStringLiteral("Quit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    buildFilterMenu();
}

void MainWindow::buildFilterMenu()
{
    if (!m_filterMenu)
        return;
    m_filterMenu->clear();

    auto *compatMenu = m_filterMenu->addMenu(QStringLiteral("Compatibility"));
    const QList<QPair<CompatibilityFilter, QString>> compatOptions = {
        {CompatibilityFilter::All, compatibilityFilterName(CompatibilityFilter::All)},
        {CompatibilityFilter::Detected, compatibilityFilterName(CompatibilityFilter::Detected)},
        {CompatibilityFilter::NeedsAttention, compatibilityFilterName(CompatibilityFilter::NeedsAttention)},
        {CompatibilityFilter::NotDetected, compatibilityFilterName(CompatibilityFilter::NotDetected)}
    };
    for (const auto &option : compatOptions) {
        auto *action = compatMenu->addAction(option.second);
        action->setCheckable(true);
        action->setChecked(m_compatibilityFilter == option.first);
        connect(action, &QAction::triggered, this, [this, filter = option.first] {
            m_compatibilityFilter = filter;
            buildFilterMenu();
            applyFilter();
        });
    }

    auto *installMenu = m_filterMenu->addMenu(QStringLiteral("Installation"));
    const QList<QPair<InstallFilter, QString>> installOptions = {
        {InstallFilter::All, installFilterName(InstallFilter::All)},
        {InstallFilter::Installed, installFilterName(InstallFilter::Installed)},
        {InstallFilter::Missing, installFilterName(InstallFilter::Missing)}
    };
    for (const auto &option : installOptions) {
        auto *action = installMenu->addAction(option.second);
        action->setCheckable(true);
        action->setChecked(m_installFilter == option.first);
        connect(action, &QAction::triggered, this, [this, filter = option.first] {
            m_installFilter = filter;
            buildFilterMenu();
            applyFilter();
        });
    }

    auto *favorites = m_filterMenu->addAction(QStringLiteral("Favorites only"));
    favorites->setCheckable(true);
    favorites->setChecked(m_favoritesOnly);
    connect(favorites, &QAction::triggered, this, [this](bool checked) {
        m_favoritesOnly = checked;
        buildFilterMenu();
        applyFilter();
    });

    m_filterMenu->addSeparator();
    auto *clear = m_filterMenu->addAction(QStringLiteral("Clear filters"));
    connect(clear, &QAction::triggered, this, [this] {
        m_compatibilityFilter = CompatibilityFilter::All;
        m_installFilter = InstallFilter::All;
        m_favoritesOnly = false;
        buildFilterMenu();
        applyFilter();
    });
    m_filterButton->setProperty("active", m_compatibilityFilter != CompatibilityFilter::All
        || m_installFilter != InstallFilter::All || m_favoritesOnly);
    m_filterButton->style()->unpolish(m_filterButton);
    m_filterButton->style()->polish(m_filterButton);
}

void MainWindow::selectFilter(const QString &id)
{
    m_filterId = id;
    m_sidebar->setCurrentItem(id);
    updateFilterLabel();
    applyFilter();
    AppSettings settings = m_library.settings();
    if (settings.rememberSidebarSelection && settings.lastSidebarItem != id) {
        settings.lastSidebarItem = id;
        m_library.updateSettings(settings);
    }
}

void MainWindow::updateFilterLabel()
{
    const QString label = m_sidebar->labelFor(m_filterId);
    m_titleLabel->setText(label);
}

bool MainWindow::matchesSearch(const Game &game) const
{
    if (m_searchText.trimmed().isEmpty())
        return true;
    const QString needle = m_searchText.trimmed();
    return game.name.contains(needle, Qt::CaseInsensitive)
        || game.source.contains(needle, Qt::CaseInsensitive)
        || game.gameDirectory.contains(needle, Qt::CaseInsensitive)
        || game.executable.contains(needle, Qt::CaseInsensitive)
        || runnerName(game.runner).contains(needle, Qt::CaseInsensitive);
}

bool MainWindow::matchesFilter(const Game &game) const
{
    if (!matchesSearch(game))
        return false;
    if (m_favoritesOnly && !game.favorite)
        return false;

    switch (m_compatibilityFilter) {
    case CompatibilityFilter::All:
        break;
    case CompatibilityFilter::Detected:
        if (!scanIsDetected(game.compatibility))
            return false;
        break;
    case CompatibilityFilter::NeedsAttention:
        if (!scanNeedsAttention(game.compatibility))
            return false;
        break;
    case CompatibilityFilter::NotDetected:
        if (game.compatibility != ScanState::NotScanned
            && game.compatibility != ScanState::NoKnownComponents)
            return false;
        break;
    }

    switch (m_installFilter) {
    case InstallFilter::All:
        break;
    case InstallFilter::Installed:
        if (!game.executableExists())
            return false;
        break;
    case InstallFilter::Missing:
        if (game.executableExists())
            return false;
        break;
    }

    if (m_filterId == QStringLiteral("library.games"))
        return true;
    if (m_filterId == QStringLiteral("library.favorites"))
        return game.favorite;
    if (m_filterId == QStringLiteral("library.recent")) {
        if (!game.lastPlayed.isValid())
            return false;
        return game.lastPlayed >= QDateTime::currentDateTime().addDays(-30);
    }
    if (m_filterId.startsWith(QStringLiteral("source.")))
        return game.source.compare(m_filterId.mid(7), Qt::CaseInsensitive) == 0;
    if (m_filterId.startsWith(QStringLiteral("runner.")))
        return runnerName(game.runner) == m_filterId.mid(7);
    if (m_filterId.startsWith(QStringLiteral("platform.")))
        return platformName(game.platform) == m_filterId.mid(9);
    if (m_filterId == QStringLiteral("compat.detected"))
        return scanIsDetected(game.compatibility);
    if (m_filterId == QStringLiteral("compat.attention"))
        return scanNeedsAttention(game.compatibility);
    if (m_filterId == QStringLiteral("compat.notdetected"))
        return game.compatibility == ScanState::NotScanned
            || game.compatibility == ScanState::NoKnownComponents;
    return true;
}

void MainWindow::sortGames(QList<Game> &games) const
{
    const AppSettings &settings = m_library.settings();
    const bool ascending = settings.ascending;
    auto compare = [settings](const Game &left, const Game &right) {
        switch (settings.sortMode) {
        case SortMode::LastPlayed:
            return left.lastPlayed < right.lastPlayed;
        case SortMode::Playtime:
            return left.playtimeSeconds < right.playtimeSeconds;
        case SortMode::Name:
            break;
        }
        return QString::localeAwareCompare(left.name, right.name) < 0;
    };
    std::sort(games.begin(), games.end(), [compare, ascending](const Game &left, const Game &right) {
        if (left.name.compare(right.name, Qt::CaseInsensitive) == 0)
            return false;
        return ascending ? compare(left, right) : compare(right, left);
    });
}

QList<Game> MainWindow::filteredGames() const
{
    QList<Game> games;
    for (const Game &game : m_library.games()) {
        if (matchesFilter(game))
            games.append(game);
    }
    sortGames(games);
    return games;
}

void MainWindow::applyFilter()
{
    const QList<Game> games = filteredGames();
    m_libraryView->setGames(games);
    m_libraryView->setSelectedGame(m_selectedGameId);
    updateFilterLabel();

    const int total = m_library.games().size();
    if (total == 0) {
        m_libraryView->showEmptyState(
            QStringLiteral("No games yet"),
            QStringLiteral("Add a game folder or executable to start building your Vanta library. "
                           "Vanta never downloads games."),
            true);
    } else if (games.isEmpty()) {
        m_libraryView->showEmptyState(
            QStringLiteral("No games match your filters"),
            QStringLiteral("Try a different search term, choose another sidebar filter, or clear the active filters."),
            false);
    } else {
        m_libraryView->showResults();
    }

    if (m_subtitleLabel) {
        const QString suffix = games.size() == 1 ? QStringLiteral("game") : QStringLiteral("games");
        m_subtitleLabel->setText(QStringLiteral("%1 %2").arg(games.size()).arg(suffix));
    }
    refreshSelectionBar();
}

void MainWindow::scheduleAutoScan()
{
    if (!m_library.settings().automaticScanning || m_scannedOnce)
        return;
    m_scannedOnce = true;
    QTimer::singleShot(150, this, [this] { scanLibrary(false); });
}

void MainWindow::scanGame(const QString &gameId, bool force)
{
    const bool found = m_library.hasGame(gameId);
    if (!found) {
        setStatusMessage(QStringLiteral("The selected game is no longer in the library."));
        return;
    }
    m_scanner.scanGame(gameId, force);
    refreshStatus();
}

void MainWindow::scanLibrary(bool force)
{
    if (m_library.games().isEmpty()) {
        setStatusMessage(QStringLiteral("Add a game before scanning."));
        return;
    }
    m_scanner.scanLibrary(force);
    refreshStatus();
}

void MainWindow::openAddDialog()
{
    AddEditDialog dialog(Game::create(), true, m_library.settings(), &m_theme, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Game game = dialog.resultGame();
    game.modifiedAt = QDateTime::currentDateTime();
    if (!m_library.addGame(game)) {
        warn(QStringLiteral("The game could not be added. Check that the name and paths are valid."));
        return;
    }
    m_selectedGameId = game.id;
    m_libraryView->setSelectedGame(game.id);
    setStatusMessage(QStringLiteral("Added %1.").arg(game.name));
    if (m_library.settings().automaticScanning)
        scanGame(game.id, false);
    if (m_library.settings().fetchCoversAutomatically && game.coverPath.trimmed().isEmpty())
        fetchCoverForGame(game.id, game.name);
}

void MainWindow::fetchCoverForGame(const QString &gameId, const QString &gameName)
{
    if (!m_coverFetcher)
        return;
    m_coverFetcher->setApiKey(m_library.settings().steamGridDbApiKey);
    m_coverFetcher->fetchCover(gameId, gameName);
}

// Opens the artwork picker so the user can see every available cover instead of
// silently accepting the first grid SteamGridDB returns.
void MainWindow::chooseCoverArt(const QString &gameId)
{
    if (!m_coverFetcher)
        return;
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    if (!m_coverFetcher->hasApiKey()) {
        warn(QStringLiteral("Add a SteamGridDB API key in Settings → Covers first."));
        return;
    }

    m_pendingPickGameId = gameId;
    m_pendingPickName = game.name;
    m_coverFetcher->setApiKey(m_library.settings().steamGridDbApiKey);
    setStatusMessage(QStringLiteral("Searching SteamGridDB for “%1”…").arg(game.name));
    m_coverFetcher->listCoverArt(gameId, game.name);
}

void MainWindow::openEditDialog(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    AddEditDialog dialog(game, false, m_library.settings(), &m_theme, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Game updated = dialog.resultGame();
    updated.modifiedAt = QDateTime::currentDateTime();
    if (!m_library.updateGame(updated)) {
        warn(QStringLiteral("The game could not be saved."));
        return;
    }
    m_scanner.invalidate(gameId);
    setStatusMessage(QStringLiteral("Saved %1.").arg(updated.name));
    if (m_library.settings().automaticScanning)
        scanGame(gameId, false);
}

void MainWindow::openDetailsDialog(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;

    GameDetailsDialog dialog(game, m_scanner.resultFor(gameId), m_launcher.isRunning(gameId), &m_theme, this);
    dialog.setLogsAvailable(runnerIsWineLike(game.runner) || !LaunchLogs::list(gameId, 1).isEmpty());
    const int result = dialog.exec();
    if (result != QDialog::Accepted)
        return;
    switch (dialog.requestedAction()) {
    case GameDetailsDialog::Action::Launch:
        requestLaunch(gameId);
        break;
    case GameDetailsDialog::Action::Stop:
        requestStop(gameId);
        break;
    case GameDetailsDialog::Action::Edit:
        openEditDialog(gameId);
        break;
    case GameDetailsDialog::Action::OnlineFix:
        openOnlineFixDialog(gameId);
        break;
    case GameDetailsDialog::Action::Mods:
        openModManager(gameId);
        break;
    case GameDetailsDialog::Action::Logs:
        openLogs(gameId);
        break;
    case GameDetailsDialog::Action::Scan:
        scanGame(gameId, true);
        break;
    case GameDetailsDialog::Action::OpenFolder:
        openGameFolder(gameId);
        break;
    case GameDetailsDialog::Action::OpenPrefix:
        openGamePrefix(gameId);
        break;
    case GameDetailsDialog::Action::None:
        break;
    }
}

void MainWindow::openOnlineFixDialog(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    const OnlineFixInfo info = onlineFixInfoFor(game);
    OnlineFixDialog dialog(game, info, OnlineFixDialog::Mode::Manage, &m_theme, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Game updated = dialog.resultGame();
    updated.modifiedAt = QDateTime::currentDateTime();
    if (!m_library.updateGame(updated)) {
        warn(QStringLiteral("The OnlineFix policy could not be saved."));
        return;
    }
    setStatusMessage(QStringLiteral("Updated the OnlineFix policy for %1.").arg(updated.name));
}

void MainWindow::openModManager(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;

    ModManagerDialog dialog(game, &m_theme, this);
    connect(&dialog, &ModManagerDialog::modsChanged, this, [this, gameId] {
        bool exists = false;
        Game updated = m_library.gameById(gameId, &exists);
        if (!exists)
            return;
        if (!updated.bepinexCompatibility) {
            updated.bepinexCompatibility = true;
            updated.modifiedAt = QDateTime::currentDateTime();
            m_library.updateGame(updated);
        }
        m_scanner.invalidate(gameId);
        if (m_library.settings().automaticScanning)
            scanGame(gameId, false);
    });
    connect(&dialog, &ModManagerDialog::communityResolved, this,
            [this, gameId](const QString &community) {
                bool exists = false;
                Game updated = m_library.gameById(gameId, &exists);
                if (!exists || community.trimmed().isEmpty()
                    || updated.thunderstoreCommunity == community) {
                    return;
                }
                updated.thunderstoreCommunity = community;
                updated.modifiedAt = QDateTime::currentDateTime();
                m_library.updateGame(updated);
            });
    dialog.exec();
}

void MainWindow::openLogs(const QString &gameId, const QString &path)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;

    LogsDialog dialog(game.id, game.name, &m_theme, this);
    if (!path.isEmpty())
        dialog.selectLog(path);
    dialog.exec();
}

void MainWindow::openLogsForSelection()
{
    if (!m_selectedGameId.isEmpty()) {
        openLogs(m_selectedGameId);
        return;
    }

    // No card is selected, so fall back to the game with the newest log
    // instead of making the user hunt for one first.
    QString fallbackId;
    QDateTime newest;
    for (const Game &game : m_library.games()) {
        const QList<LaunchLogEntry> entries = LaunchLogs::list(game.id, 1);
        if (entries.isEmpty())
            continue;
        const QDateTime stamp = entries.first().finishedAt;
        if (fallbackId.isEmpty() || !newest.isValid() || stamp > newest) {
            fallbackId = game.id;
            newest = stamp;
        }
    }
    if (!fallbackId.isEmpty()) {
        openLogs(fallbackId);
        return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(QStringLiteral("Logs"));
    box.setText(QStringLiteral("No launch log has been recorded yet. Start a game once and Vanta will keep the "
                               "runner output so you can inspect it here afterwards."));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::openSettingsDialog()
{
    SettingsDialog dialog(m_library.settings(), &m_theme, &m_library, this);
    connect(&dialog, &SettingsDialog::runnersChanged, this, [this] { refreshStatus(); });
    connect(&dialog, &SettingsDialog::libraryChanged, this, [this] { refreshLibrary(); });
    connect(&dialog, &SettingsDialog::gamesImported, this, &MainWindow::handleGamesImported);
    if (dialog.exec() != QDialog::Accepted)
        return;

    AppSettings settings = dialog.resultSettings();
    settings.windowGeometry = saveGeometry();
    settings.windowMaximized = isMaximized();
    if (!m_library.updateSettings(settings)) {
        warn(QStringLiteral("Settings could not be saved."));
        return;
    }
    m_theme.setChoice(settings.theme);
    m_theme.setAnimationsEnabled(settings.animations);
    m_theme.apply();
    m_libraryView->setViewMode(settings.viewMode);
    m_libraryView->setCoverSize(settings.coverSize);
    m_libraryView->setShowBadges(settings.showBadges);
    m_libraryView->setAnimationsEnabled(settings.animations);
    m_sidebar->setCollapsedGroups(settings.rememberCollapsedGroups ? settings.collapsedGroups : QJsonObject());
    refreshSidebar();
    applyFilter();
    refreshIcons();
    setStatusMessage(QStringLiteral("Settings saved."));
    if (dialog.replayTutorialRequested())
        showTutorial(0);
}

void MainWindow::openAboutDialog()
{
    AboutDialog dialog(&m_theme, this);
    dialog.exec();
}

void MainWindow::checkForUpdates(bool manual)
{
    // A check is a network round trip, so the manual path reports its progress
    // and the automatic one stays silent unless there is something to say.
    m_updateCheckManual = manual;
    if (manual) {
        setStatusMessage(QStringLiteral("Checking for updates…"), 4000);
        m_updateDismissed = false;
        if (m_updateBanner)
            m_updateBanner->hideBanner();
    }
    m_updateChecker.checkForUpdates(QStringLiteral(VANTA_VERSION));
}

void MainWindow::handleUpdateAvailable(const UpdateRelease &release, const QString &currentVersion)
{
    if (m_updateDismissed || !m_updateBanner)
        return;
    m_updateBanner->showRelease(release, currentVersion);
    setStatusMessage(QStringLiteral("Vanta %1 is available.")
                         .arg(UpdateChecker::normaliseVersion(release.tagName)),
                     9000);
}

void MainWindow::openUpdatePage(const QString &url)
{
    if (url.isEmpty())
        return;
    if (!QDesktopServices::openUrl(QUrl(url)))
        warn(QStringLiteral("The release page could not be opened in a browser."));
}

void MainWindow::openRunnerInstallDialog()
{
    RunnerInstallDialog dialog(&m_theme, this);
    connect(&dialog, &RunnerInstallDialog::runnersChanged, this, [this] {
        setStatusMessage(QStringLiteral("Runner discovery refreshed."));
    });
    dialog.exec();
}

void MainWindow::openLibraryImportDialog()
{
    LibraryImportDialog dialog(&m_library, &m_theme, this);
    connect(&dialog, &LibraryImportDialog::gamesImportedList, this, &MainWindow::handleGamesImported);
    dialog.exec();
}

void MainWindow::handleGamesImported(const QList<Game> &imported)
{
    if (imported.isEmpty())
        return;

    refreshLibrary();
    setStatusMessage(QStringLiteral("Ported %1 game%2.")
                         .arg(QString::number(imported.size()),
                              imported.size() == 1 ? QString() : QStringLiteral("s")));

    for (const Game &game : imported) {
        if (m_library.settings().automaticScanning)
            scanGame(game.id, false);
        if (m_library.settings().fetchCoversAutomatically && game.coverPath.trimmed().isEmpty())
            fetchCoverForGame(game.id, game.name);
    }
}

void MainWindow::fetchMissingCovers()
{
    if (!m_coverFetcher)
        return;
    int count = 0;
    for (const Game &game : m_library.games()) {
        if (game.coverPath.trimmed().isEmpty()) {
            fetchCoverForGame(game.id, game.name);
            ++count;
        }
    }
    if (count > 0) {
        setStatusMessage(QStringLiteral("Queued cover lookups for %1 game%2.")
                             .arg(QString::number(count),
                                  count == 1 ? QString() : QStringLiteral("s")));
    } else {
        setStatusMessage(QStringLiteral("All games in the library already have covers."));
    }
}

void MainWindow::showContextMenu(const QString &gameId, const QPoint &globalPosition)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    const bool running = m_launcher.isRunning(gameId);

    QMenu menu(this);
    auto *playAction = menu.addAction(running ? QStringLiteral("Stop") : QStringLiteral("Play"));
    connect(playAction, &QAction::triggered, this, [this, gameId, running] {
        if (running)
            requestStop(gameId);
        else
            requestLaunch(gameId);
    });
    auto *detailsAction = menu.addAction(QStringLiteral("Details"));
    connect(detailsAction, &QAction::triggered, this, [this, gameId] { openDetailsDialog(gameId); });
    auto *editAction = menu.addAction(QStringLiteral("Edit…"));
    connect(editAction, &QAction::triggered, this, [this, gameId] { openEditDialog(gameId); });
    menu.addSeparator();
    auto *favoriteAction = menu.addAction(game.favorite ? QStringLiteral("Remove from favorites")
                                                        : QStringLiteral("Add to favorites"));
    connect(favoriteAction, &QAction::triggered, this, [this, gameId] { toggleFavorite(gameId); });
    auto *scanAction = menu.addAction(QStringLiteral("Scan now"));
    connect(scanAction, &QAction::triggered, this, [this, gameId] { scanGame(gameId, true); });
    auto *onlineFixAction = menu.addAction(QStringLiteral("OnlineFix…"));
    connect(onlineFixAction, &QAction::triggered, this, [this, gameId] { openOnlineFixDialog(gameId); });
    if (game.platform == Platform::Windows || runnerIsWineLike(game.runner)) {
        auto *modsAction = menu.addAction(QStringLiteral("Mods…"));
        connect(modsAction, &QAction::triggered, this, [this, gameId] { openModManager(gameId); });
    }
    auto *logsAction = menu.addAction(QStringLiteral("Logs…"));
    connect(logsAction, &QAction::triggered, this, [this, gameId] { openLogs(gameId); });
    auto *coverAction = menu.addAction(QStringLiteral("Choose cover art…"));
    connect(coverAction, &QAction::triggered, this, [this, gameId] { chooseCoverArt(gameId); });
    menu.addSeparator();
    auto *folderAction = menu.addAction(QStringLiteral("Open game folder"));
    connect(folderAction, &QAction::triggered, this, [this, gameId] { openGameFolder(gameId); });
    if (runnerIsWineLike(game.runner)) {
        auto *prefixAction = menu.addAction(QStringLiteral("Open prefix folder"));
        connect(prefixAction, &QAction::triggered, this, [this, gameId] { openGamePrefix(gameId); });
    }
    menu.addSeparator();
    auto *removeAction = menu.addAction(QStringLiteral("Remove from library"));
    connect(removeAction, &QAction::triggered, this, [this, gameId] { removeGame(gameId); });

    menu.exec(globalPosition);
}

void MainWindow::showTutorial(int startIndex)
{
    showTutorialSteps();
    m_tutorial->start(startIndex);
}

void MainWindow::showTutorialSteps()
{
    QList<TutorialOverlay::Step> steps;

    TutorialOverlay::Step welcome;
    welcome.title = QStringLiteral("Welcome to Vanta");
    welcome.body = QStringLiteral("Vanta keeps your local game library in one place and launches native, Wine, and "
                                  "Proton games without hiding what it does. This short tour points out the main "
                                  "parts of the window. You can leave at any time and reopen it from the sidebar.");
    welcome.showLogo = true;
    steps.append(welcome);

    TutorialOverlay::Step sidebarStep;
    sidebarStep.target = m_sidebar;
    sidebarStep.title = QStringLiteral("Library filters");
    sidebarStep.body = QStringLiteral("The sidebar groups your games by source, runner, platform, and scan status. "
                                      "Groups collapse when you click their header, and Vanta can remember that "
                                      "layout. Settings, this tutorial, and About live at the bottom.");
    sidebarStep.targetHint = QStringLiteral("Click a filter to narrow the library instantly.");
    steps.append(sidebarStep);

    TutorialOverlay::Step searchStep;
    searchStep.target = m_searchContainer;
    searchStep.title = QStringLiteral("Search and filter");
    searchStep.body = QStringLiteral("Search matches names, sources, runners, and paths. The filter button adds "
                                     "compatibility and installation filters on top of the current sidebar "
                                     "selection.");
    searchStep.targetHint = QStringLiteral("Filters combine with the active sidebar item.");
    steps.append(searchStep);

    TutorialOverlay::Step addStep;
    addStep.target = m_addButton;
    addStep.title = QStringLiteral("Add a game");
    addStep.body = QStringLiteral("Point Vanta at a folder or an executable you already have. Vanta never downloads "
                                  "games. Adding a game also runs a read-only scan when automatic scanning is "
                                  "enabled, so runner and fix detection is available immediately.");
    addStep.targetHint = QStringLiteral("The editor has tabs for general details, the runner, OnlineFix, and environment variables.");
    steps.append(addStep);

    TutorialOverlay::Step viewStep;
    viewStep.target = m_viewOptionsButton;
    viewStep.title = QStringLiteral("Change the view");
    viewStep.body = QStringLiteral("Switch between the cover grid and a compact table, resize covers, and toggle "
                                   "badges or subtle animations. Right-click any game for launch, edit, scan, and "
                                   "favourite actions.");
    viewStep.targetHint = QStringLiteral("The small arrow opens more view options.");
    steps.append(viewStep);

    TutorialOverlay::Step fixStep;
    fixStep.title = QStringLiteral("OnlineFix is supported, not guaranteed");
    fixStep.body = QStringLiteral("Vanta can detect OnlineFix signature files and apply the launcher-side behaviour "
                                  "of OnlineFix Linux: INI updates with backups, the FreeTP app-id swap, "
                                  "ExtraProtection set to false, and launch-only Wine DLL overrides. Detection is "
                                  "not proof that a fix works, and Vanta never patches game binaries or downloads "
                                  "fix files.");
    fixStep.showLogo = true;
    steps.append(fixStep);

    TutorialOverlay::Step runnerStep;
    runnerStep.title = QStringLiteral("Runners are discovered automatically");
    runnerStep.body = QStringLiteral("Vanta finds Wine and Proton builds from Steam, Lutris, Heroic, Bottles, and its "
                                     "own runner folder. If you need a new runner, the runner dialog can install Wine "
                                     "through your package manager or download portable Wine and GE-Proton builds "
                                     "from their official GitHub releases.");
    runnerStep.targetHint = QStringLiteral("Open Settings → Runners, or use the main menu, to install a runner.");
    steps.append(runnerStep);

    TutorialOverlay::Step doneStep;
    doneStep.title = QStringLiteral("You are ready");
    doneStep.body = QStringLiteral("Double-click a card to launch. Vanta tracks playtime and the last played time, "
                                   "keeps launch diagnostics for failures, and stops games cleanly, including the "
                                   "Wine prefix when needed. Reopen this tour from the sidebar whenever you want.");
    doneStep.showLogo = true;
    steps.append(doneStep);

    m_tutorial->setSteps(steps);
    if (!m_tutorial->isVisible()) {
        disconnect(m_tutorial, &TutorialOverlay::finished, this, nullptr);
        connect(m_tutorial, &TutorialOverlay::finished, this, [this] {
            AppSettings settings = m_library.settings();
            settings.tutorialCompleted = true;
            m_library.updateSettings(settings);
        });
    }
}

OnlineFixInfo MainWindow::onlineFixInfoFor(const Game &game) const
{
    const ScanResult *result = m_scanner.resultFor(game.id);
    if (result && result->onlineFix.detected)
        return result->onlineFix;

    const QString directory = game.effectiveDirectory();
    OnlineFixInfo info;
    if (!directory.isEmpty())
        info = OnlineFixDetector::detect(game.executable, directory);
    if (!info.detected && game.fixKind != FixKind::Unknown) {
        info.detected = true;
        info.kind = game.fixKind;
        info.realAppId = game.realAppId;
        info.fakeAppId = game.fakeAppId;
        info.fixPath = game.fixPath;
        info.photonLauncherDir = game.photonLauncherDir;
    }
    // The scan result wins, but a stored app id is still usable when the scan
    // did not read one; this previously ran only when the stored value was
    // empty, which is the one case where it does nothing.
    if (info.realAppId.isEmpty() && !game.realAppId.isEmpty())
        info.realAppId = game.realAppId;
    if (info.fakeAppId.isEmpty() && !game.fakeAppId.isEmpty())
        info.fakeAppId = game.fakeAppId;
    return info;
}

void MainWindow::requestLaunch(const QString &gameId, bool applyOnlineFix)
{
    if (m_launcher.isRunning(gameId)) {
        setStatusMessage(QStringLiteral("This game is already running."));
        return;
    }
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    if (game.executable.trimmed().isEmpty() && !runnerIsWineLike(game.runner)) {
        warn(QStringLiteral("Set an executable for this game before launching it."));
        return;
    }

    m_pendingGameId = gameId;
    m_pendingDiagnostics.clear();
    m_launchCancelled = false;
    m_pendingInfo = onlineFixInfoFor(game);
    m_pendingApplyFix = false;
    m_pendingSteamSkipped = false;

    if (game.runner == RunnerType::Native || game.runner == RunnerType::Flatpak) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, false);
        return;
    }
    if (game.onlineFixMode == OnlineFixMode::Disabled) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, false);
        return;
    }

    // An explicit request (the Launch button in the detail strip) applies the fix
    // without asking, which is what "OnlineFix enabled by default" means.
    if (applyOnlineFix) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, true);
        return;
    }

    const OnlineFixPrompt policy = m_library.settings().onlineFixPrompt;
    const bool detected = m_pendingInfo.detected || game.onlineFixMode == OnlineFixMode::Force;
    if (!detected && game.onlineFixMode == OnlineFixMode::Auto) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, false);
        return;
    }

    if (policy == OnlineFixPrompt::NeverApply) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, false);
        return;
    }
    if (policy == OnlineFixPrompt::AlwaysApply) {
        continueLaunchWithPolicy(gameId, m_pendingInfo, true);
        return;
    }

    const OnlineFixDialog::LaunchChoice choice =
        OnlineFixDialog::promptForLaunch(game, m_pendingInfo, &m_theme, this);
    if (choice == OnlineFixDialog::LaunchChoice::Cancel) {
        setStatusMessage(QStringLiteral("Launch cancelled."));
        return;
    }
    continueLaunchWithPolicy(gameId, m_pendingInfo,
                             choice == OnlineFixDialog::LaunchChoice::Apply);
}

void MainWindow::continueLaunchWithPolicy(const QString &gameId, const OnlineFixInfo &info, bool applyFix)
{
    Q_UNUSED(info);
    m_pendingApplyFix = applyFix;
    Q_UNUSED(gameId);
    if (applyFix && m_library.settings().requireSteamForOnlineFix) {
        runSteamPrecondition();
        return;
    }
    applyPatchesAndLaunch();
}

void MainWindow::runSteamPrecondition()
{
    // Steam already running: nothing to ask, go straight to the launch.
    if (SteamIntegration::steamIsRunning()) {
        applyPatchesAndLaunch();
        return;
    }

    // Steam is closed. Ask the user instead of starting the client behind their back.
    const Game game = m_library.gameById(m_pendingGameId);
    const SteamPromptDialog::Choice choice = SteamPromptDialog::prompt(game.name, &m_theme, this);
    switch (choice) {
    case SteamPromptDialog::Choice::Cancel:
        setStatusMessage(QStringLiteral("Launch cancelled."));
        return;
    case SteamPromptDialog::Choice::PlayWithoutSteam:
        m_pendingSteamSkipped = true;
        setStatusMessage(QStringLiteral("Launching without Steam. Online play will not work."), 9000);
        applyPatchesAndLaunch();
        return;
    case SteamPromptDialog::Choice::OpenSteam:
        m_pendingSteamSkipped = false;
        startSteamAndWait();
        return;
    }
}

void MainWindow::createLaunchDialog(const QString &gameId, const QString &gameName)
{
    if (m_launchDialog)
        return;
    m_launchDialog = new LaunchProgressDialog(gameName, &m_theme, this);
    connect(m_launchDialog, &LaunchProgressDialog::cancelRequested, this, [this] {
        m_launchCancelled = true;
        if (m_steamPrecondition)
            m_steamPrecondition->cancel();
        closeLaunchDialog();
        setStatusMessage(QStringLiteral("Launch cancelled."));
    });
    connect(m_launchDialog, &LaunchProgressDialog::logRequested, this, [this, gameId](const QString &path) {
        openLogs(gameId, path);
    });
    m_launchDialog->show();
}

void MainWindow::startSteamAndWait()
{
    closeLaunchDialog();
    createLaunchDialog(m_pendingGameId, m_library.gameById(m_pendingGameId).name);
    m_launchDialog->setMessage(QStringLiteral("Starting Steam…"));
    m_launchDialog->setIndeterminate();
    m_launchDialog->setDetail(QStringLiteral("Vanta will launch the game once Steam is running and signed in."));

    m_steamPrecondition = new SteamPrecondition(this);
    connect(m_steamPrecondition, &SteamPrecondition::progress, this, [this](const QString &message) {
        if (m_launchDialog)
            m_launchDialog->setMessage(message);
    });
    connect(m_steamPrecondition, &SteamPrecondition::ready, this, [this] {
        if (m_launchCancelled)
            return;
        applyPatchesAndLaunch();
    });
    connect(m_steamPrecondition, &SteamPrecondition::failed, this, [this](const QString &message) {
        if (m_launchCancelled)
            return;
        abortLaunch(message);
    });
    m_steamPrecondition->ensureRunning();
}

void MainWindow::applyPatchesAndLaunch()
{
    if (m_launchCancelled)
        return;
    if (m_pendingGameId.isEmpty())
        return;

    bool found = false;
    const Game game = m_library.gameById(m_pendingGameId, &found);
    if (!found) {
        abortLaunch(QStringLiteral("The game is no longer in the library."));
        return;
    }

    m_pendingDiagnostics.clear();
    if (m_pendingApplyFix) {
        if (m_pendingSteamSkipped) {
            m_pendingDiagnostics.append(
                QStringLiteral("Steam is not running. Online features will not work for this session."));
        }
        const OnlineFixInfo info = onlineFixInfoFor(game);
        createLaunchDialog(game.id, game.name);
        m_launchDialog->setMessage(QStringLiteral("Applying OnlineFix configuration…"));
        m_launchDialog->setIndeterminate();
        const PatchResult patch = OnlineFixPatcher::apply(game, info);
        m_pendingDiagnostics.append(patch.diagnostics);
        for (const QString &file : patch.writtenFiles)
            m_pendingDiagnostics.append(QStringLiteral("Wrote: %1").arg(file));
        for (const QString &file : patch.backedUpFiles)
            m_pendingDiagnostics.append(QStringLiteral("Backup: %1").arg(file));
        for (const QString &error : patch.errors)
            m_pendingDiagnostics.append(QStringLiteral("Error: %1").arg(error));
        if (!patch.ok) {
            const QString detail = patch.errors.isEmpty()
                ? QStringLiteral("The OnlineFix configuration could not be updated.")
                : patch.errors.join(QLatin1Char('\n'));
            abortLaunch(QStringLiteral("OnlineFix configuration failed. The launch was stopped before starting the game."),
                        detail);
            return;
        }
        if (m_launchDialog)
            m_launchDialog->setDetail(m_pendingDiagnostics.join(QLatin1Char('\n')));
    }

    performLaunch();
}

void MainWindow::performLaunch()
{
    bool found = false;
    const Game game = m_library.gameById(m_pendingGameId, &found);
    if (!found) {
        abortLaunch(QStringLiteral("The game is no longer in the library."));
        return;
    }

    const OnlineFixInfo info = onlineFixInfoFor(game);
    QString error;
    const ScanResult *scanResult = m_scanner.resultFor(game.id);
    bool bepinexDetected = scanResult && scanResult->bepinexDetected;
    // The scan cache can be older than a mod installation made in the mod
    // manager, so confirm with the live detector before deciding on winhttp=n,b.
    if (!bepinexDetected)
        bepinexDetected = BepInEx::detect(game.effectiveDirectory(), game.executable).isValid();
    const LaunchPlan plan = LaunchPlanBuilder::build(game, m_library.settings(), info, m_pendingApplyFix,
                                                     bepinexDetected, &error);
    if (!plan.isValid()) {
        abortLaunch(error.isEmpty() ? QStringLiteral("The launch plan could not be built.") : error);
        return;
    }

    createLaunchDialog(game.id, game.name);
    m_launchDialog->setMessage(QStringLiteral("Starting the game…"));
    m_launchDialog->setIndeterminate();
    QStringList detail = m_pendingDiagnostics;
    detail.append(plan.diagnostics);
    if (!plan.steamRuntimeRequested && !plan.composedOverrides.isEmpty())
        detail.append(QStringLiteral("WINEDLLOVERRIDES: %1").arg(plan.composedOverrides));
    m_launchDialog->setDetail(detail.join(QLatin1Char('\n')));

    QString launchError;
    if (!m_launcher.launch(game, plan, &launchError)) {
        abortLaunch(launchError.isEmpty() ? QStringLiteral("The game could not be started.") : launchError,
                    plan.summary());
        return;
    }

    m_pendingGameId.clear();
    m_pendingInfo = OnlineFixInfo();
    m_pendingApplyFix = false;
    m_pendingDiagnostics.clear();
    updateRunningState();
}

void MainWindow::requestStop(const QString &gameId)
{
    if (!m_launcher.isRunning(gameId))
        return;
    m_launcher.stopGame(gameId);
    updateRunningState();
}

void MainWindow::abortLaunch(const QString &message, const QString &detail)
{
    m_pendingGameId.clear();
    m_pendingApplyFix = false;
    m_pendingDiagnostics.clear();
    if (m_launchDialog) {
        m_launchDialog->showFailure(message);
        if (!detail.isEmpty())
            m_launchDialog->setDetail(detail);
        return;
    }
    showDiagnostics(QStringLiteral("Launch failed"), message, detail);
}

void MainWindow::closeLaunchDialog()
{
    if (!m_launchDialog)
        return;
    m_launchDialog->finishAndClose();
    m_launchDialog.clear();
}

void MainWindow::showDiagnostics(const QString &title, const QString &message, const QString &detail)
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Critical);
    box.setWindowTitle(title);
    box.setText(message);
    if (!detail.trimmed().isEmpty())
        box.setDetailedText(detail);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::handleGameFinished(const QString &gameId, qint64 elapsedSeconds, int exitCode,
                                    bool crashed, bool stopped, const QString &diagnostic,
                                    const QString &logPath)
{
    bool found = false;
    Game game = m_library.gameById(gameId, &found);
    if (found) {
        if (elapsedSeconds > 0)
            game.playtimeSeconds += elapsedSeconds;
        game.lastPlayed = QDateTime::currentDateTime();
        game.modifiedAt = game.lastPlayed;
        m_library.updateGame(game);
    }
    updateRunningState();
    if (stopped) {
        setStatusMessage(QStringLiteral("Game stopped."));
        return;
    }
    if (crashed) {
        const QString name = found ? game.name : QStringLiteral("The game");
        setStatusMessage(QStringLiteral("The game exited unexpectedly (code %1).").arg(exitCode), 12000);
        if (logPath.isEmpty()) {
            showDiagnostics(QStringLiteral("Game exited unexpectedly"),
                            QStringLiteral("%1 exited with code %2. Diagnostics from the launch are below.")
                                .arg(name).arg(exitCode),
                            diagnostic);
            return;
        }
        const auto reply = QMessageBox::question(
            this, QStringLiteral("Game exited unexpectedly"),
            QStringLiteral("%1 exited with code %2. Vanta saved the launch log, including everything the "
                           "runner printed, so you can see what happened.\n\nOpen the log now?")
                .arg(name).arg(exitCode),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (reply == QMessageBox::Yes)
            openLogs(gameId, logPath);
        return;
    }
    setStatusMessage(QStringLiteral("Game closed."));
}

void MainWindow::updateRunningState()
{
    m_running.clear();
    const QStringList ids = m_launcher.runningGameIds();
    for (const QString &id : ids)
        m_running.insert(id);
    if (m_libraryView)
        m_libraryView->setRunningGames(ids);
    if (m_selectionBar) {
        m_selectionBar->setRunning(!m_selectedGameId.isEmpty()
                                   && m_running.contains(m_selectedGameId));
    }
    refreshStatus();
}

void MainWindow::toggleFavorite(const QString &gameId)
{
    bool found = false;
    Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    game.favorite = !game.favorite;
    game.modifiedAt = QDateTime::currentDateTime();
    m_library.updateGame(game);
    setStatusMessage(game.favorite ? QStringLiteral("Added to favorites.") : QStringLiteral("Removed from favorites."));
}

void MainWindow::removeGame(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("Remove game"));
    box.setText(QStringLiteral("Remove %1 from the library?").arg(game.name));
    box.setInformativeText(QStringLiteral("Game files are not touched. Only the library entry and its cached scan "
                                          "result are removed."));
    box.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes)
        return;

    m_scanner.invalidate(gameId);
    if (m_launcher.isRunning(gameId))
        m_launcher.stopGame(gameId);
    if (!m_library.removeGame(gameId)) {
        warn(QStringLiteral("The game could not be removed from the library."));
        return;
    }
    if (m_selectedGameId == gameId)
        m_selectedGameId.clear();
    setStatusMessage(QStringLiteral("Removed %1.").arg(game.name));
}

void MainWindow::openGameFolder(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    QString path = game.effectiveDirectory();
    if (path.isEmpty())
        path = QFileInfo(game.executable).absolutePath();
    showGameInFileManager(path);
}

void MainWindow::openGamePrefix(const QString &gameId)
{
    bool found = false;
    const Game game = m_library.gameById(gameId, &found);
    if (!found)
        return;
    const QString prefix = LaunchPlanBuilder::effectiveWinePrefix(game);
    if (prefix.isEmpty()) {
        warn(QStringLiteral("This game has no configured Wine prefix."));
        return;
    }
    if (!QDir().mkpath(prefix)) {
        warn(QStringLiteral("The prefix folder does not exist yet and could not be created."));
        return;
    }
    showGameInFileManager(prefix);
}

void MainWindow::showGameInFileManager(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        warn(QStringLiteral("The folder no longer exists: %1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_running.isEmpty() && !m_shuttingDown) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Quit Vanta"));
        box.setText(QStringLiteral("%1 game%2 still running. Quit anyway?")
                        .arg(m_running.size())
                        .arg(m_running.size() == 1 ? QString() : QStringLiteral("s are")));
        box.setInformativeText(QStringLiteral("Running games will be stopped."));
        box.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
        box.setDefaultButton(QMessageBox::Cancel);
        if (box.exec() != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    m_shuttingDown = true;
    if (m_steamPrecondition) {
        m_steamPrecondition->cancel();
        m_steamPrecondition->deleteLater();
        m_steamPrecondition = nullptr;
    }
    m_launcher.shutdown();
    m_scanner.saveCache();
    saveWindowState();
    event->accept();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_tutorial && m_tutorial->isVisible()) {
        if (QWidget *central = centralWidget())
            m_tutorial->setGeometry(central->rect());
    }
}
