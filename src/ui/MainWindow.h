#pragma once

#include "Game.h"
#include "OnlineFix.h"
#include "Scanner.h"
#include "Storage.h"
#include "Library.h"
#include "ScanController.h"
#include "Launcher.h"
#include "ThemeManager.h"
#include "UpdateChecker.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QStringList>

class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QToolButton;
class QFrame;
class LibraryView;
class Sidebar;
class TutorialOverlay;
class LaunchProgressDialog;
class SteamPrecondition;
class CoverFetcher;
class GameSelectionBar;
class UpdateBanner;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void buildHeader();
    void buildSidebar();
    void buildContent();
    void buildMenus();
    void buildFilterMenu();
    void connectSignals();
    void restoreState();
    void saveWindowState();

    void refreshLibrary();
    void refreshLibraryImmediately();
    void refreshSidebar();
    void refreshStatus();
    void refreshIcons();
    void refreshSelectionBar();
    void updateFilterLabel();
    void applyFilter();
    void persistFilterList();
    QList<Game> filteredGames() const;
    bool matchesFilter(const Game &game) const;
    bool matchesSearch(const Game &game) const;
    void sortGames(QList<Game> &games) const;
    void scheduleAutoScan();

    void selectFilter(const QString &id);
    void openAddDialog();
    void fetchCoverForGame(const QString &gameId, const QString &gameName);
    void chooseCoverArt(const QString &gameId);
    void openEditDialog(const QString &gameId);
    void openDetailsDialog(const QString &gameId);
    void openOnlineFixDialog(const QString &gameId);
    void openModManager(const QString &gameId);
    void openLogs(const QString &gameId, const QString &path = QString());
    void openLogsForSelection();
    void openSettingsDialog();
    void openAboutDialog();
    void openRunnerInstallDialog();
    void openLibraryImportDialog();
    void handleGamesImported(const QList<Game> &imported);
    void fetchMissingCovers();
    void checkForUpdates(bool manual);
    void handleUpdateAvailable(const UpdateRelease &release, const QString &currentVersion);
    void openUpdatePage(const QString &url);
    void showContextMenu(const QString &gameId, const QPoint &globalPosition);
    void showTutorial(int startIndex = 0);
    void showTutorialSteps();

    void requestLaunch(const QString &gameId, bool applyOnlineFix = false);
    void requestStop(const QString &gameId);
    void beginOnlineFixLaunch(const QString &gameId);
    void continueLaunchWithPolicy(const QString &gameId, const OnlineFixInfo &info, bool applyFix);
    void runSteamPrecondition();
    void startSteamAndWait();
    void applyPatchesAndLaunch();
    void performLaunch();
    void abortLaunch(const QString &message, const QString &detail = QString());
    void closeLaunchDialog();
    void createLaunchDialog(const QString &gameId, const QString &gameName);
    void updateRunningState();
    void handleGameFinished(const QString &gameId, qint64 elapsedSeconds, int exitCode,
                            bool crashed, bool stopped, const QString &diagnostic,
                            const QString &logPath);
    void showDiagnostics(const QString &title, const QString &message, const QString &detail);

    void scanGame(const QString &gameId, bool force);
    void scanLibrary(bool force);
    void toggleFavorite(const QString &gameId);
    void removeGame(const QString &gameId);
    void openGameFolder(const QString &gameId);
    void openGamePrefix(const QString &gameId);
    void showGameInFileManager(const QString &path);
    void setStatusMessage(const QString &message, int timeoutMs = 6000);
    void warn(const QString &message);
    OnlineFixInfo onlineFixInfoFor(const Game &game) const;

    Storage m_storage;
    GameLibrary m_library;
    ScanController m_scanner;
    ThemeManager m_theme;
    GameLauncher m_launcher;

    Sidebar *m_sidebar = nullptr;
    LibraryView *m_libraryView = nullptr;
    GameSelectionBar *m_selectionBar = nullptr;
    QFrame *m_header = nullptr;
    QFrame *m_searchContainer = nullptr;
    QLineEdit *m_searchField = nullptr;
    QToolButton *m_filterButton = nullptr;
    QToolButton *m_searchClearButton = nullptr;
    QToolButton *m_viewOptionsButton = nullptr;
    QToolButton *m_viewMenuButton = nullptr;
    QToolButton *m_addButton = nullptr;
    QToolButton *m_mainMenuButton = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_runningLabel = nullptr;
    TutorialOverlay *m_tutorial = nullptr;
    UpdateBanner *m_updateBanner = nullptr;
    UpdateChecker m_updateChecker;
    // Set once the user dismisses the banner, so a periodic check cannot bring
    // the same notification back in the same session.
    bool m_updateDismissed = false;
    bool m_updateCheckManual = false;

    QMenu *m_filterMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_mainMenu = nullptr;

    QString m_filterId = QStringLiteral("library.games");
    QString m_searchText;
    QString m_statusMessage;
    int m_statusMessageSerial = 0;
    CompatibilityFilter m_compatibilityFilter = CompatibilityFilter::All;
    InstallFilter m_installFilter = InstallFilter::All;
    bool m_favoritesOnly = false;
    // Persisted filter list; the members mirror AppSettings so the menu can be
    // rebuilt without re-reading settings on every toggle.
    bool m_hideNeedsAttention = false;
    QStringList m_hiddenRunners;
    bool m_scannedOnce = false;
    QSet<QString> m_running;
    QString m_selectedGameId;

    QPointer<LaunchProgressDialog> m_launchDialog;
    QPointer<SteamPrecondition> m_steamPrecondition;
    CoverFetcher *m_coverFetcher = nullptr;
    QString m_pendingGameId;
    // Set while the artwork picker is open, so the artwork signal knows which
    // library entry the chosen image belongs to.
    QString m_pendingPickGameId;
    QString m_pendingPickName;
    OnlineFixInfo m_pendingInfo;
    bool m_pendingApplyFix = false;
    bool m_pendingSteamSkipped = false;
    QStringList m_pendingDiagnostics;
    bool m_launchCancelled = false;
    QTimer *m_refreshTimer = nullptr;
    bool m_shuttingDown = false;
};
